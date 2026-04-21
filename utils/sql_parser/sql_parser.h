/**
 * sql_parser.h — SQL 解析器公共头文件
 *
 * 定义解析结果结构体。
 * 纯数据结构，不涉及任何文件操作。
 */
#ifndef SQL_PARSER_SQL_PARSER_H
#define SQL_PARSER_SQL_PARSER_H

#include "sql_tokenizer.h"
#include <QString>
#include <QVector>
#include <QMap>
#include <QVariant>
#include <QPair>

namespace sqlparser {

// ============================================================
//  通用返回结构：success / errorMessage / payload
// ============================================================
struct ParseResult {
    bool        success   = false;
    QString     errorMessage;
    QString     commandType;  // "CREATE_DATABASE", "SELECT", etc.
    QVariantMap payload;    // 具体参数
};

// ============================================================
//  数据库级解析结果
// ============================================================
struct DatabaseCommand {
    QString action;  // CREATE / DROP / USE / SHOW
    QString databaseName;
};

// ============================================================
//  表级解析结果
// ============================================================
struct ColumnDef {
    QString name;
    QString type;       // INT, FLOAT, CHAR, VARCHAR, TEXT
    int     length     = -1;
    bool    notNull    = false;
    bool    primaryKey = false;
    bool    autoIncrement = false;
    bool    unique     = false;
    QString defaultValue;
    QString checkClause;
    QString referencesTable;
    QString referencesColumn;
};

struct ConstraintDef {
    QString name;
    QString type;       // PRIMARY KEY, UNIQUE, CHECK, FOREIGN KEY
    QStringList columns;
    QString checkClause;
    QString referencesTable;
    QStringList referencesColumns;
};

struct TableCommand {
    QString action;  // CREATE / DROP / ALTER / SHOW / DESC
    QString tableName;

    // CREATE TABLE
    QVector<ColumnDef>     columns;
    QVector<ConstraintDef>  constraints;

    // ALTER TABLE
    QString alterAction;  // ADD COLUMN / MODIFY COLUMN / DROP COLUMN / ADD CONSTRAINT / ...
    ColumnDef     columnDef;
    ConstraintDef constraintDef;
};

// ============================================================
//  元组级解析结果
// ============================================================
struct WhereCondition {
    QString     leftColumn;
    QString     leftTable;    // 可选，多表查询
    QString     op;          // =, !=, <, >, <=, >=, LIKE, IN, IS NULL, IS NOT NULL
    QVariant    rightValue;
    QString     rightColumn;  // 列对列比较
    bool        negated    = false;

    // 组合
    QString     logicOp;      // AND / OR
    WhereCondition* leftChild  = nullptr;
    WhereCondition* rightChild = nullptr;
};

struct SelectCommand {
    QStringList projection;      // 列名列表，"*" 表示所有列
    QString     table;
    QStringList tables;         // 多表查询
    WhereCondition* whereClause = nullptr;
};

struct InsertCommand {
    QString            table;
    QStringList       columnNames;    // 可选，空则按表结构顺序
    QVector<QVector<QVariant>> rows;
};

struct UpdateCommand {
    QString            table;
    QMap<QString, QVariant> assignments;  // SET col = val
    WhereCondition*    whereClause   = nullptr;
};

struct DeleteCommand {
    QString            table;
    WhereCondition*    whereClause   = nullptr;
};

struct TupleCommand {
    QString action;  // SELECT / INSERT / UPDATE / DELETE
    SelectCommand  selectCmd;
    InsertCommand  insertCmd;
    UpdateCommand  updateCmd;
    DeleteCommand  deleteCmd;
};

// ============================================================
//  分发函数：识别 SQL 类型并调用对应解析器
// ============================================================

/** 识别 SQL 的命令类型并返回 {type, ...} */
QPair<QString, QVariantMap> classifySql(const QString& sql, const QVector<SqlToken>& tokens);

/** 从 token 流中提取数据库名（调用者已确认 tokens[0] 为 DATABASE） */
QString extractDatabaseName(const QVector<SqlToken>& tokens);

/** 从 token 流中提取表名 */
QString extractTableName(const QVector<SqlToken>& tokens);

/** 简化 WHERE 条件：仅处理单条件 col op val，不处理嵌套 AND/OR */
WhereCondition extractSimpleWhere(const QVector<SqlToken>& tokens);

// ============================================================
//  各级解析器
// ============================================================

/** 解析数据库级 SQL（CREATE/DROP/USE/SHOW DATABASE） */
ParseResult parseDatabaseSql(const QString& sql, const QVector<SqlToken>& tokens);

/** 解析表级 SQL（CREATE/DROP/ALTER/SHOW TABLES/DESC） */
ParseResult parseTableSql(const QString& sql, const QVector<SqlToken>& tokens);

/** 解析元组级 SQL（SELECT/INSERT/UPDATE/DELETE） */
ParseResult parseTupleSql(const QString& sql, const QVector<SqlToken>& tokens);

/** 统一入口：自动判断类型并解析 */
ParseResult parseSql(const QString& sql);

} // namespace sqlparser

// QVariant 中存储自定义类型必须注册元类型
Q_DECLARE_METATYPE(sqlparser::ColumnDef)
Q_DECLARE_METATYPE(QVector<sqlparser::ColumnDef>)
Q_DECLARE_METATYPE(sqlparser::ConstraintDef)
Q_DECLARE_METATYPE(QVector<sqlparser::ConstraintDef>)
Q_DECLARE_METATYPE(QVector<QVector<QVariant>>)
typedef QMap<QString, QVariant> QMapStringQVariant;
Q_DECLARE_METATYPE(QMapStringQVariant)

#endif // SQL_PARSER_SQL_PARSER_H
