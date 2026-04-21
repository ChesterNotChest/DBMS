#include "service.h"

#include <algorithm>
#include <QSet>
#include <QUuid>

namespace {

using service::currentDataRoot;
using service::compositeKeySignature;
using service::validateScalarValue;

bool validateOutgoingForeignKeys(const QString &databaseName,
                                 const QString &dataRoot,
                                 const tabledef::TableSchema &schema,
                                 const repo::TableData &candidateTable,
                                 QString *error);
bool checkKeyUniqueness(const QString &databaseName,
                        const tabledef::TableSchema &schema,
                        const repo::TableData &table,
                        QString *error);
repo::RepositoryResult writeTargetTable(repo::FlatFileTableStore &store,
                                        service::TargetTableKind kind,
                                        const QString &databaseName,
                                        const QString &tableName,
                                        const repo::TableData &table);

QString effectiveDatabaseName(const QString &targetDatabaseName)
{
    const QString trimmedTarget = targetDatabaseName.trimmed();
    if (!trimmedTarget.isEmpty()) {
        return trimmedTarget;
    }
    return service::currentDatabase.trimmed();
}

class CurrentDatabaseGuard
{
public:
    explicit CurrentDatabaseGuard(const QString &databaseName)
        : m_previous(service::currentDatabase)
    {
        if (!databaseName.trimmed().isEmpty()) {
            service::currentDatabase = databaseName.trimmed();
        }
    }

    ~CurrentDatabaseGuard()
    {
        service::currentDatabase = m_previous;
    }

private:
    QString m_previous;
};

QStringList expectedColumnNames(const tabledef::TableSchema &schema)
{
    return tabledef::schemaColumnNames(schema);
}

bool rowMatchesConditions(const repo::TableRow &row,
                          const repo::TableData &table,
                          const QList<service::SimpleCondition> &conditions,
                          QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    for (const service::SimpleCondition &condition : conditions) {
        const int columnIndex = table.columns.indexOf(condition.columnName);
        if (columnIndex < 0) {
            if (error != nullptr) {
                *error = QStringLiteral("column '%1' does not exist").arg(condition.columnName);
            }
            return false;
        }
        if (row.value(columnIndex) != condition.value) {
            return false;
        }
    }

    return true;
}

QStringList loadRowIdsForTargetTable(const QString &databaseName,
                                     service::TargetTableKind targetTableKind,
                                     const QString &targetTableName,
                                     const repo::TableData &currentTable,
                                     bool *rowIdsInitialized,
                                     QString *error)
{
    if (rowIdsInitialized != nullptr) {
        *rowIdsInitialized = false;
    }
    if (targetTableKind != service::TargetTableKind::TableDat) {
        if (error != nullptr) {
            error->clear();
        }
        return {};
    }

    CurrentDatabaseGuard databaseGuard(databaseName);
    return service::loadUserTableRowIds(targetTableName, currentTable, rowIdsInitialized, error);
}

QStringList generateTransientRowIds(int count)
{
    QStringList rowIds;
    rowIds.reserve(count);
    for (int index = 0; index < count; ++index) {
        rowIds.append(QUuid::createUuid().toString(QUuid::WithoutBraces));
    }
    return rowIds;
}

QStringList loadRowIdsWithoutSideEffects(const QString &databaseName,
                                         const QString &tableName,
                                         const repo::TableData &tableData,
                                         QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    repo::FlatFileTableStore store(currentDataRoot);
    const QString rowIdPath = store.getRowIdFilePath(databaseName, tableName);
    if (!store.exists(rowIdPath)) {
        return generateTransientRowIds(tableData.rows.size());
    }

    QString rowIdError;
    const repo::TableData rowIdTable = store.readTable(rowIdPath, &rowIdError);
    const bool isUsable = rowIdError.isEmpty()
                          && rowIdTable.columns == QStringList{QStringLiteral("row_id")}
                          && rowIdTable.rows.size() == tableData.rows.size();
    if (!isUsable) {
        return generateTransientRowIds(tableData.rows.size());
    }

    QStringList rowIds;
    rowIds.reserve(rowIdTable.rows.size());
    for (const repo::TableRow &row : rowIdTable.rows) {
        rowIds.append(row.value(0));
    }
    return rowIds;
}

const tabledef::IndexMeta *matchingUniqueIndex(const tabledef::TableSchema &schema,
                                               const tabledef::Constraint &constraint)
{
    for (const tabledef::IndexMeta &index : schema.indexes) {
        if (!index.isUnique) {
            continue;
        }
        if (index.columnNames == constraint.columns) {
            return &index;
        }
    }
    return nullptr;
}

QStringList constraintKeyValues(const repo::TableData &table,
                                const repo::TableRow &row,
                                const tabledef::Constraint &constraint,
                                QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    QStringList values;
    values.reserve(constraint.columns.size());
    for (const QString &columnName : constraint.columns) {
        const int columnIndex = table.columns.indexOf(columnName);
        if (columnIndex < 0) {
            if (error != nullptr) {
                *error = QStringLiteral("column '%1' does not exist").arg(columnName);
            }
            return {};
        }
        values.append(row.value(columnIndex));
    }
    return values;
}

bool validateChangedRowsAgainstUniqueIndexes(const QString &databaseName,
                                             const QString &tableName,
                                             const tabledef::TableSchema &schema,
                                             const repo::TableData &candidateTable,
                                             const QStringList &candidateRowIds,
                                             const QList<int> &changedRowIndexes,
                                             QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    for (const tabledef::Constraint &constraint : schema.constraints) {
        if (!tabledef::isPrimaryKeyConstraint(constraint)
            && !tabledef::isUniqueConstraint(constraint)) {
            continue;
        }

        const tabledef::IndexMeta *index = matchingUniqueIndex(schema, constraint);
        if (index == nullptr) {
            continue;
        }

        repo::SortIndexRepo sortIndexRepo(databaseName, index->indexName, tableName, currentDataRoot);
        QSet<QString> seenCandidateKeys;
        QSet<QString> changedRowIdSet;
        for (int rowIndex : changedRowIndexes) {
            if (rowIndex >= 0 && rowIndex < candidateRowIds.size()) {
                changedRowIdSet.insert(candidateRowIds.at(rowIndex));
            }
        }
        for (int rowIndex : changedRowIndexes) {
            if (rowIndex < 0 || rowIndex >= candidateTable.rows.size()) {
                continue;
            }

            const repo::TableRow &candidateRow = candidateTable.rows.at(rowIndex);
            QString keyError;
            const QStringList values = constraintKeyValues(candidateTable, candidateRow, constraint, &keyError);
            if (!keyError.isEmpty()) {
                if (error != nullptr) {
                    *error = keyError;
                }
                return false;
            }

            bool hasEmptyValue = false;
            for (const QString &value : values) {
                if (value.isEmpty()) {
                    hasEmptyValue = true;
                    break;
                }
            }
            if (tabledef::isPrimaryKeyConstraint(constraint) && hasEmptyValue) {
                if (error != nullptr) {
                    *error = QStringLiteral("primary key '%1' cannot contain empty values").arg(constraint.name);
                }
                return false;
            }
            if (tabledef::isUniqueConstraint(constraint) && hasEmptyValue) {
                continue;
            }

            const QString key = compositeKeySignature(values);
            if (seenCandidateKeys.contains(key)) {
                if (error != nullptr) {
                    *error = QStringLiteral("constraint '%1' is violated by duplicate values").arg(constraint.name);
                }
                return false;
            }
            seenCandidateKeys.insert(key);

            QString searchError;
            const QStringList matches = sortIndexRepo.search(values, &searchError);
            if (!searchError.isEmpty()) {
                if (error != nullptr) {
                    *error = searchError;
                }
                return false;
            }

            bool onlyChangedRows = true;
            for (const QString &match : matches) {
                if (!changedRowIdSet.contains(match)) {
                    onlyChangedRows = false;
                    break;
                }
            }

            if (!onlyChangedRows) {
                if (error != nullptr) {
                    *error = QStringLiteral("constraint '%1' is violated by duplicate values").arg(constraint.name);
                }
                return false;
            }
        }
    }

    return true;
}

bool validateConstraintRowsByIndex(const QString &databaseName,
                                   const QString &tableName,
                                   const tabledef::TableSchema &schema,
                                   const tabledef::Constraint &constraint,
                                   QString *error)
{
    const tabledef::IndexMeta *index = matchingUniqueIndex(schema, constraint);
    if (index == nullptr) {
        return false;
    }

    repo::SortIndexRepo sortIndexRepo(databaseName, index->indexName, tableName, currentDataRoot);
    QString indexError;
    if (!sortIndexRepo.validateUniqueKeys(&indexError)) {
        if (error != nullptr && error->isEmpty()) {
            *error = indexError;
        }
        return false;
    }

    return true;
}

bool buildCandidateRow(const tabledef::TableSchema &schema,
                       const QList<repo::TableRow> &existingRows,
                       const QMap<QString, QString> &inputRow,
                       repo::TableRow *outputRow,
                       QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    if (outputRow == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("row output pointer cannot be null");
        }
        return false;
    }

