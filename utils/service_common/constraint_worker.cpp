#include "service_common.h"

namespace service {

bool validateColumnDefinition(const ColumnDefinition &definition, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    if (definition.column.name.trimmed().isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("column name cannot be empty");
        }
        return false;
    }

    if (definition.referencedTable.trimmed().isEmpty()
        && !definition.referencedColumns.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("foreign key column '%1' is missing a referenced table")
                         .arg(definition.column.name);
        }
        return false;
    }

    if (!definition.referencedTable.trimmed().isEmpty()
        && definition.referencedColumns.size() != 1) {
        if (error != nullptr) {
            *error = QStringLiteral("foreign key column '%1' must reference exactly one column")
                         .arg(definition.column.name);
        }
        return false;
    }

    return true;
}

tabledef::Constraint makeConstraint(const QString &constraintName,
                                    tabledef::ConstraintType type,
                                    const QStringList &columns,
                                    const QString &referencedTable,
                                    const QStringList &referencedColumns,
                                    const QString &checkClause)
{
    return tabledef::Constraint{constraintName,
                                type,
                                columns,
                                referencedTable,
                                referencedColumns,
                                checkClause};
}

QList<tabledef::Constraint> buildGeneratedConstraints(const ColumnDefinition &definition)
{
    QList<tabledef::Constraint> constraints;
    const QString columnName = definition.column.name;

    if (definition.primaryKey) {
        constraints.append(makeConstraint(generatedConstraintName(columnName, QStringLiteral("pk")),
                                          tabledef::ConstraintType::PrimaryKey,
                                          {columnName}));
    }
    if (definition.unique) {
        constraints.append(makeConstraint(generatedConstraintName(columnName, QStringLiteral("uq")),
                                          tabledef::ConstraintType::Unique,
                                          {columnName}));
    }
    if (!definition.referencedTable.trimmed().isEmpty()) {
        constraints.append(makeConstraint(generatedConstraintName(columnName, QStringLiteral("fk")),
                                          tabledef::ConstraintType::ForeignKey,
                                          {columnName},
                                          definition.referencedTable,
                                          definition.referencedColumns));
    }
    if (!definition.checkClause.trimmed().isEmpty()) {
        constraints.append(makeConstraint(generatedConstraintName(columnName, QStringLiteral("ck")),
                                          tabledef::ConstraintType::Check,
                                          {columnName},
                                          QString(),
                                          {},
                                          definition.checkClause));
    }

    return constraints;
}

tabledef::TableSchema loadUserTableSchema(const QString &tableName, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    tabledef::TableSchema schema;
    schema.tableName = tableName;

    const QString databaseName = normalizeDatabaseName(QString());
    repo::MetaRepo metaRepo(databaseName, tableName, currentDataRoot);
    schema.columns = metaRepo.listColumns(error);
    if (error != nullptr && !error->isEmpty()) {
        return {};
    }

    repo::ConstraintRepo constraintRepo(databaseName, tableName, currentDataRoot);
    schema.constraints = constraintRepo.listConstraints(error);
    if (error != nullptr && !error->isEmpty()) {
        return {};
    }

    return schema;
}

repo::TableData loadUserTableData(const QString &tableName, QString *error)
{
    const QString databaseName = normalizeDatabaseName(QString());
    repo::TableRepo tableRepo(databaseName, tableName, currentDataRoot);
    return tableRepo.readTable(error);
}

} // namespace service