#include "table_manu.h"

namespace tabledef {

QString boolToString(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

bool stringToBool(const QString &value)
{
    return value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0
           || value == QStringLiteral("1");
}

QString columnTypeToString(ColumnType type)
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

bool tryParseColumnType(const QString &value, ColumnType *type)
{
    if (type == nullptr) {
        return false;
    }
    if (value == QStringLiteral("INT")) {
        *type = ColumnType::Int;
        return true;
    }
    if (value == QStringLiteral("VARCHAR")) {
        *type = ColumnType::Varchar;
        return true;
    }
    if (value == QStringLiteral("FLOAT")) {
        *type = ColumnType::Float;
        return true;
    }
    return false;
}

QString constraintTypeToString(ConstraintType type)
{
    switch (type) {
    case ConstraintType::PrimaryKey:
        return QStringLiteral("PRIMARY_KEY");
    case ConstraintType::Unique:
        return QStringLiteral("UNIQUE");
    case ConstraintType::Check:
        return QStringLiteral("CHECK");
    case ConstraintType::ForeignKey:
        return QStringLiteral("FOREIGN_KEY");
    }
    return QStringLiteral("CHECK");
}

bool tryParseConstraintType(const QString &value, ConstraintType *type)
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
    if (value == QStringLiteral("FOREIGN_KEY")) {
        *type = ConstraintType::ForeignKey;
        return true;
    }
    return false;
}

QString foreignKeyActionToString(ForeignKeyAction action)
{
    switch (action) {
    case ForeignKeyAction::NoAction:
        return QStringLiteral("NO ACTION");
    case ForeignKeyAction::Restrict:
        return QStringLiteral("RESTRICT");
    case ForeignKeyAction::Cascade:
        return QStringLiteral("CASCADE");
    case ForeignKeyAction::SetNull:
        return QStringLiteral("SET NULL");
    case ForeignKeyAction::SetDefault:
        return QStringLiteral("SET DEFAULT");
    }
    return QStringLiteral("NO ACTION");
}

bool tryParseForeignKeyAction(const QString &value, ForeignKeyAction *action)
{
    if (action == nullptr) {
        return false;
    }
    if (value == QStringLiteral("NO ACTION") || value == QStringLiteral("NO_ACTION")) {
        *action = ForeignKeyAction::NoAction;
        return true;
    }
    if (value == QStringLiteral("RESTRICT")) {
        *action = ForeignKeyAction::Restrict;
        return true;
    }
    if (value == QStringLiteral("CASCADE")) {
        *action = ForeignKeyAction::Cascade;
        return true;
    }
    if (value == QStringLiteral("SET NULL") || value == QStringLiteral("SET_NULL")) {
        *action = ForeignKeyAction::SetNull;
        return true;
    }
    if (value == QStringLiteral("SET DEFAULT") || value == QStringLiteral("SET_DEFAULT")) {
        *action = ForeignKeyAction::SetDefault;
        return true;
    }
    return false;
}

} // namespace tabledef
