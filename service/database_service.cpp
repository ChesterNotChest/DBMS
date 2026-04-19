#include "service.h"

namespace {

bool databaseExists(const QString &databaseName, QString *error)
{
    service::TableDmlService dmlService;
    const service::SelectRowsResult result = dmlService.selectRows(
        QString(),
        QString(),
        service::TargetTableKind::RootDbf,
        tabledef::buildDatabaseRootSchema(),
        {QStringLiteral("database_name")},
        {service::SimpleCondition{QStringLiteral("database_name"), databaseName}});
    if (!result.errorMessage.isEmpty()) {
        if (error != nullptr) {
            *error = result.errorMessage;
        }
        return false;
    }
    return result.affectedRowCount > 0;
}

} // namespace

namespace service::database_service {

TaskResult createDatabase(const QString &databaseName)
{
    TaskResult result;
    const QString normalizedDatabaseName = databaseName.trimmed();
    if (normalizedDatabaseName.isEmpty()) {
        result.errorMessage = QStringLiteral("database name cannot be empty");
        return result;
    }

    QString error;
    if (databaseExists(normalizedDatabaseName, &error)) {
        result.errorMessage = QStringLiteral("database '%1' already exists").arg(normalizedDatabaseName);
        return result;
    }
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    repo::FlatFileTableStore store(currentDataRoot);
    const repo::RepositoryResult rootReady = repo::DatabaseRepo(currentDataRoot).initialize();
    if (!rootReady.ok) {
        result.errorMessage = rootReady.error;
        return result;
    }

    const repo::RepositoryResult directoryReady = store.ensureDirectory(
        store.getDatabaseDirectory(normalizedDatabaseName));
    if (!directoryReady.ok) {
        result.errorMessage = directoryReady.error;
        return result;
    }

    const repo::RepositoryResult tabReady = repo::TabRepo(normalizedDatabaseName, currentDataRoot).initialize();
    if (!tabReady.ok) {
        store.removeDirectoryRecursively(store.getDatabaseDirectory(normalizedDatabaseName));
        result.errorMessage = tabReady.error;
        return result;
    }

    service::TableDmlService dmlService;
    const service::TableDmlResult insertResult = dmlService.insertRows(
        QString(),
        QString(),
        service::TargetTableKind::RootDbf,
        tabledef::buildDatabaseRootSchema(),
        {QMap<QString, QString>{{QStringLiteral("database_name"), normalizedDatabaseName}}},
        service::ValidationMode::SystemMeta);
    if (!insertResult.success) {
        store.removeDirectoryRecursively(store.getDatabaseDirectory(normalizedDatabaseName));
        result.errorMessage = insertResult.errorMessage;
        return result;
    }

    result.success = true;
    return result;
}

TaskResult dropDatabase(const QString &databaseName)
{
    TaskResult result;
    const QString normalizedDatabaseName = databaseName.trimmed();
    if (normalizedDatabaseName.isEmpty()) {
        result.errorMessage = QStringLiteral("database name cannot be empty");
        return result;
    }

    QString error;
    if (!databaseExists(normalizedDatabaseName, &error)) {
        if (!error.isEmpty()) {
            result.errorMessage = error;
        } else {
            result.errorMessage = QStringLiteral("database '%1' does not exist").arg(normalizedDatabaseName);
        }
        return result;
    }

    repo::FlatFileTableStore store(currentDataRoot);
    const repo::RepositoryResult removeDirectoryResult =
        store.removeDirectoryRecursively(store.getDatabaseDirectory(normalizedDatabaseName));
    if (!removeDirectoryResult.ok) {
        result.errorMessage = removeDirectoryResult.error;
        return result;
    }

    service::TableDmlService dmlService;
    const service::TableDmlResult deleteResult = dmlService.deleteRows(
        QString(),
        QString(),
        service::TargetTableKind::RootDbf,
        tabledef::buildDatabaseRootSchema(),
        {service::SimpleCondition{QStringLiteral("database_name"), normalizedDatabaseName}},
        service::ValidationMode::SystemMeta);
    if (!deleteResult.success) {
        result.errorMessage = deleteResult.errorMessage;
        return result;
    }

    if (currentDatabase == normalizedDatabaseName) {
        currentDatabase.clear();
    }

    result.success = true;
    result.affectedRowCount = deleteResult.affectedRowCount;
    return result;
}

TaskResult useDatabase(const QString &databaseName)
{
    TaskResult result;
    const QString normalizedDatabaseName = databaseName.trimmed();
    if (normalizedDatabaseName.isEmpty()) {
        result.errorMessage = QStringLiteral("database name cannot be empty");
        return result;
    }

    QString error;
    if (!databaseExists(normalizedDatabaseName, &error)) {
        if (!error.isEmpty()) {
            result.errorMessage = error;
        } else {
            result.errorMessage = QStringLiteral("database '%1' does not exist").arg(normalizedDatabaseName);
        }
        return result;
    }

    currentDatabase = normalizedDatabaseName;
    result.success = true;
    return result;
}

SelectRowsResult showDatabases()
{
    TableDmlService dmlService;
    return dmlService.selectRows(QString(),
                                 QString(),
                                 TargetTableKind::RootDbf,
                                 tabledef::buildDatabaseRootSchema(),
                                 {QStringLiteral("*")},
                                 {});
}

} // namespace service::database_service