#ifndef UTILS_LOGIC_SIMPLE_LOGIC_H
#define UTILS_LOGIC_SIMPLE_LOGIC_H

#include "logic_ast.h"

namespace logic {

LogicEvalResult evaluateUnaryNode(const LogicNode &node,
                                  const LogicRowContext &rowContext,
                                  const LogicEvalContext &evalContext);

LogicEvalResult evaluateBinaryNode(const LogicNode &node,
                                   const LogicRowContext &rowContext,
                                   const LogicEvalContext &evalContext);

LogicEvalResult evaluateComparisonNode(const LogicNode &node,
                                       const LogicRowContext &rowContext);

LogicEvalResult evaluateNullTestNode(const LogicNode &node,
                                     const LogicRowContext &rowContext);

} // namespace logic

#endif // UTILS_LOGIC_SIMPLE_LOGIC_H