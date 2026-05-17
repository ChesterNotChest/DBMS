#include "logic_evaluator.h"

#include "set_logic.h"
#include "simple_logic.h"
#include "subquery_logic.h"

namespace logic {

namespace {

bool containsSubqueryNode(const LogicNode &node)
{
    if (isSubqueryNodeType(node.type)) {
        return true;
    }
    for (const LogicNode &child : node.children) {
        if (containsSubqueryNode(child)) {
            return true;
        }
    }
    return false;
}

LogicEvalResult evaluateColumnReferenceNode(const LogicNode &node,
                                            const LogicRowContext &rowContext)
{
    auto it = rowContext.cellsByName.constFind(node.reference.name);
    if (it == rowContext.cellsByName.constEnd()
        && node.reference.scope == LogicReferenceScope::Local
        && node.reference.name.contains(QLatin1Char('.'))) {
        const QString localName = node.reference.name.mid(node.reference.name.lastIndexOf(QLatin1Char('.')) + 1);
        it = rowContext.cellsByName.constFind(localName);
    }
    if (it == rowContext.cellsByName.constEnd()) {
        return {false, LogicTruthValue::Unknown, {QStringLiteral("missing column '%1'").arg(node.reference.name), -1}};
    }
    const LogicCellValue &value = it.value();
    if (value.isNull || value.value.isEmpty()) {
        return {true, LogicTruthValue::Unknown, {}};
    }
    return {true, LogicTruthValue::True, {}};
}

} // namespace

LogicEvalResult evaluateLogicExpression(const LogicNode &root,
                                        const LogicRowContext &rowContext,
                                        const LogicEvalContext &evalContext)
{
    ensureLogicMetaTypesRegistered();

    switch (root.type) {
    case LogicNodeType::Literal:
        if (root.literalIsNull || root.literalValue.isEmpty()) {
            return {true, LogicTruthValue::Unknown, {}};
        }
        return {true, LogicTruthValue::True, {}};
    case LogicNodeType::ColumnRef:
        return evaluateColumnReferenceNode(root, rowContext);
    case LogicNodeType::Unary:
        return evaluateUnaryNode(root, rowContext, evalContext);
    case LogicNodeType::Binary:
        return evaluateBinaryNode(root, rowContext, evalContext);
    case LogicNodeType::Comparison:
        return evaluateComparisonNode(root, rowContext);
    case LogicNodeType::NullTest:
        return evaluateNullTestNode(root, rowContext);
    case LogicNodeType::InList:
        return evaluateInListNode(root, rowContext);
    case LogicNodeType::InSubquery:
        if (!evalContext.allowSubquery) {
            return {false, LogicTruthValue::Unknown, {QStringLiteral("subqueries are not allowed in this context"), -1}};
        }
        return evaluateInSubqueryNode(root, rowContext, evalContext);
    case LogicNodeType::ExistsSubquery:
        if (!evalContext.allowSubquery) {
            return {false, LogicTruthValue::Unknown, {QStringLiteral("subqueries are not allowed in this context"), -1}};
        }
        return evaluateExistsSubqueryNode(root, rowContext, evalContext);
    case LogicNodeType::QuantifiedSubquery:
        if (!evalContext.allowSubquery) {
            return {false, LogicTruthValue::Unknown, {QStringLiteral("subqueries are not allowed in this context"), -1}};
        }
        return evaluateQuantifiedSubqueryNode(root, rowContext, evalContext);
    case LogicNodeType::Between:
        return evaluateBetweenNode(root, rowContext);
    }

    return {false, LogicTruthValue::Unknown, {QStringLiteral("unsupported logic node"), -1}};
}

LogicEvalResult evaluateCheckConstraintForRow(const LogicNode &checkAst,
                                              const LogicRowContext &candidateRowContext,
                                              const LogicEvalContext &evalContext)
{
    if (containsSubqueryNode(checkAst)) {
        return {false, LogicTruthValue::Unknown, {QStringLiteral("CHECK does not allow subqueries"), -1}};
    }

    LogicEvalContext checkContext = evalContext;
    checkContext.allowSubquery = false;
    const LogicEvalResult result = evaluateLogicExpression(checkAst, candidateRowContext, checkContext);
    if (!result.success) {
        return result;
    }
    if (result.truth != LogicTruthValue::True) {
        return {false, result.truth, {QStringLiteral("CHECK constraint failed"), -1}};
    }
    return result;
}

} // namespace logic