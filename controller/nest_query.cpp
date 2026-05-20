#include "nest_query.h"

#include "../utils/logic/logic.h"
#include "../utils/logic/logic_ast.h"
#include "../utils/service_common/service_common.h"

#include <algorithm>
#include <cmath>
#include <QSet>

namespace service {

namespace {

struct QueryExecutionScope
{
    QString previousDatabase;
    QString previousDataRoot;
    QStringList previousSkipSharedReadLockTables;

    static QStringList &skipSharedReadLockTables()
    {
        static thread_local QStringList tables;
        return tables;
    }

    explicit QueryExecutionScope(const QueryExecuteContext &context)
        : previousDatabase(currentDatabase),
          previousDataRoot(getDataRoot()),
          previousSkipSharedReadLockTables(skipSharedReadLockTables())
    {
        if (!context.dataRoot.trimmed().isEmpty()) {
            setDataRoot(context.dataRoot);
        }
        if (!context.currentDatabase.trimmed().isEmpty()) {
            currentDatabase = context.currentDatabase.trimmed();
        }
        skipSharedReadLockTables() = context.skipSharedReadLockTables;
    }

    ~QueryExecutionScope()
    {
        currentDatabase = previousDatabase;
        setDataRoot(previousDataRoot);
        skipSharedReadLockTables() = previousSkipSharedReadLockTables;
    }
};

struct SelectTableSource
{
    QString tableName;
    QString tableAlias;
    tabledef::TableSchema schema;
    repo::TableData data;
    bool tableNameQualifierVisible = true;
};

struct MultiProjectionItem
{
    QString sourceName;
    QString resolvedKey;
    QString outputName;
    tabledef::ColumnType type = tabledef::ColumnType::Varchar;
};

enum class AggregateFunction
{
    Count,
    Sum,
    Avg,
    Min,
    Max,
};

struct AggregateSpec
{
    AggregateFunction function = AggregateFunction::Count;
    QString argumentName;
    QString resolvedArgumentKey;
    bool isStar = false;
    QString sourceText;
    QString syntheticName;
    QString outputName;
    tabledef::ColumnType outputType = tabledef::ColumnType::Varchar;
};

struct AggregateProjectionItem
{
    bool isAggregate = false;
    QString sourceName;
    QString resolvedKey;
    int aggregateIndex = -1;
    QString outputName;
    tabledef::ColumnType outputType = tabledef::ColumnType::Varchar;
};

struct GroupedRows
{
    QString groupKey;
    logic::LogicRowContext representative;
    QVector<logic::LogicRowContext> rows;
};

struct MultiNameResolution
{
    QMap<QString, QString> visibleNameToKey;
    QMap<QString, tabledef::ColumnType> keyTypes;
    QSet<QString> ambiguousBareColumns;
    QStringList starKeys;
    QStringList starOutputColumns;
};

bool simpleConditionsFromPayload(const QVariantList &conditionsPayload,
                                 QList<SimpleCondition> *conditions,
                                 QString *error)
{
    if (conditions != nullptr) conditions->clear();
    if (error != nullptr) error->clear();

    for (const QVariant &conditionValue : conditionsPayload) {
        if (conditionValue.typeId() != QMetaType::QVariantMap) {
            if (error != nullptr) *error = QStringLiteral("WHERE payload is incomplete");
            return false;
        }

        const QVariantMap conditionMap = conditionValue.toMap();
        const QString columnName = conditionMap.value(QStringLiteral("columnName")).toString().trimmed();
        if (columnName.isEmpty()) {
            if (error != nullptr) *error = QStringLiteral("WHERE payload is incomplete");
            return false;
        }

        if (conditions != nullptr) {
            conditions->append(SimpleCondition{columnName, conditionMap.value(QStringLiteral("value")).toString()});
        }
    }

    return true;
}

void appendRequiredReference(QStringList *names, const QString &name)
{
    if (names == nullptr || name.trimmed().isEmpty() || names->contains(name)) {
        return;
    }
    names->append(name);
}

QStringList selectLocalPrefixes(const QVariantMap &payload)
{
    QStringList prefixes;
    auto appendSource = [&](const QVariantMap &source) {
        const QString tableName = source.value(QStringLiteral("tableName")).toString().trimmed();
        const QString tableAlias = source.value(QStringLiteral("tableAlias")).toString().trimmed();
        if (!tableName.isEmpty() && !prefixes.contains(tableName + QLatin1Char('.'))) {
            prefixes.append(tableName + QLatin1Char('.'));
        }
        if (!tableAlias.isEmpty() && !prefixes.contains(tableAlias + QLatin1Char('.'))) {
            prefixes.append(tableAlias + QLatin1Char('.'));
        }
    };

    const QVariantList sources = payload.value(QStringLiteral("fromSources")).toList();
    if (!sources.isEmpty()) {
        for (const QVariant &sourceValue : sources) {
            appendSource(sourceValue.toMap());
        }
        return prefixes;
    }

    appendSource(payload);
    return prefixes;
}

bool isLocalQualifiedReference(const QString &name, const QStringList &localPrefixes)
{
    for (const QString &prefix : localPrefixes) {
        if (!prefix.isEmpty() && name.startsWith(prefix)) {
            return true;
        }
    }
    return false;
}

QStringList requiredOuterReferences(const logic::LogicNode &node, const QStringList &localPrefixes)
{
    QStringList names;
    if (logic::isSubqueryNodeType(node.type)) {
        return names;
    }
    if (node.type == logic::LogicNodeType::ColumnRef) {
        if (node.reference.scope == logic::LogicReferenceScope::Outer) {
            appendRequiredReference(&names, node.reference.name);
        } else if (node.reference.name.contains(QLatin1Char('.'))
                   && !isLocalQualifiedReference(node.reference.name, localPrefixes)) {
            appendRequiredReference(&names, node.reference.name);
        }
    }
    for (const logic::LogicNode &child : node.children) {
        const QStringList childNames = requiredOuterReferences(child, localPrefixes);
        for (const QString &name : childNames) {
            appendRequiredReference(&names, name);
        }
    }
    return names;
}

logic::LogicRowContext buildRowContext(const tabledef::TableSchema &schema,
                                       const repo::TableData &table,
                                       int rowIndex,
                                       const QString &tableAlias = QString())
{
    logic::LogicRowContext rowContext;
    rowContext.tableName = schema.tableName;

    if (rowIndex < 0 || rowIndex >= table.rows.size()) {
        return rowContext;
    }

    const repo::TableRow &row = table.rows.at(rowIndex);
    for (int columnIndex = 0; columnIndex < schema.columns.size(); ++columnIndex) {
        const tabledef::Column &column = schema.columns.at(columnIndex);
        const QString value = columnIndex < row.size() ? row.at(columnIndex) : QString();
        rowContext.cellsByName.insert(column.name,
                                      logic::LogicCellValue{value,
                                                            column.type,
                                                            value.isEmpty()});
        if (!schema.tableName.trimmed().isEmpty()) {
            rowContext.cellsByName.insert(schema.tableName + QLatin1Char('.') + column.name,
                                          logic::LogicCellValue{value,
                                                                column.type,
                                                                value.isEmpty()});
        }
        if (!tableAlias.trimmed().isEmpty()) {
            rowContext.cellsByName.insert(tableAlias.trimmed() + QLatin1Char('.') + column.name,
                                          logic::LogicCellValue{value,
                                                                column.type,
                                                                value.isEmpty()});
        }
    }
    return rowContext;
}

void mergeBindings(logic::LogicRowContext *rowContext, const logic::CorrelationBindings &bindings)
{
    if (rowContext == nullptr) {
        return;
    }

    for (const logic::CorrelatedBinding &binding : bindings.items) {
        if (!rowContext->cellsByName.contains(binding.name)) {
            rowContext->cellsByName.insert(binding.name,
                                           logic::LogicCellValue{binding.value, binding.type, binding.isNull});
        }
    }
}

QString unqualifiedName(const QString &name)
{
    const int dotIndex = name.lastIndexOf(QLatin1Char('.'));
    return dotIndex >= 0 && dotIndex + 1 < name.size() ? name.mid(dotIndex + 1) : name;
}

QMap<QString, QString> visibleColumnMap(const tabledef::TableSchema &schema, const QString &tableAlias)
{
    QMap<QString, QString> visible;
    for (const tabledef::Column &column : schema.columns) {
        visible.insert(column.name, column.name);
        if (!schema.tableName.trimmed().isEmpty()) {
            visible.insert(schema.tableName + QLatin1Char('.') + column.name, column.name);
        }
        if (!tableAlias.trimmed().isEmpty()) {
            visible.insert(tableAlias.trimmed() + QLatin1Char('.') + column.name, column.name);
        }
    }
    return visible;
}

QString canonicalPrefix(const SelectTableSource &source)
{
    const QString alias = source.tableAlias.trimmed();
    return alias.isEmpty() ? source.tableName.trimmed() : alias;
}

QString qualifiedKey(const SelectTableSource &source, const tabledef::Column &column)
{
    return canonicalPrefix(source) + QLatin1Char('.') + column.name;
}

logic::LogicCellValue nullCell(tabledef::ColumnType type)
{
    return logic::LogicCellValue{QString(), type, true};
}

logic::LogicRowContext buildNullContextFromSample(const logic::LogicRowContext &sample)
{
    logic::LogicRowContext nullContext;
    nullContext.tableName = sample.tableName;
    for (auto it = sample.cellsByName.cbegin(); it != sample.cellsByName.cend(); ++it) {
        nullContext.cellsByName.insert(it.key(), nullCell(it.value().type));
    }
    return nullContext;
}

bool loadSourcesFromPayload(const QVariantMap &payload,
                            QList<SelectTableSource> *sources,
                            QString *error)
{
    if (sources != nullptr) {
        sources->clear();
    }

    QVariantList sourcePayload = payload.value(QStringLiteral("fromSources")).toList();
    if (sourcePayload.isEmpty()) {
        QVariantMap source;
        source.insert(QStringLiteral("tableName"), payload.value(QStringLiteral("tableName")).toString());
        source.insert(QStringLiteral("tableAlias"), payload.value(QStringLiteral("tableAlias")).toString());
        sourcePayload.append(source);
    }

    for (const QVariant &value : sourcePayload) {
        const QVariantMap sourceMap = value.toMap();
        const QString tableName = sourceMap.value(QStringLiteral("tableName")).toString().trimmed();
        const QString tableAlias = sourceMap.value(QStringLiteral("tableAlias")).toString().trimmed();
        if (tableName.isEmpty()) {
            if (error != nullptr) {
                *error = QStringLiteral("SELECT: expected table name");
            }
            return false;
        }

        QString schemaError;
        const tabledef::TableSchema schema = loadUserTableSchema(tableName, &schemaError);
        if (!schemaError.isEmpty()) {
            if (error != nullptr) {
                *error = schemaError;
            }
            return false;
        }

        const QStringList skipLockedTables = QueryExecutionScope::skipSharedReadLockTables();
        const bool skipSharedReadLock = skipLockedTables.contains(tableName, Qt::CaseInsensitive);
        const SelectRowsResult rows = skipSharedReadLock
                                          ? tuple_service::selectRowsUnlocked(tableName,
                                                                              QStringList{},
                                                                              QList<SimpleCondition>{},
                                                                              -1)
                                          : tuple_service::selectRows(tableName,
                                                                      QStringList{},
                                                                      QList<SimpleCondition>{},
                                                                      -1);
        if (!rows.success) {
            if (error != nullptr) {
                *error = rows.errorMessage;
            }
            return false;
        }

        if (sources != nullptr) {
            sources->append(SelectTableSource{tableName, tableAlias, schema, rows.resultTable, true});
        }
    }
    return true;
}

bool validateAndAnnotateSourceNames(QList<SelectTableSource> *sources, QString *error)
{
    if (sources == nullptr) {
        return true;
    }

    QMap<QString, int> tableNameCounts;
    for (const SelectTableSource &source : *sources) {
        tableNameCounts[source.tableName.trimmed()] += 1;
    }

    QSet<QString> reservedUniqueTablePrefixes;
    QSet<QString> aliases;
    QSet<QString> unaliasedTables;
    for (SelectTableSource &source : *sources) {
        const QString tableName = source.tableName.trimmed();
        const QString alias = source.tableAlias.trimmed();
        if (tableName.isEmpty()) {
            if (error != nullptr) {
                *error = QStringLiteral("SELECT: expected table name");
            }
            return false;
        }
        if (alias.isEmpty()) {
            if (unaliasedTables.contains(tableName) || tableNameCounts.value(tableName) > 1) {
                if (error != nullptr) {
                    *error = QStringLiteral("SELECT: duplicate table '%1' requires aliases").arg(tableName);
                }
                return false;
            }
            unaliasedTables.insert(tableName);
        }

        if (tableNameCounts.value(tableName) == 1) {
            if (reservedUniqueTablePrefixes.contains(tableName) || aliases.contains(tableName)) {
                if (error != nullptr) {
                    *error = QStringLiteral("SELECT: duplicate table qualifier '%1'").arg(tableName);
                }
                return false;
            }
            reservedUniqueTablePrefixes.insert(tableName);
        }

        if (!alias.isEmpty()) {
            if (aliases.contains(alias)
                || reservedUniqueTablePrefixes.contains(alias)
                || tableNameCounts.contains(alias)) {
                if (error != nullptr) {
                    *error = QStringLiteral("SELECT: duplicate table qualifier '%1'").arg(alias);
                }
                return false;
            }
            aliases.insert(alias);
        }

        source.tableNameQualifierVisible = tableNameCounts.value(tableName) == 1;
    }
    return true;
}

bool buildMultiNameResolution(const QList<SelectTableSource> &sources,
                              MultiNameResolution *resolution,
                              QString *error)
{
    if (resolution == nullptr) {
        return true;
    }
    resolution->visibleNameToKey.clear();
    resolution->keyTypes.clear();
    resolution->ambiguousBareColumns.clear();
    resolution->starKeys.clear();
    resolution->starOutputColumns.clear();

    QMap<QString, QString> firstBareColumnKey;
    for (const SelectTableSource &source : sources) {
        const QString prefix = canonicalPrefix(source);
        if (prefix.isEmpty()) {
            if (error != nullptr) {
                *error = QStringLiteral("SELECT: expected table name");
            }
            return false;
        }

        for (const tabledef::Column &column : source.schema.columns) {
            const QString key = qualifiedKey(source, column);
            resolution->keyTypes.insert(key, column.type);
            resolution->starKeys.append(key);
            resolution->starOutputColumns.append(key);

            resolution->visibleNameToKey.insert(prefix + QLatin1Char('.') + column.name, key);
            if (source.tableNameQualifierVisible) {
                resolution->visibleNameToKey.insert(source.tableName + QLatin1Char('.') + column.name, key);
            }
            if (!source.tableAlias.trimmed().isEmpty()) {
                resolution->visibleNameToKey.insert(source.tableAlias + QLatin1Char('.') + column.name, key);
            }

            if (!firstBareColumnKey.contains(column.name)) {
                firstBareColumnKey.insert(column.name, key);
            } else if (firstBareColumnKey.value(column.name) != key) {
                resolution->ambiguousBareColumns.insert(column.name);
            }
        }
    }

    for (auto it = firstBareColumnKey.cbegin(); it != firstBareColumnKey.cend(); ++it) {
        if (!resolution->ambiguousBareColumns.contains(it.key())) {
            resolution->visibleNameToKey.insert(it.key(), it.value());
        }
    }
    return true;
}

bool resolveMultiColumn(const MultiNameResolution &resolution,
                        const QString &name,
                        QString *resolvedKey,
                        QString *error)
{
    const QString trimmed = name.trimmed();
    if (resolution.ambiguousBareColumns.contains(trimmed)) {
        if (error != nullptr) {
            *error = QStringLiteral("ambiguous column '%1'").arg(trimmed);
        }
        return false;
    }
    const auto found = resolution.visibleNameToKey.constFind(trimmed);
    if (found == resolution.visibleNameToKey.constEnd()) {
        const int dotIndex = trimmed.indexOf(QLatin1Char('.'));
        if (error != nullptr) {
            if (dotIndex > 0) {
                *error = QStringLiteral("unknown table or alias '%1'").arg(trimmed.left(dotIndex));
            } else {
                *error = QStringLiteral("SELECT: column '%1' does not exist").arg(trimmed);
            }
        }
        return false;
    }
    if (resolvedKey != nullptr) {
        *resolvedKey = found.value();
    }
    return true;
}

logic::LogicRowContext buildSourceRowContext(const SelectTableSource &source, int rowIndex)
{
    logic::LogicRowContext rowContext;
    rowContext.tableName = source.tableName;
    if (rowIndex < 0 || rowIndex >= source.data.rows.size()) {
        return rowContext;
    }

    const repo::TableRow &row = source.data.rows.at(rowIndex);
    const QString prefix = canonicalPrefix(source);
    for (int columnIndex = 0; columnIndex < source.schema.columns.size(); ++columnIndex) {
        const tabledef::Column &column = source.schema.columns.at(columnIndex);
        const QString value = columnIndex < row.size() ? row.at(columnIndex) : QString();
        const logic::LogicCellValue cell{value, column.type, value.isEmpty()};
        rowContext.cellsByName.insert(prefix + QLatin1Char('.') + column.name, cell);
        if (source.tableNameQualifierVisible) {
            rowContext.cellsByName.insert(source.tableName + QLatin1Char('.') + column.name, cell);
        }
        if (!source.tableAlias.trimmed().isEmpty()) {
            rowContext.cellsByName.insert(source.tableAlias + QLatin1Char('.') + column.name, cell);
        }
        rowContext.cellsByName.insert(qualifiedKey(source, column), cell);
    }
    return rowContext;
}

logic::LogicRowContext buildNullSourceContext(const SelectTableSource &source)
{
    logic::LogicRowContext rowContext;
    rowContext.tableName = source.tableName;
    const QString prefix = canonicalPrefix(source);
    for (const tabledef::Column &column : source.schema.columns) {
        const logic::LogicCellValue cell = nullCell(column.type);
        rowContext.cellsByName.insert(prefix + QLatin1Char('.') + column.name, cell);
        if (source.tableNameQualifierVisible) {
            rowContext.cellsByName.insert(source.tableName + QLatin1Char('.') + column.name, cell);
        }
        if (!source.tableAlias.trimmed().isEmpty()) {
            rowContext.cellsByName.insert(source.tableAlias + QLatin1Char('.') + column.name, cell);
        }
        rowContext.cellsByName.insert(qualifiedKey(source, column), cell);
    }
    return rowContext;
}

logic::LogicRowContext mergeRowContexts(const logic::LogicRowContext &left,
                                        const logic::LogicRowContext &right)
{
    logic::LogicRowContext merged = left;
    for (auto it = right.cellsByName.cbegin(); it != right.cellsByName.cend(); ++it) {
        merged.cellsByName.insert(it.key(), it.value());
    }
    return merged;
}

void removeAmbiguousBareColumns(logic::LogicRowContext *rowContext,
                                const MultiNameResolution &resolution)
{
    if (rowContext == nullptr) {
        return;
    }
    for (const QString &name : resolution.ambiguousBareColumns) {
        rowContext->cellsByName.remove(name);
    }
}

void addUniqueBareColumns(logic::LogicRowContext *rowContext,
                          const MultiNameResolution &resolution)
{
    if (rowContext == nullptr) {
        return;
    }
    for (auto it = resolution.visibleNameToKey.cbegin(); it != resolution.visibleNameToKey.cend(); ++it) {
        if (it.key().contains(QLatin1Char('.'))) {
            continue;
        }
        const auto cell = rowContext->cellsByName.constFind(it.value());
        if (cell != rowContext->cellsByName.constEnd()) {
            rowContext->cellsByName.insert(it.key(), cell.value());
        }
    }
}

bool resolveColumnName(const QMap<QString, QString> &visibleColumns,
                       const QString &name,
                       QString *resolved,
                       QString *error)
{
    const auto found = visibleColumns.constFind(name.trimmed());
    if (found == visibleColumns.constEnd()) {
        if (error != nullptr) {
            *error = QStringLiteral("SELECT: column '%1' does not exist").arg(name);
        }
        return false;
    }
    if (resolved != nullptr) {
        *resolved = found.value();
    }
    return true;
}

QVariantList projectionItemsFromPayload(const QVariantMap &payload)
{
    QVariantList items = payload.value(QStringLiteral("projectionItems")).toList();
    if (!items.isEmpty()) {
        return items;
    }
    const QStringList projection = payload.value(QStringLiteral("projection")).toStringList();
    for (const QString &columnName : projection) {
        QVariantMap item;
        item.insert(QStringLiteral("sourceColumn"), columnName);
        item.insert(QStringLiteral("outputColumn"), unqualifiedName(columnName));
        items.append(item);
    }
    return items;
}

bool applySimpleConditions(const repo::TableData &table,
                           int rowIndex,
                           const QList<SimpleCondition> &conditions)
{
    if (conditions.isEmpty()) {
        return true;
    }

    const repo::TableRow &row = table.rows.at(rowIndex);
    for (const SimpleCondition &condition : conditions) {
        const int columnIndex = table.columns.indexOf(condition.columnName);
        if (columnIndex < 0) {
            return false;
        }
        if (row.value(columnIndex) != condition.value) {
            return false;
        }
    }
    return true;
}

bool evaluateFilter(const logic::LogicNode *ast,
                    logic::LogicRowContext rowContext,
                    const logic::CorrelationBindings *bindings,
                    const logic::LogicEvalContext &evalContext,
                    QString *error)
{
    if (bindings != nullptr) {
        mergeBindings(&rowContext, *bindings);
    }
    if (ast == nullptr) {
        return true;
    }
    const logic::LogicEvalResult evalResult = logic::evaluateLogicExpression(*ast, rowContext, evalContext);
    if (!evalResult.success) {
        if (error != nullptr) {
            *error = evalResult.error.message;
        }
        return false;
    }
    return evalResult.truth == logic::LogicTruthValue::True;
}

QVector<logic::LogicRowContext> rowsForSource(const SelectTableSource &source)
{
    QVector<logic::LogicRowContext> rows;
    rows.reserve(source.data.rows.size());
    for (int rowIndex = 0; rowIndex < source.data.rows.size(); ++rowIndex) {
        rows.append(buildSourceRowContext(source, rowIndex));
    }
    return rows;
}

QVector<logic::LogicRowContext> joinRowsets(const QVector<logic::LogicRowContext> &leftRows,
                                            const SelectTableSource &rightSource,
                                            const QString &joinType,
                                            const MultiNameResolution &resolution,
                                            const logic::LogicNode *onAst,
                                            const logic::CorrelationBindings *bindings,
                                            const logic::LogicEvalContext &evalContext,
                                            const logic::LogicRowContext *leftNullTemplate,
                                            QString *error)
{
    QVector<logic::LogicRowContext> output;
    const QVector<logic::LogicRowContext> rightRows = rowsForSource(rightSource);
    QVector<bool> matchedRight(rightRows.size(), false);
    const logic::LogicRowContext nullRight = buildNullSourceContext(rightSource);

    for (const logic::LogicRowContext &leftRow : leftRows) {
        bool matchedLeft = false;
        for (int rightIndex = 0; rightIndex < rightRows.size(); ++rightIndex) {
            logic::LogicRowContext combined = mergeRowContexts(leftRow, rightRows.at(rightIndex));
            removeAmbiguousBareColumns(&combined, resolution);
            addUniqueBareColumns(&combined, resolution);
            QString evalError;
            const bool matched = evaluateFilter(onAst, combined, bindings, evalContext, &evalError);
            if (!evalError.isEmpty()) {
                if (error != nullptr) {
                    *error = evalError;
                }
                return {};
            }
            if (matched) {
                matchedLeft = true;
                matchedRight[rightIndex] = true;
                output.append(combined);
            }
        }
        if (!matchedLeft && (joinType == QStringLiteral("left") || joinType == QStringLiteral("full"))) {
            output.append(mergeRowContexts(leftRow, nullRight));
        }
    }

    if (joinType == QStringLiteral("right") || joinType == QStringLiteral("full")) {
        const logic::LogicRowContext nullLeft = !leftRows.isEmpty()
                                                    ? buildNullContextFromSample(leftRows.first())
                                                    : (leftNullTemplate != nullptr ? *leftNullTemplate : logic::LogicRowContext{});
        for (int rightIndex = 0; rightIndex < rightRows.size(); ++rightIndex) {
            if (!matchedRight.value(rightIndex)) {
                output.append(mergeRowContexts(nullLeft, rightRows.at(rightIndex)));
            }
        }
    }
    return output;
}

bool buildJoinedFilteredRows(QueryExecutor *executor,
                             const sqlparser::ParseResult &parsed,
                             const logic::CorrelationBindings *bindings,
                             QList<SelectTableSource> *sources,
                             MultiNameResolution *resolution,
                             QVector<logic::LogicRowContext> *filteredRows,
                             QString *error)
{
    if (sources != nullptr) {
        sources->clear();
    }
    if (filteredRows != nullptr) {
        filteredRows->clear();
    }

    QList<SelectTableSource> loadedSources;
    if (!loadSourcesFromPayload(parsed.payload, &loadedSources, error)) {
        return false;
    }
    if (!validateAndAnnotateSourceNames(&loadedSources, error)) {
        return false;
    }
    if (loadedSources.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("SELECT: expected table name");
        }
        return false;
    }

