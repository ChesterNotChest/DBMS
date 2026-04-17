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
        constraint.checkClause,
    };
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

    *constraint = tabledef::Constraint{row.at(0), type, columns, row.at(3)};
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

    const RepositoryResult databaseDirReady =
        m_store.ensureDirectory(m_store.getDatabaseDirectory(m_databaseName));
    if (!databaseDirReady.ok) {
        return databaseDirReady;
    }

    if (m_store.exists(getConstraintFilePath())) {
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
