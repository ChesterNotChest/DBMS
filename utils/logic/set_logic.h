#ifndef UTILS_LOGIC_SET_LOGIC_H
#define UTILS_LOGIC_SET_LOGIC_H

#include "logic_ast.h"

namespace logic {

LogicEvalResult evaluateInListNode(const LogicNode &node,
                                   const LogicRowContext &rowContext);

LogicEvalResult evaluateBetweenNode(const LogicNode &node,
                                   const LogicRowContext &rowContext);

LogicEvalResult evaluateQuantifiedSetComparison(const LogicNode &node,
                                                const QList<setdef::SetValue> &values,
                                                const LogicRowContext &rowContext);

} // namespace logic

#endif // UTILS_LOGIC_SET_LOGIC_H