    MultiNameResolution builtResolution;
    if (!buildMultiNameResolution(loadedSources, &builtResolution, error)) {
        return false;
    }

    logic::LogicEvalContext evalContext;
    evalContext.subqueryExecutor = executor;
    evalContext.currentDatabase = currentDatabase;
    evalContext.dataRoot = getDataRoot();
    evalContext.skipSharedReadLockTables = QueryExecutionScope::skipSharedReadLockTables();
    evalContext.allowSubquery = true;

    logic::LogicNode whereAst;
    const bool hasWhereAst = parsed.payload.contains(QStringLiteral("whereAst"));
    if (hasWhereAst) {
        whereAst = parsed.payload.value(QStringLiteral("whereAst")).value<logic::LogicNode>();
    }

    logic::LogicRowContext nullTemplate = buildNullSourceContext(loadedSources.first());
    QVector<logic::LogicRowContext> joinedRows = rowsForSource(loadedSources.first());
    const QVariantList joinPayload = parsed.payload.value(QStringLiteral("joins")).toList();
    if (joinPayload.isEmpty()) {
        for (int sourceIndex = 1; sourceIndex < loadedSources.size(); ++sourceIndex) {
            joinedRows = joinRowsets(joinedRows,
                                     loadedSources.at(sourceIndex),
                                     QStringLiteral("inner"),
                                     builtResolution,
                                     nullptr,
                                     bindings,
                                     evalContext,
                                     &nullTemplate,
                                     error);
            if (error != nullptr && !error->isEmpty()) {
                return false;
            }
            nullTemplate = mergeRowContexts(nullTemplate, buildNullSourceContext(loadedSources.at(sourceIndex)));
        }
    } else {
        for (const QVariant &joinValue : joinPayload) {
            const QVariantMap joinMap = joinValue.toMap();
            const int rightIndex = joinMap.value(QStringLiteral("rightSourceIndex")).toInt();
            if (rightIndex <= 0 || rightIndex >= loadedSources.size()) {
                if (error != nullptr) {
                    *error = QStringLiteral("SELECT: invalid JOIN payload");
                }
                return false;
            }
            const logic::LogicNode onAst = joinMap.value(QStringLiteral("onAst")).value<logic::LogicNode>();
            joinedRows = joinRowsets(joinedRows,
                                     loadedSources.at(rightIndex),
                                     joinMap.value(QStringLiteral("joinType"), QStringLiteral("inner")).toString(),
                                     builtResolution,
                                     &onAst,
                                     bindings,
                                     evalContext,
                                     &nullTemplate,
                                     error);
            if (error != nullptr && !error->isEmpty()) {
                return false;
            }
            nullTemplate = mergeRowContexts(nullTemplate, buildNullSourceContext(loadedSources.at(rightIndex)));
        }
    }

