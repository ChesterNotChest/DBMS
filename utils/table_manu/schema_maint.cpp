#include "table_manu.h"

#include "../service_common/service_common.h"

#include "../../repo/repo.h"

#include <QSet>

namespace tabledef {

QStringList schemaColumnNames(const TableSchema &schema)
{
    QStringList names;
    names.reserve(schema.columns.size());
    for (const Column &column : schema.columns) {
        names.append(column.name);
    }
    return names;
}

int findColumnIndex(const TableSchema &schema, const QString &columnName)
{
    for (int index = 0; index < schema.columns.size(); ++index) {
        if (schema.columns.at(index).name == columnName) {
            return index;
        }
    }
    return -1;
}

bool hasColumn(const TableSchema &schema, const QString &columnName)
{
    return findColumnIndex(schema, columnName) >= 0;
}

int findConstraintIndex(const TableSchema &schema, const QString &constraintName)
{
    for (int index = 0; index < schema.constraints.size(); ++index) {
        if (schema.constraints.at(index).name == constraintName) {
            return index;
        }
    }
    return -1;
}

bool hasConstraint(const TableSchema &schema, const QString &constraintName)
{
    return findConstraintIndex(schema, constraintName) >= 0;
}

QStringList schemaIndexNames(const TableSchema &schema)
{
    QStringList names;
    names.reserve(schema.indexes.size());
    for (const IndexMeta &index : schema.indexes) {
        names.append(index.indexName);
    }
    return names;
}

int findIndexIndex(const TableSchema &schema, const QString &indexName)
{
    for (int index = 0; index < schema.indexes.size(); ++index) {
        if (schema.indexes.at(index).indexName == indexName) {
            return index;
        }
    }
    return -1;
}

bool hasIndex(const TableSchema &schema, const QString &indexName)
{
    return findIndexIndex(schema, indexName) >= 0;
}

bool constraintTouchesColumn(const Constraint &constraint, const QString &columnName)
{
    return constraint.columns.contains(columnName);
}

bool isForeignKeyConstraint(const Constraint &constraint)
{
    return constraint.type == ConstraintType::ForeignKey;
}

bool isPrimaryKeyConstraint(const Constraint &constraint)
{
    return constraint.type == ConstraintType::PrimaryKey;
}

bool isUniqueConstraint(const Constraint &constraint)
{
    return constraint.type == ConstraintType::Unique;
}

bool isCheckConstraint(const Constraint &constraint)
{
    return constraint.type == ConstraintType::Check;
}

bool constraintTypeRequiresReferenceTarget(ConstraintType type)
{
    return type == ConstraintType::ForeignKey;
}

bool isForeignKeyReferenceComplete(const Constraint &constraint)
{
    return !constraint.columns.isEmpty()
           && !constraint.referencedTable.trimmed().isEmpty()
           && !constraint.referencedColumns.isEmpty()
           && constraint.columns.size() == constraint.referencedColumns.size();
}

bool isConstraintDefinitionComplete(const Constraint &constraint)
{
    if (constraint.name.trimmed().isEmpty()) {
        return false;
    }

    if (constraint.columns.isEmpty()
        && constraint.type != ConstraintType::Check) {
        return false;
    }

    if (constraintTypeRequiresReferenceTarget(constraint.type)) {
        return isForeignKeyReferenceComplete(constraint);
    }

    if (constraint.type == ConstraintType::Check) {
        return !constraint.checkClause.trimmed().isEmpty();
    }

    return true;
}

bool hasPrimaryKeyConstraint(const TableSchema &schema)
{
    for (const Constraint &constraint : schema.constraints) {
        if (isPrimaryKeyConstraint(constraint)) {
            return true;
        }
    }
    return false;
}

bool sameConstraintSemantics(const Constraint &lhs, const Constraint &rhs)
{
    if (lhs.type != rhs.type || lhs.columns != rhs.columns) {
        return false;
    }

    switch (lhs.type) {
    case ConstraintType::PrimaryKey:
    case ConstraintType::Unique:
        return true;
    case ConstraintType::Check:
        return lhs.checkClause == rhs.checkClause;
    case ConstraintType::ForeignKey:
        return lhs.referencedTable == rhs.referencedTable
               && lhs.referencedColumns == rhs.referencedColumns;
    }

    return false;
}

bool sameIndexSemantics(const IndexMeta &lhs, const IndexMeta &rhs)
{
    return lhs.columnNames == rhs.columnNames && lhs.isUnique == rhs.isUnique;
}

bool validateIndexDefinition(const TableSchema &schema,
                             const IndexMeta &candidate,
                             const QString &skipIndexName,
                             QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    if (candidate.indexName.trimmed().isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("index name cannot be empty");
        }
        return false;
    }
    if (candidate.columnNames.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("index '%1' must reference at least one column").arg(candidate.indexName);
        }
        return false;
    }

    QSet<QString> uniqueColumns;
    for (const QString &columnName : candidate.columnNames) {
        if (!hasColumn(schema, columnName)) {
            if (error != nullptr) {
                *error = QStringLiteral("column '%1' does not exist").arg(columnName);
            }
            return false;
        }
        uniqueColumns.insert(columnName);
    }
    if (uniqueColumns.size() != candidate.columnNames.size()) {
        if (error != nullptr) {
            *error = QStringLiteral("index '%1' contains duplicate columns").arg(candidate.indexName);
        }
        return false;
    }

    for (const IndexMeta &existing : schema.indexes) {
        if (!skipIndexName.trimmed().isEmpty() && existing.indexName == skipIndexName) {
            continue;
        }
        if (existing.indexName == candidate.indexName) {
            if (error != nullptr) {
                *error = QStringLiteral("index '%1' already exists").arg(candidate.indexName);
            }
            return false;
        }
        if (sameIndexSemantics(existing, candidate)) {
            if (error != nullptr) {
                *error = QStringLiteral("index '%1' duplicates existing index '%2'")
                                 .arg(candidate.indexName, existing.indexName);
            }
            return false;
        }
    }

    return true;
}

