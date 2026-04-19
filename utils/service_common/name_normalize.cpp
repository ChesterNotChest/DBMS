#include "service_common.h"

namespace service {

QString normalizeDatabaseName(const QString &databaseName)
{
    const QString trimmedDatabaseName = databaseName.trimmed();
    if (!trimmedDatabaseName.isEmpty()) {
        return trimmedDatabaseName;
    }
    return currentDatabase.trimmed();
}

QString generatedConstraintName(const QString &columnName, const QString &suffix)
{
    return columnName + QStringLiteral("__") + suffix;
}

} // namespace service