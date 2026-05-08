#ifndef UTILS_SERVICE_COMMON_SERVICE_COMMON_H
#define UTILS_SERVICE_COMMON_SERVICE_COMMON_H

#include "../../service/service.h"
#include "../thread_runtime/lock_manager.h"
#include "../thread_runtime/catalog_cache.h"

namespace service {

// 名称与标识生成。
// 这里的函数只处理纯字符串层面的规范化与命名，不依赖具体表数据。
QString normalizeDatabaseName(const QString &databaseName);
QString generatedConstraintName(const QString &columnName, const QString &suffix);

// 字段与约束的通用校验/构造。
// 这组函数服务于 DDL / DML / schema 校验的共同前置逻辑。
bool validateColumnDefinition(const ColumnDefinition &definition, QString *error);
bool validateScalarValue(const tabledef::Column &column, const QString &value, QString *error);
QString compositeKeySignature(const QStringList &values);
bool rowExistsInTable(const repo::TableData &table,
                      const QStringList &columnNames,
                      const QStringList &values,
                      QString *error);
bool validateConstraintRows(const QString &databaseName,
                            const QString &dataRoot,
                            const tabledef::TableSchema &schema,
                            const QStringList &tableColumns,
                            const QList<QStringList> &tableRows,
                            QString *error = nullptr);
bool validateNoIncomingForeignKeyReferences(const QString &databaseName,
                                            const QString &dataRoot,
                                            const QString &targetTableName,
                                            QString *error = nullptr);
QList<tabledef::Constraint> buildGeneratedConstraints(const ColumnDefinition &definition);
tabledef::Constraint makeConstraint(const QString &constraintName,
                                    tabledef::ConstraintType type,
                                    const QStringList &columns,
                                    const QString &referencedTable = QString(),
                                    const QStringList &referencedColumns = QStringList(),
                                    const QString &checkClause = QString(),
                                    const QString &indexName = QString(),
                                    tabledef::ForeignKeyAction onDeleteAction = tabledef::ForeignKeyAction::NoAction,
                                    tabledef::ForeignKeyAction onUpdateAction = tabledef::ForeignKeyAction::NoAction);

// schema 文本化输出。
// 主要给 describe/show create 之类的输出型接口使用。
QString formatColumnDefinition(const tabledef::Column &column);
QString formatConstraintDefinition(const tabledef::Constraint &constraint);
QString buildCreateTableText(const tabledef::TableSchema &schema);

// 表/索引/row-id 的载入与维护。
// 这些函数统一封装 service 层对持久化元数据和行定位信息的访问。
QList<tabledef::IndexMeta> loadUserTableIndexes(const QString &tableName, QString *error);
QList<tabledef::Constraint> loadUserTableConstraints(const QString &tableName, QString *error);
tabledef::TableSchema loadUserTableSchema(const QString &tableName, QString *error);
repo::TableData loadUserTableData(const QString &tableName, QString *error);
QStringList loadUserTableRowIds(const QString &tableName,
                                const repo::TableData &tableData,
                                bool *initialized,
                                QString *error);
bool saveUserTableRowIds(const QString &tableName, const QStringList &rowIds, QString *error);
bool rebuildTableIndexes(const QString &tableName,
                         const tabledef::TableSchema &schema,
                         const repo::TableData &tableData,
                         const QStringList &rowIds,
                         QString *error);
bool insertTableIndexes(const QString &tableName,
                        const tabledef::TableSchema &schema,
                        const repo::TableData &tableData,
                        const QStringList &rowIds,
                        const QList<int> &insertedRowIndexes,
                        QString *error);
bool updateTableIndexes(const QString &tableName,
                        const tabledef::TableSchema &schema,
                        const repo::TableData &currentTable,
                        const repo::TableData &candidateTable,
                        const QStringList &rowIds,
                        const QList<int> &changedRowIndexes,
                        QString *error);
bool deleteTableIndexes(const QString &tableName,
                        const tabledef::TableSchema &schema,
                        const repo::TableData &tableData,
                        const QStringList &rowIds,
                        const QList<int> &deletedRowIndexes,
                        QString *error);
bool ensureConstraintBoundIndex(const QString &tableName,
                                const tabledef::Constraint &constraint,
                                const repo::TableData &tableData,
                                QString *error);
bool removeConstraintBoundIndex(const QString &tableName,
                               const QString &constraintName,
                               QString *error);

} // namespace service

#endif // UTILS_SERVICE_COMMON_SERVICE_COMMON_H