    for (auto it = inputRow.constBegin(); it != inputRow.constEnd(); ++it) {
        if (!tabledef::hasColumn(schema, it.key())) {
            if (error != nullptr) {
                *error = QStringLiteral("column '%1' does not exist in schema").arg(it.key());
            }
            return false;
        }
    }

    repo::TableRow candidateRow;
    candidateRow.reserve(schema.columns.size());
    for (int columnIndex = 0; columnIndex < schema.columns.size(); ++columnIndex) {
        const tabledef::Column &column = schema.columns.at(columnIndex);
        QString value = inputRow.value(column.name);

        if (value.isEmpty()) {
            if (column.autoIncrement) {
                if (column.type != tabledef::ColumnType::Int) {
                    if (error != nullptr) {
                        *error = QStringLiteral("AUTO_INCREMENT column '%1' must use INT type")
                                     .arg(column.name);
                    }
                    return false;
                }

                qlonglong maxValue = 0;
                bool hasValue = false;
                for (const repo::TableRow &row : existingRows) {
                    if (columnIndex >= row.size()) {
                        continue;
                    }
                    bool ok = false;
                    const qlonglong numericValue = row.at(columnIndex).toLongLong(&ok);
                    if (ok && (!hasValue || numericValue > maxValue)) {
                        maxValue = numericValue;
                        hasValue = true;
                    }
                }
                value = QString::number(hasValue ? maxValue + 1 : 1);
            } else if (!column.defaultValue.isEmpty()) {
                value = column.defaultValue;
            }
        }

        if (column.notNull && value.isEmpty()) {
            if (error != nullptr) {
                *error = QStringLiteral("column '%1' cannot be null").arg(column.name);
            }
            return false;
        }

        if (!validateScalarValue(column, value, error)) {
            return false;
        }

        candidateRow.append(value);
    }

    *outputRow = candidateRow;
    return true;
}

struct TableMutationState
{
    QString databaseName;
    QString tableName;
    tabledef::TableSchema schema;
    repo::TableData originalTable;
    repo::TableData candidateTable;
    QStringList originalRowIds;
    QStringList candidateRowIds;
    bool dirty = false;
};

enum class ReferencedMutationKind {
    Update,
    Delete
};

QString tableMutationKey(const QString &databaseName, const QString &tableName)
{
    return databaseName + QStringLiteral("::") + tableName;
}

tabledef::TableSchema loadTableSchemaForMutation(const QString &databaseName,
                                                 const QString &tableName,
                                                 QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    tabledef::TableSchema schema;
    schema.tableName = tableName;

    repo::MetaRepo metaRepo(databaseName, tableName, currentDataRoot);
    schema.columns = metaRepo.listColumns(error);
    if (error != nullptr && !error->isEmpty()) {
        return {};
    }

    repo::ConstraintRepo constraintRepo(databaseName, tableName, currentDataRoot);
    schema.constraints = constraintRepo.listConstraints(error);
    if (error != nullptr && !error->isEmpty()) {
        return {};
    }

    repo::IndexRepo indexRepo(databaseName, tableName, currentDataRoot);
    schema.indexes = indexRepo.listIndexes(error);
    if (error != nullptr && !error->isEmpty()) {
        return {};
    }

    return schema;
}

TableMutationState *ensureTableMutationState(const QString &databaseName,
                                            const QString &tableName,
                                            QMap<QString, TableMutationState> *states,
                                            QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    if (states == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("table mutation state map cannot be null");
        }
        return nullptr;
    }

    const QString key = tableMutationKey(databaseName, tableName);
    if (states->contains(key)) {
        return &(*states)[key];
    }

    TableMutationState state;
    state.databaseName = databaseName;
    state.tableName = tableName;
    state.schema = loadTableSchemaForMutation(databaseName, tableName, error);
    if (error != nullptr && !error->isEmpty()) {
        return nullptr;
    }

    state.originalTable = repo::TableRepo(databaseName, tableName, currentDataRoot).readTable(error);
    if (error != nullptr && !error->isEmpty()) {
        return nullptr;
    }
    state.candidateTable = state.originalTable;

    state.originalRowIds = loadRowIdsWithoutSideEffects(databaseName,
                                                        tableName,
                                                        state.originalTable,
                                                        error);
    if (error != nullptr && !error->isEmpty()) {
        return nullptr;
    }
    state.candidateRowIds = state.originalRowIds;

    states->insert(key, state);
    return &(*states)[key];
}

bool validateRowAgainstSchema(const repo::TableRow &row,
                              const tabledef::TableSchema &schema,
                              QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    if (row.size() != schema.columns.size()) {
        if (error != nullptr) {
            *error = QStringLiteral("row width does not match schema column count");
        }
        return false;
    }

    for (int index = 0; index < schema.columns.size(); ++index) {
        const tabledef::Column &column = schema.columns.at(index);
        const QString value = row.at(index);
        if (column.notNull && value.isEmpty()) {
            if (error != nullptr) {
                *error = QStringLiteral("column '%1' cannot be null").arg(column.name);
            }
            return false;
        }
        if (!validateScalarValue(column, value, error)) {
            return false;
        }
    }

    return true;
}

QStringList rowValuesForColumns(const repo::TableData &table,
                               const repo::TableRow &row,
                               const QStringList &columnNames,
                               QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    QStringList values;
    values.reserve(columnNames.size());
    for (const QString &columnName : columnNames) {
        const int columnIndex = table.columns.indexOf(columnName);
        if (columnIndex < 0) {
            if (error != nullptr) {
                *error = QStringLiteral("column '%1' does not exist").arg(columnName);
            }
            return {};
        }
        values.append(row.value(columnIndex));
    }
    return values;
}

QList<int> findReferencingRows(const repo::TableData &childTable,
                               const tabledef::Constraint &constraint,
                               const QStringList &referencedValues,
                               QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    QList<int> indexes;
    for (int rowIndex = 0; rowIndex < childTable.rows.size(); ++rowIndex) {
        const repo::TableRow &row = childTable.rows.at(rowIndex);
        QString rowError;
        const QStringList rowValues =
            rowValuesForColumns(childTable, row, constraint.columns, &rowError);
        if (!rowError.isEmpty()) {
            if (error != nullptr) {
                *error = rowError;
            }
            return {};
        }

        bool hasEmptyValue = false;
        for (const QString &value : rowValues) {
            if (value.isEmpty()) {
                hasEmptyValue = true;
                break;
            }
        }
        if (hasEmptyValue) {
            continue;
        }

        if (rowValues == referencedValues) {
            indexes.append(rowIndex);
        }
    }

    return indexes;
}

QStringList defaultValuesForForeignKeyColumns(const tabledef::TableSchema &schema,
                                              const QStringList &columnNames,
                                              QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    QStringList values;
    values.reserve(columnNames.size());
    for (const QString &columnName : columnNames) {
        const int columnIndex = tabledef::findColumnIndex(schema, columnName);
        if (columnIndex < 0) {
            if (error != nullptr) {
                *error = QStringLiteral("column '%1' does not exist").arg(columnName);
            }
            return {};
        }
        const tabledef::Column &column = schema.columns.at(columnIndex);
        if (column.defaultValue.isEmpty()) {
            if (error != nullptr) {
                *error = QStringLiteral("column '%1' has no default value for SET DEFAULT").arg(column.name);
            }
            return {};
        }
        values.append(column.defaultValue);
    }
    return values;
}

bool writeForeignKeyValues(repo::TableRow *row,
                           const repo::TableData &table,
                           const tabledef::TableSchema &schema,
                           const tabledef::Constraint &constraint,
                           const QStringList &values,
                           QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    if (row == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("row pointer cannot be null");
        }
        return false;
    }
    if (constraint.columns.size() != values.size()) {
        if (error != nullptr) {
            *error = QStringLiteral("foreign key value count does not match column count");
        }
        return false;
    }

    for (int index = 0; index < constraint.columns.size(); ++index) {
        const QString &columnName = constraint.columns.at(index);
        const int tableColumnIndex = table.columns.indexOf(columnName);
        const int schemaColumnIndex = tabledef::findColumnIndex(schema, columnName);
        if (tableColumnIndex < 0 || schemaColumnIndex < 0) {
            if (error != nullptr) {
                *error = QStringLiteral("column '%1' does not exist").arg(columnName);
            }
            return false;
        }

        const tabledef::Column &column = schema.columns.at(schemaColumnIndex);
        const QString &value = values.at(index);
        if (column.notNull && value.isEmpty()) {
            if (error != nullptr) {
                *error = QStringLiteral("column '%1' cannot be null").arg(column.name);
            }
            return false;
        }
        if (!validateScalarValue(column, value, error)) {
            return false;
        }
        (*row)[tableColumnIndex] = value;
    }

    return true;
}