bool validateConstraintDefinitions(const TableSchema &schema,
                                   const QString &skipConstraintName,
                                   QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    for (int index = 0; index < schema.constraints.size(); ++index) {
        const Constraint &candidate = schema.constraints.at(index);
        if (!isConstraintDefinitionComplete(candidate)) {
            if (error != nullptr) {
                *error = QStringLiteral("constraint '%1' is incomplete").arg(candidate.name);
            }
            return false;
        }

        for (int otherIndex = 0; otherIndex < schema.constraints.size(); ++otherIndex) {
            if (index == otherIndex) {
                continue;
            }

            const Constraint &existing = schema.constraints.at(otherIndex);
            if (!skipConstraintName.trimmed().isEmpty() && existing.name == skipConstraintName) {
                continue;
            }

            if (candidate.name == existing.name) {
                if (error != nullptr) {
                    *error = QStringLiteral("constraint '%1' already exists").arg(candidate.name);
                }
                return false;
            }

            if (isPrimaryKeyConstraint(candidate) && isPrimaryKeyConstraint(existing)) {
                if (error != nullptr) {
                    *error = QStringLiteral("primary key already exists");
                }
                return false;
            }

            if (sameConstraintSemantics(candidate, existing)) {
                if (error != nullptr) {
                    *error = QStringLiteral("constraint '%1' duplicates existing constraint '%2'")
                                 .arg(candidate.name, existing.name);
                }
                return false;
            }
        }
    }

    return true;
}

