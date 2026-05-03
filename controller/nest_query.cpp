#include "nest_query.h"

#include "../utils/logic/logic.h"
#include "../utils/service_common/service_common.h"

#include <algorithm>

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
                                       int rowIndex)
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
    }
    return rowContext;
}

void mergeBindings(logic::LogicRowContext *rowContext, const logic::CorrelationBindings &bindings)
{
    if (rowContext == nullptr) {
        return;
    }

    for (const logic::CorrelatedBinding &binding : bindings.items) {
        rowContext->cellsByName.insert(binding.name,
                                       logic::LogicCellValue{binding.value, binding.type, binding.isNull});
    }
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
        selectResult.resultTable.columns = projection;
        for (const QString &columnName : projection) {
            selectResult.columnTypes.append(columnTypeForName(columnName));
        }
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
        logic::LogicRowContext rowContext = buildRowContext(schema, tableData, rowIndex);
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
            for (const QString &columnName : projection) {
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