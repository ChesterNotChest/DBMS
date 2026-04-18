#include "table_manu.h"

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

} // namespace tabledef
