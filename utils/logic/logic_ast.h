#ifndef UTILS_LOGIC_LOGIC_AST_H
#define UTILS_LOGIC_LOGIC_AST_H

#include "logic_types.h"

namespace logic {

inline bool isSubqueryNodeType(LogicNodeType type)
{
    return type == LogicNodeType::InSubquery
           || type == LogicNodeType::ExistsSubquery
           || type == LogicNodeType::QuantifiedSubquery
           || type == LogicNodeType::ScalarSubquery;
}

inline bool isLiteralNode(const LogicNode &node)
{
    return node.type == LogicNodeType::Literal;
}

inline bool isColumnReferenceNode(const LogicNode &node)
{
    return node.type == LogicNodeType::ColumnRef;
}

inline bool isNullNode(const LogicNode &node)
{
    return node.type == LogicNodeType::Literal && node.literalIsNull;
}

} // namespace logic

#endif // UTILS_LOGIC_LOGIC_AST_H
