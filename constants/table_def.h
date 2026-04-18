#ifndef CONSTANTS_TABLE_DEF_H
#define CONSTANTS_TABLE_DEF_H

#include <QList>
#include <QString>
#include <QStringList>

namespace tabledef {

// DBMS 支持的字段逻辑类型。
// 这里描述的是“表字段的类型”，不是 C++ 变量本身的类型。
enum class ColumnType {
    Int,
    Varchar,
    Float
};

// DBMS 支持的约束逻辑类型。
// 当前阶段主要落地主键、唯一、检查和外键约束。
enum class ConstraintType {
    PrimaryKey,
    Unique,
    Check,
    ForeignKey
};

// 单个字段的结构化定义。
// 它既可以描述用户表中的一列，也可以描述 root.dbf / .tab / .meta / .con 这些系统二维表的一列。
struct Column
{
    QString name;
    ColumnType type = ColumnType::Varchar;
    int length = 0;
    bool notNull = false;
    QString defaultValue;
    bool autoIncrement = false;
    QString check;
};

// 单个约束的结构化定义。
// columns 表示约束关联到哪些列；
// referencedTable / referencedColumns 用于描述 FOREIGN KEY 指向的目标；
// checkClause 主要供 CHECK 约束使用。
struct Constraint
{
    QString name;
    ConstraintType type = ConstraintType::Check;
    QStringList columns;
    QString referencedTable;
    QStringList referencedColumns;
    QString checkClause;
};

// 一张二维表的完整 schema 定义。
// tableName 表示逻辑名称；columns 和 constraints 一起描述该表的结构。
struct TableSchema
{
    QString tableName;
    QList<Column> columns;
    QList<Constraint> constraints;
};

} // namespace tabledef

#endif // CONSTANTS_TABLE_DEF_H