    for (logic::LogicRowContext &row : joinedRows) {
        removeAmbiguousBareColumns(&row, builtResolution);
        addUniqueBareColumns(&row, builtResolution);
    }

    QVector<logic::LogicRowContext> rows;
    rows.reserve(joinedRows.size());
    for (logic::LogicRowContext row : joinedRows) {
        QString evalError;
        const bool include = evaluateFilter(hasWhereAst ? &whereAst : nullptr,
                                            row,
                                            bindings,
                                            evalContext,
                                            &evalError);
        if (!evalError.isEmpty()) {
            if (error != nullptr) {
                *error = evalError;
            }
            return false;
        }
        if (include) {
            rows.append(row);
        }
    }

    if (sources != nullptr) {
        *sources = loadedSources;
    }
    if (resolution != nullptr) {
        *resolution = builtResolution;
    }
    if (filteredRows != nullptr) {
        *filteredRows = rows;
    }
    return true;
}

bool resolveMultiProjection(const QVariantMap &payload,
                            const MultiNameResolution &resolution,
                            QList<MultiProjectionItem> *projectionItems,
                            QStringList *outputColumns,
                            QList<tabledef::ColumnType> *columnTypes,
                            QString *error)
{
    if (projectionItems != nullptr) projectionItems->clear();
    if (outputColumns != nullptr) outputColumns->clear();
    if (columnTypes != nullptr) columnTypes->clear();

    const bool selectAll = payload.value(QStringLiteral("selectAll"), false).toBool();
    if (selectAll) {
        for (int i = 0; i < resolution.starKeys.size(); ++i) {
            const QString key = resolution.starKeys.at(i);
            const tabledef::ColumnType type = resolution.keyTypes.value(key, tabledef::ColumnType::Varchar);
            const QString outputName = resolution.starOutputColumns.value(i, key);
            if (projectionItems != nullptr) {
                projectionItems->append(MultiProjectionItem{key, key, outputName, type});
            }
            if (outputColumns != nullptr) outputColumns->append(outputName);
            if (columnTypes != nullptr) columnTypes->append(type);
        }
        return true;
    }

    const QVariantList items = projectionItemsFromPayload(payload);
    if (items.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("SELECT: expected projection columns");
        }
        return false;
    }
    for (const QVariant &value : items) {
        const QVariantMap item = value.toMap();
        const QString sourceName = item.value(QStringLiteral("sourceColumn")).toString();
        const QString outputName = item.value(QStringLiteral("outputColumn"), unqualifiedName(sourceName)).toString();
        QString resolvedKey;
        if (!resolveMultiColumn(resolution, sourceName, &resolvedKey, error)) {
            return false;
        }
        const tabledef::ColumnType type = resolution.keyTypes.value(resolvedKey, tabledef::ColumnType::Varchar);
        const QString finalOutputName = outputName.trimmed().isEmpty() ? unqualifiedName(sourceName) : outputName;
        if (projectionItems != nullptr) {
            projectionItems->append(MultiProjectionItem{sourceName, resolvedKey, finalOutputName, type});
        }
        if (outputColumns != nullptr) outputColumns->append(finalOutputName);
        if (columnTypes != nullptr) columnTypes->append(type);
    }
    return true;
}

