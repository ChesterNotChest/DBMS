#include "set_logic.h"

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

LogicCellValue cellValueFromNode(const LogicNode &node)
{
    LogicCellValue value;
    if (node.type == LogicNodeType::Literal) {
        value.value = node.literalValue;
        value.type = node.literalType;
        value.isNull = node.literalIsNull;
    }
    return value;
}

LogicTruthValue compareValues(const LogicCellValue &lhs,
                              const LogicCellValue &rhs,
                              LogicCompareOperator op)
{
    if (lhs.isNull || rhs.isNull) {
        return LogicTruthValue::Unknown;
    }

    const bool numericCompare = lhs.type == tabledef::ColumnType::Int
                                || lhs.type == tabledef::ColumnType::Float
                                || rhs.type == tabledef::ColumnType::Int
                                || rhs.type == tabledef::ColumnType::Float;
    if (numericCompare) {
        bool leftOk = false;
        bool rightOk = false;
        const double leftValue = lhs.value.toDouble(&leftOk);
        const double rightValue = rhs.value.toDouble(&rightOk);
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
        case LogicCompareOperator::Like: return LogicTruthValue::Unknown;
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
    case LogicCompareOperator::Like: return LogicTruthValue::Unknown;
    }
    return LogicTruthValue::Unknown;
}

} // namespace

LogicEvalResult evaluateInListNode(const LogicNode &node,
                                   const LogicRowContext &rowContext)
{
    if (node.children.isEmpty()) {
        return {false, LogicTruthValue::Unknown, {QStringLiteral("IN node expects lhs"), -1}};
    }

    const LogicNode &lhsNode = node.children.first();
    LogicCellValue lhsValue;
    if (lhsNode.type == LogicNodeType::ColumnRef) {
        bool found = false;
        lhsValue = rowContext.cellsByName.value(lhsNode.reference.name);
        found = rowContext.cellsByName.contains(lhsNode.reference.name);
        if (!found && lhsNode.reference.name.contains(QLatin1Char('.'))) {
            const QString localName = lhsNode.reference.name.mid(lhsNode.reference.name.lastIndexOf(QLatin1Char('.')) + 1);
            lhsValue = rowContext.cellsByName.value(localName);
            found = rowContext.cellsByName.contains(localName);
        }
        if (!found) {
            return {false, LogicTruthValue::Unknown, {QStringLiteral("missing column '%1'").arg(lhsNode.reference.name), -1}};
        }
    } else if (lhsNode.type == LogicNodeType::Literal) {
        lhsValue = cellValueFromNode(lhsNode);
    }

    bool sawNull = false;
    for (int index = 1; index < node.children.size(); ++index) {
        const LogicNode &candidateNode = node.children.at(index);
        if (candidateNode.type != LogicNodeType::Literal) {
            return {false, LogicTruthValue::Unknown, {QStringLiteral("IN literal list must contain only literals"), -1}};
        }
        const LogicCellValue candidateValue = cellValueFromNode(candidateNode);
        if (candidateValue.isNull || candidateValue.value.isEmpty()) {
            sawNull = true;
            continue;
        }
        const LogicTruthValue compareTruth = compareValues(lhsValue, candidateValue, LogicCompareOperator::Eq);
        if (compareTruth == LogicTruthValue::True) {
            return {true, node.negated ? LogicTruthValue::False : LogicTruthValue::True, {}};
        }
        if (compareTruth == LogicTruthValue::Unknown) {
            sawNull = true;
        }
    }

    const LogicTruthValue truth = sawNull ? LogicTruthValue::Unknown : LogicTruthValue::False;
    return {true, node.negated ? negateTruth(truth) : truth, {}};
}

