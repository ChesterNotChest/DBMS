#include "repo.h"

#include <QFileInfo>
#include <QJsonParseError>

namespace {

QJsonArray toJsonArray(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values) {
        array.append(value);
    }
    return array;
}

QStringList toStringList(const QJsonArray &array)
{
    QStringList values;
    values.reserve(array.size());
    for (const QJsonValue &value : array) {
        values.append(value.toString());
    }
    return values;
}

repo::RepositoryResult validateRowWidth(const repo::TableData &table, const repo::TableRow &row)
{
    if (!table.columns.isEmpty() && row.size() != table.columns.size()) {
        return repo::RepositoryResult::failure(
            QStringLiteral("row width %1 does not match column count %2")
                .arg(row.size())
                .arg(table.columns.size()));
    }
    if (table.columns.isEmpty() && !table.rows.isEmpty()
        && row.size() != table.rows.first().size()) {
        return repo::RepositoryResult::failure(
            QStringLiteral("row width %1 does not match existing row width %2")
                .arg(row.size())
                .arg(table.rows.first().size()));
    }
    return repo::RepositoryResult::success();
}

} // namespace

namespace repo {

bool TableData::isRectangular() const
{
    int expectedSize = columns.size();
    if (expectedSize == 0 && !rows.isEmpty()) {
        expectedSize = rows.first().size();
    }
    for (const TableRow &row : rows) {
        if (row.size() != expectedSize) {
            return false;
        }
    }
    return true;
}

RepositoryResult RepositoryResult::success()
{
    return {true, {}};
}

RepositoryResult RepositoryResult::failure(const QString &errorMessage)
{
    return {false, errorMessage};
}

FlatFileTableStore::FlatFileTableStore(QString dataRoot)
    : m_dataRoot(std::move(dataRoot))
{
}

QString FlatFileTableStore::defaultDataRoot()
{
    return QDir::cleanPath(QDir::current().absoluteFilePath(QStringLiteral("data")));
}

QString FlatFileTableStore::dataRoot() const
{
    return m_dataRoot;
}

QString FlatFileTableStore::rootFilePath() const
{
    return QDir(m_dataRoot).absoluteFilePath(QStringLiteral("root.dbf"));
}

QString FlatFileTableStore::metaFilePath(const QString &databaseName) const
{
    return QDir(m_dataRoot).absoluteFilePath(databaseName + QStringLiteral(".meta"));
}

QString FlatFileTableStore::databaseDirectory(const QString &databaseName) const
{
    return QDir(m_dataRoot).absoluteFilePath(databaseName);
}

QString FlatFileTableStore::tableFilePath(const QString &databaseName, const QString &tableName) const
{
    return QDir(databaseDirectory(databaseName))
        .absoluteFilePath(tableName + QStringLiteral(".dat"));
}

QString FlatFileTableStore::sortIndexDirectory(const QString &databaseName) const
{
    return QDir(databaseDirectory(databaseName)).absoluteFilePath(QStringLiteral("indexes"));
}

QString FlatFileTableStore::sortIndexFilePath(const QString &databaseName,
                                              const QString &indexName,
                                              const QString &sourceTable) const
{
    QString fileName = indexName;
    if (!sourceTable.trimmed().isEmpty()) {
        fileName = sourceTable + QStringLiteral("__") + indexName;
    }
    return QDir(sortIndexDirectory(databaseName)).absoluteFilePath(fileName + QStringLiteral(".idx"));
}

QString FlatFileTableStore::toStorageRelativePath(const QString &absolutePath) const
{
    return QDir(QFileInfo(m_dataRoot).absolutePath()).relativeFilePath(absolutePath);
}

bool FlatFileTableStore::exists(const QString &path) const
{
    return QFileInfo::exists(path);
}

RepositoryResult FlatFileTableStore::ensureDataRoot() const
{
    return ensureDirectory(m_dataRoot);
}

RepositoryResult FlatFileTableStore::ensureDirectory(const QString &path) const
{
    QDir directory;
    if (directory.mkpath(path)) {
        return RepositoryResult::success();
    }
    return RepositoryResult::failure(
        QStringLiteral("failed to create directory '%1'").arg(QDir::cleanPath(path)));
}

RepositoryResult FlatFileTableStore::removeFile(const QString &path) const
{
    QFile file(path);
    if (!file.exists()) {
        return RepositoryResult::success();
    }
    if (file.remove()) {
        return RepositoryResult::success();
    }
    return RepositoryResult::failure(QStringLiteral("failed to remove file '%1'").arg(path));
}

TableData FlatFileTableStore::readTable(const QString &path, QString *error) const
{
    if (error != nullptr) {
        error->clear();
    }

    QFile file(path);
    if (!file.exists()) {
        if (error != nullptr) {
            *error = QStringLiteral("table file '%1' does not exist").arg(path);
        }
        return {};
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error != nullptr) {
            *error = QStringLiteral("failed to open '%1' for reading").arg(path);
        }
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("failed to parse '%1': %2")
                         .arg(path, parseError.errorString());
        }
        return {};
    }

    const QJsonObject object = document.object();
    if (!object.value(QStringLiteral("columns")).isArray()
        || !object.value(QStringLiteral("rows")).isArray()) {
        if (error != nullptr) {
            *error = QStringLiteral("table file '%1' is missing columns or rows").arg(path);
        }
        return {};
    }

    TableData table;
    table.columns = toStringList(object.value(QStringLiteral("columns")).toArray());
    const QJsonArray jsonRows = object.value(QStringLiteral("rows")).toArray();
    for (const QJsonValue &jsonRow : jsonRows) {
        if (!jsonRow.isArray()) {
            if (error != nullptr) {
                *error = QStringLiteral("table file '%1' contains a non-array row").arg(path);
            }
            return {};
        }
        table.rows.append(toStringList(jsonRow.toArray()));
    }

    if (!table.isRectangular()) {
        if (error != nullptr) {
            *error = QStringLiteral("table file '%1' is not a valid rectangular table").arg(path);
        }
        return {};
    }

    return table;
}

