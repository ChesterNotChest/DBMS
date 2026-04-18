#include "repo.h"

#include <QDir>
#include <QFileInfo>

namespace {

QString buildMissingNameError(const QString &entityName)
{
    return QStringLiteral("name for %1 cannot be empty").arg(entityName);
}

} // namespace

namespace repo {

DatabaseRepo::DatabaseRepo(QString dataRoot)
    : m_store(std::move(dataRoot))
{
}

RepositoryResult DatabaseRepo::initialize() const
{
    const RepositoryResult rootReady = m_store.ensureDataRoot();
    if (!rootReady.ok) {
        return rootReady;
    }

    if (m_store.exists(m_store.getRootFilePath())) {
        return RepositoryResult::success();
    }

    return m_store.createEmptyTable(
        m_store.getRootFilePath(),
        tabledef::schemaColumnNames(getSchema()));
}

QList<DatabaseEntry> DatabaseRepo::listDatabases(QString *error) const
{
    const RepositoryResult initResult = initialize();
    if (!initResult.ok) {
        if (error != nullptr) {
            *error = initResult.error;
        }
        return {};
    }

    const TableData table = m_store.readTable(m_store.getRootFilePath(), error);
    QList<DatabaseEntry> databases;
    for (const TableRow &row : table.rows) {
        if (row.isEmpty()) {
            continue;
        }
        databases.append(DatabaseEntry{row.at(0)});
    }
    return databases;
}

bool DatabaseRepo::hasDatabase(const QString &databaseName, QString *error) const
{
    const QList<DatabaseEntry> databases = listDatabases(error);
    for (const DatabaseEntry &database : databases) {
        if (database.name == databaseName) {
            return true;
        }
    }
    return false;
}

RepositoryResult DatabaseRepo::createDatabase(const QString &databaseName) const
{
    if (databaseName.trimmed().isEmpty()) {
        return RepositoryResult::failure(buildMissingNameError(QStringLiteral("database")));
    }

    const RepositoryResult initResult = initialize();
    if (!initResult.ok) {
        return initResult;
    }

    QString lookupError;
    if (hasDatabase(databaseName, &lookupError)) {
        return RepositoryResult::failure(
            QStringLiteral("database '%1' already exists").arg(databaseName));
    }
    if (!lookupError.isEmpty()) {
        return RepositoryResult::failure(lookupError);
    }

    const RepositoryResult directoryReady =
        m_store.ensureDirectory(m_store.getDatabaseDirectory(databaseName));
    if (!directoryReady.ok) {
        return directoryReady;
    }

    const TabRepo tabRepo(databaseName, m_store.getDataRoot());
    const RepositoryResult tabReady = tabRepo.initialize();
    if (!tabReady.ok) {
        return tabReady;
    }

    return m_store.appendRow(m_store.getRootFilePath(), TableRow{databaseName});
}

RepositoryResult DatabaseRepo::renameDatabase(const QString &databaseName,
                                              const QString &newDatabaseName) const
{
    if (databaseName.trimmed().isEmpty() || newDatabaseName.trimmed().isEmpty()) {
        return RepositoryResult::failure(buildMissingNameError(QStringLiteral("database")));
    }
    if (databaseName == newDatabaseName) {
        return RepositoryResult::success();
    }

    const RepositoryResult initResult = initialize();
    if (!initResult.ok) {
        return initResult;
    }

    QString error;
    TableData table = m_store.readTable(m_store.getRootFilePath(), &error);
    if (!error.isEmpty()) {
        return RepositoryResult::failure(error);
    }

    int currentIndex = -1;
    for (int index = 0; index < table.rows.size(); ++index) {
        if (!table.rows.at(index).isEmpty() && table.rows.at(index).at(0) == databaseName) {
            currentIndex = index;
        }
        if (!table.rows.at(index).isEmpty() && table.rows.at(index).at(0) == newDatabaseName) {
            return RepositoryResult::failure(
                QStringLiteral("database '%1' already exists").arg(newDatabaseName));
        }
    }

    if (currentIndex < 0) {
        return RepositoryResult::failure(
            QStringLiteral("database '%1' does not exist").arg(databaseName));
    }

    QDir dataRootDirectory(m_store.getDataRoot());
    if (QDir(m_store.getDatabaseDirectory(databaseName)).exists()
        && !dataRootDirectory.rename(databaseName, newDatabaseName)) {
        return RepositoryResult::failure(
            QStringLiteral("failed to rename database directory '%1' to '%2'")
                .arg(databaseName, newDatabaseName));
    }

    QDir newDatabaseDirectory(m_store.getDatabaseDirectory(newDatabaseName));
    const QString oldTabName = m_store.getTabFileName(databaseName);
    const QString newTabName = m_store.getTabFileName(newDatabaseName);
    if (QFileInfo::exists(newDatabaseDirectory.absoluteFilePath(oldTabName))
        && !newDatabaseDirectory.rename(oldTabName, newTabName)) {
        return RepositoryResult::failure(
            QStringLiteral("failed to rename table catalog file '%1' to '%2'")
                .arg(oldTabName, newTabName));
    }

    table.rows[currentIndex] = TableRow{newDatabaseName};
    return m_store.writeTable(m_store.getRootFilePath(), table);
}

RepositoryResult DatabaseRepo::deleteDatabase(const QString &databaseName) const
{
    const RepositoryResult initResult = initialize();
    if (!initResult.ok) {
        return initResult;
    }

    QString error;
    TableData table = m_store.readTable(m_store.getRootFilePath(), &error);
    if (!error.isEmpty()) {
        return RepositoryResult::failure(error);
    }

    int targetIndex = -1;
    for (int index = 0; index < table.rows.size(); ++index) {
        if (!table.rows.at(index).isEmpty() && table.rows.at(index).at(0) == databaseName) {
            targetIndex = index;
            break;
        }
    }

    if (targetIndex < 0) {
        return RepositoryResult::failure(
            QStringLiteral("database '%1' does not exist").arg(databaseName));
    }

    table.rows.removeAt(targetIndex);
    const RepositoryResult writeResult = m_store.writeTable(m_store.getRootFilePath(), table);
    if (!writeResult.ok) {
        return writeResult;
    }

    return m_store.removeDirectoryRecursively(m_store.getDatabaseDirectory(databaseName));
}

TableData DatabaseRepo::rootTable(QString *error) const
{
    const RepositoryResult initResult = initialize();
    if (!initResult.ok) {
        if (error != nullptr) {
            *error = initResult.error;
        }
        return {};
    }

    return m_store.readTable(m_store.getRootFilePath(), error);
}

QString DatabaseRepo::getRootFilePath() const
{
    return m_store.getRootFilePath();
}

tabledef::TableSchema DatabaseRepo::getSchema() const
{
    return tabledef::buildDatabaseRootSchema();
}

} // namespace repo