bool validateMutationStateLocally(const TableMutationState &state,
                                  const QList<int> &changedRowIndexes,
                                  const QMap<QString, TableMutationState> *states,
                                  bool validateOutgoingConstraints,
                                  QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    for (const repo::TableRow &row : state.candidateTable.rows) {
        if (!validateRowAgainstSchema(row, state.schema, error)) {
            return false;
        }
    }

    QString indexError;
    validateChangedRowsAgainstUniqueIndexes(state.databaseName,
                                            state.tableName,
                                            state.schema,
                                            state.candidateTable,
                                            state.candidateRowIds,
                                            changedRowIndexes,
                                            &indexError);
    if (!indexError.isEmpty()) {
        if (error != nullptr) {
            *error = indexError;
        }
        return false;
    }

    if (!checkKeyUniqueness(state.databaseName, state.schema, state.candidateTable, error)) {
        return false;
    }
    if (validateOutgoingConstraints) {
        for (const tabledef::Constraint &constraint : state.schema.constraints) {
            if (!tabledef::isForeignKeyConstraint(constraint)) {
                continue;
            }
            if (!tabledef::isForeignKeyReferenceComplete(constraint)) {
                if (error != nullptr) {
                    *error = QStringLiteral("foreign key '%1' is incomplete").arg(constraint.name);
                }
                return false;
            }

            repo::TableData parentTable;
            const QString parentKey = tableMutationKey(state.databaseName, constraint.referencedTable);
            if (states != nullptr && states->contains(parentKey)) {
                parentTable = states->value(parentKey).candidateTable;
            } else {
                parentTable = repo::TableRepo(state.databaseName, constraint.referencedTable, currentDataRoot)
                                  .readTable(error);
                if (error != nullptr && !error->isEmpty()) {
                    return false;
                }
            }

            for (const repo::TableRow &row : state.candidateTable.rows) {
                QStringList values;
                values.reserve(constraint.columns.size());
                bool hasEmptyValue = false;

                for (const QString &columnName : constraint.columns) {
                    const int columnIndex = state.candidateTable.columns.indexOf(columnName);
                    if (columnIndex < 0) {
                        if (error != nullptr) {
                            *error = QStringLiteral("column '%1' does not exist").arg(columnName);
                        }
                        return false;
                    }
                    const QString value = row.value(columnIndex);
                    if (value.isEmpty()) {
                        hasEmptyValue = true;
                    }
                    values.append(value);
                }

                if (hasEmptyValue) {
                    continue;
                }

                QString rowError;
                if (!service::rowExistsInTable(parentTable, constraint.referencedColumns, values, &rowError)) {
                    if (!rowError.isEmpty()) {
                        if (error != nullptr) {
                            *error = rowError;
                        }
                        return false;
                    }
                    if (error != nullptr) {
                        *error = QStringLiteral("foreign key '%1' references missing parent row")
                                     .arg(constraint.name);
                    }
                    return false;
                }
            }
        }
    }

    return true;
}

QList<int> allRowIndexes(const repo::TableData &table)
{
    QList<int> indexes;
    indexes.reserve(table.rows.size());
    for (int rowIndex = 0; rowIndex < table.rows.size(); ++rowIndex) {
        indexes.append(rowIndex);
    }
    return indexes;
}

bool validateAllMutationStates(const QMap<QString, TableMutationState> &states, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    QStringList keys = states.keys();
    std::sort(keys.begin(), keys.end());
    for (const QString &key : keys) {
        const TableMutationState &state = states.value(key);
        if (!state.dirty) {
            continue;
        }
        if (!validateMutationStateLocally(state,
                                          allRowIndexes(state.candidateTable),
                                          &states,
                                          true,
                                          error)) {
            return false;
        }
    }

    return true;
}

bool commitMutationStates(const QMap<QString, TableMutationState> &states, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    QStringList keys = states.keys();
    std::sort(keys.begin(), keys.end());

    QList<QString> appliedKeys;
    repo::FlatFileTableStore store(currentDataRoot);
    for (const QString &key : keys) {
        const TableMutationState &state = states.value(key);
        if (!state.dirty) {
            continue;
        }

        const repo::RepositoryResult writeResult =
            writeTargetTable(store,
                             service::TargetTableKind::TableDat,
                             state.databaseName,
                             state.tableName,
                             state.candidateTable);
        if (!writeResult.ok) {
            if (error != nullptr) {
                *error = writeResult.error;
            }
            break;
        }

        CurrentDatabaseGuard databaseGuard(state.databaseName);
        QString commitError;
        if (!service::saveUserTableRowIds(state.tableName, state.candidateRowIds, &commitError)
            || !service::rebuildTableIndexes(state.tableName,
                                             state.schema,
                                             state.candidateTable,
                                             state.candidateRowIds,
                                             &commitError)) {
            if (error != nullptr) {
                *error = commitError;
            }
            break;
        }

        appliedKeys.append(key);
    }

    if (error == nullptr || error->isEmpty()) {
        return true;
    }

    for (int index = appliedKeys.size() - 1; index >= 0; --index) {
        const TableMutationState &state = states.value(appliedKeys.at(index));
        writeTargetTable(store,
                         service::TargetTableKind::TableDat,
                         state.databaseName,
                         state.tableName,
                         state.originalTable);
        CurrentDatabaseGuard databaseGuard(state.databaseName);
        service::saveUserTableRowIds(state.tableName, state.originalRowIds, nullptr);
        service::rebuildTableIndexes(state.tableName,
                                     state.schema,
                                     state.originalTable,
                                     state.originalRowIds,
                                     nullptr);
    }

    return false;
}

bool checkKeyUniqueness(const QString &databaseName,
                        const tabledef::TableSchema &schema,
                        const repo::TableData &table,
                        QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    Q_UNUSED(databaseName);

    for (const tabledef::Constraint &constraint : schema.constraints) {
        if (!tabledef::isPrimaryKeyConstraint(constraint)
            && !tabledef::isUniqueConstraint(constraint)) {
            continue;
        }

        QSet<QString> seenKeys;
        for (const repo::TableRow &row : table.rows) {
            QStringList values;
            values.reserve(constraint.columns.size());
            bool hasEmptyValue = false;

            for (const QString &columnName : constraint.columns) {
                const int columnIndex = table.columns.indexOf(columnName);
                if (columnIndex < 0) {
                    if (error != nullptr) {
                        *error = QStringLiteral("column '%1' does not exist").arg(columnName);
                    }
                    return false;
                }

                const QString value = row.value(columnIndex);
                if (value.isEmpty()) {
                    hasEmptyValue = true;
                }
                values.append(value);
            }

            if (tabledef::isPrimaryKeyConstraint(constraint) && hasEmptyValue) {
                if (error != nullptr) {
                    *error = QStringLiteral("primary key '%1' cannot contain empty values")
                                 .arg(constraint.name);
                }
                return false;
            }
            if (tabledef::isUniqueConstraint(constraint) && hasEmptyValue) {
                continue;
            }

            const QString key = compositeKeySignature(values);
            if (seenKeys.contains(key)) {
                if (error != nullptr) {
                    *error = QStringLiteral("constraint '%1' is violated by duplicate values")
                                 .arg(constraint.name);
                }
                return false;
            }
            seenKeys.insert(key);
        }
    }

    return true;
}

