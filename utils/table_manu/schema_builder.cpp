#include "table_manu.h"

namespace tabledef {

TableSchema buildDatabaseRootSchema()
{
    TableSchema schema;
    schema.tableName = QStringLiteral("root.dbf");
    schema.columns = {
        Column{QStringLiteral("database_name"), ColumnType::Varchar, 255, true},
    };
    schema.constraints = {
        Constraint{QStringLiteral("pk_root_database"), ConstraintType::PrimaryKey,
                   {QStringLiteral("database_name")}, QString(), {}, QString()},
    };
    return schema;
}

TableSchema buildDatabaseTableCatalogSchema(const QString &databaseName)
{
    TableSchema schema;
    schema.tableName = databaseName.trimmed().isEmpty()
                           ? QStringLiteral("[database].tab")
                           : databaseName + QStringLiteral(".tab");
    schema.columns = {
        Column{QStringLiteral("table_name"), ColumnType::Varchar, 255, true},
    };
    schema.constraints = {
        Constraint{QStringLiteral("pk_table_catalog"), ConstraintType::PrimaryKey,
                   {QStringLiteral("table_name")}, QString(), {}, QString()},
    };
    return schema;
}

TableSchema buildTableMetaSchema(const QString &tableName)
{
    Q_UNUSED(tableName);

    TableSchema schema;
    schema.tableName = QStringLiteral("table.meta");
    schema.columns = {
        Column{QStringLiteral("column_name"), ColumnType::Varchar, 255, true},
        Column{QStringLiteral("column_type"), ColumnType::Varchar, 64, true},
        Column{QStringLiteral("length"), ColumnType::Int, 0, false, QStringLiteral("0")},
        Column{QStringLiteral("not_null"), ColumnType::Varchar, 8, false, QStringLiteral("false")},
        Column{QStringLiteral("default_value"), ColumnType::Varchar, 2048, false},
        Column{QStringLiteral("auto_increment"), ColumnType::Varchar, 8, false, QStringLiteral("false")},
        Column{QStringLiteral("ordinal_position"), ColumnType::Int, 0, true},
    };
    schema.constraints = {
        Constraint{QStringLiteral("pk_column_name"), ConstraintType::PrimaryKey,
                   {QStringLiteral("column_name")}, QString(), {}, QString()},
    };
    return schema;
}

TableSchema buildTableConstraintSchema(const QString &tableName)
{
    Q_UNUSED(tableName);

    TableSchema schema;
    schema.tableName = QStringLiteral("table.con");
    schema.columns = {
        Column{QStringLiteral("constraint_name"), ColumnType::Varchar, 255, true},
        Column{QStringLiteral("constraint_type"), ColumnType::Varchar, 64, true},
        Column{QStringLiteral("constraint_columns"), ColumnType::Varchar, 1024, false},
        Column{QStringLiteral("referenced_table"), ColumnType::Varchar, 255, false},
        Column{QStringLiteral("referenced_columns"), ColumnType::Varchar, 1024, false},
        Column{QStringLiteral("check_clause"), ColumnType::Varchar, 2048, false},
    };
    schema.constraints = {
        Constraint{QStringLiteral("pk_constraint_name"), ConstraintType::PrimaryKey,
                   {QStringLiteral("constraint_name")}, QString(), {}, QString()},
    };
    return schema;
}

} // namespace tabledef