LogicEvalResult evaluateBetweenNode(const LogicNode &node,
                                    const LogicRowContext &rowContext)
{
    if (node.children.size() != 3) {
        return {false, LogicTruthValue::Unknown, {QStringLiteral("BETWEEN node expects 3 children"), -1}};
    }

    const LogicNode &lhsNode = node.children.at(0);
    const LogicNode &lowerNode = node.children.at(1);
    const LogicNode &upperNode = node.children.at(2);

    LogicCellValue lhsValue;
    if (lhsNode.type == LogicNodeType::ColumnRef) {
        bool found = false;
        lhsValue = rowContext.cellsByName.value(lhsNode.reference.name);
        found = rowContext.cellsByName.contains(lhsNode.reference.name);
        if (!found && lhsNode.reference.name.contains(QLatin1Char('.'))) {
            const QString localName = lhsNode.reference.name.mid(lhsNode.reference.name.lastIndexOf(QLatin1Char('.')) + 1);
            lhsValue = rowContext.cellsByName.value(localName);
            found = rowContext.cellsByName.contains(localName);
        }
        if (!found) {
            return {false, LogicTruthValue::Unknown, {QStringLiteral("missing column '%1'").arg(lhsNode.reference.name), -1}};
        }
    } else if (lhsNode.type == LogicNodeType::Literal) {
        lhsValue = cellValueFromNode(lhsNode);
    } else {
        return {false, LogicTruthValue::Unknown, {QStringLiteral("BETWEEN requires a column or literal on the left side"), -1}};
    }

    if (lowerNode.type != LogicNodeType::Literal || upperNode.type != LogicNodeType::Literal) {
        return {false, LogicTruthValue::Unknown, {QStringLiteral("BETWEEN bounds must be literals"), -1}};
    }

    const LogicTruthValue lowerTruth = compareValues(lhsValue, cellValueFromNode(lowerNode), LogicCompareOperator::Gte);
    const LogicTruthValue upperTruth = compareValues(lhsValue, cellValueFromNode(upperNode), LogicCompareOperator::Lte);
    LogicTruthValue truth = LogicTruthValue::Unknown;
    if (lowerTruth == LogicTruthValue::False || upperTruth == LogicTruthValue::False) {
        truth = LogicTruthValue::False;
    } else if (lowerTruth == LogicTruthValue::True && upperTruth == LogicTruthValue::True) {
        truth = LogicTruthValue::True;
    }
    return {true, node.negated ? negateTruth(truth) : truth, {}};
}

LogicEvalResult evaluateQuantifiedSetComparison(const LogicNode &node,
                                                const QList<setdef::SetValue> &values,
                                                const LogicRowContext &rowContext)
{
    if (node.children.size() != 1) {
        return {false, LogicTruthValue::Unknown, {QStringLiteral("quantified comparison expects lhs"), -1}};
    }

    const LogicNode &lhsNode = node.children.first();
    LogicCellValue lhsValue;
    bool found = false;
    if (lhsNode.type == LogicNodeType::ColumnRef) {
        lhsValue = rowContext.cellsByName.value(lhsNode.reference.name);
        found = rowContext.cellsByName.contains(lhsNode.reference.name);
        if (!found && lhsNode.reference.name.contains(QLatin1Char('.'))) {
            const QString localName = lhsNode.reference.name.mid(lhsNode.reference.name.lastIndexOf(QLatin1Char('.')) + 1);
            lhsValue = rowContext.cellsByName.value(localName);
            found = rowContext.cellsByName.contains(localName);
        }
        if (!found) {
            return {false, LogicTruthValue::Unknown, {QStringLiteral("missing column '%1'").arg(lhsNode.reference.name), -1}};
        }
    } else if (lhsNode.type == LogicNodeType::Literal) {
        lhsValue = cellValueFromNode(lhsNode);
    }

    bool sawNull = false;
    bool anyTrue = false;
    bool allTrue = true;
    for (const setdef::SetValue &item : values) {
        if (item.isNull) {
            sawNull = true;
            continue;
        }
        LogicCellValue rhsValue;
        rhsValue.value = item.rawValue;
        rhsValue.type = item.type;
        rhsValue.isNull = false;
        const LogicTruthValue compareTruth = compareValues(lhsValue, rhsValue, node.compareOperator);
        if (node.quantifier == LogicQuantifier::Any) {
            if (compareTruth == LogicTruthValue::True) {
                anyTrue = true;
                break;
            }
            if (compareTruth == LogicTruthValue::Unknown) {
                sawNull = true;
            }
        } else {
            if (compareTruth == LogicTruthValue::False) {
                allTrue = false;
                break;
            }
            if (compareTruth == LogicTruthValue::Unknown) {
                sawNull = true;
            }
        }
    }

    LogicTruthValue truth = LogicTruthValue::Unknown;
    if (node.quantifier == LogicQuantifier::Any) {
        if (anyTrue) {
            truth = LogicTruthValue::True;
        } else if (sawNull) {
            truth = LogicTruthValue::Unknown;
        } else {
            truth = LogicTruthValue::False;
        }
    } else {
        if (values.isEmpty()) {
            truth = LogicTruthValue::True;
        } else if (allTrue && !sawNull) {
            truth = LogicTruthValue::True;
        } else if (allTrue && sawNull) {
            truth = LogicTruthValue::Unknown;
        } else {
            truth = LogicTruthValue::False;
        }
    }

    return {true, node.negated ? negateTruth(truth) : truth, {}};
}

} // namespace logic
