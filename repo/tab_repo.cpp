#include "repo.h"

#include <QDir>
#include <QFileInfo>

namespace repo {

TabRepo::TabRepo(QString databaseName, QString dataRoot)
    : m_databaseName(std::move(databaseName))
    , m_store(std::move(dataRoot))
{
}

RepositoryResult TabRepo::initialize() const
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

    if (m_store.exists(getTabFilePath())) {
        return RepositoryResult::success();
    }

    return m_store.createEmptyTable(getTabFilePath(), tabledef::schemaColumnNames(getSchema()));
}

QList<TableEntry> TabRepo::listTables(QString *error) const
{
    const RepositoryResult initResult = initialize();
    if (!initResult.ok) {
        if (error != nullptr) {
            *error = initResult.error;
        }
        return {};
    }

    const TableData table = m_store.readTable(getTabFilePath(), error);
    QList<TableEntry> tables;
    for (const TableRow &row : table.rows) {
        if (row.isEmpty()) {
            continue;
        }
        tables.append(TableEntry{row.at(0)});
    }
    return tables;
}

bool TabRepo::hasTable(const QString &tableName, QString *error) const
{
    const QList<TableEntry> tables = listTables(error);
    for (const TableEntry &table : tables) {
        if (table.name == tableName) {
            return true;
        }
    }
    return false;
}

RepositoryResult TabRepo::createTableEntry(const QString &tableName) const
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

    return m_store.appendRow(getTabFilePath(), TableRow{tableName});
}

RepositoryResult TabRepo::renameTableEntry(const QString &tableName,
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
    TableData table = m_store.readTable(getTabFilePath(), &error);
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
    if (QFileInfo::exists(m_store.getTableDirectory(m_databaseName, tableName))
        && !databaseDirectory.rename(tableName, newTableName)) {
        return RepositoryResult::failure(
            QStringLiteral("failed to rename table directory '%1' to '%2'")
                .arg(tableName, newTableName));
    }

    table.rows[currentIndex] = TableRow{newTableName};
    return m_store.writeTable(getTabFilePath(), table);
}

RepositoryResult TabRepo::deleteTableEntry(const QString &tableName) const
{
    const RepositoryResult initResult = initialize();
    if (!initResult.ok) {
        return initResult;
    }

    QString error;
    TableData table = m_store.readTable(getTabFilePath(), &error);
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
    return m_store.writeTable(getTabFilePath(), table);
}

TableData TabRepo::tabTable(QString *error) const
{
    const RepositoryResult initResult = initialize();
    if (!initResult.ok) {
        if (error != nullptr) {
            *error = initResult.error;
        }
        return {};
    }

    return m_store.readTable(getTabFilePath(), error);
}

QString TabRepo::getTabFilePath() const
{
    return m_store.getTabFilePath(m_databaseName);
}

tabledef::TableSchema TabRepo::getSchema() const
{
    return tabledef::buildDatabaseTableCatalogSchema(m_databaseName);
}

} // namespace repo
