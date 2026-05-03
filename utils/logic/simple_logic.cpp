#include "simple_logic.h"
#include "logic_evaluator.h"

namespace logic {

namespace {

LogicTruthValue negateTruth(LogicTruthValue truth)
{
    switch (truth) {
    case LogicTruthValue::True:
        return LogicTruthValue::False;
    case LogicTruthValue::False:
        return LogicTruthValue::True;
    case LogicTruthValue::Unknown:
        return LogicTruthValue::Unknown;
    }
    return LogicTruthValue::Unknown;
}

double numericValueFor(const LogicCellValue &cell, bool *ok)
{
    if (ok != nullptr) {
        *ok = false;
    }
    if (cell.isNull || cell.value.isEmpty()) {
        return 0.0;
    }
    const double value = cell.value.toDouble(ok);
    return value;
}

LogicCellValue resolveCellValue(const LogicNode &node,
                               const LogicRowContext &rowContext,
                               bool *found)
{
    if (found != nullptr) {
        *found = false;
    }

    if (node.type != LogicNodeType::ColumnRef) {
        return {};
    }

    const QString key = node.reference.name;
    auto it = rowContext.cellsByName.constFind(key);
    if (it == rowContext.cellsByName.constEnd()
        && node.reference.scope == LogicReferenceScope::Local
        && key.contains(QLatin1Char('.'))) {
        const QString localName = key.mid(key.lastIndexOf(QLatin1Char('.')) + 1);
        it = rowContext.cellsByName.constFind(localName);
    }
    if (it == rowContext.cellsByName.constEnd()) {
        return {};
    }
    if (found != nullptr) {
        *found = true;
    }
    return it.value();
}

LogicTruthValue compareCells(const LogicCellValue &lhs,
                             const LogicCellValue &rhs,
                             LogicCompareOperator op)
{
    if (lhs.isNull || rhs.isNull) {
        return LogicTruthValue::Unknown;
    }

    bool leftOk = false;
    bool rightOk = false;
    const bool numericCompare = lhs.type == tabledef::ColumnType::Int
                                || lhs.type == tabledef::ColumnType::Float
                                || rhs.type == tabledef::ColumnType::Int
                                || rhs.type == tabledef::ColumnType::Float;
    if (numericCompare) {
        const double leftValue = numericValueFor(lhs, &leftOk);
        const double rightValue = numericValueFor(rhs, &rightOk);
        if (!leftOk || !rightOk) {
            return LogicTruthValue::Unknown;
        }

        switch (op) {
        case LogicCompareOperator::Eq: return leftValue == rightValue ? LogicTruthValue::True : LogicTruthValue::False;
        case LogicCompareOperator::NotEq: return leftValue != rightValue ? LogicTruthValue::True : LogicTruthValue::False;
        case LogicCompareOperator::Lt: return leftValue < rightValue ? LogicTruthValue::True : LogicTruthValue::False;
        case LogicCompareOperator::Lte: return leftValue <= rightValue ? LogicTruthValue::True : LogicTruthValue::False;
        case LogicCompareOperator::Gt: return leftValue > rightValue ? LogicTruthValue::True : LogicTruthValue::False;
        case LogicCompareOperator::Gte: return leftValue >= rightValue ? LogicTruthValue::True : LogicTruthValue::False;
        }
    }

    const int compareResult = QString::localeAwareCompare(lhs.value, rhs.value);
    switch (op) {
    case LogicCompareOperator::Eq: return compareResult == 0 ? LogicTruthValue::True : LogicTruthValue::False;
    case LogicCompareOperator::NotEq: return compareResult != 0 ? LogicTruthValue::True : LogicTruthValue::False;
    case LogicCompareOperator::Lt: return compareResult < 0 ? LogicTruthValue::True : LogicTruthValue::False;
    case LogicCompareOperator::Lte: return compareResult <= 0 ? LogicTruthValue::True : LogicTruthValue::False;
    case LogicCompareOperator::Gt: return compareResult > 0 ? LogicTruthValue::True : LogicTruthValue::False;
    case LogicCompareOperator::Gte: return compareResult >= 0 ? LogicTruthValue::True : LogicTruthValue::False;
    }
    return LogicTruthValue::Unknown;
}

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

} // namespace

LogicEvalResult evaluateUnaryNode(const LogicNode &node,
                                  const LogicRowContext &rowContext,
                                  const LogicEvalContext &evalContext)
{
    if (node.children.size() != 1) {
        return {false, LogicTruthValue::Unknown, {QStringLiteral("unary node expects one child"), -1}};
    }

    const LogicEvalResult childResult = evaluateLogicExpression(node.children.first(), rowContext, evalContext);
    if (!childResult.success) {
        return childResult;
    }
    return {true, negateTruth(childResult.truth), {}};
}

LogicEvalResult evaluateBinaryNode(const LogicNode &node,
                                   const LogicRowContext &rowContext,
                                   const LogicEvalContext &evalContext)
{
    if (node.children.size() != 2) {
        return {false, LogicTruthValue::Unknown, {QStringLiteral("binary node expects two children"), -1}};
    }

    const LogicEvalResult lhs = evaluateLogicExpression(node.children.at(0), rowContext, evalContext);
    if (!lhs.success) {
        return lhs;
    }
    const LogicEvalResult rhs = evaluateLogicExpression(node.children.at(1), rowContext, evalContext);
    if (!rhs.success) {
        return rhs;
    }

    if (node.binaryOperator == LogicBinaryOperator::And) {
        if (lhs.truth == LogicTruthValue::False || rhs.truth == LogicTruthValue::False) {
            return {true, LogicTruthValue::False, {}};
        }
        if (lhs.truth == LogicTruthValue::Unknown || rhs.truth == LogicTruthValue::Unknown) {
            return {true, LogicTruthValue::Unknown, {}};
        }
        return {true, LogicTruthValue::True, {}};
    }

    if (lhs.truth == LogicTruthValue::True || rhs.truth == LogicTruthValue::True) {
        return {true, LogicTruthValue::True, {}};
    }
    if (lhs.truth == LogicTruthValue::Unknown || rhs.truth == LogicTruthValue::Unknown) {
        return {true, LogicTruthValue::Unknown, {}};
    }
    return {true, LogicTruthValue::False, {}};
}

LogicEvalResult evaluateComparisonNode(const LogicNode &node,
                                       const LogicRowContext &rowContext)
{
    if (node.children.size() != 2) {
        return {false, LogicTruthValue::Unknown, {QStringLiteral("comparison node expects two children"), -1}};
    }

    bool lhsFound = false;
    bool rhsFound = false;
    const LogicNode &lhsNode = node.children.at(0);
    const LogicNode &rhsNode = node.children.at(1);

    LogicCellValue lhsValue;
    LogicCellValue rhsValue;

    if (lhsNode.type == LogicNodeType::ColumnRef) {
        lhsValue = resolveCellValue(lhsNode, rowContext, &lhsFound);
        if (!lhsFound) {
            return {false, LogicTruthValue::Unknown, {QStringLiteral("missing column '%1'").arg(lhsNode.reference.name), -1}};
        }
    } else if (lhsNode.type == LogicNodeType::Literal) {
        lhsValue.value = lhsNode.literalValue;
        lhsValue.type = lhsNode.literalType;
        lhsValue.isNull = lhsNode.literalIsNull;
    }

    if (rhsNode.type == LogicNodeType::ColumnRef) {
        rhsValue = resolveCellValue(rhsNode, rowContext, &rhsFound);
        if (!rhsFound) {
            return {false, LogicTruthValue::Unknown, {QStringLiteral("missing column '%1'").arg(rhsNode.reference.name), -1}};
        }
    } else if (rhsNode.type == LogicNodeType::Literal) {
        rhsValue.value = rhsNode.literalValue;
        rhsValue.type = rhsNode.literalType;
        rhsValue.isNull = rhsNode.literalIsNull;
    }

    const LogicTruthValue truth = compareCells(lhsValue, rhsValue, node.compareOperator);
    return {true, truth, {}};
}

LogicEvalResult evaluateNullTestNode(const LogicNode &node,
                                     const LogicRowContext &rowContext)
{
    if (node.children.size() != 1) {
        return {false, LogicTruthValue::Unknown, {QStringLiteral("null test expects one child"), -1}};
    }

    const LogicNode &child = node.children.first();
    LogicCellValue value;
    bool found = false;
    if (child.type == LogicNodeType::ColumnRef) {
        value = resolveCellValue(child, rowContext, &found);
        if (!found) {
            return {false, LogicTruthValue::Unknown, {QStringLiteral("missing column '%1'").arg(child.reference.name), -1}};
        }
    } else if (child.type == LogicNodeType::Literal) {
        value.value = child.literalValue;
        value.type = child.literalType;
        value.isNull = child.literalIsNull;
    }

    const bool isNull = value.isNull || value.value.isEmpty();
    const bool truth = node.isNotNullTest ? !isNull : isNull;
    return {true, truth ? LogicTruthValue::True : LogicTruthValue::False, {}};
}

} // namespace logic