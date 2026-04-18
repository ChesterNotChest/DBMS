#include "repo.h"

namespace {

repo::TableRow toColumnRow(const tabledef::Column &column, int ordinalPosition)
{
    return {
        column.name,
        tabledef::columnTypeToString(column.type),
        QString::number(column.length),
        tabledef::boolToString(column.notNull),
        column.defaultValue,
        tabledef::boolToString(column.autoIncrement),
        QString::number(ordinalPosition),
    };
}

bool columnFromRow(const repo::TableRow &row, tabledef::Column *column, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    if (column == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("column output pointer cannot be null");
        }
        return false;
    }
    if (row.size() < 7) {
        if (error != nullptr) {
            *error = QStringLiteral("column row is incomplete");
        }
        return false;
    }

    tabledef::ColumnType type = tabledef::ColumnType::Varchar;
    if (!tabledef::tryParseColumnType(row.at(1), &type)) {
        if (error != nullptr) {
            *error = QStringLiteral("unknown column type '%1'").arg(row.at(1));
        }
        return false;
    }

    bool lengthOk = false;
    const int length = row.at(2).toInt(&lengthOk);
    if (!lengthOk) {
        if (error != nullptr) {
            *error = QStringLiteral("invalid column length '%1'").arg(row.at(2));
        }
        return false;
    }

    *column = tabledef::Column{
        row.at(0),
        type,
        length,
        tabledef::stringToBool(row.at(3)),
        row.at(4),
        tabledef::stringToBool(row.at(5)),
        QString(),
    };
    return true;
}

} // namespace

namespace repo {

MetaRepo::MetaRepo(QString databaseName, QString tableName, QString dataRoot)
    : m_databaseName(std::move(databaseName))
    , m_tableName(std::move(tableName))
    , m_store(std::move(dataRoot))
{
}

RepositoryResult MetaRepo::initialize() const
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

    if (m_store.exists(getMetaFilePath())) {
        return RepositoryResult::success();
    }

    return m_store.createEmptyTable(getMetaFilePath(), tabledef::schemaColumnNames(getSchema()));
}

QList<tabledef::Column> MetaRepo::listColumns(QString *error) const
{
    const RepositoryResult initResult = initialize();
    if (!initResult.ok) {
        if (error != nullptr) {
            *error = initResult.error;
        }
        return {};
    }

    const TableData table = m_store.readTable(getMetaFilePath(), error);
    QList<tabledef::Column> columns;
    for (const TableRow &row : table.rows) {
        tabledef::Column column;
        QString parseError;
        if (!columnFromRow(row, &column, &parseError)) {
            if (error != nullptr) {
                *error = parseError;
            }
            return {};
        }
        columns.append(column);
    }
    return columns;
}

bool MetaRepo::hasColumn(const QString &columnName, QString *error) const
{
    const QList<tabledef::Column> columns = listColumns(error);
    for (const tabledef::Column &column : columns) {
        if (column.name == columnName) {
            return true;
        }
    }
    return false;
}

RepositoryResult MetaRepo::createColumn(const tabledef::Column &column) const
{
    if (column.name.trimmed().isEmpty()) {
        return RepositoryResult::failure(QStringLiteral("column name cannot be empty"));
    }

    const RepositoryResult initResult = initialize();
    if (!initResult.ok) {
        return initResult;
    }

    QString lookupError;
    if (hasColumn(column.name, &lookupError)) {
        return RepositoryResult::failure(
            QStringLiteral("column '%1' already exists").arg(column.name));
    }
    if (!lookupError.isEmpty()) {
        return RepositoryResult::failure(lookupError);
    }

    QString error;
    const TableData table = m_store.readTable(getMetaFilePath(), &error);
    if (!error.isEmpty()) {
        return RepositoryResult::failure(error);
    }

    return m_store.appendRow(getMetaFilePath(), toColumnRow(column, table.rows.size() + 1));
}

RepositoryResult MetaRepo::updateColumn(const QString &columnName,
                                        const tabledef::Column &column) const
{
    if (columnName.trimmed().isEmpty() || column.name.trimmed().isEmpty()) {
        return RepositoryResult::failure(QStringLiteral("column name cannot be empty"));
    }

    const RepositoryResult initResult = initialize();
    if (!initResult.ok) {
        return initResult;
    }

    QString error;
    TableData table = m_store.readTable(getMetaFilePath(), &error);
    if (!error.isEmpty()) {
        return RepositoryResult::failure(error);
    }

    int targetIndex = -1;
    int ordinalPosition = -1;
    for (int index = 0; index < table.rows.size(); ++index) {
        if (!table.rows.at(index).isEmpty() && table.rows.at(index).at(0) == columnName) {
            targetIndex = index;
            ordinalPosition = table.rows.at(index).size() >= 7
                                  ? table.rows.at(index).at(6).toInt()
                                  : index + 1;
            continue;
        }
        if (!table.rows.at(index).isEmpty() && table.rows.at(index).at(0) == column.name) {
            return RepositoryResult::failure(
                QStringLiteral("column '%1' already exists").arg(column.name));
        }
    }

    if (targetIndex < 0) {
        return RepositoryResult::failure(
            QStringLiteral("column '%1' does not exist").arg(columnName));
    }

    table.rows[targetIndex] = toColumnRow(column, ordinalPosition);
    return m_store.writeTable(getMetaFilePath(), table);
}

RepositoryResult MetaRepo::deleteColumn(const QString &columnName) const
{
    if (columnName.trimmed().isEmpty()) {
        return RepositoryResult::failure(QStringLiteral("column name cannot be empty"));
    }

    const RepositoryResult initResult = initialize();
    if (!initResult.ok) {
        return initResult;
    }

    QString error;
    TableData table = m_store.readTable(getMetaFilePath(), &error);
    if (!error.isEmpty()) {
        return RepositoryResult::failure(error);
    }

    int targetIndex = -1;
    for (int index = 0; index < table.rows.size(); ++index) {
        if (!table.rows.at(index).isEmpty() && table.rows.at(index).at(0) == columnName) {
            targetIndex = index;
            break;
        }
    }

    if (targetIndex < 0) {
        return RepositoryResult::failure(
            QStringLiteral("column '%1' does not exist").arg(columnName));
    }

    table.rows.removeAt(targetIndex);
    for (int index = 0; index < table.rows.size(); ++index) {
        if (table.rows[index].size() >= 7) {
            table.rows[index][6] = QString::number(index + 1);
        }
    }
    return m_store.writeTable(getMetaFilePath(), table);
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

    return m_store.readTable(getMetaFilePath(), error);
}

QString MetaRepo::getMetaFilePath() const
{
    return m_store.getMetaFilePath(m_databaseName, m_tableName);
}

tabledef::TableSchema MetaRepo::getSchema() const
{
    return tabledef::buildTableMetaSchema(m_tableName);
}

} // namespace repo
