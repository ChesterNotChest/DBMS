#include "repo.h"

namespace repo {

SortIndexRepo::SortIndexRepo(QString databaseName,
                             QString indexName,
                             QString sourceTable,
                             QString dataRoot)
    : m_databaseName(std::move(databaseName))
    , m_indexName(std::move(indexName))
    , m_sourceTable(std::move(sourceTable))
    , m_store(std::move(dataRoot))
{
}

RepositoryResult SortIndexRepo::createIndex(const QStringList &columns) const
{
    if (m_databaseName.trimmed().isEmpty()) {
        return RepositoryResult::failure(QStringLiteral("database name cannot be empty"));
    }
    if (m_indexName.trimmed().isEmpty()) {
        return RepositoryResult::failure(QStringLiteral("index name cannot be empty"));
    }

    const RepositoryResult directoryReady =
        m_store.ensureDirectory(m_store.sortIndexDirectory(m_databaseName));
    if (!directoryReady.ok) {
        return directoryReady;
    }

    if (m_store.exists(indexFilePath())) {
        return RepositoryResult::failure(
            QStringLiteral("sort index '%1' already exists").arg(m_indexName));
    }

    return m_store.createEmptyTable(indexFilePath(), columns);
}

RepositoryResult SortIndexRepo::dropIndex() const
{
    return m_store.removeFile(indexFilePath());
}

TableData SortIndexRepo::readIndex(QString *error) const
{
    return m_store.readTable(indexFilePath(), error);
}

RepositoryResult SortIndexRepo::replaceIndex(const TableData &table) const
{
    return m_store.writeTable(indexFilePath(), table);
}

RepositoryResult SortIndexRepo::insertRow(const TableRow &row) const
{
    return m_store.appendRow(indexFilePath(), row);
}

RepositoryResult SortIndexRepo::updateRow(int rowIndex, const TableRow &row) const
{
    return m_store.updateRow(indexFilePath(), rowIndex, row);
}

RepositoryResult SortIndexRepo::deleteRow(int rowIndex) const
{
    return m_store.deleteRow(indexFilePath(), rowIndex);
}

QString SortIndexRepo::indexFilePath() const
{
    return m_store.sortIndexFilePath(m_databaseName, m_indexName, m_sourceTable);
}

} // namespace repo
