/*
 * 这个文件包含了一些针对 TableSchema 的实用工具函数，主要用于 数据库级 校验。
*/

#include "table_manu.h"

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

bool isDefaultForeignKeyAction(ForeignKeyAction action)
{
    return action == ForeignKeyAction::NoAction;
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

    if (!isDefaultForeignKeyAction(constraint.onDeleteAction)
        || !isDefaultForeignKeyAction(constraint.onUpdateAction)) {
        return false;
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
               && lhs.referencedColumns == rhs.referencedColumns
               && lhs.onDeleteAction == rhs.onDeleteAction
               && lhs.onUpdateAction == rhs.onUpdateAction;
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

        if (!isForeignKeyConstraint(candidate)
            && (candidate.onDeleteAction != ForeignKeyAction::NoAction
                || candidate.onUpdateAction != ForeignKeyAction::NoAction)) {
            if (error != nullptr) {
                *error = QStringLiteral("constraint '%1' cannot define foreign key actions")
                             .arg(candidate.name);
            }
            return false;
        }

        if (isForeignKeyConstraint(candidate)) {
            for (const QString &columnName : candidate.columns) {
                const int columnIndex = findColumnIndex(schema, columnName);
                if (columnIndex < 0) {
                    if (error != nullptr) {
                        *error = QStringLiteral("column '%1' does not exist").arg(columnName);
                    }
                    return false;
                }

                const Column &column = schema.columns.at(columnIndex);
                if ((candidate.onDeleteAction == ForeignKeyAction::SetNull
                     || candidate.onUpdateAction == ForeignKeyAction::SetNull)
                    && column.notNull) {
                    if (error != nullptr) {
                        *error = QStringLiteral("foreign key '%1' cannot use SET NULL on NOT NULL column '%2'")
                                     .arg(candidate.name, column.name);
                    }
                    return false;
                }

                if ((candidate.onDeleteAction == ForeignKeyAction::SetDefault
                     || candidate.onUpdateAction == ForeignKeyAction::SetDefault)
                    && column.defaultValue.isEmpty()) {
                    if (error != nullptr) {
                        *error = QStringLiteral("foreign key '%1' cannot use SET DEFAULT on column '%2' without default value")
                                     .arg(candidate.name, column.name);
                    }
                    return false;
                }
            }
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

} // namespace tabledef
