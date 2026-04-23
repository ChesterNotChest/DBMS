#ifndef CONSTANTS_SET_DEF_H
#define CONSTANTS_SET_DEF_H

#include "table_def.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace setdef {

// Data source for set predicates: literal list or subquery result.
enum class SetSourceType {
    LiteralList,
    SubqueryResult
};

// SQL quantifier for ANY / ALL.
enum class SetQuantifier {
    Any,
    All
};

// Membership mode for IN / NOT IN.
enum class SetMembershipMode {
    In,
    NotIn
};

// Comparison operators allowed by quantified set predicates.
enum class SetCompareOperator {
    Eq,
    NotEq,
    Lt,
    Lte,
    Gt,
    Gte
};

// Single normalized value inside a set.
struct SetValue
{
    QString rawValue;
    tabledef::ColumnType type = tabledef::ColumnType::Varchar;
    bool isNull = false;
};

// Normalized set. A subquery-backed set must be single-column.
struct ValueSet
{
    SetSourceType sourceType = SetSourceType::LiteralList;
    QString sourceSql;
    QStringList columnNames;
    QList<SetValue> values;
};

} // namespace setdef

#endif // CONSTANTS_SET_DEF_H