QueryExecuteResult makeResultFromSelect(const SelectRowsResult &selectResult,
                                        const QString &text);

bool resolveMultiOrderBy(const QVariantMap &payload,
                         const MultiNameResolution &resolution,
                         const QList<MultiProjectionItem> &projectionItems,
                         QString *orderKey,
                         tabledef::ColumnType *orderType,
                         bool *descending,
                         QString *error)
{
    if (orderKey != nullptr) orderKey->clear();
    if (orderType != nullptr) *orderType = tabledef::ColumnType::Varchar;
    if (descending != nullptr) *descending = payload.value(QStringLiteral("orderByDescending"), false).toBool();

    const QString rawOrderBy = payload.value(QStringLiteral("orderByColumn")).toString().trimmed();
    if (rawOrderBy.isEmpty()) {
        return true;
    }
    for (const MultiProjectionItem &item : projectionItems) {
        if (item.outputName == rawOrderBy) {
            if (orderKey != nullptr) *orderKey = item.resolvedKey;
            if (orderType != nullptr) *orderType = item.type;
            return true;
        }
    }
    QString resolved;
    if (!resolveMultiColumn(resolution, rawOrderBy, &resolved, error)) {
        return false;
    }
    if (orderKey != nullptr) *orderKey = resolved;
    if (orderType != nullptr) *orderType = resolution.keyTypes.value(resolved, tabledef::ColumnType::Varchar);
    return true;
}

int compareCellValues(const QString &left,
                      const QString &right,
                      tabledef::ColumnType type)
{
    if (left.isEmpty() && right.isEmpty()) return 0;
    if (left.isEmpty()) return -1;
    if (right.isEmpty()) return 1;
    if (type == tabledef::ColumnType::Int) {
        bool leftOk = false;
        bool rightOk = false;
        const qlonglong leftValue = left.toLongLong(&leftOk);
        const qlonglong rightValue = right.toLongLong(&rightOk);
        if (leftOk && rightOk) {
            return leftValue < rightValue ? -1 : (leftValue > rightValue ? 1 : 0);
        }
    } else if (type == tabledef::ColumnType::Float) {
        bool leftOk = false;
        bool rightOk = false;
        const double leftValue = left.toDouble(&leftOk);
        const double rightValue = right.toDouble(&rightOk);
        if (leftOk && rightOk) {
            return leftValue < rightValue ? -1 : (leftValue > rightValue ? 1 : 0);
        }
    }
    const int cmp = QString::compare(left, right);
    return cmp < 0 ? -1 : (cmp > 0 ? 1 : 0);
}

