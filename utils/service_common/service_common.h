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
                                    const QString &checkClause = QString());

QString formatColumnDefinition(const tabledef::Column &column);
QString formatConstraintDefinition(const tabledef::Constraint &constraint);
QString buildCreateTableText(const tabledef::TableSchema &schema);

tabledef::TableSchema loadUserTableSchema(const QString &tableName, QString *error);
repo::TableData loadUserTableData(const QString &tableName, QString *error);

} // namespace service

#endif // UTILS_SERVICE_COMMON_SERVICE_COMMON_H