bool validateOutgoingForeignKeys(const QString &databaseName,
                                 const QString &dataRoot,
                                 const tabledef::TableSchema &schema,
                                 const repo::TableData &candidateTable,
                                 QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    for (const tabledef::Constraint &constraint : schema.constraints) {
        if (!tabledef::isForeignKeyConstraint(constraint)) {
            continue;
        }
        if (!tabledef::isForeignKeyReferenceComplete(constraint)) {
            if (error != nullptr) {
                *error = QStringLiteral("foreign key '%1' is incomplete").arg(constraint.name);
            }
            return false;
        }

        repo::TableData parentTable;
        if (constraint.referencedTable == schema.tableName) {
            parentTable = candidateTable;
        } else {
            parentTable = repo::TableRepo(databaseName, constraint.referencedTable, dataRoot)
                              .readTable(error);
            if (error != nullptr && !error->isEmpty()) {
                return false;
            }
        }

        for (const repo::TableRow &row : candidateTable.rows) {
            QStringList values;
            values.reserve(constraint.columns.size());
            bool hasEmptyValue = false;

            for (const QString &columnName : constraint.columns) {
                const int columnIndex = candidateTable.columns.indexOf(columnName);
                if (columnIndex < 0) {
                    if (error != nullptr) {
                        *error = QStringLiteral("column '%1' does not exist").arg(columnName);
                    }
                    return false;
                }
                const QString value = row.value(columnIndex);
                if (value.isEmpty()) {
                    hasEmptyValue = true;
                }
                values.append(value);
            }

            if (hasEmptyValue) {
                continue;
            }

            QString rowError;
            if (!service::rowExistsInTable(parentTable, constraint.referencedColumns, values, &rowError)) {
                if (!rowError.isEmpty()) {
                    if (error != nullptr) {
                        *error = rowError;
                    }
                    return false;
                }
                if (error != nullptr) {
                    *error = QStringLiteral("foreign key '%1' references missing parent row")
                                 .arg(constraint.name);
                }
                return false;
            }
        }
    }

    return true;
}

bool validateIncomingForeignKeys(const QString &databaseName,
                                 const QString &dataRoot,
                                 const QString &targetTableName,
                                 const repo::TableData &candidateTable,
                                 ReferencedMutationKind mutationKind,
                                 QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    repo::TabRepo tabRepo(databaseName, dataRoot);
    QString tabError;
    const QList<repo::TableEntry> tableEntries = tabRepo.listTables(&tabError);
    if (!tabError.isEmpty()) {
        if (error != nullptr) {
            *error = tabError;
        }
        return false;
    }

    for (const repo::TableEntry &tableEntry : tableEntries) {
        repo::ConstraintRepo constraintRepo(databaseName, tableEntry.name, dataRoot);
        QString constraintError;
        const QList<tabledef::Constraint> constraints = constraintRepo.listConstraints(&constraintError);
        if (!constraintError.isEmpty()) {
            if (error != nullptr) {
                *error = constraintError;
            }
            return false;
        }

        repo::TableData childTable;
        if (tableEntry.name == targetTableName) {
            childTable = candidateTable;
        } else {
            childTable = repo::TableRepo(databaseName, tableEntry.name, dataRoot)
                             .readTable(&constraintError);
            if (!constraintError.isEmpty()) {
                if (error != nullptr) {
                    *error = constraintError;
                }
                return false;
            }
        }

        for (const tabledef::Constraint &constraint : constraints) {
            if (!tabledef::isForeignKeyConstraint(constraint)
                || constraint.referencedTable != targetTableName) {
                continue;
            }
            if (!tabledef::isForeignKeyReferenceComplete(constraint)) {
                if (error != nullptr) {
                    *error = QStringLiteral("foreign key '%1' is incomplete").arg(constraint.name);
                }
                return false;
            }

            const tabledef::ForeignKeyAction action =
                mutationKind == ReferencedMutationKind::Delete
                    ? constraint.onDeleteAction
                    : constraint.onUpdateAction;
            if (action != tabledef::ForeignKeyAction::NoAction
                && action != tabledef::ForeignKeyAction::Restrict) {
                continue;
            }

            for (const repo::TableRow &row : childTable.rows) {
                QStringList values;
                values.reserve(constraint.columns.size());
                bool hasEmptyValue = false;

                for (const QString &columnName : constraint.columns) {
                    const int columnIndex = childTable.columns.indexOf(columnName);
                    if (columnIndex < 0) {
                        if (error != nullptr) {
                            *error = QStringLiteral("column '%1' does not exist").arg(columnName);
                        }
                        return false;
                    }
                    const QString value = row.value(columnIndex);
                    if (value.isEmpty()) {
                        hasEmptyValue = true;
                    }
                    values.append(value);
                }

                if (hasEmptyValue) {
                    continue;
                }

                QString rowError;
                if (!service::rowExistsInTable(candidateTable, constraint.referencedColumns, values, &rowError)) {
                    if (!rowError.isEmpty()) {
                        if (error != nullptr) {
                            *error = rowError;
                        }
                        return false;
                    }
                    if (error != nullptr) {
                        *error = QStringLiteral("foreign key '%1' from table '%2' would be broken")
                                     .arg(constraint.name, tableEntry.name);
                    }
                    return false;
                }
            }
        }
    }

    return true;
}

struct ForeignKeyGraphEdge
{
    QString parentTableName;
    QString childTableName;
    tabledef::Constraint constraint;
};

struct ForeignKeyCascadePlan
{
    QMap<QString, QList<ForeignKeyGraphEdge>> edgesByParent;
    QStringList traversalOrder;
    bool hasCycle = false;
};

struct ForeignKeyDependent
{
    QString childTableName;
    tabledef::Constraint constraint;
    QList<int> childRowIndexes;
    QStringList oldParentValues;
    QStringList newParentValues;
    tabledef::ForeignKeyAction action = tabledef::ForeignKeyAction::NoAction;
    ReferencedMutationKind mutationKind = ReferencedMutationKind::Update;
};

QString foreignKeyVisitToken(const QString &tableName,
                             const QString &constraintName,
                             const QStringList &values,
                             ReferencedMutationKind kind)
{
    return QStringLiteral("%1|%2|%3|%4")
        .arg(tableName,
             constraintName,
             kind == ReferencedMutationKind::Delete ? QStringLiteral("delete")
                                                    : QStringLiteral("update"),
             compositeKeySignature(values));
}

void visitForeignKeyGraph(const QString &tableName,
                          const QMap<QString, QList<ForeignKeyGraphEdge>> &edgesByParent,
                          QSet<QString> *visiting,
                          QSet<QString> *visited,
                          QStringList *traversalOrder,
                          bool *hasCycle)
{
    if (visiting != nullptr && visiting->contains(tableName)) {
        if (hasCycle != nullptr) {
            *hasCycle = true;
        }
        return;
    }
    if (visited != nullptr && visited->contains(tableName)) {
        return;
    }

    if (visiting != nullptr) {
        visiting->insert(tableName);
    }
    if (traversalOrder != nullptr) {
        traversalOrder->append(tableName);
    }

    const QList<ForeignKeyGraphEdge> edges = edgesByParent.value(tableName);
    for (const ForeignKeyGraphEdge &edge : edges) {
        visitForeignKeyGraph(edge.childTableName,
                             edgesByParent,
                             visiting,
                             visited,
                             traversalOrder,
                             hasCycle);
    }

    if (visiting != nullptr) {
        visiting->remove(tableName);
    }
    if (visited != nullptr) {
        visited->insert(tableName);
    }
}

ForeignKeyCascadePlan planForeignKeyCascade(const QString &databaseName,
                                            const QString &rootTableName,
                                            QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    ForeignKeyCascadePlan plan;
    repo::TabRepo tabRepo(databaseName, currentDataRoot);
    QString tabError;
    const QList<repo::TableEntry> tableEntries = tabRepo.listTables(&tabError);
    if (!tabError.isEmpty()) {
        if (error != nullptr) {
            *error = tabError;
        }
        return {};
    }

    for (const repo::TableEntry &tableEntry : tableEntries) {
        repo::ConstraintRepo constraintRepo(databaseName, tableEntry.name, currentDataRoot);
        const QList<tabledef::Constraint> constraints = constraintRepo.listConstraints(error);
        if (error != nullptr && !error->isEmpty()) {
            return {};
        }

        for (const tabledef::Constraint &constraint : constraints) {
            if (!tabledef::isForeignKeyConstraint(constraint)) {
                continue;
            }
            if (!tabledef::isForeignKeyReferenceComplete(constraint)) {
                if (error != nullptr) {
                    *error = QStringLiteral("foreign key '%1' is incomplete").arg(constraint.name);
                }
                return {};
            }

            ForeignKeyGraphEdge edge;
            edge.parentTableName = constraint.referencedTable;
            edge.childTableName = tableEntry.name;
            edge.constraint = constraint;
            plan.edgesByParent[edge.parentTableName].append(edge);
        }
    }

    for (auto it = plan.edgesByParent.begin(); it != plan.edgesByParent.end(); ++it) {
        std::sort(it.value().begin(), it.value().end(), [](const ForeignKeyGraphEdge &lhs,
                                                           const ForeignKeyGraphEdge &rhs) {
            if (lhs.childTableName != rhs.childTableName) {
                return lhs.childTableName < rhs.childTableName;
            }
            return lhs.constraint.name < rhs.constraint.name;
        });
    }

    QSet<QString> visiting;
    QSet<QString> visited;
    visitForeignKeyGraph(rootTableName,
                         plan.edgesByParent,
                         &visiting,
                         &visited,
                         &plan.traversalOrder,
                         &plan.hasCycle);

    return plan;
}

