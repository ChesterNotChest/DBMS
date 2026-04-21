#ifndef UTILS_SERVICE_COMMON_SERVICE_COMMON_H
#define UTILS_SERVICE_COMMON_SERVICE_COMMON_H

#include "../../service/service.h"

namespace service {

QString normalizeDatabaseName(const QString &databaseName);
QString generatedConstraintName(const QString &columnName, const QString &suffix);

bool validateColumnDefinition(const ColumnDefinition &definition, QString *error);
QList<tabledef::Constraint> buildGeneratedConstraints(const ColumnDefinition &definition);
tabledef::Constraint makeConstraint(const QString &constraintName,
                                    tabledef::ConstraintType type,
                                    const QStringList &columns,
                                    const QString &referencedTable = QString(),
                                    const QStringList &referencedColumns = QStringList(),
                                    const QString &checkClause = QString(),
                                    const QString &indexName = QString());

QString formatColumnDefinition(const tabledef::Column &column);
QString formatConstraintDefinition(const tabledef::Constraint &constraint);
QString buildCreateTableText(const tabledef::TableSchema &schema);

QList<tabledef::IndexMeta> loadUserTableIndexes(const QString &tableName, QString *error);
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