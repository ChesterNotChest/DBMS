#include "repo.h"

#include <QDir>
#include <QFileInfo>

namespace {

const QStringList kMetaColumns = {QStringLiteral("table_name"),
                                  QStringLiteral("table_file")};

} // namespace

namespace repo {

MetaRepo::MetaRepo(QString databaseName, QString dataRoot)
    : m_databaseName(std::move(databaseName))
    , m_store(std::move(dataRoot))
{
}

RepositoryResult MetaRepo::initialize() const
{
    if (m_databaseName.trimmed().isEmpty()) {
        return RepositoryResult::failure(QStringLiteral("database name cannot be empty"));
    }

    const RepositoryResult rootReady = m_store.ensureDataRoot();
    if (!rootReady.ok) {
        return rootReady;
    }

    const RepositoryResult databaseDirReady =
        m_store.ensureDirectory(m_store.getDatabaseDirectory(m_databaseName));
    if (!databaseDirReady.ok) {
        return databaseDirReady;
    }

    if (m_store.exists(m_store.getMetaFilePath(m_databaseName))) {
        return RepositoryResult::success();
    }

    return m_store.createEmptyTable(m_store.getMetaFilePath(m_databaseName), kMetaColumns);
}

QList<TableEntry> MetaRepo::listTables(QString *error) const
{
    const RepositoryResult initResult = initialize();
    if (!initResult.ok) {
        if (error != nullptr) {
            *error = initResult.error;
        }
        return {};
    }

    const TableData table = m_store.readTable(m_store.getMetaFilePath(m_databaseName), error);
    QList<TableEntry> tables;
    for (const TableRow &row : table.rows) {
        if (row.size() < 2) {
            continue;
        }
        tables.append(TableEntry{row.at(0), row.at(1)});
    }
    return tables;
}

bool MetaRepo::hasTable(const QString &tableName, QString *error) const
{
    const QList<TableEntry> tables = listTables(error);
    for (const TableEntry &table : tables) {
        if (table.name == tableName) {
            return true;
        }
    }
    return false;
}

RepositoryResult MetaRepo::createTableEntry(const QString &tableName) const
{
    if (tableName.trimmed().isEmpty()) {
        return RepositoryResult::failure(QStringLiteral("table name cannot be empty"));
    }

    const RepositoryResult initResult = initialize();
    if (!initResult.ok) {
        return initResult;
    }

    QString lookupError;
    if (hasTable(tableName, &lookupError)) {
        return RepositoryResult::failure(
            QStringLiteral("table '%1' already exists").arg(tableName));
    }
    if (!lookupError.isEmpty()) {
        return RepositoryResult::failure(lookupError);
    }

    const QString tablePath = m_store.getTableFilePath(m_databaseName, tableName);
    return m_store.appendRow(
        m_store.getMetaFilePath(m_databaseName),
        TableRow{tableName, m_store.toStorageRelativePath(tablePath)});
}

RepositoryResult MetaRepo::renameTableEntry(const QString &tableName,
                                            const QString &newTableName) const
{
    if (tableName.trimmed().isEmpty() || newTableName.trimmed().isEmpty()) {
        return RepositoryResult::failure(QStringLiteral("table name cannot be empty"));
    }
    if (tableName == newTableName) {
        return RepositoryResult::success();
    }

    const RepositoryResult initResult = initialize();
    if (!initResult.ok) {
        return initResult;
    }

    QString error;
    TableData table = m_store.readTable(m_store.getMetaFilePath(m_databaseName), &error);
    if (!error.isEmpty()) {
        return RepositoryResult::failure(error);
    }

    int currentIndex = -1;
    for (int index = 0; index < table.rows.size(); ++index) {
        if (!table.rows.at(index).isEmpty() && table.rows.at(index).at(0) == tableName) {
            currentIndex = index;
        }
        if (!table.rows.at(index).isEmpty() && table.rows.at(index).at(0) == newTableName) {
            return RepositoryResult::failure(
                QStringLiteral("table '%1' already exists").arg(newTableName));
        }
    }

    if (currentIndex < 0) {
        return RepositoryResult::failure(
            QStringLiteral("table '%1' does not exist").arg(tableName));
    }

    QDir databaseDirectory(m_store.getDatabaseDirectory(m_databaseName));
    const QString oldFileName = m_store.getTableFileName(tableName);
    const QString newFileName = m_store.getTableFileName(newTableName);
    if (QFileInfo::exists(m_store.getTableFilePath(m_databaseName, tableName))
        && !databaseDirectory.rename(oldFileName, newFileName)) {
        return RepositoryResult::failure(
            QStringLiteral("failed to rename table file '%1' to '%2'")
                .arg(oldFileName, newFileName));
    }

    table.rows[currentIndex] = TableRow{
        newTableName,
        m_store.toStorageRelativePath(m_store.getTableFilePath(m_databaseName, newTableName))};
    return m_store.writeTable(m_store.getMetaFilePath(m_databaseName), table);
}

RepositoryResult MetaRepo::deleteTableEntry(const QString &tableName) const
{
    const RepositoryResult initResult = initialize();
    if (!initResult.ok) {
        return initResult;
    }

    QString error;
    TableData table = m_store.readTable(m_store.getMetaFilePath(m_databaseName), &error);
    if (!error.isEmpty()) {
        return RepositoryResult::failure(error);
    }

    int targetIndex = -1;
    for (int index = 0; index < table.rows.size(); ++index) {
        if (!table.rows.at(index).isEmpty() && table.rows.at(index).at(0) == tableName) {
            targetIndex = index;
            break;
        }
    }

    if (targetIndex < 0) {
        return RepositoryResult::failure(
            QStringLiteral("table '%1' does not exist").arg(tableName));
    }

    table.rows.removeAt(targetIndex);
    return m_store.writeTable(m_store.getMetaFilePath(m_databaseName), table);
}

TableData MetaRepo::metaTable(QString *error) const
{
    const RepositoryResult initResult = initialize();
    if (!initResult.ok) {
        if (error != nullptr) {
            *error = initResult.error;
        }
        return {};
    }

    return m_store.readTable(m_store.getMetaFilePath(m_databaseName), error);
}

QString MetaRepo::getMetaFilePath() const
{
    return m_store.getMetaFilePath(m_databaseName);
}

} // namespace repo
