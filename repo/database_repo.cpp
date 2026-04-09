#include "repo.h"

#include <QDir>
#include <QFileInfo>

namespace {

const QStringList kRootColumns = {QStringLiteral("database_name"),
                                  QStringLiteral("meta_file")};

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

    if (m_store.exists(m_store.rootFilePath())) {
        return RepositoryResult::success();
    }

    return m_store.createEmptyTable(m_store.rootFilePath(), kRootColumns);
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

    const TableData table = m_store.readTable(m_store.rootFilePath(), error);
    QList<DatabaseEntry> databases;
    for (const TableRow &row : table.rows) {
        if (row.size() < 2) {
            continue;
        }
        databases.append(DatabaseEntry{row.at(0), row.at(1)});
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
        m_store.ensureDirectory(m_store.databaseDirectory(databaseName));
    if (!directoryReady.ok) {
        return directoryReady;
    }

    const MetaRepo metaRepo(databaseName, m_store.dataRoot());
    const RepositoryResult metaReady = metaRepo.initialize();
    if (!metaReady.ok) {
        return metaReady;
    }

    const QString metaPath = m_store.metaFilePath(databaseName);
    return m_store.appendRow(
        m_store.rootFilePath(),
        TableRow{databaseName, m_store.toStorageRelativePath(metaPath)});
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
    TableData table = m_store.readTable(m_store.rootFilePath(), &error);
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

    QDir dataRootDirectory(m_store.dataRoot());
    const QString oldMetaName = databaseName + QStringLiteral(".meta");
    const QString newMetaName = newDatabaseName + QStringLiteral(".meta");
    if (QFileInfo::exists(m_store.metaFilePath(databaseName))
        && !dataRootDirectory.rename(oldMetaName, newMetaName)) {
        return RepositoryResult::failure(
            QStringLiteral("failed to rename meta file '%1' to '%2'")
                .arg(oldMetaName, newMetaName));
    }

    if (QDir(m_store.databaseDirectory(databaseName)).exists()
        && !dataRootDirectory.rename(databaseName, newDatabaseName)) {
        return RepositoryResult::failure(
            QStringLiteral("failed to rename database directory '%1' to '%2'")
                .arg(databaseName, newDatabaseName));
    }

    const QString renamedMetaPath = m_store.metaFilePath(newDatabaseName);
    if (QFileInfo::exists(renamedMetaPath)) {
        TableData metaTable = m_store.readTable(renamedMetaPath, &error);
        if (!error.isEmpty()) {
            return RepositoryResult::failure(error);
        }

        for (TableRow &row : metaTable.rows) {
            if (row.size() >= 2) {
                row[1] = m_store.toStorageRelativePath(
                    m_store.tableFilePath(newDatabaseName, row.at(0)));
            }
        }

        const RepositoryResult metaWrite = m_store.writeTable(renamedMetaPath, metaTable);
        if (!metaWrite.ok) {
            return metaWrite;
        }
    }

    table.rows[currentIndex] =
        TableRow{newDatabaseName, m_store.toStorageRelativePath(renamedMetaPath)};
    return m_store.writeTable(m_store.rootFilePath(), table);
}

RepositoryResult DatabaseRepo::deleteDatabase(const QString &databaseName) const
{
    const RepositoryResult initResult = initialize();
    if (!initResult.ok) {
        return initResult;
    }

    QString error;
    TableData table = m_store.readTable(m_store.rootFilePath(), &error);
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
    const RepositoryResult writeResult = m_store.writeTable(m_store.rootFilePath(), table);
    if (!writeResult.ok) {
        return writeResult;
    }

    const RepositoryResult removeMeta = m_store.removeFile(m_store.metaFilePath(databaseName));
    if (!removeMeta.ok) {
        return removeMeta;
    }

    QDir databaseDirectory(m_store.databaseDirectory(databaseName));
    if (databaseDirectory.exists() && !databaseDirectory.removeRecursively()) {
        return RepositoryResult::failure(
            QStringLiteral("failed to remove database directory '%1'")
                .arg(databaseDirectory.absolutePath()));
    }

    return RepositoryResult::success();
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

    return m_store.readTable(m_store.rootFilePath(), error);
}

QString DatabaseRepo::rootFilePath() const
{
    return m_store.rootFilePath();
}

} // namespace repo