void sortJoinedRows(QVector<logic::LogicRowContext> *rows,
                    const QString &orderKey,
                    tabledef::ColumnType orderType,
                    bool descending)
{
    if (rows == nullptr || orderKey.trimmed().isEmpty()) {
        return;
    }
    std::stable_sort(rows->begin(), rows->end(), [&](const logic::LogicRowContext &left, const logic::LogicRowContext &right) {
        const QString leftValue = left.cellsByName.value(orderKey).value;
        const QString rightValue = right.cellsByName.value(orderKey).value;
        const int comparison = compareCellValues(leftValue, rightValue, orderType);
        return descending ? comparison > 0 : comparison < 0;
    });
}

bool aggregateFunctionFromName(const QString &name, AggregateFunction *function)
{
    const QString upper = name.trimmed().toUpper();
    if (upper == QStringLiteral("COUNT")) {
        if (function != nullptr) *function = AggregateFunction::Count;
        return true;
    }
    if (upper == QStringLiteral("SUM")) {
        if (function != nullptr) *function = AggregateFunction::Sum;
        return true;
    }
    if (upper == QStringLiteral("AVG")) {
        if (function != nullptr) *function = AggregateFunction::Avg;
        return true;
    }
    if (upper == QStringLiteral("MIN")) {
        if (function != nullptr) *function = AggregateFunction::Min;
        return true;
    }
    if (upper == QStringLiteral("MAX")) {
        if (function != nullptr) *function = AggregateFunction::Max;
        return true;
    }
    return false;
}

bool isNumericType(tabledef::ColumnType type)
{
    return type == tabledef::ColumnType::Int || type == tabledef::ColumnType::Float;
}

QString aggregateFunctionDisplayName(AggregateFunction function)
{
    switch (function) {
    case AggregateFunction::Count: return QStringLiteral("COUNT");
    case AggregateFunction::Sum: return QStringLiteral("SUM");
    case AggregateFunction::Avg: return QStringLiteral("AVG");
    case AggregateFunction::Min: return QStringLiteral("MIN");
    case AggregateFunction::Max: return QStringLiteral("MAX");
    }
    return QString();
}

bool resolveAggregateSpecs(const QVariantMap &payload,
                           const MultiNameResolution &resolution,
                           QList<AggregateSpec> *specs,
                           QString *error)
{
    if (specs != nullptr) specs->clear();
    const QVariantList items = payload.value(QStringLiteral("aggregateItems")).toList();
    for (int i = 0; i < items.size(); ++i) {
        const QVariantMap item = items.at(i).toMap();
        AggregateFunction function;
        if (!aggregateFunctionFromName(item.value(QStringLiteral("functionName")).toString(), &function)) {
            if (error != nullptr) {
                *error = QStringLiteral("SELECT: unsupported aggregate function");
            }
            return false;
        }

        AggregateSpec spec;
        spec.function = function;
        spec.argumentName = item.value(QStringLiteral("argument")).toString().trimmed();
        spec.isStar = item.value(QStringLiteral("isStar")).toBool();
        spec.sourceText = item.value(QStringLiteral("sourceText")).toString();
        spec.syntheticName = item.value(QStringLiteral("syntheticName"), QStringLiteral("__agg_%1").arg(i)).toString();
        spec.outputName = item.value(QStringLiteral("outputColumn"), spec.sourceText).toString();

        if (spec.isStar) {
            if (function != AggregateFunction::Count) {
                if (error != nullptr) {
                    *error = QStringLiteral("SELECT: unsupported aggregate argument");
                }
                return false;
            }
            spec.outputType = tabledef::ColumnType::Int;
        } else {
            if (!resolveMultiColumn(resolution, spec.argumentName, &spec.resolvedArgumentKey, error)) {
                return false;
            }
            const tabledef::ColumnType argumentType = resolution.keyTypes.value(spec.resolvedArgumentKey, tabledef::ColumnType::Varchar);
            if ((function == AggregateFunction::Sum || function == AggregateFunction::Avg)
                && !isNumericType(argumentType)) {
                if (error != nullptr) {
                    *error = QStringLiteral("aggregate function '%1' requires a numeric column")
                                 .arg(aggregateFunctionDisplayName(function));
                }
                return false;
            }
            if (function == AggregateFunction::Count) {
                spec.outputType = tabledef::ColumnType::Int;
            } else if (function == AggregateFunction::Avg) {
                spec.outputType = tabledef::ColumnType::Float;
            } else {
                spec.outputType = argumentType;
            }
        }
        if (specs != nullptr) {
            specs->append(spec);
        }
    }
    return true;
}

bool resolveGroupKeys(const QVariantMap &payload,
                      const MultiNameResolution &resolution,
                      QStringList *groupKeys,
                      QString *error)
{
    if (groupKeys != nullptr) groupKeys->clear();
    const QStringList groupColumns = payload.value(QStringLiteral("groupByColumns")).toStringList();
    for (const QString &column : groupColumns) {
        QString resolved;
        if (!resolveMultiColumn(resolution, column, &resolved, error)) {
            if (error != nullptr && error->startsWith(QStringLiteral("SELECT: column"))) {
                *error = QStringLiteral("GROUP BY column '%1' does not exist").arg(column);
            }
            return false;
        }
        if (groupKeys != nullptr) {
            groupKeys->append(resolved);
        }
    }
    return true;
}

QString serializeGroupKeyPart(const logic::LogicCellValue &cell)
{
    const QString value = cell.isNull ? QStringLiteral("<NULL>") : cell.value;
    return QString::number(value.size()) + QLatin1Char(':') + value;
}

QVector<GroupedRows> groupRows(const QVector<logic::LogicRowContext> &rows,
                               const QStringList &groupKeys,
                               bool aggregateQuery)
{
    QVector<GroupedRows> groups;
    if (groupKeys.isEmpty()) {
        if (aggregateQuery) {
            GroupedRows group;
            group.groupKey = QStringLiteral("__all__");
            group.rows = rows;
            if (!rows.isEmpty()) {
                group.representative = rows.first();
            }
            groups.append(group);
        }
        return groups;
    }

    QMap<QString, int> groupIndexByKey;
    for (const logic::LogicRowContext &row : rows) {
        QString serialized;
        for (const QString &key : groupKeys) {
            serialized += serializeGroupKeyPart(row.cellsByName.value(key));
            serialized += QLatin1Char('|');
        }
        int groupIndex = groupIndexByKey.value(serialized, -1);
        if (groupIndex < 0) {
            GroupedRows group;
            group.groupKey = serialized;
            group.representative = row;
            groupIndex = groups.size();
            groups.append(group);
            groupIndexByKey.insert(serialized, groupIndex);
        }
        groups[groupIndex].rows.append(row);
    }
    return groups;
}

logic::LogicCellValue computeAggregateValue(const AggregateSpec &spec,
                                            const GroupedRows &group)
{
    if (spec.function == AggregateFunction::Count) {
        int count = 0;
        if (spec.isStar) {
            count = group.rows.size();
        } else {
            for (const logic::LogicRowContext &row : group.rows) {
                const logic::LogicCellValue cell = row.cellsByName.value(spec.resolvedArgumentKey);
                if (!cell.isNull && !cell.value.isEmpty()) {
                    ++count;
                }
            }
        }
        return logic::LogicCellValue{QString::number(count), tabledef::ColumnType::Int, false};
    }

    bool hasValue = false;
    double numericTotal = 0.0;
    int numericCount = 0;
    logic::LogicCellValue bestValue;

    for (const logic::LogicRowContext &row : group.rows) {
        const logic::LogicCellValue cell = row.cellsByName.value(spec.resolvedArgumentKey);
        if (cell.isNull || cell.value.isEmpty()) {
            continue;
        }
        if (spec.function == AggregateFunction::Sum || spec.function == AggregateFunction::Avg) {
            bool ok = false;
            const double value = cell.value.toDouble(&ok);
            if (!ok) {
                continue;
            }
            numericTotal += value;
            ++numericCount;
            hasValue = true;
            continue;
        }
        if (!hasValue) {
            bestValue = cell;
            hasValue = true;
            continue;
        }
        const int comparison = compareCellValues(cell.value, bestValue.value, cell.type);
        if ((spec.function == AggregateFunction::Min && comparison < 0)
            || (spec.function == AggregateFunction::Max && comparison > 0)) {
            bestValue = cell;
        }
    }

    if (!hasValue) {
        return logic::LogicCellValue{QString(), spec.outputType, true};
    }
    if (spec.function == AggregateFunction::Sum) {
        const QString value = spec.outputType == tabledef::ColumnType::Int
                                  ? QString::number(static_cast<qlonglong>(std::llround(numericTotal)))
                                  : QString::number(numericTotal, 'g', 15);
        return logic::LogicCellValue{value, spec.outputType, false};
    }
    if (spec.function == AggregateFunction::Avg) {
        const double average = numericCount == 0 ? 0.0 : numericTotal / numericCount;
        return logic::LogicCellValue{QString::number(average, 'g', 15), tabledef::ColumnType::Float, false};
    }
    return bestValue;
}

