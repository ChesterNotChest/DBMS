#include "repo.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace {

QString serializeColumns(const QStringList &columns)
{
    QJsonArray jsonColumns;
    for (const QString &column : columns) {
        jsonColumns.append(column);
    }
    return QString::fromUtf8(QJsonDocument(jsonColumns).toJson(QJsonDocument::Compact));
}

QStringList deserializeColumns(const QString &serializedColumns, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    if (serializedColumns.trimmed().isEmpty()) {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(serializedColumns.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (error != nullptr) {
            *error = QStringLiteral("failed to parse index columns: %1").arg(parseError.errorString());
        }
        return {};
    }

    QStringList columns;
    const QJsonArray jsonColumns = document.array();
    columns.reserve(jsonColumns.size());
    for (const QJsonValue &value : jsonColumns) {
        columns.append(value.toString());
    }
    return columns;
}

repo::TableRow toIndexRow(const tabledef::IndexMeta &index)
{
    return {
        index.indexName,
        serializeColumns(index.columnNames),
        tabledef::boolToString(index.isUnique),
    };
}

bool indexFromRow(const repo::TableRow &row, tabledef::IndexMeta *index, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    if (index == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("index output pointer cannot be null");
        }
        return false;
    }
    if (row.size() < 3) {
        if (error != nullptr) {
            *error = QStringLiteral("index row is incomplete");
        }
        return false;
    }

    QString decodeError;
    const QStringList columns = deserializeColumns(row.at(1), &decodeError);
    if (!decodeError.isEmpty()) {
        if (error != nullptr) {
            *error = decodeError;
        }
        return false;
    }

    *index = tabledef::IndexMeta{row.at(0), columns, tabledef::stringToBool(row.at(2))};
    return true;
}

} // namespace

