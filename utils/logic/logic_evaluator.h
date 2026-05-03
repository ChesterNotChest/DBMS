#ifndef UTILS_LOGIC_LOGIC_EVALUATOR_H
#define UTILS_LOGIC_LOGIC_EVALUATOR_H

#include "logic_ast.h"

namespace logic {

LogicEvalResult evaluateLogicExpression(const LogicNode &root,
                                        const LogicRowContext &rowContext,
                                        const LogicEvalContext &evalContext);

LogicEvalResult evaluateCheckConstraintForRow(const LogicNode &checkAst,
                                              const LogicRowContext &candidateRowContext,
                                              const LogicEvalContext &evalContext);

} // namespace logic

#endif // UTILS_LOGIC_LOGIC_EVALUATOR_H