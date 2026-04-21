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
    const QJsonDocument document = QJsonDocument::fromJson(
        serializedColumns.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (error != nullptr) {
            *error = QStringLiteral("failed to parse constraint columns: %1")
                         .arg(parseError.errorString());
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

repo::TableRow toConstraintRow(const tabledef::Constraint &constraint)
{
    return {
        constraint.name,
        tabledef::constraintTypeToString(constraint.type),
        serializeColumns(constraint.columns),
        constraint.referencedTable,
        serializeColumns(constraint.referencedColumns),
        constraint.checkClause,
        constraint.indexName,
    };
}

repo::TableData migrateConstraintTableIfNeeded(const repo::TableData &table)
{
    const QStringList expectedColumns =
        tabledef::schemaColumnNames(tabledef::buildTableConstraintSchema());
    if (table.columns == expectedColumns) {
        return table;
    }

    repo::TableData migrated;
    migrated.columns = expectedColumns;
    migrated.rows.reserve(table.rows.size());
    for (const repo::TableRow &row : table.rows) {
        repo::TableRow migratedRow;
        migratedRow.reserve(expectedColumns.size());
        migratedRow.append(row.value(0));
        migratedRow.append(row.value(1));
        migratedRow.append(row.value(2));

        const bool looksLegacyRow = row.size() <= 4;
        migratedRow.append(looksLegacyRow ? QString() : row.value(3));
        migratedRow.append(looksLegacyRow ? QString() : row.value(4));
        migratedRow.append(looksLegacyRow ? row.value(3) : row.value(5));
        migratedRow.append(row.size() >= 7 ? row.value(6) : QString());

        migrated.rows.append(migratedRow);
    }
    return migrated;
}

bool constraintFromRow(const repo::TableRow &row,
                       tabledef::Constraint *constraint,
                       QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    if (constraint == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("constraint output pointer cannot be null");
        }
        return false;
    }
    if (row.size() < 4) {
        if (error != nullptr) {
            *error = QStringLiteral("constraint row is incomplete");
        }
        return false;
    }

    tabledef::ConstraintType type = tabledef::ConstraintType::Check;
    if (!tabledef::tryParseConstraintType(row.at(1), &type)) {
        if (error != nullptr) {
            *error = QStringLiteral("unknown constraint type '%1'").arg(row.at(1));
        }
        return false;
    }

    QString decodeError;
    const QStringList columns = deserializeColumns(row.at(2), &decodeError);
    if (!decodeError.isEmpty()) {
        if (error != nullptr) {
            *error = decodeError;
        }
        return false;
    }

    const bool looksLegacyRow = row.size() <= 4;
    const QString referencedTable = looksLegacyRow ? QString() : row.value(3);
    const QString referencedColumnsText = looksLegacyRow ? QString() : row.value(4);
    const QString checkClause = looksLegacyRow ? row.value(3) : row.value(5);
    const QString indexName = row.size() >= 7 ? row.value(6) : QString();

    QString referencedColumnsError;
    const QStringList referencedColumns =
        deserializeColumns(referencedColumnsText, &referencedColumnsError);
    if (!referencedColumnsError.isEmpty()) {
        if (error != nullptr) {
            *error = referencedColumnsError;
        }
        return false;
    }

    *constraint = tabledef::Constraint{
        row.at(0),
        type,
        columns,
        referencedTable,
        referencedColumns,
        checkClause,
        indexName,
    };
    return true;
}

} // namespace

namespace repo {

ConstraintRepo::ConstraintRepo(QString databaseName,
                               QString tableName,
                               QString dataRoot)
    : m_databaseName(std::move(databaseName))
    , m_tableName(std::move(tableName))
    , m_store(std::move(dataRoot))
{
}

RepositoryResult ConstraintRepo::initialize() const
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

    if (m_store.exists(getConstraintFilePath())) {
        QString error;
        const TableData table = m_store.readTable(getConstraintFilePath(), &error);
        if (!error.isEmpty()) {
            return RepositoryResult::failure(error);
        }

        const TableData migratedTable = migrateConstraintTableIfNeeded(table);
        if (migratedTable.columns != table.columns || migratedTable.rows != table.rows) {
            return m_store.writeTable(getConstraintFilePath(), migratedTable);
        }
        return RepositoryResult::success();
    }

    return m_store.createEmptyTable(
        getConstraintFilePath(),
        tabledef::schemaColumnNames(getSchema()));
}