bool aggregateProjectionItems(const QVariantMap &payload,
                              const MultiNameResolution &resolution,
                              const QStringList &groupKeys,
                              const QList<AggregateSpec> &specs,
                              QList<AggregateProjectionItem> *projectionItems,
                              QStringList *outputColumns,
                              QList<tabledef::ColumnType> *columnTypes,
                              QString *error)
{
    if (projectionItems != nullptr) projectionItems->clear();
    if (outputColumns != nullptr) outputColumns->clear();
    if (columnTypes != nullptr) columnTypes->clear();

    if (payload.value(QStringLiteral("selectAll"), false).toBool()) {
        if (error != nullptr) {
            *error = QStringLiteral("SELECT: '*' is not supported in aggregate queries");
        }
        return false;
    }

    const QVariantList items = projectionItemsFromPayload(payload);
    QSet<QString> outputNames;
    for (const QVariant &value : items) {
        const QVariantMap item = value.toMap();
        const QString itemKind = item.value(QStringLiteral("itemKind"), QStringLiteral("column")).toString();
        const QString sourceName = item.value(QStringLiteral("sourceColumn")).toString();
        const QString outputName = item.value(QStringLiteral("outputColumn"), unqualifiedName(sourceName)).toString();
        AggregateProjectionItem projectionItem;
        projectionItem.isAggregate = itemKind == QStringLiteral("aggregate");
        projectionItem.sourceName = sourceName;
        projectionItem.outputName = outputName.trimmed().isEmpty() ? unqualifiedName(sourceName) : outputName;
        if (outputNames.contains(projectionItem.outputName)) {
            if (error != nullptr) {
                *error = QStringLiteral("SELECT: duplicate output alias '%1'").arg(projectionItem.outputName);
            }
            return false;
        }
        outputNames.insert(projectionItem.outputName);

        if (projectionItem.isAggregate) {
            const int aggregateIndex = item.value(QStringLiteral("aggregateIndex"), -1).toInt();
            if (aggregateIndex < 0 || aggregateIndex >= specs.size()) {
                if (error != nullptr) {
                    *error = QStringLiteral("SELECT aggregate payload is incomplete");
                }
                return false;
            }
            projectionItem.aggregateIndex = aggregateIndex;
            projectionItem.resolvedKey = specs.at(aggregateIndex).syntheticName;
            projectionItem.outputType = specs.at(aggregateIndex).outputType;
        } else {
            QString resolvedKey;
            if (!resolveMultiColumn(resolution, sourceName, &resolvedKey, error)) {
                return false;
            }
            if (!groupKeys.contains(resolvedKey)) {
                if (error != nullptr) {
                    *error = QStringLiteral("SELECT: non-aggregate column '%1' must appear in GROUP BY").arg(sourceName);
                }
                return false;
            }
            projectionItem.resolvedKey = resolvedKey;
            projectionItem.outputType = resolution.keyTypes.value(resolvedKey, tabledef::ColumnType::Varchar);
        }

        if (projectionItems != nullptr) projectionItems->append(projectionItem);
        if (outputColumns != nullptr) outputColumns->append(projectionItem.outputName);
        if (columnTypes != nullptr) columnTypes->append(projectionItem.outputType);
    }
    return true;
}

logic::LogicRowContext buildAggregateRowContext(const GroupedRows &group,
                                                const QStringList &groupKeys,
                                                const MultiNameResolution &resolution,
                                                const QList<AggregateSpec> &specs,
                                                const QList<AggregateProjectionItem> &projectionItems)
{
    logic::LogicRowContext rowContext;

    for (const QString &key : groupKeys) {
        if (group.representative.cellsByName.contains(key)) {
            const logic::LogicCellValue value = group.representative.cellsByName.value(key);
            rowContext.cellsByName.insert(key, value);
            for (auto it = resolution.visibleNameToKey.cbegin(); it != resolution.visibleNameToKey.cend(); ++it) {
                if (it.value() == key) {
                    rowContext.cellsByName.insert(it.key(), value);
                }
            }
        }
    }

    for (int i = 0; i < specs.size(); ++i) {
        const AggregateSpec &spec = specs.at(i);
        const logic::LogicCellValue value = computeAggregateValue(spec, group);
        rowContext.cellsByName.insert(spec.syntheticName, value);
        if (!spec.sourceText.isEmpty()) {
            rowContext.cellsByName.insert(spec.sourceText, value);
        }
        if (!spec.outputName.isEmpty()) {
            rowContext.cellsByName.insert(spec.outputName, value);
        }
    }

    for (const AggregateProjectionItem &item : projectionItems) {
        if (item.isAggregate && item.aggregateIndex >= 0 && item.aggregateIndex < specs.size()) {
            const AggregateSpec &spec = specs.at(item.aggregateIndex);
            rowContext.cellsByName.insert(item.outputName, rowContext.cellsByName.value(spec.syntheticName));
        } else if (!item.isAggregate && rowContext.cellsByName.contains(item.resolvedKey)) {
            rowContext.cellsByName.insert(item.outputName, rowContext.cellsByName.value(item.resolvedKey));
        }
    }
    return rowContext;
}

bool resolveAggregateOrderBy(const QVariantMap &payload,
                             const QList<AggregateProjectionItem> &projectionItems,
                             const MultiNameResolution &resolution,
                             const QStringList &groupKeys,
                             const QList<AggregateSpec> &specs,
                             QString *orderKey,
                             tabledef::ColumnType *orderType,
                             bool *descending,
                             QString *error)
{
    if (orderKey != nullptr) orderKey->clear();
    if (orderType != nullptr) *orderType = tabledef::ColumnType::Varchar;
    if (descending != nullptr) *descending = payload.value(QStringLiteral("orderByDescending"), false).toBool();
    const QString rawOrderBy = payload.value(QStringLiteral("orderByColumn")).toString().trimmed();
    if (rawOrderBy.isEmpty()) {
        return true;
    }
    for (const AggregateProjectionItem &item : projectionItems) {
        if (item.outputName == rawOrderBy) {
            if (orderKey != nullptr) *orderKey = item.outputName;
            if (orderType != nullptr) *orderType = item.outputType;
            return true;
        }
    }
    for (const AggregateSpec &spec : specs) {
        if (spec.sourceText == rawOrderBy || spec.syntheticName == rawOrderBy || spec.outputName == rawOrderBy) {
            if (orderKey != nullptr) *orderKey = spec.syntheticName;
            if (orderType != nullptr) *orderType = spec.outputType;
            return true;
        }
    }
    QString resolved;
    if (resolveMultiColumn(resolution, rawOrderBy, &resolved, nullptr) && groupKeys.contains(resolved)) {
        if (orderKey != nullptr) *orderKey = resolved;
        if (orderType != nullptr) *orderType = resolution.keyTypes.value(resolved, tabledef::ColumnType::Varchar);
        return true;
    }
    if (error != nullptr) {
        *error = QStringLiteral("ORDER BY column '%1' does not exist in aggregate result").arg(rawOrderBy);
    }
    return false;
}

void sortAggregateRows(QVector<logic::LogicRowContext> *rows,
                       const QString &orderKey,
                       tabledef::ColumnType orderType,
                       bool descending)
{
    sortJoinedRows(rows, orderKey, orderType, descending);
}

