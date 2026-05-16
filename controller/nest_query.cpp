#include "nest_query.h"

#include "../utils/logic/logic.h"
#include "../utils/service_common/service_common.h"

#include <algorithm>
#include <QSet>

namespace service {

namespace {

struct QueryExecutionScope
{
    QString previousDatabase;
    QString previousDataRoot;

    explicit QueryExecutionScope(const QueryExecuteContext &context)
        : previousDatabase(currentDatabase),
          previousDataRoot(getDataRoot())
    {
        if (!context.dataRoot.trimmed().isEmpty()) {
            setDataRoot(context.dataRoot);
        }
        if (!context.currentDatabase.trimmed().isEmpty()) {
            currentDatabase = context.currentDatabase.trimmed();
        }
    }

    ~QueryExecutionScope()
    {
        currentDatabase = previousDatabase;
        setDataRoot(previousDataRoot);
    }
};

struct SelectTableSource
{
    QString tableName;
    QString tableAlias;
    tabledef::TableSchema schema;
    repo::TableData data;
};

struct MultiProjectionItem
{
    QString sourceName;
    QString resolvedKey;
    QString outputName;
    tabledef::ColumnType type = tabledef::ColumnType::Varchar;
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

QStringList requiredOuterReferences(const logic::LogicNode &node)
{
    QStringList names;
    if (node.type == logic::LogicNodeType::ColumnRef
        && node.reference.scope == logic::LogicReferenceScope::Outer) {
        names.append(node.reference.name);
    }
    for (const logic::LogicNode &child : node.children) {
        const QStringList childNames = requiredOuterReferences(child);
        for (const QString &name : childNames) {
            if (!names.contains(name)) {
                names.append(name);
            }
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

        const SelectRowsResult rows = tuple_service::selectRows(tableName,
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
            sources->append(SelectTableSource{tableName, tableAlias, schema, rows.resultTable});
        }
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
    QSet<QString> prefixes;
    QSet<QString> unaliasedTables;

    for (const SelectTableSource &source : sources) {
        const QString prefix = canonicalPrefix(source);
        if (prefix.isEmpty()) {
            if (error != nullptr) {
                *error = QStringLiteral("SELECT: expected table name");
            }
            return false;
        }
        if (prefixes.contains(prefix)) {
            if (error != nullptr) {
                *error = QStringLiteral("SELECT: duplicate table alias '%1'").arg(prefix);
            }
            return false;
        }
        prefixes.insert(prefix);
        if (source.tableAlias.trimmed().isEmpty()) {
            if (unaliasedTables.contains(source.tableName)) {
                if (error != nullptr) {
                    *error = QStringLiteral("SELECT: duplicate table '%1' requires aliases").arg(source.tableName);
                }
                return false;
            }
            unaliasedTables.insert(source.tableName);
        }

        for (const tabledef::Column &column : source.schema.columns) {
            const QString key = qualifiedKey(source, column);
            resolution->keyTypes.insert(key, column.type);
            resolution->starKeys.append(key);
            resolution->starOutputColumns.append(key);

            resolution->visibleNameToKey.insert(prefix + QLatin1Char('.') + column.name, key);
            resolution->visibleNameToKey.insert(source.tableName + QLatin1Char('.') + column.name, key);
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
        rowContext.cellsByName.insert(source.tableName + QLatin1Char('.') + column.name, cell);
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
        rowContext.cellsByName.insert(source.tableName + QLatin1Char('.') + column.name, cell);
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

QueryExecuteResult execMultiTableSelect(QueryExecutor *executor,
                                        const sqlparser::ParseResult &parsed,
                                        const logic::CorrelationBindings *bindings)
{
    QueryExecuteResult result;

    QString error;
    QList<SelectTableSource> sources;
    if (!loadSourcesFromPayload(parsed.payload, &sources, &error)) {
        result.success = false;
        result.errorMessage = error;
        result.text = error;
        return result;
    }
    if (sources.isEmpty()) {
        result.success = false;
        result.errorMessage = QStringLiteral("SELECT: expected table name");
        result.text = result.errorMessage;
        return result;
    }

    MultiNameResolution resolution;
    if (!buildMultiNameResolution(sources, &resolution, &error)) {
        result.success = false;
        result.errorMessage = error;
        result.text = error;
        return result;
    }

    logic::LogicEvalContext evalContext;
    evalContext.subqueryExecutor = executor;
    evalContext.currentDatabase = currentDatabase;
    evalContext.dataRoot = getDataRoot();
    evalContext.allowSubquery = true;

    logic::LogicNode whereAst;
    const bool hasWhereAst = parsed.payload.contains(QStringLiteral("whereAst"));
    if (hasWhereAst) {
        whereAst = parsed.payload.value(QStringLiteral("whereAst")).value<logic::LogicNode>();
    }

    logic::LogicRowContext nullTemplate = buildNullSourceContext(sources.first());
    QVector<logic::LogicRowContext> joinedRows = rowsForSource(sources.first());
    const QVariantList joinPayload = parsed.payload.value(QStringLiteral("joins")).toList();
    if (joinPayload.isEmpty()) {
        for (int sourceIndex = 1; sourceIndex < sources.size(); ++sourceIndex) {
            joinedRows = joinRowsets(joinedRows,
                                     sources.at(sourceIndex),
                                     QStringLiteral("inner"),
                                     nullptr,
                                     bindings,
                                     evalContext,
                                     &nullTemplate,
                                     &error);
            if (!error.isEmpty()) {
                result.success = false;
                result.errorMessage = error;
                result.text = error;
                return result;
            }
            nullTemplate = mergeRowContexts(nullTemplate, buildNullSourceContext(sources.at(sourceIndex)));
        }
    } else {
        for (const QVariant &joinValue : joinPayload) {
            const QVariantMap joinMap = joinValue.toMap();
            const int rightIndex = joinMap.value(QStringLiteral("rightSourceIndex")).toInt();
            if (rightIndex <= 0 || rightIndex >= sources.size()) {
                result.success = false;
                result.errorMessage = QStringLiteral("SELECT: invalid JOIN payload");
                result.text = result.errorMessage;
                return result;
            }
            const logic::LogicNode onAst = joinMap.value(QStringLiteral("onAst")).value<logic::LogicNode>();
            joinedRows = joinRowsets(joinedRows,
                                     sources.at(rightIndex),
                                     joinMap.value(QStringLiteral("joinType"), QStringLiteral("inner")).toString(),
                                     &onAst,
                                     bindings,
                                     evalContext,
                                     &nullTemplate,
                                     &error);
            if (!error.isEmpty()) {
                result.success = false;
                result.errorMessage = error;
                result.text = error;
                return result;
            }
            nullTemplate = mergeRowContexts(nullTemplate, buildNullSourceContext(sources.at(rightIndex)));
        }
    }

    for (logic::LogicRowContext &row : joinedRows) {
        removeAmbiguousBareColumns(&row, resolution);
        addUniqueBareColumns(&row, resolution);
    }

    QVector<logic::LogicRowContext> filteredRows;
    filteredRows.reserve(joinedRows.size());
    for (logic::LogicRowContext row : joinedRows) {
        QString evalError;
        const bool include = evaluateFilter(hasWhereAst ? &whereAst : nullptr,
                                            row,
                                            bindings,
                                            evalContext,
                                            &evalError);
        if (!evalError.isEmpty()) {
            result.success = false;
            result.errorMessage = evalError;
            result.text = evalError;
            return result;
        }
        if (include) {
            filteredRows.append(row);
        }
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

    const SelectRowsResult fullCandidateRows = tuple_service::selectRows(tableName,
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
    evalContext.allowSubquery = true;

    logic::LogicNode whereAst;
    const bool hasWhereAst = parsed.payload.contains(QStringLiteral("whereAst"));
    if (hasWhereAst) {
        whereAst = parsed.payload.value(QStringLiteral("whereAst")).value<logic::LogicNode>();
    }

    if (bindings != nullptr && hasWhereAst) {
        const QStringList requiredReferences = requiredOuterReferences(whereAst);
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
