#include "nest_query.h"

#include <utility>

namespace service {

namespace {

struct QueryExecutionScope
{
    QString previousDatabase;
    QString previousDataRoot;

    // 禁止隐式转换，确保只有通过显式构造函数创建实例
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
            conditions->append(SimpleCondition{
                columnName,
                conditionMap.value(QStringLiteral("value")).toString()
            });
        }
    }

    return true;
}

QueryExecuteResult makeResultFromSelect(const QString &commandType,
                                        const QVariantMap &payload,
                                        const SelectRowsResult &selectResult,
                                        const QString &text = QString())
{
    QueryExecuteResult result;
    result.success = selectResult.success;
    result.errorMessage = selectResult.errorMessage;
    result.text = text;
    result.commandType = commandType;
    result.affectedRows = selectResult.affectedRowCount;
    result.selectResult = selectResult;
    result.payload = payload;
    return result;
}

} // namespace

QueryExecuteResult QueryExecutor::executeSql(const QString &sql,
                                             const QueryExecuteContext &context) const
{
    QueryExecutionScope scope(context);
    const sqlparser::ParseResult parsed = sqlparser::parseSql(sql);
    return executeParsed(parsed, {});
}

QueryExecuteResult QueryExecutor::executeParsed(const sqlparser::ParseResult &parsed,
                                                const QueryExecuteContext &context) const
{
    QueryExecutionScope scope(context);

    if (!parsed.success) {
        QueryExecuteResult result;
        result.success = false;
        result.errorMessage = parsed.errorMessage;
        result.text = parsed.errorMessage;
        result.commandType = parsed.commandType;
        result.payload = parsed.payload;
        return result;
    }

    const QString commandType = parsed.commandType;
    if (commandType == QStringLiteral("SELECT")) {
        return execSelect(parsed);
    }

    QueryExecuteResult result;
    result.success = false;
    result.errorMessage = QStringLiteral("QueryExecutor only supports SELECT subqueries");
    result.commandType = commandType;
    result.payload = parsed.payload;
    return result;
}

QueryExecuteResult QueryExecutor::executeSelectSql(const QString &sql,
                                                   const QueryExecuteContext &context) const
{
    QueryExecutionScope scope(context);
    const sqlparser::ParseResult parsed = sqlparser::parseSql(sql);
    if (!parsed.success) {
        QueryExecuteResult result;
        result.success = false;
        result.errorMessage = parsed.errorMessage;
        result.text = parsed.errorMessage;
        result.commandType = parsed.commandType;
        result.payload = parsed.payload;
        return result;
    }
    if (parsed.commandType != QStringLiteral("SELECT")) {
        QueryExecuteResult result;
        result.success = false;
        result.errorMessage = QStringLiteral("QueryExecutor::executeSelectSql only accepts SELECT");
        result.commandType = parsed.commandType;
        result.payload = parsed.payload;
        return result;
    }
    return execSelect(parsed);
}

QueryExecuteResult QueryExecutor::execSelect(const sqlparser::ParseResult &parsed) const
{
    if (currentDatabase.isEmpty()) {
        QueryExecuteResult result;
        result.success = false;
        result.errorMessage = QStringLiteral("No database selected. Use USE database_name;");
        result.commandType = parsed.commandType;
        result.payload = parsed.payload;
        return result;
    }

    QList<SimpleCondition> conditions;
    QString conditionError;
    if (!simpleConditionsFromPayload(parsed.payload.value(QStringLiteral("conditions")).toList(),
                                     &conditions,
                                     &conditionError)) {
        QueryExecuteResult result;
        result.success = false;
        result.errorMessage = conditionError;
        result.commandType = parsed.commandType;
        result.payload = parsed.payload;
        return result;
    }

    const SelectRowsResult selectResult = tuple_service::selectRows(
        parsed.payload.value(QStringLiteral("tableName")).toString(),
        parsed.payload.value(QStringLiteral("projection")).toStringList(),
        conditions,
        parsed.payload.value(QStringLiteral("limit"), -1).toInt());

    return makeResultFromSelect(parsed.commandType, parsed.payload, selectResult);
}

} // namespace service
