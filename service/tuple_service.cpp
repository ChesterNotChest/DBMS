#include "service.h"
#include "../constants/thread_perf_def.h"

namespace {

QList<thread_runtime::RuntimeLockKey> mutationLockKeys(const QString &databaseName,
                                                       const QStringList &tableNames)
{
    QList<thread_runtime::RuntimeLockKey> keys;
    keys.reserve(tableNames.size());
    for (const QString &tableName : tableNames) {
        keys.append(thread_runtime::tableLockKey(service::currentDataRoot, databaseName, tableName));
    }
    return keys;
}

std::vector<thread_runtime::ScopedRuntimeLock> acquireMutationLocks(const QString &databaseName,
                                                                    const QString &tableName,
                                                                    QString *error)
{
    const QStringList relatedTables = service::collectMutationRelatedTables(databaseName, tableName, error);
    if (error != nullptr && !error->isEmpty()) {
        return {};
    }

    return thread_runtime::RuntimeLockManager::instance().acquireOrderedLocks(
        mutationLockKeys(databaseName, relatedTables),
        thread_runtime::RuntimeLockMode::Exclusive,
        threadperf::kTableLockAcquireTimeoutMs,
        error);
}

} // namespace

namespace service::tuple_service {

SelectRowsResult selectRows(const QString &tableName,
                            const QStringList &projectionColumns,
                            const QList<SimpleCondition> &conditions,
                            int limit)
{
    SelectRowsResult result;
    const QString databaseName = normalizeDatabaseName(QString());
    QString error;
    thread_runtime::ScopedRuntimeLock runtimeLock;
    if (threadperf::kEnableSharedReadLock) {
        runtimeLock = thread_runtime::RuntimeLockManager::instance().acquireLock(
            thread_runtime::tableLockKey(currentDataRoot, databaseName, tableName),
            thread_runtime::RuntimeLockMode::Shared,
            threadperf::kTableLockAcquireTimeoutMs,
            &error);
        if (!runtimeLock.isValid()) {
            result.errorMessage = error;
            return result;
        }
    }

    const tabledef::TableSchema schema = loadUserTableSchema(tableName, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    TableDmlService dmlService;
    return dmlService.selectRows(databaseName,
                                 tableName,
                                 TargetTableKind::TableDat,
                                 schema,
                                 projectionColumns.isEmpty() ? QStringList{QStringLiteral("*")} : projectionColumns,
                                 conditions,
                                 limit);
}

TaskResult insertRows(const QString &tableName,
                      const QList<QMap<QString, QString>> &rows)
{
    TaskResult result;
    const QString databaseName = normalizeDatabaseName(QString());
    QString error;
    std::vector<thread_runtime::ScopedRuntimeLock> runtimeLocks = acquireMutationLocks(databaseName, tableName, &error);
    if (runtimeLocks.empty() && !error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    const tabledef::TableSchema schema = loadUserTableSchema(tableName, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    TableDmlService dmlService;
    const TableDmlResult dmlResult = dmlService.insertRows(databaseName,
                                                          tableName,
                                                          TargetTableKind::TableDat,
                                                          schema,
                                                          rows,
                                                          ValidationMode::UserData);
    result.success = dmlResult.success;
    result.errorMessage = dmlResult.errorMessage;
    result.affectedRowCount = dmlResult.affectedRowCount;
    return result;
}

TaskResult deleteRows(const QString &tableName,
                      const QList<SimpleCondition> &conditions)
{
    TaskResult result;
    const QString databaseName = normalizeDatabaseName(QString());
    QString error;
    std::vector<thread_runtime::ScopedRuntimeLock> runtimeLocks = acquireMutationLocks(databaseName, tableName, &error);
    if (runtimeLocks.empty() && !error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    const tabledef::TableSchema schema = loadUserTableSchema(tableName, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    TableDmlService dmlService;
    const TableDmlResult dmlResult = dmlService.deleteRows(databaseName,
                                                          tableName,
                                                          TargetTableKind::TableDat,
                                                          schema,
                                                          conditions,
                                                          ValidationMode::UserData);
    result.success = dmlResult.success;
    result.errorMessage = dmlResult.errorMessage;
    result.affectedRowCount = dmlResult.affectedRowCount;
    return result;
}

TaskResult updateRows(const QString &tableName,
                      const QMap<QString, QString> &assignmentMap,
                      const QList<SimpleCondition> &conditions)
{
    TaskResult result;
    const QString databaseName = normalizeDatabaseName(QString());
    QString error;
    std::vector<thread_runtime::ScopedRuntimeLock> runtimeLocks = acquireMutationLocks(databaseName, tableName, &error);
    if (runtimeLocks.empty() && !error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    const tabledef::TableSchema schema = loadUserTableSchema(tableName, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    TableDmlService dmlService;
    const TableDmlResult dmlResult = dmlService.updateRows(databaseName,
                                                          tableName,
                                                          TargetTableKind::TableDat,
                                                          schema,
                                                          assignmentMap,
                                                          conditions,
                                                          ValidationMode::UserData);
    result.success = dmlResult.success;
    result.errorMessage = dmlResult.errorMessage;
    result.affectedRowCount = dmlResult.affectedRowCount;
    return result;
}

} // namespace service::tuple_service