int traversalOrderIndex(const ForeignKeyCascadePlan &plan, const QString &tableName)
{
    const int index = plan.traversalOrder.indexOf(tableName);
    return index < 0 ? plan.traversalOrder.size() : index;
}

QList<ForeignKeyDependent> collectForeignKeyDependents(const QString &databaseName,
                                                       const QString &targetTableName,
                                                       const repo::TableData &beforeTable,
                                                       const repo::TableData &afterTable,
                                                       const QList<int> &affectedParentRowIndexes,
                                                       ReferencedMutationKind mutationKind,
                                                       const ForeignKeyCascadePlan &plan,
                                                       QMap<QString, TableMutationState> *states,
                                                       QSet<QString> *visited,
                                                       QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    if (states == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("table mutation state map cannot be null");
        }
        return {};
    }

    QList<ForeignKeyDependent> dependents;
    const QList<ForeignKeyGraphEdge> graphEdges = plan.edgesByParent.value(targetTableName);
    for (const ForeignKeyGraphEdge &edge : graphEdges) {
        TableMutationState *childState =
            ensureTableMutationState(databaseName, edge.childTableName, states, error);
        if (childState == nullptr) {
            return {};
        }

        const tabledef::Constraint &constraint = edge.constraint;

        for (int parentRowIndex : affectedParentRowIndexes) {
            if (parentRowIndex < 0 || parentRowIndex >= beforeTable.rows.size()) {
                continue;
            }

            QString keyError;
            const QStringList oldValues =
                rowValuesForColumns(beforeTable,
                                    beforeTable.rows.at(parentRowIndex),
                                    constraint.referencedColumns,
                                    &keyError);
            if (!keyError.isEmpty()) {
                if (error != nullptr) {
                    *error = keyError;
                }
                return {};
            }

            QStringList newValues;
            if (mutationKind == ReferencedMutationKind::Update) {
                if (parentRowIndex >= afterTable.rows.size()) {
                    if (error != nullptr) {
                        *error = QStringLiteral("updated parent row index %1 is out of range").arg(parentRowIndex);
                    }
                    return {};
                }

                newValues = rowValuesForColumns(afterTable,
                                                afterTable.rows.at(parentRowIndex),
                                                constraint.referencedColumns,
                                                &keyError);
                if (!keyError.isEmpty()) {
                    if (error != nullptr) {
                        *error = keyError;
                    }
                    return {};
                }

                if (oldValues == newValues) {
                    continue;
                }
            }

            const QString visitToken =
                foreignKeyVisitToken(edge.childTableName, constraint.name, oldValues, mutationKind);
            if (visited != nullptr && visited->contains(visitToken)) {
                continue;
            }

            const QList<int> matchedChildRows =
                findReferencingRows(childState->candidateTable, constraint, oldValues, &keyError);
            if (!keyError.isEmpty()) {
                if (error != nullptr) {
                    *error = keyError;
                }
                return {};
            }
            if (matchedChildRows.isEmpty()) {
                continue;
            }

            if (visited != nullptr) {
                visited->insert(visitToken);
            }

            ForeignKeyDependent dependent;
            dependent.childTableName = edge.childTableName;
            dependent.constraint = constraint;
            dependent.childRowIndexes = matchedChildRows;
            dependent.oldParentValues = oldValues;
            dependent.newParentValues = newValues;
            dependent.action =
                mutationKind == ReferencedMutationKind::Delete
                    ? constraint.onDeleteAction
                    : constraint.onUpdateAction;
            dependent.mutationKind = mutationKind;
            dependents.append(dependent);
        }
    }

    return dependents;
}

QList<ForeignKeyDependent> orderForeignKeyDependents(const QList<ForeignKeyDependent> &dependents,
                                                     const ForeignKeyCascadePlan &cascadePlan,
                                                     QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    QList<ForeignKeyDependent> orderedDependents = dependents;
    std::sort(orderedDependents.begin(),
              orderedDependents.end(),
              [&cascadePlan](const ForeignKeyDependent &lhs, const ForeignKeyDependent &rhs) {
        const int lhsOrder = traversalOrderIndex(cascadePlan, lhs.childTableName);
        const int rhsOrder = traversalOrderIndex(cascadePlan, rhs.childTableName);
        if (lhsOrder != rhsOrder) {
            return lhsOrder < rhsOrder;
        }
        if (lhs.childTableName != rhs.childTableName) {
            return lhs.childTableName < rhs.childTableName;
        }
        if (lhs.constraint.name != rhs.constraint.name) {
            return lhs.constraint.name < rhs.constraint.name;
        }
        return compositeKeySignature(lhs.oldParentValues) < compositeKeySignature(rhs.oldParentValues);
    });

    for (const ForeignKeyDependent &dependent : orderedDependents) {
        if (dependent.action == tabledef::ForeignKeyAction::NoAction
            || dependent.action == tabledef::ForeignKeyAction::Restrict) {
            if (error != nullptr) {
                *error = QStringLiteral("foreign key '%1' from table '%2' would be broken")
                             .arg(dependent.constraint.name, dependent.childTableName);
            }
            return {};
        }
    }

    return orderedDependents;
}

bool applyForeignKeyCascade(const QString &databaseName,
                            const QString &targetTableName,
                            const repo::TableData &beforeTable,
                            const repo::TableData &afterTable,
                            const QList<int> &affectedParentRowIndexes,
                            ReferencedMutationKind mutationKind,
                            const ForeignKeyCascadePlan &cascadePlan,
                            QMap<QString, TableMutationState> *states,
                            QSet<QString> *visited,
                            QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    const QList<ForeignKeyDependent> dependents =
        collectForeignKeyDependents(databaseName,
                                    targetTableName,
                                    beforeTable,
                                    afterTable,
                                    affectedParentRowIndexes,
                                    mutationKind,
                                    cascadePlan,
                                    states,
                                    visited,
                                    error);
    if (error != nullptr && !error->isEmpty()) {
        return false;
    }

    const QList<ForeignKeyDependent> plan = orderForeignKeyDependents(dependents,
                                                                      cascadePlan,
                                                                      error);
    if (error != nullptr && !error->isEmpty()) {
        return false;
    }

    for (const ForeignKeyDependent &dependent : plan) {
        TableMutationState *childState =
            ensureTableMutationState(databaseName, dependent.childTableName, states, error);
        if (childState == nullptr) {
            return false;
        }

        const repo::TableData beforeChildTable = childState->candidateTable;
        QString mutationError;
        QList<int> changedChildRowIndexes =
            findReferencingRows(childState->candidateTable,
                                dependent.constraint,
                                dependent.oldParentValues,
                                &mutationError);
        if (!mutationError.isEmpty()) {
            if (error != nullptr) {
                *error = mutationError;
            }
            return false;
        }
        if (changedChildRowIndexes.isEmpty()) {
            continue;
        }

        ReferencedMutationKind childMutationKind = ReferencedMutationKind::Update;

        if (dependent.action == tabledef::ForeignKeyAction::Cascade
            && dependent.mutationKind == ReferencedMutationKind::Delete) {
            std::sort(changedChildRowIndexes.begin(), changedChildRowIndexes.end());
            QList<int> uniqueChangedIndexes;
            uniqueChangedIndexes.reserve(changedChildRowIndexes.size());
            for (int childRowIndex : changedChildRowIndexes) {
                if (uniqueChangedIndexes.isEmpty() || uniqueChangedIndexes.last() != childRowIndex) {
                    uniqueChangedIndexes.append(childRowIndex);
                }
            }
            changedChildRowIndexes = uniqueChangedIndexes;
            for (int index = changedChildRowIndexes.size() - 1; index >= 0; --index) {
                const int childRowIndex = changedChildRowIndexes.at(index);
                if (childRowIndex >= 0 && childRowIndex < childState->candidateTable.rows.size()) {
                    childState->candidateTable.rows.removeAt(childRowIndex);
                }
                if (childRowIndex >= 0 && childRowIndex < childState->candidateRowIds.size()) {
                    childState->candidateRowIds.removeAt(childRowIndex);
                }
            }
            childMutationKind = ReferencedMutationKind::Delete;
        } else {
            QStringList assignedValues;
            if (dependent.action == tabledef::ForeignKeyAction::Cascade) {
                assignedValues = dependent.newParentValues;
            } else if (dependent.action == tabledef::ForeignKeyAction::SetNull) {
                assignedValues = QStringList(dependent.constraint.columns.size(), QString());
            } else if (dependent.action == tabledef::ForeignKeyAction::SetDefault) {
                assignedValues = defaultValuesForForeignKeyColumns(childState->schema,
                                                                  dependent.constraint.columns,
                                                                  &mutationError);
                if (!mutationError.isEmpty()) {
                    if (error != nullptr) {
                        *error = mutationError;
                    }
                    return false;
                }
            }

            for (int childRowIndex : changedChildRowIndexes) {
                if (childRowIndex < 0 || childRowIndex >= childState->candidateTable.rows.size()) {
                    continue;
                }

                repo::TableRow updatedRow = childState->candidateTable.rows.at(childRowIndex);
                if (!writeForeignKeyValues(&updatedRow,
                                           childState->candidateTable,
                                           childState->schema,
                                           dependent.constraint,
                                           assignedValues,
                                           &mutationError)) {
                    if (error != nullptr) {
                        *error = mutationError;
                    }
                    return false;
                }
                if (!validateRowAgainstSchema(updatedRow, childState->schema, &mutationError)) {
                    if (error != nullptr) {
                        *error = mutationError;
                    }
                    return false;
                }

                childState->candidateTable.rows[childRowIndex] = updatedRow;
            }
        }

        childState->dirty = true;
        if (!validateMutationStateLocally(*childState,
                                          changedChildRowIndexes,
                                          states,
                                          false,
                                          error)) {
            return false;
        }

        if (!applyForeignKeyCascade(databaseName,
                                    dependent.childTableName,
                                    beforeChildTable,
                                    childState->candidateTable,
                                    changedChildRowIndexes,
                                    childMutationKind,
                                    cascadePlan,
                                    states,
                                    visited,
                                    error)) {
            return false;
        }
    }

    return true;
}

