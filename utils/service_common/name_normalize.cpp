#include "service_common.h"

namespace service {

thread_local QString currentDatabase;

// 这里保持最小粒度，只放数据库名与派生名称的纯字符串规则。
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