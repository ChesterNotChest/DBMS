#ifndef UTILS_TABLE_MANU_TABLE_MANU_H
#define UTILS_TABLE_MANU_TABLE_MANU_H

#include "../../constants/table_def.h"

namespace tabledef {

// table_manu.h 是“表结构操作工具”的统一入口。
// constants/table_def.h 只保留静态定义；
// 这里则集中放与这些定义配套的类型转换、schema 校验、schema 构造函数。

// bool 与系统表字符串之间的互转工具。
QString boolToString(bool value);
bool stringToBool(const QString &value);

// 字段类型与持久化字符串之间的互转工具。
QString columnTypeToString(ColumnType type);
bool tryParseColumnType(const QString &value, ColumnType *type);

// 约束类型与持久化字符串之间的互转工具。
QString constraintTypeToString(ConstraintType type);
bool tryParseConstraintType(const QString &value, ConstraintType *type);

// 从 schema 中提取出二维表表头列名。
// repo 在创建 root.dbf / .tab / .meta / .con 这类系统表时会直接使用它。
QStringList schemaColumnNames(const TableSchema &schema);

// 面向列定义的 schema 查询工具。
// 适合在 CREATE COLUMN / DROP COLUMN / MODIFY COLUMN 前做快速检查。
int findColumnIndex(const TableSchema &schema, const QString &columnName);
bool hasColumn(const TableSchema &schema, const QString &columnName);

// 面向约束定义的 schema 查询工具。
// 适合在 ADD CONSTRAINT / DROP CONSTRAINT / MODIFY CONSTRAINT 前做快速检查。
int findConstraintIndex(const TableSchema &schema, const QString &constraintName);
bool hasConstraint(const TableSchema &schema, const QString &constraintName);

// 判断某个约束是否关联到指定列。
// 在删除列或修改列时，常用于查找需要联动处理的约束。
bool constraintTouchesColumn(const Constraint &constraint, const QString &columnName);

// 约束类型判断工具。
// task/service 层在分流 PK / UNIQUE / CHECK / FK 的逻辑时会频繁用到。
bool isForeignKeyConstraint(const Constraint &constraint);
bool isPrimaryKeyConstraint(const Constraint &constraint);
bool isUniqueConstraint(const Constraint &constraint);
bool isCheckConstraint(const Constraint &constraint);

// 约束完整性校验工具。
// 当前尤其服务于 FOREIGN KEY 的最小实现：要求引用目标信息齐全。
bool constraintTypeRequiresReferenceTarget(ConstraintType type);
bool isForeignKeyReferenceComplete(const Constraint &constraint);
bool isConstraintDefinitionComplete(const Constraint &constraint);
bool hasPrimaryKeyConstraint(const TableSchema &schema);
bool sameConstraintSemantics(const Constraint &lhs, const Constraint &rhs);
bool validateConstraintDefinitions(const TableSchema &schema,
								   const QString &skipConstraintName = QString(),
								   QString *error = nullptr);
bool validateConstraintAgainstSchema(const TableSchema &schema,
									 const Constraint &candidate,
									 const QString &skipConstraintName = QString(),
									 QString *error = nullptr);
bool validateConstraintRows(const QString &databaseName,
							const QString &dataRoot,
							const TableSchema &schema,
							const QStringList &tableColumns,
							const QList<QStringList> &tableRows,
							QString *error = nullptr);
bool validateNoIncomingForeignKeyReferences(const QString &databaseName,
										   const QString &dataRoot,
										   const QString &targetTableName,
										   QString *error = nullptr);

// 系统表 schema 构造器。
// 这些函数统一定义 root.dbf、[database].tab、[table]/table.meta、[table]/table.con 的结构。
TableSchema buildDatabaseRootSchema();
TableSchema buildDatabaseTableCatalogSchema(const QString &databaseName = QString());
TableSchema buildTableMetaSchema(const QString &tableName = QString());
TableSchema buildTableConstraintSchema(const QString &tableName = QString());

} // namespace tabledef

#endif // UTILS_TABLE_MANU_TABLE_MANU_H
