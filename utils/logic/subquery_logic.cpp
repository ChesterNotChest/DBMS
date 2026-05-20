#include "subquery_logic.h"

#include "logic_evaluator.h"
#include "set_logic.h"
#include "../../controller/nest_query.h"

#include <algorithm>

namespace logic {

using service::QueryExecuteContext;
using service::QueryExecuteResult;
using service::QueryExecutor;

LogicSubqueryExecutorAdapter::LogicSubqueryExecutorAdapter(QueryExecutor *executor)
    : m_executor(executor)
{
}

QueryExecuteResult LogicSubqueryExecutorAdapter::executeSelectSql(const QString &sql,
                                                                  const QueryExecuteContext &context)
{
    return m_executor != nullptr ? m_executor->executeSelectSql(sql, context) : QueryExecuteResult{};
}

QueryExecuteResult LogicSubqueryExecutorAdapter::executeCorrelatedSelect(const QString &sql,
                                                                         const CorrelationBindings &bindings,
                                                                         const QueryExecuteContext &context)
{
    return m_executor != nullptr ? m_executor->executeCorrelatedSelect(sql, bindings, context) : QueryExecuteResult{};
}

CorrelationBindings buildCorrelationBindings(const LogicRowContext &outerRowContext,
                                             const QStringList &referencedOuterNames)
{
    CorrelationBindings bindings;
    for (const QString &outerName : referencedOuterNames) {
        auto it = outerRowContext.cellsByName.constFind(outerName);
        if (it == outerRowContext.cellsByName.constEnd()) {
            continue;
        }
        bindings.items.append(CorrelatedBinding{outerName, it.value().value, it.value().type, it.value().isNull});
    }
    return bindings;
}

QList<setdef::SetValue> normalizeSelectResultToSet(const service::SelectRowsResult &result)
{
    QList<setdef::SetValue> values;
    if (!result.success || result.resultTable.rows.isEmpty()) {
        return values;
    }

    const tabledef::ColumnType valueType = result.columnTypes.isEmpty()
                                               ? tabledef::ColumnType::Varchar
                                               : result.columnTypes.first();

    for (const repo::TableRow &row : result.resultTable.rows) {
        if (row.isEmpty()) {
            values.append(setdef::SetValue{QString(), valueType, true});
            continue;
        }
        values.append(setdef::SetValue{row.first(), valueType, row.first().isEmpty()});
    }
    return values;
}

static QueryExecuteResult executeSubquery(const LogicNode &node,
                                          const LogicRowContext &rowContext,
                                          const LogicEvalContext &evalContext,
                                          bool correlated)
{
    QueryExecuteResult result;
    if (evalContext.subqueryExecutor == nullptr) {
        result.success = false;
        result.errorMessage = QStringLiteral("subquery executor is not configured");
        return result;
    }

    const QueryExecuteContext queryContext{evalContext.currentDatabase,
                                           evalContext.dataRoot};
    if (correlated) {
        const CorrelationBindings bindings = buildCorrelationBindings(rowContext, node.referencedOuterNames);
        for (const QString &referencedName : node.referencedOuterNames) {
            if (std::none_of(bindings.items.cbegin(), bindings.items.cend(), [&](const CorrelatedBinding &binding) {
                    return binding.name == referencedName;
                })) {
                result.success = false;
                result.errorMessage = QStringLiteral("missing correlated binding '%1'").arg(referencedName);
                return result;
            }
        }
        return evalContext.subqueryExecutor->executeCorrelatedSelect(node.subquerySql, bindings, queryContext);
    }
    return evalContext.subqueryExecutor->executeSelectSql(node.subquerySql, queryContext);
}

LogicEvalResult evaluateExistsSubqueryNode(const LogicNode &node,
                                           const LogicRowContext &rowContext,
                                           const LogicEvalContext &evalContext)
{
    const QueryExecuteResult subqueryResult = executeSubquery(node, rowContext, evalContext, !node.referencedOuterNames.isEmpty());
    if (!subqueryResult.success) {
        return {false, LogicTruthValue::Unknown, {subqueryResult.errorMessage, -1}};
    }
    return {true, subqueryResult.selectResult.resultTable.rows.isEmpty() ? LogicTruthValue::False : LogicTruthValue::True, {}};
}

LogicEvalResult evaluateInSubqueryNode(const LogicNode &node,
                                       const LogicRowContext &rowContext,
                                       const LogicEvalContext &evalContext)
{
    const QueryExecuteResult subqueryResult = executeSubquery(node, rowContext, evalContext, !node.referencedOuterNames.isEmpty());
    if (!subqueryResult.success) {
        return {false, LogicTruthValue::Unknown, {subqueryResult.errorMessage, -1}};
    }
    if (subqueryResult.selectResult.resultTable.columns.size() > 1) {
        return {false, LogicTruthValue::Unknown, {QStringLiteral("subquery must return a single column"), -1}};
    }

    QList<setdef::SetValue> values = normalizeSelectResultToSet(subqueryResult.selectResult);
    return evaluateQuantifiedSetComparison(node, values, rowContext);
}

LogicEvalResult evaluateQuantifiedSubqueryNode(const LogicNode &node,
                                               const LogicRowContext &rowContext,
                                               const LogicEvalContext &evalContext)
{
    const QueryExecuteResult subqueryResult = executeSubquery(node, rowContext, evalContext, !node.referencedOuterNames.isEmpty());
    if (!subqueryResult.success) {
        return {false, LogicTruthValue::Unknown, {subqueryResult.errorMessage, -1}};
    }
    if (subqueryResult.selectResult.resultTable.columns.size() > 1) {
        return {false, LogicTruthValue::Unknown, {QStringLiteral("subquery must return a single column"), -1}};
    }

    const QList<setdef::SetValue> values = normalizeSelectResultToSet(subqueryResult.selectResult);
    return evaluateQuantifiedSetComparison(node, values, rowContext);
}

} // namespace logic
