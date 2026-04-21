#include "service_common.h"

namespace service {

// 把 schema 对象转成 SQL/描述文本，避免 service 层直接拼接格式字符串。
QString formatColumnDefinition(const tabledef::Column &column)
{
    QStringList parts;
    parts.append(column.name);
    if (column.type == tabledef::ColumnType::Varchar && column.length > 0) {
        parts.append(QStringLiteral("VARCHAR(%1)").arg(column.length));
    } else {
        parts.append(tabledef::columnTypeToString(column.type));
    }

    if (column.notNull) {
        parts.append(QStringLiteral("NOT NULL"));
    }
    if (!column.defaultValue.isEmpty()) {
        parts.append(QStringLiteral("DEFAULT %1").arg(column.defaultValue));
    }
    if (column.autoIncrement) {
        parts.append(QStringLiteral("AUTO_INCREMENT"));
    }

    return parts.join(QStringLiteral(" "));
}

QString formatConstraintDefinition(const tabledef::Constraint &constraint)
{
    const QString columnList = constraint.columns.join(QStringLiteral(", "));
    switch (constraint.type) {
    case tabledef::ConstraintType::PrimaryKey:
        return QStringLiteral("CONSTRAINT %1 PRIMARY KEY (%2)").arg(constraint.name, columnList);
    case tabledef::ConstraintType::Unique:
        return QStringLiteral("CONSTRAINT %1 UNIQUE (%2)").arg(constraint.name, columnList);
    case tabledef::ConstraintType::Check:
        return QStringLiteral("CONSTRAINT %1 CHECK (%2)").arg(constraint.name, constraint.checkClause);
    case tabledef::ConstraintType::ForeignKey:
    {
        QString definition = QStringLiteral("CONSTRAINT %1 FOREIGN KEY (%2) REFERENCES %3(%4)")
                                 .arg(constraint.name,
                                      columnList,
                                      constraint.referencedTable,
                                      constraint.referencedColumns.join(QStringLiteral(", ")));
        if (constraint.onDeleteAction != tabledef::ForeignKeyAction::NoAction) {
            definition.append(QStringLiteral(" ON DELETE %1")
                                  .arg(tabledef::foreignKeyActionToString(constraint.onDeleteAction)));
        }
        if (constraint.onUpdateAction != tabledef::ForeignKeyAction::NoAction) {
            definition.append(QStringLiteral(" ON UPDATE %1")
                                  .arg(tabledef::foreignKeyActionToString(constraint.onUpdateAction)));
        }
        return definition;
    }
    }

    return constraint.name;
}

// 统一 CREATE TABLE 的输出格式，供 show create 直接复用。
QString buildCreateTableText(const tabledef::TableSchema &schema)
{
    QStringList lines;
    lines.append(QStringLiteral("CREATE TABLE %1 (").arg(schema.tableName));

    const int columnCount = schema.columns.size();
    for (int columnIndex = 0; columnIndex < columnCount; ++columnIndex) {
        QString line = QStringLiteral("  %1").arg(formatColumnDefinition(schema.columns.at(columnIndex)));
        if (columnIndex < columnCount - 1 || !schema.constraints.isEmpty()) {
            line.append(QStringLiteral(","));
        }
        lines.append(line);
    }

    for (int constraintIndex = 0; constraintIndex < schema.constraints.size(); ++constraintIndex) {
        QString line = QStringLiteral("  %1").arg(formatConstraintDefinition(schema.constraints.at(constraintIndex)));
        if (constraintIndex < schema.constraints.size() - 1) {
            line.append(QStringLiteral(","));
        }
        lines.append(line);
    }

    lines.append(QStringLiteral(");"));
    return lines.join(QStringLiteral("\n"));
}

} // namespace service