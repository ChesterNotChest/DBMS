#include "auth_service.h"

#include "../constants/cli_client_def.h"

#include <QDir>

namespace service::auth_service {

namespace {

QString authDatabaseName()
{
    return QString::fromLatin1(cliclient::kAuthDatabaseName);
}

QString userTableName()
{
    return QString::fromLatin1(cliclient::kUserTableName);
}

QString privilegeTableName()
{
    return QString::fromLatin1(cliclient::kPrivilegeTableName);
}

QString rootUserName()
{
    return QString::fromLatin1(cliclient::kRootUserName);
}

repo::FlatFileTableStore storeFor(const QString &dataRoot)
{
    return repo::FlatFileTableStore(dataRoot.trimmed().isEmpty()
                                        ? repo::FlatFileTableStore::defaultDataRoot()
                                        : QDir::cleanPath(dataRoot));
}

QString userTablePath(const repo::FlatFileTableStore &store)
{
    return store.getTableFilePath(authDatabaseName(), userTableName());
}

QString privilegeTablePath(const repo::FlatFileTableStore &store)
{
    return store.getTableFilePath(authDatabaseName(), privilegeTableName());
}

repo::RepositoryResult ensureAuthTable(const repo::FlatFileTableStore &store,
                                       const QString &tableName,
                                       const QStringList &columns)
{
    const repo::RepositoryResult directoryReady =
        store.ensureDirectory(store.getTableDirectory(authDatabaseName(), tableName));
    if (!directoryReady.ok) {
        return directoryReady;
    }

    const QString path = store.getTableFilePath(authDatabaseName(), tableName);
    if (store.exists(path)) {
        return repo::RepositoryResult::success();
    }
    return store.createEmptyTable(path, columns);
}

TaskResult taskFailure(const QString &message)
{
    TaskResult result;
    result.errorMessage = message;
    return result;
}

TaskResult taskSuccess(int affectedRows = 0)
{
    TaskResult result;
    result.success = true;
    result.affectedRowCount = affectedRows;
    return result;
}

bool isRoot(const QString &userName)
{
    return userName.trimmed() == rootUserName();
}

TaskResult requireRoot(const QString &requestUser)
{
    if (!isRoot(requestUser)) {
        return taskFailure(QStringLiteral("permission denied: root user required"));
    }
    return taskSuccess();
}

repo::TableData readUsers(const repo::FlatFileTableStore &store, QString *error)
{
    return store.readTable(userTablePath(store), error);
}

repo::TableData readPrivileges(const repo::FlatFileTableStore &store, QString *error)
{
    return store.readTable(privilegeTablePath(store), error);
}

int findUserRow(const repo::TableData &users, const QString &userName)
{
    for (int rowIndex = 0; rowIndex < users.rows.size(); ++rowIndex) {
        const repo::TableRow &row = users.rows.at(rowIndex);
        if (!row.isEmpty() && row.at(0) == userName) {
            return rowIndex;
        }
    }
    return -1;
}

int findPrivilegeRow(const repo::TableData &privileges,
                     const QString &userName,
                     const QString &databaseName,
                     const QString &tableName = QString())
{
    for (int rowIndex = 0; rowIndex < privileges.rows.size(); ++rowIndex) {
        const repo::TableRow &row = privileges.rows.at(rowIndex);
        bool userMatch = !row.isEmpty() && row.at(0) == userName;
        bool dbMatch = row.size() >= 2 && row.at(1) == databaseName;
        bool tableMatch = true;
        if (!tableName.isEmpty()) {
            if (row.size() >= 3) {
                tableMatch = row.at(2) == tableName;
            } else {
                tableMatch = false;
            }
        }
        if (userMatch && dbMatch && tableMatch) {
            return rowIndex;
        }
    }
    return -1;
}

bool databaseExists(const QString &databaseName, const QString &dataRoot, QString *error)
{
    if (databaseName == authDatabaseName()) {
        return true;
    }
    return repo::DatabaseRepo(dataRoot).hasDatabase(databaseName, error);
}

bool isAuthDatabase(const QString &databaseName)
{
    return databaseName.trimmed() == authDatabaseName();
}

} // namespace

TaskResult initializeAuthStore(const QString &dataRoot)
{
    repo::FlatFileTableStore store = storeFor(dataRoot);
    const repo::RepositoryResult rootReady = store.ensureDataRoot();
    if (!rootReady.ok) {
        return taskFailure(rootReady.error);
    }

    const repo::RepositoryResult authDirReady =
        store.ensureDirectory(store.getDatabaseDirectory(authDatabaseName()));
    if (!authDirReady.ok) {
        return taskFailure(authDirReady.error);
    }

    repo::RepositoryResult tableReady =
        ensureAuthTable(store, userTableName(), {QStringLiteral("user_name"),
                                                 QStringLiteral("password"),
                                                 QStringLiteral("enabled")});
    if (!tableReady.ok) {
        return taskFailure(tableReady.error);
    }

    tableReady = ensureAuthTable(store, privilegeTableName(), {QStringLiteral("user_name"),
                                                               QStringLiteral("database_name"),
                                                               QStringLiteral("privilege")});
    if (!tableReady.ok) {
        return taskFailure(tableReady.error);
    }

    QString error;
    repo::TableData users = readUsers(store, &error);
    if (!error.isEmpty()) {
        return taskFailure(error);
    }

    if (findUserRow(users, rootUserName()) < 0) {
        users.rows.append({rootUserName(),
                           QString::fromLatin1(cliclient::kRootInitialPassword),
                           QStringLiteral("true")});
        const repo::RepositoryResult writeResult = store.writeTable(userTablePath(store), users);
        if (!writeResult.ok) {
            return taskFailure(writeResult.error);
        }
    }

    return taskSuccess();
}

AuthResult authenticate(const QString &userName, const QString &password, const QString &dataRoot)
{
    AuthResult result;
    const QString normalizedUser = userName.trimmed();
    if (normalizedUser.isEmpty()) {
        result.errorMessage = QStringLiteral("user name cannot be empty");
        return result;
    }

    TaskResult initResult = initializeAuthStore(dataRoot);
    if (!initResult.success) {
        result.errorMessage = initResult.errorMessage;
        return result;
    }

    repo::FlatFileTableStore store = storeFor(dataRoot);
    QString error;
    const repo::TableData users = readUsers(store, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    const int rowIndex = findUserRow(users, normalizedUser);
    if (rowIndex < 0) {
        result.errorMessage = QStringLiteral("user '%1' does not exist").arg(normalizedUser);
        return result;
    }

    const repo::TableRow row = users.rows.at(rowIndex);
    if (row.size() < 3 || row.at(2) != QStringLiteral("true")) {
        result.errorMessage = QStringLiteral("user '%1' is disabled").arg(normalizedUser);
        return result;
    }
    if (row.at(1) != password) {
        result.errorMessage = QStringLiteral("invalid password");
        return result;
    }

    result.success = true;
    result.userName = normalizedUser;
    return result;
}

TaskResult createUser(const QString &requestUser,
                      const QString &newUserName,
                      const QString &password,
                      const QString &dataRoot)
{
    TaskResult rootResult = requireRoot(requestUser);
    if (!rootResult.success) {
        return rootResult;
    }

    const QString normalizedUser = newUserName.trimmed();
    if (normalizedUser.isEmpty()) {
        return taskFailure(QStringLiteral("user name cannot be empty"));
    }

    TaskResult initResult = initializeAuthStore(dataRoot);
    if (!initResult.success) {
        return initResult;
    }

    repo::FlatFileTableStore store = storeFor(dataRoot);
    QString error;
    repo::TableData users = readUsers(store, &error);
    if (!error.isEmpty()) {
        return taskFailure(error);
    }
    if (findUserRow(users, normalizedUser) >= 0) {
        return taskFailure(QStringLiteral("user '%1' already exists").arg(normalizedUser));
    }

    users.rows.append({normalizedUser, password, QStringLiteral("true")});
    const repo::RepositoryResult writeResult = store.writeTable(userTablePath(store), users);
    if (!writeResult.ok) {
        return taskFailure(writeResult.error);
    }
    return taskSuccess(1);
}

TaskResult dropUser(const QString &requestUser, const QString &targetUserName, const QString &dataRoot)
{
    TaskResult rootResult = requireRoot(requestUser);
    if (!rootResult.success) {
        return rootResult;
    }

    const QString normalizedUser = targetUserName.trimmed();
    if (normalizedUser == rootUserName()) {
        return taskFailure(QStringLiteral("root user cannot be dropped"));
    }

    TaskResult initResult = initializeAuthStore(dataRoot);
    if (!initResult.success) {
        return initResult;
    }

    repo::FlatFileTableStore store = storeFor(dataRoot);
    QString error;
    repo::TableData users = readUsers(store, &error);
    if (!error.isEmpty()) {
        return taskFailure(error);
    }

    const int userRow = findUserRow(users, normalizedUser);
    if (userRow < 0) {
        return taskFailure(QStringLiteral("user '%1' does not exist").arg(normalizedUser));
    }
    users.rows.removeAt(userRow);

    repo::TableData privileges = readPrivileges(store, &error);
    if (!error.isEmpty()) {
        return taskFailure(error);
    }
    for (int index = privileges.rows.size() - 1; index >= 0; --index) {
        if (!privileges.rows.at(index).isEmpty() && privileges.rows.at(index).at(0) == normalizedUser) {
            privileges.rows.removeAt(index);
        }
    }

    repo::RepositoryResult writeResult = store.writeTable(userTablePath(store), users);
    if (!writeResult.ok) {
        return taskFailure(writeResult.error);
    }
    writeResult = store.writeTable(privilegeTablePath(store), privileges);
    if (!writeResult.ok) {
        return taskFailure(writeResult.error);
    }
    return taskSuccess(1);
}

TaskResult alterUserPassword(const QString &requestUser,
                             const QString &targetUserName,
                             const QString &password,
                             const QString &dataRoot)
{
    TaskResult rootResult = requireRoot(requestUser);
    if (!rootResult.success) {
        return rootResult;
    }

    TaskResult initResult = initializeAuthStore(dataRoot);
    if (!initResult.success) {
        return initResult;
    }

    const QString normalizedUser = targetUserName.trimmed();
    repo::FlatFileTableStore store = storeFor(dataRoot);
    QString error;
    repo::TableData users = readUsers(store, &error);
    if (!error.isEmpty()) {
        return taskFailure(error);
    }

    const int rowIndex = findUserRow(users, normalizedUser);
    if (rowIndex < 0) {
        return taskFailure(QStringLiteral("user '%1' does not exist").arg(normalizedUser));
    }
    users.rows[rowIndex][1] = password;
    const repo::RepositoryResult writeResult = store.writeTable(userTablePath(store), users);
    if (!writeResult.ok) {
        return taskFailure(writeResult.error);
    }
    return taskSuccess(1);
}

TaskResult grantDatabaseAll(const QString &requestUser,
                            const QString &targetUserName,
                            const QString &databaseName,
                            const QString &tableName,
                            const QString &dataRoot)
{
    TaskResult rootResult = requireRoot(requestUser);
    if (!rootResult.success) {
        return rootResult;
    }

    TaskResult initResult = initializeAuthStore(dataRoot);
    if (!initResult.success) {
        return initResult;
    }

    const QString normalizedUser = targetUserName.trimmed();
    const QString normalizedDatabase = databaseName.trimmed();
    const QString normalizedTable = tableName.trimmed();
    if (isAuthDatabase(normalizedDatabase)) {
        return taskFailure(QStringLiteral("permission denied: system database '%1' is protected")
                               .arg(normalizedDatabase));
    }

    repo::FlatFileTableStore store = storeFor(dataRoot);
    QString error;
    const repo::TableData users = readUsers(store, &error);
    if (!error.isEmpty()) {
        return taskFailure(error);
    }
    if (findUserRow(users, normalizedUser) < 0) {
        return taskFailure(QStringLiteral("user '%1' does not exist").arg(normalizedUser));
    }
    if (!databaseExists(normalizedDatabase, store.getDataRoot(), &error)) {
        return taskFailure(error.isEmpty()
                               ? QStringLiteral("database '%1' does not exist").arg(normalizedDatabase)
                               : error);
    }

    repo::TableData privileges = readPrivileges(store, &error);
    if (!error.isEmpty()) {
        return taskFailure(error);
    }
    if (findPrivilegeRow(privileges, normalizedUser, normalizedDatabase, normalizedTable) < 0) {
        if (normalizedTable.isEmpty()) {
            privileges.rows.append({normalizedUser, normalizedDatabase, QString(), QStringLiteral("ALL")});
        } else {
            privileges.rows.append({normalizedUser, normalizedDatabase, normalizedTable, QStringLiteral("ALL")});
        }
        const repo::RepositoryResult writeResult = store.writeTable(privilegeTablePath(store), privileges);
        if (!writeResult.ok) {
            return taskFailure(writeResult.error);
        }
        return taskSuccess(1);
    }
    return taskSuccess(0);
}

TaskResult revokeDatabaseAll(const QString &requestUser,
                             const QString &targetUserName,
                             const QString &databaseName,
                             const QString &tableName,
                             const QString &dataRoot)
{
    TaskResult rootResult = requireRoot(requestUser);
    if (!rootResult.success) {
        return rootResult;
    }

    TaskResult initResult = initializeAuthStore(dataRoot);
    if (!initResult.success) {
        return initResult;
    }

    const QString normalizedUser = targetUserName.trimmed();
    const QString normalizedDatabase = databaseName.trimmed();
    const QString normalizedTable = tableName.trimmed();
    if (isAuthDatabase(normalizedDatabase)) {
        return taskFailure(QStringLiteral("permission denied: system database '%1' is protected")
                               .arg(normalizedDatabase));
    }

    repo::FlatFileTableStore store = storeFor(dataRoot);
    QString error;
    repo::TableData privileges = readPrivileges(store, &error);
    if (!error.isEmpty()) {
        return taskFailure(error);
    }
    const int rowIndex = findPrivilegeRow(privileges, normalizedUser, normalizedDatabase, normalizedTable);
    if (rowIndex >= 0) {
        privileges.rows.removeAt(rowIndex);
        const repo::RepositoryResult writeResult = store.writeTable(privilegeTablePath(store), privileges);
        if (!writeResult.ok) {
            return taskFailure(writeResult.error);
        }
        return taskSuccess(1);
    }
    return taskSuccess(0);
}

bool userHasDatabasePrivilege(const QString &userName,
                              const QString &databaseName,
                              const QString &dataRoot,
                              QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    if (isRoot(userName)) {
        return true;
    }

    TaskResult initResult = initializeAuthStore(dataRoot);
    if (!initResult.success) {
        if (error != nullptr) {
            *error = initResult.errorMessage;
        }
        return false;
    }

    repo::FlatFileTableStore store = storeFor(dataRoot);
    QString readError;
    const repo::TableData privileges = readPrivileges(store, &readError);
    if (!readError.isEmpty()) {
        if (error != nullptr) {
            *error = readError;
        }
        return false;
    }
    return findPrivilegeRow(privileges, userName.trimmed(), databaseName.trimmed()) >= 0;
}

bool userHasTablePrivilege(const QString &userName,
                           const QString &databaseName,
                           const QString &tableName,
                           const QString &dataRoot,
                           QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    if (isRoot(userName)) {
        return true;
    }
    if (tableName.isEmpty()) {
        return userHasDatabasePrivilege(userName, databaseName, dataRoot, error);
    }

    TaskResult initResult = initializeAuthStore(dataRoot);
    if (!initResult.success) {
        if (error != nullptr) {
            *error = initResult.errorMessage;
        }
        return false;
    }

    repo::FlatFileTableStore store = storeFor(dataRoot);
    QString readError;
    const repo::TableData privileges = readPrivileges(store, &readError);
    if (!readError.isEmpty()) {
        if (error != nullptr) {
            *error = readError;
        }
        return false;
    }
    return (findPrivilegeRow(privileges, userName.trimmed(), databaseName.trimmed(), tableName.trimmed()) >= 0) ||
           (findPrivilegeRow(privileges, userName.trimmed(), databaseName.trimmed()) >= 0);
}

TaskResult authorize(const QString &userName,
                     const QString &commandType,
                     const QString &targetDatabase,
                     const QString &targetTable,
                     const QString &dataRoot)
{
    if (isRoot(userName)) {
        return taskSuccess();
    }
    if (commandType == QStringLiteral("SHOW_DATABASES")) {
        return taskSuccess();
    }
    if (commandType == QStringLiteral("CREATE_DATABASE")) {
        return taskFailure(QStringLiteral("permission denied: root user required"));
    }
    if (commandType == QStringLiteral("CREATE_USER")
        || commandType == QStringLiteral("DROP_USER")
        || commandType == QStringLiteral("ALTER_USER")
        || commandType == QStringLiteral("GRANT_ALL")
        || commandType == QStringLiteral("REVOKE_ALL")) {
        return taskFailure(QStringLiteral("permission denied: root user required"));
    }

    const QString databaseName = targetDatabase.trimmed();
    const QString tableName = targetTable.trimmed();
    if (databaseName.isEmpty()) {
        return taskSuccess();
    }
    if (isAuthDatabase(databaseName)) {
        return taskFailure(QStringLiteral("permission denied: system database '%1' is protected")
                               .arg(databaseName));
    }

    QString error;
    if (!userHasTablePrivilege(userName, databaseName, tableName, dataRoot, &error)) {
        if (tableName.isEmpty()) {
            return taskFailure(error.isEmpty()
                                   ? QStringLiteral("permission denied for user '%1' on database '%2'")
                                         .arg(userName, databaseName)
                                   : error);
        } else {
            return taskFailure(error.isEmpty()
                                   ? QStringLiteral("permission denied for user '%1' on table '%2.%3'")
                                         .arg(userName, databaseName, tableName)
                                   : error);
        }
    }
    return taskSuccess();
}

} // namespace service::auth_service
