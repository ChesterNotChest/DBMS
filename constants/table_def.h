#ifndef CONSTANTS_TABLE_DEF_H
#define CONSTANTS_TABLE_DEF_H

#include <QList>
#include <QString>
#include <QStringList>

namespace tabledef {

enum class ColumnType {
    Int,
    Varchar,
    Float
};

enum class ConstraintType {
    PrimaryKey,
    Unique,
    Check
};

struct Column
{
    QString name;
    ColumnType type = ColumnType::Varchar;
    int length = 0;
    bool notNull = false;
    QString defaultValue;
    QString check;
};

struct Constraint
{
    QString name;
    ConstraintType type = ConstraintType::Check;
    QStringList columns;
    QString checkClause;
};

struct TableSchema
{
    QString tableName;
    QList<Column> columns;
    QList<Constraint> constraints;
};

inline QString columnTypeToString(ColumnType type)
{
    switch (type) {
    case ColumnType::Int:
        return QStringLiteral("INT");
    case ColumnType::Varchar:
        return QStringLiteral("VARCHAR");
    case ColumnType::Float:
        return QStringLiteral("FLOAT");
    }
    return QStringLiteral("VARCHAR");
}

inline QString constraintTypeToString(ConstraintType type)
{
    switch (type) {
    case ConstraintType::PrimaryKey:
        return QStringLiteral("PRIMARY_KEY");
    case ConstraintType::Unique:
        return QStringLiteral("UNIQUE");
    case ConstraintType::Check:
        return QStringLiteral("CHECK");
    }
    return QStringLiteral("CHECK");
}

inline bool tryParseConstraintType(const QString &value, ConstraintType *type)
{
    if (type == nullptr) {
        return false;
    }
    if (value == QStringLiteral("PRIMARY_KEY")) {
        *type = ConstraintType::PrimaryKey;
        return true;
    }
    if (value == QStringLiteral("UNIQUE")) {
        *type = ConstraintType::Unique;
        return true;
    }
    if (value == QStringLiteral("CHECK")) {
        *type = ConstraintType::Check;
        return true;
    }
    return false;
}

inline QStringList schemaColumnNames(const TableSchema &schema)
{
    QStringList names;
    names.reserve(schema.columns.size());
    for (const Column &column : schema.columns) {
        names.append(column.name);
    }
    return names;
}

inline TableSchema buildDatabaseRootSchema()
{
    TableSchema schema;
    schema.tableName = QStringLiteral("root.dbf");
    schema.columns = {
        Column{QStringLiteral("database_name"), ColumnType::Varchar, 255, true},
        Column{QStringLiteral("meta_file"), ColumnType::Varchar, 255, true},
    };
    schema.constraints = {
        Constraint{QStringLiteral("pk_root_database"), ConstraintType::PrimaryKey,
                   {QStringLiteral("database_name")}, QString()},
    };
    return schema;
}

inline TableSchema buildDatabaseMetaSchema(const QString &databaseName = QString())
{
    TableSchema schema;
    schema.tableName = databaseName.trimmed().isEmpty()
                           ? QStringLiteral("[database].meta")
                           : databaseName + QStringLiteral(".meta");
    schema.columns = {
        Column{QStringLiteral("table_name"), ColumnType::Varchar, 255, true},
        Column{QStringLiteral("table_file"), ColumnType::Varchar, 255, true},
    };
    schema.constraints = {
        Constraint{QStringLiteral("pk_meta_table"), ConstraintType::PrimaryKey,
                   {QStringLiteral("table_name")}, QString()},
    };
    return schema;
}

inline TableSchema buildTableConstraintSchema(const QString &tableName = QString())
{
    TableSchema schema;
    schema.tableName = tableName.trimmed().isEmpty()
                           ? QStringLiteral("[table].con")
                           : tableName + QStringLiteral(".con");
    schema.columns = {
        Column{QStringLiteral("constraint_name"), ColumnType::Varchar, 255, true},
        Column{QStringLiteral("constraint_type"), ColumnType::Varchar, 64, true},
        Column{QStringLiteral("constraint_columns"), ColumnType::Varchar, 1024, false},
        Column{QStringLiteral("check_clause"), ColumnType::Varchar, 2048, false},
    };
    schema.constraints = {
        Constraint{QStringLiteral("pk_constraint_name"), ConstraintType::PrimaryKey,
                   {QStringLiteral("constraint_name")}, QString()},
    };
    return schema;
}

} // namespace tabledef

#endif // CONSTANTS_TABLE_DEF_H