repo::TableData projectRows(const repo::TableData &table,
                            const QStringList &projectionColumns,
                            const QList<service::SimpleCondition> &conditions,
                            QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    if (projectionColumns.size() == 1 && projectionColumns.first() == QStringLiteral("*")) {
        repo::TableData result = table;
        result.rows.clear();
        for (const repo::TableRow &row : table.rows) {
            if (!rowMatchesConditions(row, table, conditions, error)) {
                if (error != nullptr && !error->isEmpty()) {
                    return {};
                }
                continue;
            }
            result.rows.append(row);
        }
        return result;
    }

    repo::TableData result;
    result.columns = projectionColumns;
    for (const QString &columnName : projectionColumns) {
        if (!table.columns.contains(columnName)) {
            if (error != nullptr) {
                *error = QStringLiteral("column '%1' does not exist").arg(columnName);
            }
            return {};
        }
    }

    for (const repo::TableRow &row : table.rows) {
        if (!rowMatchesConditions(row, table, conditions, error)) {
            if (error != nullptr && !error->isEmpty()) {
                return {};
            }
            continue;
        }

        repo::TableRow projectedRow;
        projectedRow.reserve(projectionColumns.size());
        for (const QString &columnName : projectionColumns) {
            const int columnIndex = table.columns.indexOf(columnName);
            projectedRow.append(row.value(columnIndex));
        }
        result.rows.append(projectedRow);
    }

    return result;
}

repo::TableData deleteMatchedRows(const repo::TableData &table, const QList<int> &matchedIndexes)
{
    repo::TableData result = table;
    for (int index = matchedIndexes.size() - 1; index >= 0; --index) {
        const int rowIndex = matchedIndexes.at(index);
        if (rowIndex >= 0 && rowIndex < result.rows.size()) {
            result.rows.removeAt(rowIndex);
        }
    }
    return result;
}

QString targetTablePath(repo::FlatFileTableStore &store,
                        service::TargetTableKind kind,
                        const QString &databaseName,
                        const QString &tableName)
{
    switch (kind) {
    case service::TargetTableKind::RootDbf:
        return store.getRootFilePath();
    case service::TargetTableKind::DatabaseTab:
        return store.getTabFilePath(databaseName);
    case service::TargetTableKind::TableMeta:
        return store.getMetaFilePath(databaseName, tableName);
    case service::TargetTableKind::TableCon:
        return store.getConstraintFilePath(databaseName, tableName);
    case service::TargetTableKind::TableDat:
        return store.getTableFilePath(databaseName, tableName);
    }

    return {};
}

repo::TableData readTargetTable(repo::FlatFileTableStore &store,
                                service::TargetTableKind kind,
                                const QString &databaseName,
                                const QString &tableName,
                                QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    switch (kind) {
    case service::TargetTableKind::RootDbf:
        return repo::DatabaseRepo(store.getDataRoot()).rootTable(error);
    case service::TargetTableKind::DatabaseTab:
        return repo::TabRepo(databaseName, store.getDataRoot()).tabTable(error);
    case service::TargetTableKind::TableMeta:
        return repo::MetaRepo(databaseName, tableName, store.getDataRoot()).metaTable(error);
    case service::TargetTableKind::TableCon:
        return repo::ConstraintRepo(databaseName, tableName, store.getDataRoot()).constraintTable(error);
    case service::TargetTableKind::TableDat:
        return repo::TableRepo(databaseName, tableName, store.getDataRoot()).readTable(error);
    }

    if (error != nullptr) {
        *error = QStringLiteral("unknown target table kind");
    }
    return {};
}

repo::RepositoryResult writeTargetTable(repo::FlatFileTableStore &store,
                                       service::TargetTableKind kind,
                                       const QString &databaseName,
                                       const QString &tableName,
                                       const repo::TableData &table)
{
    const QString path = targetTablePath(store, kind, databaseName, tableName);
    if (path.isEmpty()) {
        return repo::RepositoryResult::failure(QStringLiteral("unknown target table kind"));
    }
    return store.writeTable(path, table);
}

bool schemaMatchesTable(const tabledef::TableSchema &schema, const repo::TableData &table)
{
    return table.columns == expectedColumnNames(schema);
}

} // namespace