bool validateConstraintRows(const QString &databaseName,
                            const QString &dataRoot,
                            const TableSchema &schema,
                            const QStringList &tableColumns,
                            const QList<QStringList> &tableRows,
                            QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    Q_UNUSED(databaseName);
    Q_UNUSED(dataRoot);

    for (const Constraint &constraint : schema.constraints) {
        if (isPrimaryKeyConstraint(constraint) || isUniqueConstraint(constraint)) {
            QSet<QString> seenKeys;
            for (const QStringList &row : tableRows) {
                QStringList values;
                values.reserve(constraint.columns.size());
                bool hasEmptyValue = false;

                for (const QString &columnName : constraint.columns) {
                    const int columnIndex = tableColumns.indexOf(columnName);
                    if (columnIndex < 0) {
                        if (error != nullptr) {
                            *error = QStringLiteral("column '%1' does not exist").arg(columnName);
                        }
                        return false;
                    }

                    const QString value = row.value(columnIndex);
                    if (value.isEmpty()) {
                        hasEmptyValue = true;
                    }
                    values.append(value);
                }

                if (isPrimaryKeyConstraint(constraint) && hasEmptyValue) {
                    if (error != nullptr) {
                        *error = QStringLiteral("primary key '%1' cannot contain empty values")
                                     .arg(constraint.name);
                    }
                    return false;
                }

                if (isUniqueConstraint(constraint) && hasEmptyValue) {
                    continue;
                }

                const QString key = service::compositeKeySignature(values);
                if (seenKeys.contains(key)) {
                    if (error != nullptr) {
                        *error = QStringLiteral("constraint '%1' is violated by duplicate values")
                                     .arg(constraint.name);
                    }
                    return false;
                }
                seenKeys.insert(key);
            }
            continue;
        }

        if (!isForeignKeyConstraint(constraint)) {
            continue;
        }
        if (!isForeignKeyReferenceComplete(constraint)) {
            if (error != nullptr) {
                *error = QStringLiteral("foreign key '%1' is incomplete").arg(constraint.name);
            }
            return false;
        }

        repo::TabRepo tabRepo(databaseName, dataRoot);
        QString tabError;
        if (!tabRepo.hasTable(constraint.referencedTable, &tabError)) {
            if (error != nullptr) {
                *error = tabError.isEmpty()
                             ? QStringLiteral("referenced table '%1' does not exist").arg(constraint.referencedTable)
                             : tabError;
            }
            return false;
        }

        repo::MetaRepo parentMeta(databaseName, constraint.referencedTable, dataRoot);
        const QList<Column> parentColumns = parentMeta.listColumns(&tabError);
        if (!tabError.isEmpty()) {
            if (error != nullptr) {
                *error = tabError;
            }
            return false;
        }

        for (const QString &referencedColumn : constraint.referencedColumns) {
            if (!hasColumn(TableSchema{constraint.referencedTable, parentColumns, {}}, referencedColumn)) {
                if (error != nullptr) {
                    *error = QStringLiteral("referenced column '%1' does not exist").arg(referencedColumn);
                }
                return false;
            }
        }

        repo::TableData parentTable = repo::TableRepo(databaseName, constraint.referencedTable, dataRoot)
                                          .readTable(&tabError);
        if (!tabError.isEmpty()) {
            if (error != nullptr) {
                *error = tabError;
            }
            return false;
        }

        for (const QStringList &row : tableRows) {
            QStringList values;
            values.reserve(constraint.columns.size());
            bool hasEmptyValue = false;

            for (const QString &columnName : constraint.columns) {
                const int columnIndex = tableColumns.indexOf(columnName);
                if (columnIndex < 0) {
                    if (error != nullptr) {
                        *error = QStringLiteral("column '%1' does not exist").arg(columnName);
                    }
                    return false;
                }

                const QString value = row.value(columnIndex);
                if (value.isEmpty()) {
                    hasEmptyValue = true;
                }
                values.append(value);
            }

            if (hasEmptyValue) {
                continue;
            }

            QString rowError;
            if (!service::rowExistsInTable(parentTable, constraint.referencedColumns, values, &rowError)) {
                if (error != nullptr) {
                    *error = rowError.isEmpty()
                                 ? QStringLiteral("foreign key '%1' references missing parent row")
                                       .arg(constraint.name)
                                 : rowError;
                }
                return false;
            }
        }
    }

    return true;
}

bool validateNoIncomingForeignKeyReferences(const QString &databaseName,
                                           const QString &dataRoot,
                                           const QString &targetTableName,
                                           QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    repo::TabRepo tabRepo(databaseName, dataRoot);
    QString tabError;
    const QList<repo::TableEntry> tableEntries = tabRepo.listTables(&tabError);
    if (!tabError.isEmpty()) {
        if (error != nullptr) {
            *error = tabError;
        }
        return false;
    }

    for (const repo::TableEntry &tableEntry : tableEntries) {
        if (tableEntry.name == targetTableName) {
            continue;
        }

        repo::ConstraintRepo constraintRepo(databaseName, tableEntry.name, dataRoot);
        const QList<Constraint> constraints = constraintRepo.listConstraints(&tabError);
        if (!tabError.isEmpty()) {
            if (error != nullptr) {
                *error = tabError;
            }
            return false;
        }

        for (const Constraint &constraint : constraints) {
            if (isForeignKeyConstraint(constraint) && constraint.referencedTable == targetTableName) {
                if (error != nullptr) {
                    *error = QStringLiteral("table '%1' is referenced by foreign key '%2' from table '%3'")
                                 .arg(targetTableName, constraint.name, tableEntry.name);
                }
                return false;
            }
        }
    }

    return true;
}

} // namespace tabledef