RepositoryResult FlatFileTableStore::writeTable(const QString &path, const TableData &table) const
{
    if (!table.isRectangular()) {
        return RepositoryResult::failure(
            QStringLiteral("cannot write non-rectangular table to '%1'").arg(path));
    }

    const RepositoryResult directoryReady =
        ensureDirectory(QFileInfo(path).absolutePath());
    if (!directoryReady.ok) {
        return directoryReady;
    }

    QJsonObject object;
    object.insert(QStringLiteral("columns"), toJsonArray(table.columns));

    QJsonArray jsonRows;
    for (const TableRow &row : table.rows) {
        jsonRows.append(toJsonArray(row));
    }
    object.insert(QStringLiteral("rows"), jsonRows);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return RepositoryResult::failure(
            QStringLiteral("failed to open '%1' for writing").arg(path));
    }

    const qint64 written =
        file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (written < 0) {
        return RepositoryResult::failure(QStringLiteral("failed to write '%1'").arg(path));
    }

    return RepositoryResult::success();
}

RepositoryResult FlatFileTableStore::createEmptyTable(const QString &path,
                                                      const QStringList &columns) const
{
    return writeTable(path, TableData{columns, {}});
}

RepositoryResult FlatFileTableStore::appendRow(const QString &path, const TableRow &row) const
{
    QString error;
    TableData table = readTable(path, &error);
    if (!error.isEmpty()) {
        return RepositoryResult::failure(error);
    }

    const RepositoryResult validation = validateRowWidth(table, row);
    if (!validation.ok) {
        return validation;
    }

    table.rows.append(row);
    return writeTable(path, table);
}

RepositoryResult FlatFileTableStore::updateRow(const QString &path,
                                               int rowIndex,
                                               const TableRow &row) const
{
    QString error;
    TableData table = readTable(path, &error);
    if (!error.isEmpty()) {
        return RepositoryResult::failure(error);
    }

    if (rowIndex < 0 || rowIndex >= table.rows.size()) {
        return RepositoryResult::failure(
            QStringLiteral("row index %1 is out of range").arg(rowIndex));
    }

    const RepositoryResult validation = validateRowWidth(table, row);
    if (!validation.ok) {
        return validation;
    }

    table.rows[rowIndex] = row;
    return writeTable(path, table);
}

RepositoryResult FlatFileTableStore::deleteRow(const QString &path, int rowIndex) const
{
    QString error;
    TableData table = readTable(path, &error);
    if (!error.isEmpty()) {
        return RepositoryResult::failure(error);
    }

    if (rowIndex < 0 || rowIndex >= table.rows.size()) {
        return RepositoryResult::failure(
            QStringLiteral("row index %1 is out of range").arg(rowIndex));
    }

    table.rows.removeAt(rowIndex);
    return writeTable(path, table);
}

TableRepo::TableRepo(QString databaseName, QString tableName, QString dataRoot)
    : m_databaseName(std::move(databaseName))
    , m_tableName(std::move(tableName))
    , m_store(std::move(dataRoot))
{
}

RepositoryResult TableRepo::createTable(const QStringList &columns) const
{
    if (m_databaseName.trimmed().isEmpty()) {
        return RepositoryResult::failure(QStringLiteral("database name cannot be empty"));
    }
    if (m_tableName.trimmed().isEmpty()) {
        return RepositoryResult::failure(QStringLiteral("table name cannot be empty"));
    }

    const RepositoryResult directoryReady =
        m_store.ensureDirectory(m_store.databaseDirectory(m_databaseName));
    if (!directoryReady.ok) {
        return directoryReady;
    }

    if (m_store.exists(tableFilePath())) {
        return RepositoryResult::failure(
            QStringLiteral("table '%1' already exists").arg(m_tableName));
    }

    return m_store.createEmptyTable(tableFilePath(), columns);
}

RepositoryResult TableRepo::dropTable() const
{
    return m_store.removeFile(tableFilePath());
}

TableData TableRepo::readTable(QString *error) const
{
    return m_store.readTable(tableFilePath(), error);
}

RepositoryResult TableRepo::replaceTable(const TableData &table) const
{
    return m_store.writeTable(tableFilePath(), table);
}

RepositoryResult TableRepo::insertRow(const TableRow &row) const
{
    return m_store.appendRow(tableFilePath(), row);
}

RepositoryResult TableRepo::updateRow(int rowIndex, const TableRow &row) const
{
    return m_store.updateRow(tableFilePath(), rowIndex, row);
}

RepositoryResult TableRepo::deleteRow(int rowIndex) const
{
    return m_store.deleteRow(tableFilePath(), rowIndex);
}

QString TableRepo::tableFilePath() const
{
    return m_store.tableFilePath(m_databaseName, m_tableName);
}

} // namespace repo