namespace service {

SelectRowsResult TableDmlService::selectRows(const QString &targetDatabaseName,
                                             const QString &targetTableName,
                                             TargetTableKind targetTableKind,
                                             const tabledef::TableSchema &targetSchema,
                                             const QStringList &projectionColumns,
                                             const QList<SimpleCondition> &simpleConditions) const
{
    SelectRowsResult result;

    if (projectionColumns.isEmpty()) {
        result.errorMessage = QStringLiteral("projection columns cannot be empty");
        return result;
    }

    const QString databaseName = effectiveDatabaseName(targetDatabaseName);
    if (databaseName.isEmpty() && targetTableKind != TargetTableKind::RootDbf) {
        result.errorMessage = QStringLiteral("database name cannot be empty");
        return result;
    }

    repo::FlatFileTableStore store(currentDataRoot);
    QString error;
    const repo::TableData table = readTargetTable(store, targetTableKind, databaseName, targetTableName, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    if (!schemaMatchesTable(targetSchema, table)) {
        result.errorMessage = QStringLiteral("table schema does not match expected schema");
        return result;
    }

    result.resultTable = projectRows(table, projectionColumns, simpleConditions, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    result.success = true;
    result.affectedRowCount = result.resultTable.rows.size();
    return result;
}

TableDmlResult TableDmlService::insertRows(const QString &targetDatabaseName,
                                           const QString &targetTableName,
                                           TargetTableKind targetTableKind,
                                           const tabledef::TableSchema &targetSchema,
                                           const QList<QMap<QString, QString>> &rows,
                                           ValidationMode validationMode) const
{
    TableDmlResult result;

    if (rows.isEmpty()) {
        result.success = true;
        return result;
    }

    const QString databaseName = effectiveDatabaseName(targetDatabaseName);
    if (databaseName.isEmpty() && targetTableKind != TargetTableKind::RootDbf) {
        result.errorMessage = QStringLiteral("database name cannot be empty");
        return result;
    }

    repo::FlatFileTableStore store(currentDataRoot);
    QString error;
    repo::TableData currentTable = readTargetTable(store, targetTableKind, databaseName, targetTableName, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    if (!schemaMatchesTable(targetSchema, currentTable)) {
        result.errorMessage = QStringLiteral("table schema does not match expected schema");
        return result;
    }

    bool rowIdsInitialized = false;
    QStringList currentRowIds = loadRowIdsForTargetTable(databaseName,
                                                         targetTableKind,
                                                         targetTableName,
                                                         currentTable,
                                                         &rowIdsInitialized,
                                                         &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }
    if (rowIdsInitialized && targetTableKind == TargetTableKind::TableDat) {
        CurrentDatabaseGuard databaseGuard(databaseName);
        if (!rebuildTableIndexes(targetTableName, targetSchema, currentTable, currentRowIds, &error)) {
            result.errorMessage = error;
            return result;
        }
    }

    repo::TableData candidateTable = currentTable;
    QStringList candidateRowIds = currentRowIds;
    for (const QMap<QString, QString> &rowMap : rows) {
        repo::TableRow candidateRow;
        if (!buildCandidateRow(targetSchema, candidateTable.rows, rowMap, &candidateRow, &error)) {
            result.errorMessage = error;
            return result;
        }
        candidateTable.rows.append(candidateRow);
        candidateRowIds.append(QUuid::createUuid().toString(QUuid::WithoutBraces));
    }

    const QList<int> insertedRowIndexes = [&candidateTable, &currentTable, &rows]() {
        QList<int> indexes;
        indexes.reserve(rows.size());
        for (int rowIndex = currentTable.rows.size(); rowIndex < candidateTable.rows.size(); ++rowIndex) {
            indexes.append(rowIndex);
        }
        return indexes;
    }();

    if (validationMode == ValidationMode::UserData) {
        if (targetTableKind == TargetTableKind::TableDat) {
            QString indexError;
            validateChangedRowsAgainstUniqueIndexes(databaseName,
                                                    targetTableName,
                                                    targetSchema,
                                                    candidateTable,
                                                    candidateRowIds,
                                                    insertedRowIndexes,
                                                    &indexError);
            if (!indexError.isEmpty()) {
                result.errorMessage = indexError;
                return result;
            }
        }

        if (!checkKeyUniqueness(databaseName, targetSchema, candidateTable, &error)) {
            result.errorMessage = error;
            return result;
        }
        if (targetTableKind == TargetTableKind::TableDat
            && !validateOutgoingForeignKeys(databaseName, currentDataRoot, targetSchema, candidateTable, &error)) {
            result.errorMessage = error;
            return result;
        }
    }

    const repo::RepositoryResult writeResult =
        writeTargetTable(store, targetTableKind, databaseName, targetTableName, candidateTable);
    if (!writeResult.ok) {
        result.errorMessage = writeResult.error;
        return result;
    }

    if (targetTableKind == TargetTableKind::TableDat) {
        CurrentDatabaseGuard databaseGuard(databaseName);
        if (!saveUserTableRowIds(targetTableName, candidateRowIds, &error)) {
            writeTargetTable(store, targetTableKind, databaseName, targetTableName, currentTable);
            result.errorMessage = error;
            return result;
        }
        if (!insertTableIndexes(targetTableName,
                                targetSchema,
                                candidateTable,
                                candidateRowIds,
                                insertedRowIndexes,
                                &error)) {
            writeTargetTable(store, targetTableKind, databaseName, targetTableName, currentTable);
            saveUserTableRowIds(targetTableName, currentRowIds, nullptr);
            rebuildTableIndexes(targetTableName, targetSchema, currentTable, currentRowIds, nullptr);
            result.errorMessage = error;
            return result;
        }
    }

    result.success = true;
    result.affectedRowCount = rows.size();
    return result;
}

TableDmlResult TableDmlService::updateRows(const QString &targetDatabaseName,
                                           const QString &targetTableName,
                                           TargetTableKind targetTableKind,
                                           const tabledef::TableSchema &targetSchema,
                                           const QMap<QString, QString> &assignmentMap,
                                           const QList<SimpleCondition> &simpleConditions,
                                           ValidationMode validationMode) const
{
    TableDmlResult result;

    const QString databaseName = effectiveDatabaseName(targetDatabaseName);
    if (databaseName.isEmpty() && targetTableKind != TargetTableKind::RootDbf) {
        result.errorMessage = QStringLiteral("database name cannot be empty");
        return result;
    }

    repo::FlatFileTableStore store(currentDataRoot);
    QString error;
    repo::TableData currentTable = readTargetTable(store, targetTableKind, databaseName, targetTableName, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    if (!schemaMatchesTable(targetSchema, currentTable)) {
        result.errorMessage = QStringLiteral("table schema does not match expected schema");
        return result;
    }

    bool rowIdsInitialized = false;
    QStringList currentRowIds = loadRowIdsForTargetTable(databaseName,
                                                         targetTableKind,
                                                         targetTableName,
                                                         currentTable,
                                                         &rowIdsInitialized,
                                                         &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }
    if (rowIdsInitialized && targetTableKind == TargetTableKind::TableDat) {
        CurrentDatabaseGuard databaseGuard(databaseName);
        if (!rebuildTableIndexes(targetTableName, targetSchema, currentTable, currentRowIds, &error)) {
            result.errorMessage = error;
            return result;
        }
    }

    for (auto it = assignmentMap.constBegin(); it != assignmentMap.constEnd(); ++it) {
        if (!tabledef::hasColumn(targetSchema, it.key())) {
            result.errorMessage = QStringLiteral("column '%1' does not exist in schema").arg(it.key());
            return result;
        }
    }

    QList<int> matchedRowIndexes;
    for (int rowIndex = 0; rowIndex < currentTable.rows.size(); ++rowIndex) {
        QString matchError;
        if (!rowMatchesConditions(currentTable.rows.at(rowIndex), currentTable, simpleConditions, &matchError)) {
            if (!matchError.isEmpty()) {
                result.errorMessage = matchError;
                return result;
            }
            continue;
        }
        matchedRowIndexes.append(rowIndex);
    }

    if (matchedRowIndexes.isEmpty()) {
        result.success = true;
        return result;
    }

    repo::TableData candidateTable = currentTable;
    QStringList candidateRowIds = currentRowIds;
    for (int rowIndex : matchedRowIndexes) {
        repo::TableRow updatedRow = candidateTable.rows.at(rowIndex);
        for (auto it = assignmentMap.constBegin(); it != assignmentMap.constEnd(); ++it) {
            const int columnIndex = candidateTable.columns.indexOf(it.key());
            if (columnIndex < 0) {
                result.errorMessage = QStringLiteral("column '%1' does not exist").arg(it.key());
                return result;
            }

            const tabledef::Column &column = targetSchema.columns.at(columnIndex);
            const QString newValue = it.value();
            if (column.notNull && newValue.isEmpty()) {
                result.errorMessage = QStringLiteral("column '%1' cannot be null").arg(column.name);
                return result;
            }
            if (!validateScalarValue(column, newValue, &error)) {
                result.errorMessage = error;
                return result;
            }

            updatedRow[columnIndex] = newValue;
        }
        candidateTable.rows[rowIndex] = updatedRow;
    }

    if (validationMode == ValidationMode::UserData
        && targetTableKind == TargetTableKind::TableDat) {
        QMap<QString, TableMutationState> mutationStates;
        TableMutationState rootState;
        rootState.databaseName = databaseName;
        rootState.tableName = targetTableName;
        rootState.schema = targetSchema;
        rootState.originalTable = currentTable;
        rootState.candidateTable = candidateTable;
        rootState.originalRowIds = currentRowIds;
        rootState.candidateRowIds = candidateRowIds;
        rootState.dirty = true;
        mutationStates.insert(tableMutationKey(databaseName, targetTableName), rootState);

        if (!validateMutationStateLocally(rootState,
                                          matchedRowIndexes,
                                          &mutationStates,
                                          false,
                                          &error)) {
            result.errorMessage = error;
            return result;
        }

        if (!validateIncomingForeignKeys(databaseName,
                                         currentDataRoot,
                                         targetTableName,
                                         candidateTable,
                                         ReferencedMutationKind::Update,
                                         &error)) {
            result.errorMessage = error;
            return result;
        }

        const ForeignKeyCascadePlan cascadePlan =
            planForeignKeyCascade(databaseName, targetTableName, &error);
        if (!error.isEmpty()) {
            result.errorMessage = error;
            return result;
        }

        QSet<QString> visited;
        if (!applyForeignKeyCascade(databaseName,
                                    targetTableName,
                                    currentTable,
                                    candidateTable,
                                    matchedRowIndexes,
                                    ReferencedMutationKind::Update,
                                    cascadePlan,
                                    &mutationStates,
                                    &visited,
                                    &error)) {
            result.errorMessage = error;
            return result;
        }

        if (!validateAllMutationStates(mutationStates, &error)) {
            result.errorMessage = error;
            return result;
        }

        if (!commitMutationStates(mutationStates, &error)) {
            result.errorMessage = error;
            return result;
        }

        result.success = true;
        result.affectedRowCount = matchedRowIndexes.size();
        return result;
    }

    if (validationMode == ValidationMode::UserData) {
        if (targetTableKind == TargetTableKind::TableDat) {
            QString indexError;
            validateChangedRowsAgainstUniqueIndexes(databaseName,
                                                    targetTableName,
                                                    targetSchema,
                                                    candidateTable,
                                                    candidateRowIds,
                                                    matchedRowIndexes,
                                                    &indexError);
            if (!indexError.isEmpty()) {
                result.errorMessage = indexError;
                return result;
            }
        }

        if (!checkKeyUniqueness(databaseName, targetSchema, candidateTable, &error)) {
            result.errorMessage = error;
            return result;
        }
        if (targetTableKind == TargetTableKind::TableDat
            && !validateOutgoingForeignKeys(databaseName, currentDataRoot, targetSchema, candidateTable, &error)) {
            result.errorMessage = error;
            return result;
        }
    }

    const repo::RepositoryResult writeResult =
        writeTargetTable(store, targetTableKind, databaseName, targetTableName, candidateTable);
    if (!writeResult.ok) {
        result.errorMessage = writeResult.error;
        return result;
    }

    if (targetTableKind == TargetTableKind::TableDat) {
        CurrentDatabaseGuard databaseGuard(databaseName);
        if (!saveUserTableRowIds(targetTableName, candidateRowIds, &error)) {
            writeTargetTable(store, targetTableKind, databaseName, targetTableName, currentTable);
            result.errorMessage = error;
            return result;
        }
        if (!updateTableIndexes(targetTableName,
                                targetSchema,
                                currentTable,
                                candidateTable,
                                candidateRowIds,
                                matchedRowIndexes,
                                &error)) {
            writeTargetTable(store, targetTableKind, databaseName, targetTableName, currentTable);
            saveUserTableRowIds(targetTableName, currentRowIds, nullptr);
            rebuildTableIndexes(targetTableName, targetSchema, currentTable, currentRowIds, nullptr);
            result.errorMessage = error;
            return result;
        }
    }

    result.success = true;
    result.affectedRowCount = matchedRowIndexes.size();
    return result;
}

TableDmlResult TableDmlService::deleteRows(const QString &targetDatabaseName,
                                           const QString &targetTableName,
                                           TargetTableKind targetTableKind,
                                           const tabledef::TableSchema &targetSchema,
                                           const QList<SimpleCondition> &simpleConditions,
                                           ValidationMode validationMode) const
{
    TableDmlResult result;

    const QString databaseName = effectiveDatabaseName(targetDatabaseName);
    if (databaseName.isEmpty() && targetTableKind != TargetTableKind::RootDbf) {
        result.errorMessage = QStringLiteral("database name cannot be empty");
        return result;
    }

    repo::FlatFileTableStore store(currentDataRoot);
    QString error;
    repo::TableData currentTable = readTargetTable(store, targetTableKind, databaseName, targetTableName, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    if (!schemaMatchesTable(targetSchema, currentTable)) {
        result.errorMessage = QStringLiteral("table schema does not match expected schema");
        return result;
    }

    bool rowIdsInitialized = false;
    QStringList currentRowIds = loadRowIdsForTargetTable(databaseName,
                                                         targetTableKind,
                                                         targetTableName,
                                                         currentTable,
                                                         &rowIdsInitialized,
                                                         &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }
    if (rowIdsInitialized && targetTableKind == TargetTableKind::TableDat) {
        CurrentDatabaseGuard databaseGuard(databaseName);
        if (!rebuildTableIndexes(targetTableName, targetSchema, currentTable, currentRowIds, &error)) {
            result.errorMessage = error;
            return result;
        }
    }

    QList<int> matchedRowIndexes;
    for (int rowIndex = 0; rowIndex < currentTable.rows.size(); ++rowIndex) {
        QString matchError;
        if (!rowMatchesConditions(currentTable.rows.at(rowIndex), currentTable, simpleConditions, &matchError)) {
            if (!matchError.isEmpty()) {
                result.errorMessage = matchError;
                return result;
            }
            continue;
        }
        matchedRowIndexes.append(rowIndex);
    }

    if (matchedRowIndexes.isEmpty()) {
        result.success = true;
        return result;
    }

    repo::TableData candidateTable = deleteMatchedRows(currentTable, matchedRowIndexes);
    QStringList candidateRowIds = currentRowIds;
    for (int index = matchedRowIndexes.size() - 1; index >= 0; --index) {
        const int rowIndex = matchedRowIndexes.at(index);
        if (rowIndex >= 0 && rowIndex < candidateRowIds.size()) {
            candidateRowIds.removeAt(rowIndex);
        }
    }

    if (validationMode == ValidationMode::UserData
        && targetTableKind == TargetTableKind::TableDat) {
        QMap<QString, TableMutationState> mutationStates;
        TableMutationState rootState;
        rootState.databaseName = databaseName;
        rootState.tableName = targetTableName;
        rootState.schema = targetSchema;
        rootState.originalTable = currentTable;
        rootState.candidateTable = candidateTable;
        rootState.originalRowIds = currentRowIds;
        rootState.candidateRowIds = candidateRowIds;
        rootState.dirty = true;
        mutationStates.insert(tableMutationKey(databaseName, targetTableName), rootState);

        if (!validateMutationStateLocally(rootState,
                                          matchedRowIndexes,
                                          &mutationStates,
                                          false,
                                          &error)) {
            result.errorMessage = error;
            return result;
        }

        if (!validateIncomingForeignKeys(databaseName,
                                         currentDataRoot,
                                         targetTableName,
                                         candidateTable,
                                         ReferencedMutationKind::Delete,
                                         &error)) {
            result.errorMessage = error;
            return result;
        }

        const ForeignKeyCascadePlan cascadePlan =
            planForeignKeyCascade(databaseName, targetTableName, &error);
        if (!error.isEmpty()) {
            result.errorMessage = error;
            return result;
        }

        QSet<QString> visited;
        if (!applyForeignKeyCascade(databaseName,
                                    targetTableName,
                                    currentTable,
                                    candidateTable,
                                    matchedRowIndexes,
                                    ReferencedMutationKind::Delete,
                                    cascadePlan,
                                    &mutationStates,
                                    &visited,
                                    &error)) {
            result.errorMessage = error;
            return result;
        }

        if (!validateAllMutationStates(mutationStates, &error)) {
            result.errorMessage = error;
            return result;
        }

        if (!commitMutationStates(mutationStates, &error)) {
            result.errorMessage = error;
            return result;
        }

        result.success = true;
        result.affectedRowCount = matchedRowIndexes.size();
        return result;
    }

    if (validationMode == ValidationMode::UserData) {
        if (targetTableKind == TargetTableKind::TableDat) {
            QString indexError;
            validateChangedRowsAgainstUniqueIndexes(databaseName,
                                                    targetTableName,
                                                    targetSchema,
                                                    candidateTable,
                                                    candidateRowIds,
                                                    matchedRowIndexes,
                                                    &indexError);
            if (!indexError.isEmpty()) {
                result.errorMessage = indexError;
                return result;
            }
        }

        if (!checkKeyUniqueness(databaseName, targetSchema, candidateTable, &error)) {
            result.errorMessage = error;
            return result;
        }
        if (targetTableKind == TargetTableKind::TableDat
            && !validateOutgoingForeignKeys(databaseName, currentDataRoot, targetSchema, candidateTable, &error)) {
            result.errorMessage = error;
            return result;
        }
    }

    const repo::RepositoryResult writeResult =
        writeTargetTable(store, targetTableKind, databaseName, targetTableName, candidateTable);
    if (!writeResult.ok) {
        result.errorMessage = writeResult.error;
        return result;
    }

    if (targetTableKind == TargetTableKind::TableDat) {
        CurrentDatabaseGuard databaseGuard(databaseName);
        if (!saveUserTableRowIds(targetTableName, candidateRowIds, &error)) {
            writeTargetTable(store, targetTableKind, databaseName, targetTableName, currentTable);
            result.errorMessage = error;
            return result;
        }
        if (!deleteTableIndexes(targetTableName,
                                targetSchema,
                                currentTable,
                                currentRowIds,
                                matchedRowIndexes,
                                &error)) {
            writeTargetTable(store, targetTableKind, databaseName, targetTableName, currentTable);
            saveUserTableRowIds(targetTableName, currentRowIds, nullptr);
            rebuildTableIndexes(targetTableName, targetSchema, currentTable, currentRowIds, nullptr);
            result.errorMessage = error;
            return result;
        }
    }

    result.success = true;
    result.affectedRowCount = matchedRowIndexes.size();
    return result;
}

} // namespace service
