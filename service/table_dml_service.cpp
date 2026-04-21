#include "service.h"

#include <QSet>
#include <QUuid>

namespace {

using service::currentDataRoot;
using service::compositeKeySignature;
using service::validateScalarValue;

QString effectiveDatabaseName(const QString &targetDatabaseName)
{
    const QString trimmedTarget = targetDatabaseName.trimmed();
    if (!trimmedTarget.isEmpty()) {
        return trimmedTarget;
    }
    return service::currentDatabase.trimmed();
}

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

QStringList loadRowIdsForTargetTable(service::TargetTableKind targetTableKind,
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

    return service::loadUserTableRowIds(targetTableName, currentTable, rowIdsInitialized, error);
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

        repo::TableData parentTable = repo::TableRepo(databaseName, constraint.referencedTable, dataRoot)
                                          .readTable(error);
        if (error != nullptr && !error->isEmpty()) {
            return false;
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

        repo::TableData childTable = repo::TableRepo(databaseName, tableEntry.name, dataRoot)
                                         .readTable(&constraintError);
        if (!constraintError.isEmpty()) {
            if (error != nullptr) {
                *error = constraintError;
            }
            return false;
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
    QStringList currentRowIds = loadRowIdsForTargetTable(targetTableKind,
                                                         targetTableName,
                                                         currentTable,
                                                         &rowIdsInitialized,
                                                         &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }
    if (rowIdsInitialized && targetTableKind == TargetTableKind::TableDat) {
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
    QStringList currentRowIds = loadRowIdsForTargetTable(targetTableKind,
                                                         targetTableName,
                                                         currentTable,
                                                         &rowIdsInitialized,
                                                         &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }
    if (rowIdsInitialized && targetTableKind == TargetTableKind::TableDat) {
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
        if (targetTableKind == TargetTableKind::TableDat
            && !validateIncomingForeignKeys(databaseName,
                                            currentDataRoot,
                                            targetTableName,
                                            candidateTable,
                                            &error)) {
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
    QStringList currentRowIds = loadRowIdsForTargetTable(targetTableKind,
                                                         targetTableName,
                                                         currentTable,
                                                         &rowIdsInitialized,
                                                         &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }
    if (rowIdsInitialized && targetTableKind == TargetTableKind::TableDat) {
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
        if (targetTableKind == TargetTableKind::TableDat
            && !validateIncomingForeignKeys(databaseName,
                                            currentDataRoot,
                                            targetTableName,
                                            candidateTable,
                                            &error)) {
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