QList<tabledef::Constraint> ConstraintRepo::listConstraints(QString *error) const
{
    const RepositoryResult initResult = initialize();
    if (!initResult.ok) {
        if (error != nullptr) {
            *error = initResult.error;
        }
        return {};
    }

    const TableData table = m_store.readTable(getConstraintFilePath(), error);
    QList<tabledef::Constraint> constraints;
    for (const TableRow &row : table.rows) {
        tabledef::Constraint constraint;
        QString parseError;
        if (!constraintFromRow(row, &constraint, &parseError)) {
            if (error != nullptr) {
                *error = parseError;
            }
            return {};
        }
        constraints.append(constraint);
    }
    return constraints;
}

bool ConstraintRepo::hasConstraint(const QString &constraintName, QString *error) const
{
    const QList<tabledef::Constraint> constraints = listConstraints(error);
    for (const tabledef::Constraint &constraint : constraints) {
        if (constraint.name == constraintName) {
            return true;
        }
    }
    return false;
}

RepositoryResult ConstraintRepo::createConstraint(const tabledef::Constraint &constraint) const
{
    if (constraint.name.trimmed().isEmpty()) {
        return RepositoryResult::failure(QStringLiteral("constraint name cannot be empty"));
    }

    const RepositoryResult initResult = initialize();
    if (!initResult.ok) {
        return initResult;
    }

    QString lookupError;
    if (hasConstraint(constraint.name, &lookupError)) {
        return RepositoryResult::failure(
            QStringLiteral("constraint '%1' already exists").arg(constraint.name));
    }
    if (!lookupError.isEmpty()) {
        return RepositoryResult::failure(lookupError);
    }

    return m_store.appendRow(getConstraintFilePath(), toConstraintRow(constraint));
}

RepositoryResult ConstraintRepo::updateConstraint(const QString &constraintName,
                                                  const tabledef::Constraint &constraint) const
{
    if (constraintName.trimmed().isEmpty()) {
        return RepositoryResult::failure(QStringLiteral("constraint name cannot be empty"));
    }
    if (constraint.name.trimmed().isEmpty()) {
        return RepositoryResult::failure(QStringLiteral("new constraint name cannot be empty"));
    }

    const RepositoryResult initResult = initialize();
    if (!initResult.ok) {
        return initResult;
    }

    QString error;
    TableData table = m_store.readTable(getConstraintFilePath(), &error);
    if (!error.isEmpty()) {
        return RepositoryResult::failure(error);
    }

    int targetIndex = -1;
    for (int index = 0; index < table.rows.size(); ++index) {
        if (!table.rows.at(index).isEmpty() && table.rows.at(index).at(0) == constraintName) {
            targetIndex = index;
            continue;
        }
        if (!table.rows.at(index).isEmpty() && table.rows.at(index).at(0) == constraint.name) {
            return RepositoryResult::failure(
                QStringLiteral("constraint '%1' already exists").arg(constraint.name));
        }
    }

    if (targetIndex < 0) {
        return RepositoryResult::failure(
            QStringLiteral("constraint '%1' does not exist").arg(constraintName));
    }

    table.rows[targetIndex] = toConstraintRow(constraint);
    return m_store.writeTable(getConstraintFilePath(), table);
}

RepositoryResult ConstraintRepo::deleteConstraint(const QString &constraintName) const
{
    if (constraintName.trimmed().isEmpty()) {
        return RepositoryResult::failure(QStringLiteral("constraint name cannot be empty"));
    }

    const RepositoryResult initResult = initialize();
    if (!initResult.ok) {
        return initResult;
    }

    QString error;
    TableData table = m_store.readTable(getConstraintFilePath(), &error);
    if (!error.isEmpty()) {
        return RepositoryResult::failure(error);
    }

    int targetIndex = -1;
    for (int index = 0; index < table.rows.size(); ++index) {
        if (!table.rows.at(index).isEmpty() && table.rows.at(index).at(0) == constraintName) {
            targetIndex = index;
            break;
        }
    }

    if (targetIndex < 0) {
        return RepositoryResult::failure(
            QStringLiteral("constraint '%1' does not exist").arg(constraintName));
    }

    table.rows.removeAt(targetIndex);
    return m_store.writeTable(getConstraintFilePath(), table);
}

TableData ConstraintRepo::constraintTable(QString *error) const
{
    const RepositoryResult initResult = initialize();
    if (!initResult.ok) {
        if (error != nullptr) {
            *error = initResult.error;
        }
        return {};
    }

    return m_store.readTable(getConstraintFilePath(), error);
}

QString ConstraintRepo::getConstraintFilePath() const
{
    return m_store.getConstraintFilePath(m_databaseName, m_tableName);
}

tabledef::TableSchema ConstraintRepo::getSchema() const
{
    return tabledef::buildTableConstraintSchema(m_tableName);
}

} // namespace repo
