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
        m_store.ensureDirectory(m_store.getSortIndexDirectory(m_databaseName));
    if (!directoryReady.ok) {
        return directoryReady;
    }

    if (m_store.exists(getIndexFilePath())) {
        return RepositoryResult::failure(
            QStringLiteral("sort index '%1' already exists").arg(m_indexName));
    }

    return m_store.createEmptyTable(getIndexFilePath(), columns);
}

RepositoryResult SortIndexRepo::dropIndex() const
{
    return m_store.removeFile(getIndexFilePath());
}

TableData SortIndexRepo::readIndex(QString *error) const
{
    return m_store.readTable(getIndexFilePath(), error);
}

RepositoryResult SortIndexRepo::replaceIndex(const TableData &table) const
{
    return m_store.writeTable(getIndexFilePath(), table);
}

RepositoryResult SortIndexRepo::insertRow(const TableRow &row) const
{
    return m_store.appendRow(getIndexFilePath(), row);
}

RepositoryResult SortIndexRepo::updateRow(int rowIndex, const TableRow &row) const
{
    return m_store.updateRow(getIndexFilePath(), rowIndex, row);
}

RepositoryResult SortIndexRepo::deleteRow(int rowIndex) const
{
    return m_store.deleteRow(getIndexFilePath(), rowIndex);
}

QString SortIndexRepo::getIndexFilePath() const
{
    return m_store.getSortIndexFilePath(m_databaseName, m_indexName, m_sourceTable);
}

} // namespace repo