namespace repo {

IndexRepo::IndexRepo(QString databaseName,
                     QString tableName,
                     QString dataRoot)
    : m_databaseName(std::move(databaseName))
    , m_tableName(std::move(tableName))
    , m_store(std::move(dataRoot))
{
}

RepositoryResult IndexRepo::initialize() const
{
    if (m_databaseName.trimmed().isEmpty()) {
        return RepositoryResult::failure(QStringLiteral("database name cannot be empty"));
    }
    if (m_tableName.trimmed().isEmpty()) {
        return RepositoryResult::failure(QStringLiteral("table name cannot be empty"));
    }

    const RepositoryResult rootReady = m_store.ensureDataRoot();
    if (!rootReady.ok) {
        return rootReady;
    }

    const RepositoryResult tableDirReady =
        m_store.ensureDirectory(m_store.getTableDirectory(m_databaseName, m_tableName));
    if (!tableDirReady.ok) {
        return tableDirReady;
    }

    if (m_store.exists(getIndexMetaFilePath())) {
        return RepositoryResult::success();
    }

    return m_store.createEmptyTable(getIndexMetaFilePath(), tabledef::schemaColumnNames(getSchema()));
}

QList<tabledef::IndexMeta> IndexRepo::listIndexes(QString *error) const
{
    const RepositoryResult initResult = initialize();
    if (!initResult.ok) {
        if (error != nullptr) {
            *error = initResult.error;
        }
        return {};
    }

    const TableData table = m_store.readTable(getIndexMetaFilePath(), error);
    QList<tabledef::IndexMeta> indexes;
    for (const TableRow &row : table.rows) {
        tabledef::IndexMeta index;
        QString parseError;
        if (!indexFromRow(row, &index, &parseError)) {
            if (error != nullptr) {
                *error = parseError;
            }
            return {};
        }
        indexes.append(index);
    }
    return indexes;
}

bool IndexRepo::hasIndex(const QString &indexName, QString *error) const
{
    const QList<tabledef::IndexMeta> indexes = listIndexes(error);
    for (const tabledef::IndexMeta &index : indexes) {
        if (index.indexName == indexName) {
            return true;
        }
    }
    return false;
}

RepositoryResult IndexRepo::createIndex(const tabledef::IndexMeta &index) const
{
    if (index.indexName.trimmed().isEmpty()) {
        return RepositoryResult::failure(QStringLiteral("index name cannot be empty"));
    }

    const RepositoryResult initResult = initialize();
    if (!initResult.ok) {
        return initResult;
    }

    QString lookupError;
    if (hasIndex(index.indexName, &lookupError)) {
        return RepositoryResult::failure(QStringLiteral("index '%1' already exists").arg(index.indexName));
    }
    if (!lookupError.isEmpty()) {
        return RepositoryResult::failure(lookupError);
    }

    return m_store.appendRow(getIndexMetaFilePath(), toIndexRow(index));
}

RepositoryResult IndexRepo::updateIndex(const QString &indexName,
                                        const tabledef::IndexMeta &index) const
{
    if (indexName.trimmed().isEmpty() || index.indexName.trimmed().isEmpty()) {
        return RepositoryResult::failure(QStringLiteral("index name cannot be empty"));
    }

    const RepositoryResult initResult = initialize();
    if (!initResult.ok) {
        return initResult;
    }

    QString error;
    TableData table = m_store.readTable(getIndexMetaFilePath(), &error);
    if (!error.isEmpty()) {
        return RepositoryResult::failure(error);
    }

    int targetIndex = -1;
    for (int rowIndex = 0; rowIndex < table.rows.size(); ++rowIndex) {
        if (!table.rows.at(rowIndex).isEmpty() && table.rows.at(rowIndex).at(0) == indexName) {
            targetIndex = rowIndex;
            continue;
        }
        if (!table.rows.at(rowIndex).isEmpty() && table.rows.at(rowIndex).at(0) == index.indexName) {
            return RepositoryResult::failure(QStringLiteral("index '%1' already exists").arg(index.indexName));
        }
    }

    if (targetIndex < 0) {
        return RepositoryResult::failure(QStringLiteral("index '%1' does not exist").arg(indexName));
    }

    table.rows[targetIndex] = toIndexRow(index);
    return m_store.writeTable(getIndexMetaFilePath(), table);
}

RepositoryResult IndexRepo::deleteIndex(const QString &indexName) const
{
    if (indexName.trimmed().isEmpty()) {
        return RepositoryResult::failure(QStringLiteral("index name cannot be empty"));
    }

    const RepositoryResult initResult = initialize();
    if (!initResult.ok) {
        return initResult;
    }

    QString error;
    TableData table = m_store.readTable(getIndexMetaFilePath(), &error);
    if (!error.isEmpty()) {
        return RepositoryResult::failure(error);
    }

    int targetIndex = -1;
    for (int rowIndex = 0; rowIndex < table.rows.size(); ++rowIndex) {
        if (!table.rows.at(rowIndex).isEmpty() && table.rows.at(rowIndex).at(0) == indexName) {
            targetIndex = rowIndex;
            break;
        }
    }

    if (targetIndex < 0) {
        return RepositoryResult::failure(QStringLiteral("index '%1' does not exist").arg(indexName));
    }

    table.rows.removeAt(targetIndex);
    return m_store.writeTable(getIndexMetaFilePath(), table);
}

TableData IndexRepo::indexTable(QString *error) const
{
    const RepositoryResult initResult = initialize();
    if (!initResult.ok) {
        if (error != nullptr) {
            *error = initResult.error;
        }
        return {};
    }

    return m_store.readTable(getIndexMetaFilePath(), error);
}

QString IndexRepo::getIndexMetaFilePath() const
{
    return m_store.getIndexMetaFilePath(m_databaseName, m_tableName);
}

tabledef::TableSchema IndexRepo::getSchema() const
{
    return tabledef::buildTableIndexSchema(m_tableName);
}

} // namespace repo