QueryExecuteResult execAggregateSelect(QueryExecutor *executor,
                                       const sqlparser::ParseResult &parsed,
                                       const logic::CorrelationBindings *bindings)
{
    QueryExecuteResult result;
    QString error;
    QList<SelectTableSource> sources;
    MultiNameResolution resolution;
    QVector<logic::LogicRowContext> filteredRows;
    if (!buildJoinedFilteredRows(executor, parsed, bindings, &sources, &resolution, &filteredRows, &error)) {
        result.success = false;
        result.errorMessage = error;
        result.text = error;
        return result;
    }

    QList<AggregateSpec> specs;
    if (!resolveAggregateSpecs(parsed.payload, resolution, &specs, &error)) {
        result.success = false;
        result.errorMessage = error;
        result.text = error;
        return result;
    }
    QStringList groupKeys;
    if (!resolveGroupKeys(parsed.payload, resolution, &groupKeys, &error)) {
        result.success = false;
        result.errorMessage = error;
        result.text = error;
        return result;
    }
    if (specs.isEmpty() && groupKeys.isEmpty() && parsed.payload.contains(QStringLiteral("havingAst"))) {
        result.success = false;
        result.errorMessage = QStringLiteral("HAVING requires GROUP BY or aggregate projection");
        result.text = result.errorMessage;
        return result;
    }

    QList<AggregateProjectionItem> projectionItems;
    QStringList outputColumns;
    QList<tabledef::ColumnType> columnTypes;
    if (!aggregateProjectionItems(parsed.payload,
                                  resolution,
                                  groupKeys,
                                  specs,
                                  &projectionItems,
                                  &outputColumns,
                                  &columnTypes,
                                  &error)) {
        result.success = false;
        result.errorMessage = error;
        result.text = error;
        return result;
    }

    QVector<GroupedRows> groups = groupRows(filteredRows, groupKeys, true);
    QVector<logic::LogicRowContext> aggregateRows;
    aggregateRows.reserve(groups.size());

    logic::LogicEvalContext evalContext;
    evalContext.subqueryExecutor = executor;
    evalContext.currentDatabase = currentDatabase;
    evalContext.dataRoot = getDataRoot();
    evalContext.skipSharedReadLockTables = QueryExecutionScope::skipSharedReadLockTables();
    evalContext.allowSubquery = true;
    logic::LogicNode havingAst;
    const bool hasHavingAst = parsed.payload.contains(QStringLiteral("havingAst"));
    if (hasHavingAst) {
        havingAst = parsed.payload.value(QStringLiteral("havingAst")).value<logic::LogicNode>();
    }

    for (const GroupedRows &group : groups) {
        logic::LogicRowContext aggregateRow = buildAggregateRowContext(group, groupKeys, resolution, specs, projectionItems);
        QString evalError;
        const bool include = evaluateFilter(hasHavingAst ? &havingAst : nullptr,
                                            aggregateRow,
                                            bindings,
                                            evalContext,
                                            &evalError);
        if (!evalError.isEmpty()) {
            result.success = false;
            result.errorMessage = QStringLiteral("HAVING column '%1' does not exist in aggregate result")
                                      .arg(evalError.contains(QLatin1Char('\'')) ? evalError.section(QLatin1Char('\''), 1, 1) : evalError);
            result.text = result.errorMessage;
            return result;
        }
        if (include) {
            aggregateRows.append(aggregateRow);
        }
    }

    QString orderKey;
    tabledef::ColumnType orderType = tabledef::ColumnType::Varchar;
    bool descending = false;
    if (!resolveAggregateOrderBy(parsed.payload,
                                 projectionItems,
                                 resolution,
                                 groupKeys,
                                 specs,
                                 &orderKey,
                                 &orderType,
                                 &descending,
                                 &error)) {
        result.success = false;
        result.errorMessage = error;
        result.text = error;
        return result;
    }
    sortAggregateRows(&aggregateRows, orderKey, orderType, descending);

    SelectRowsResult selectResult;
    selectResult.success = true;
    selectResult.resultTable.columns = outputColumns;
    selectResult.columnTypes = columnTypes;
    const int limit = parsed.payload.value(QStringLiteral("limit"), -1).toInt();
    int emitted = 0;
    for (const logic::LogicRowContext &row : aggregateRows) {
        if (limit >= 0 && emitted >= limit) {
            break;
        }
        repo::TableRow outputRow;
        outputRow.reserve(projectionItems.size());
        for (const AggregateProjectionItem &item : projectionItems) {
            outputRow.append(row.cellsByName.value(item.isAggregate ? item.outputName : item.resolvedKey).value);
        }
        selectResult.resultTable.rows.append(outputRow);
        ++emitted;
    }
    selectResult.affectedRowCount = selectResult.resultTable.rows.size();
    return makeResultFromSelect(selectResult, QString());
}

QueryExecuteResult execMultiTableSelect(QueryExecutor *executor,
                                        const sqlparser::ParseResult &parsed,
                                        const logic::CorrelationBindings *bindings)
{
    QueryExecuteResult result;

    QString error;
    QList<SelectTableSource> sources;
    MultiNameResolution resolution;
    QVector<logic::LogicRowContext> filteredRows;
    if (!buildJoinedFilteredRows(executor, parsed, bindings, &sources, &resolution, &filteredRows, &error)) {
        result.success = false;
        result.errorMessage = error;
        result.text = error;
        return result;
    }

    QList<MultiProjectionItem> projectionItems;
    QStringList outputColumns;
    QList<tabledef::ColumnType> columnTypes;
    if (!resolveMultiProjection(parsed.payload,
                                resolution,
                                &projectionItems,
                                &outputColumns,
                                &columnTypes,
                                &error)) {
        result.success = false;
        result.errorMessage = error;
        result.text = error;
        return result;
    }

    QString orderKey;
    tabledef::ColumnType orderType = tabledef::ColumnType::Varchar;
    bool descending = false;
    if (!resolveMultiOrderBy(parsed.payload,
                             resolution,
                             projectionItems,
                             &orderKey,
                             &orderType,
                             &descending,
                             &error)) {
        result.success = false;
        result.errorMessage = error;
        result.text = error;
        return result;
    }
    sortJoinedRows(&filteredRows, orderKey, orderType, descending);

    const int limit = parsed.payload.value(QStringLiteral("limit"), -1).toInt();
    SelectRowsResult selectResult;
    selectResult.success = true;
    selectResult.resultTable.columns = outputColumns;
    selectResult.columnTypes = columnTypes;

    int emitted = 0;
    for (const logic::LogicRowContext &row : filteredRows) {
        if (limit >= 0 && emitted >= limit) {
            break;
        }
        repo::TableRow outputRow;
        outputRow.reserve(projectionItems.size());
        for (const MultiProjectionItem &item : projectionItems) {
            outputRow.append(row.cellsByName.value(item.resolvedKey).value);
        }
        selectResult.resultTable.rows.append(outputRow);
        ++emitted;
    }
    selectResult.affectedRowCount = selectResult.resultTable.rows.size();
    return makeResultFromSelect(selectResult, QString());
}

QueryExecuteResult makeResultFromSelect(const SelectRowsResult &selectResult,
                                        const QString &text = QString())
{
    QueryExecuteResult result;
    result.success = selectResult.success;
    result.errorMessage = selectResult.errorMessage;
    result.text = text;
    result.affectedRows = selectResult.affectedRowCount;
    result.selectResult = selectResult;
    return result;
}

} // namespace

QueryExecuteResult QueryExecutor::executeSql(const QString &sql,
                                             const QueryExecuteContext &context)
{
    QueryExecutionScope scope(context);
    const sqlparser::ParseResult parsed = sqlparser::parseSql(sql);
    return executeParsed(parsed, context);
}

QueryExecuteResult QueryExecutor::executeParsed(const sqlparser::ParseResult &parsed,
                                                const QueryExecuteContext &context)
{
    QueryExecutionScope scope(context);

    if (!parsed.success) {
        QueryExecuteResult result;
        result.success = false;
        result.errorMessage = parsed.errorMessage;
        result.text = parsed.errorMessage;
        return result;
    }

    if (parsed.commandType == QStringLiteral("SELECT")) {
        return execSelect(parsed, nullptr);
    }

    QueryExecuteResult result;
    result.success = false;
    result.errorMessage = QStringLiteral("QueryExecutor only supports SELECT subqueries");
    return result;
}

QueryExecuteResult QueryExecutor::executeSelectSql(const QString &sql,
                                                   const QueryExecuteContext &context)
{
    QueryExecutionScope scope(context);
    const sqlparser::ParseResult parsed = sqlparser::parseSql(sql);
    if (!parsed.success) {
        QueryExecuteResult result;
        result.success = false;
        result.errorMessage = parsed.errorMessage;
        result.text = parsed.errorMessage;
        return result;
    }
    if (parsed.commandType != QStringLiteral("SELECT")) {
        QueryExecuteResult result;
        result.success = false;
        result.errorMessage = QStringLiteral("QueryExecutor::executeSelectSql only accepts SELECT");
        return result;
    }
    return execSelect(parsed, nullptr);
}

QueryExecuteResult QueryExecutor::executeCorrelatedSelect(const QString &sql,
                                                          const logic::CorrelationBindings &bindings,
                                                          const QueryExecuteContext &context)
{
    QueryExecutionScope scope(context);
    const sqlparser::ParseResult parsed = sqlparser::parseSql(sql);
    if (!parsed.success) {
        QueryExecuteResult result;
        result.success = false;
        result.errorMessage = parsed.errorMessage;
        result.text = parsed.errorMessage;
        return result;
    }
    if (parsed.commandType != QStringLiteral("SELECT")) {
        QueryExecuteResult result;
        result.success = false;
        result.errorMessage = QStringLiteral("QueryExecutor::executeCorrelatedSelect only accepts SELECT");
        return result;
    }
    return execSelect(parsed, &bindings);
}

QueryExecuteResult QueryExecutor::execSelect(const sqlparser::ParseResult &parsed,
                                             const logic::CorrelationBindings *bindings)
{
    QueryExecuteResult result;

    if (currentDatabase.isEmpty()) {
        result.success = false;
        result.errorMessage = QStringLiteral("No database selected. Use USE database_name;");
        result.text = result.errorMessage;
        return result;
    }

    if (parsed.payload.value(QStringLiteral("isAggregateQuery")).toBool()) {
        return execAggregateSelect(this, parsed, bindings);
    }

    if (parsed.payload.value(QStringLiteral("isMultiTable")).toBool()) {
        return execMultiTableSelect(this, parsed, bindings);
    }

    const QString tableName = parsed.payload.value(QStringLiteral("tableName")).toString();
    if (tableName.trimmed().isEmpty()) {
        result.success = false;
        result.errorMessage = QStringLiteral("SELECT: expected table name");
        result.text = result.errorMessage;
        return result;
    }

    QString error;
    const tabledef::TableSchema schema = loadUserTableSchema(tableName, &error);
    if (!error.isEmpty()) {
        result.success = false;
        result.errorMessage = error;
        result.text = error;
        return result;
    }

    const QStringList skipLockedTables = QueryExecutionScope::skipSharedReadLockTables();
    const bool skipSharedReadLock = skipLockedTables.contains(tableName, Qt::CaseInsensitive);
    const SelectRowsResult fullCandidateRows = skipSharedReadLock
                                                   ? tuple_service::selectRowsUnlocked(tableName,
                                                                                       QStringList{},
                                                                                       QList<SimpleCondition>{},
                                                                                       -1)
                                                   : tuple_service::selectRows(tableName,
                                                                               QStringList{},
                                                                               QList<SimpleCondition>{},
                                                                               -1);
    if (!fullCandidateRows.success) {
        result.success = false;
        result.errorMessage = fullCandidateRows.errorMessage;
        result.text = fullCandidateRows.errorMessage;
        return result;
    }
    const repo::TableData &tableData = fullCandidateRows.resultTable;

    logic::LogicEvalContext evalContext;
    evalContext.subqueryExecutor = this;
    evalContext.currentDatabase = currentDatabase;
    evalContext.dataRoot = getDataRoot();
    evalContext.skipSharedReadLockTables = QueryExecutionScope::skipSharedReadLockTables();
    evalContext.allowSubquery = true;

    logic::LogicNode whereAst;
    const bool hasWhereAst = parsed.payload.contains(QStringLiteral("whereAst"));
    if (hasWhereAst) {
        whereAst = parsed.payload.value(QStringLiteral("whereAst")).value<logic::LogicNode>();
    }

    if (bindings != nullptr && hasWhereAst) {
        const QStringList requiredReferences = requiredOuterReferences(whereAst, selectLocalPrefixes(parsed.payload));
        for (const QString &requiredReference : requiredReferences) {
            const bool hasBinding = std::any_of(bindings->items.cbegin(), bindings->items.cend(), [&](const logic::CorrelatedBinding &binding) {
                return binding.name == requiredReference;
            });
            if (!hasBinding) {
                result.success = false;
                result.errorMessage = QStringLiteral("missing correlated binding '%1'").arg(requiredReference);
                result.text = result.errorMessage;
                return result;
            }
        }
    }

    const QStringList projection = parsed.payload.value(QStringLiteral("projection")).toStringList();
    const bool selectAll = parsed.payload.value(QStringLiteral("selectAll"), false).toBool();
    const int limit = parsed.payload.value(QStringLiteral("limit"), -1).toInt();
    const QString tableAlias = parsed.payload.value(QStringLiteral("tableAlias")).toString().trimmed();
    const QMap<QString, QString> visibleColumns = visibleColumnMap(schema, tableAlias);

    auto columnTypeForName = [&](const QString &columnName) {
        for (const tabledef::Column &column : schema.columns) {
            if (column.name == columnName) {
                return column.type;
            }
        }
        return tabledef::ColumnType::Varchar;
    };

    SelectRowsResult selectResult;
    selectResult.success = true;
    QStringList resolvedProjection;
    QStringList outputProjection;

    if (selectAll) {
        selectResult.resultTable.columns = schema.columns.isEmpty()
                                              ? tableData.columns
                                              : tabledef::schemaColumnNames(schema);
        if (!schema.columns.isEmpty()) {
            for (const tabledef::Column &column : schema.columns) {
                selectResult.columnTypes.append(column.type);
            }
        }
    } else {
        if (projection.isEmpty()) {
            result.success = false;
            result.errorMessage = QStringLiteral("SELECT: expected projection columns");
            result.text = result.errorMessage;
            return result;
        }
        const QVariantList projectionItems = projectionItemsFromPayload(parsed.payload);
        for (const QVariant &itemValue : projectionItems) {
            const QVariantMap item = itemValue.toMap();
            const QString sourceColumn = item.value(QStringLiteral("sourceColumn")).toString();
            const QString outputColumn = item.value(QStringLiteral("outputColumn"), unqualifiedName(sourceColumn)).toString();
            QString resolvedColumn;
            if (!resolveColumnName(visibleColumns, sourceColumn, &resolvedColumn, &error)) {
                result.success = false;
                result.errorMessage = error;
                result.text = error;
                return result;
            }
            resolvedProjection.append(resolvedColumn);
            outputProjection.append(outputColumn.trimmed().isEmpty() ? unqualifiedName(sourceColumn) : outputColumn);
            selectResult.columnTypes.append(columnTypeForName(resolvedColumn));
        }
        selectResult.resultTable.columns = outputProjection;
    }

    const bool useSimpleConditions = !hasWhereAst
                                     && parsed.payload.contains(QStringLiteral("conditions"));
    QList<SimpleCondition> simpleConditions;
    if (useSimpleConditions) {
        QString conditionError;
        if (!simpleConditionsFromPayload(parsed.payload.value(QStringLiteral("conditions")).toList(),
                                         &simpleConditions,
                                         &conditionError)) {
            result.success = false;
            result.errorMessage = conditionError;
            result.text = conditionError;
            return result;
        }
    }

    for (int rowIndex = 0; rowIndex < tableData.rows.size(); ++rowIndex) {
        logic::LogicRowContext rowContext = buildRowContext(schema, tableData, rowIndex, tableAlias);
        if (bindings != nullptr) {
            mergeBindings(&rowContext, *bindings);
        }

        bool includeRow = true;
        if (hasWhereAst) {
            const logic::LogicEvalResult whereResult = logic::evaluateLogicExpression(whereAst,
                                                                                      rowContext,
                                                                                      evalContext);
            if (!whereResult.success) {
                result.success = false;
                result.errorMessage = whereResult.error.message;
                result.text = whereResult.error.message;
                return result;
            }
            includeRow = whereResult.truth == logic::LogicTruthValue::True;
        } else if (useSimpleConditions) {
            includeRow = applySimpleConditions(tableData, rowIndex, simpleConditions);
        }

        if (!includeRow) {
            continue;
        }

        repo::TableRow projectedRow;
        if (selectAll) {
            projectedRow = tableData.rows.at(rowIndex);
        } else {
            projectedRow.reserve(projection.size());
            for (const QString &columnName : resolvedProjection) {
                int columnIndex = -1;
                for (int schemaIndex = 0; schemaIndex < schema.columns.size(); ++schemaIndex) {
                    if (schema.columns.at(schemaIndex).name == columnName) {
                        columnIndex = schemaIndex;
                        break;
                    }
                }
                if (columnIndex < 0) {
                    result.success = false;
                    result.errorMessage = QStringLiteral("SELECT: column '%1' does not exist").arg(columnName);
                    result.text = result.errorMessage;
                    return result;
                }
                projectedRow.append(rowContext.cellsByName.value(columnName).value);
            }
        }

        selectResult.resultTable.rows.append(projectedRow);
        if (limit >= 0 && selectResult.resultTable.rows.size() >= limit) {
            break;
        }
    }

    selectResult.affectedRowCount = selectResult.resultTable.rows.size();
    result = makeResultFromSelect(selectResult);
    return result;
}

} // namespace service
