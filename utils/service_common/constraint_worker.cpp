#include "service_common.h"

#include <QDebug>
#include <QUuid>

namespace {

QStringList generateRowIds(int count)
{
    QStringList rowIds;
    rowIds.reserve(count);
    for (int index = 0; index < count; ++index) {
        rowIds.append(QUuid::createUuid().toString(QUuid::WithoutBraces));
    }
    return rowIds;
}

repo::TableData rowIdTable(const QStringList &rowIds)
{
    repo::TableData table;
    table.columns = {QStringLiteral("row_id")};
    table.rows.reserve(rowIds.size());
    for (const QString &rowId : rowIds) {
        table.rows.append(repo::TableRow{rowId});
    }
    return table;
}

QStringList keyValuesForIndexRow(const repo::TableData &table,
                                 const tabledef::IndexMeta &index,
                                 int rowIndex,
                                 QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    if (rowIndex < 0 || rowIndex >= table.rows.size()) {
        if (error != nullptr) {
            *error = QStringLiteral("row index %1 is out of range").arg(rowIndex);
        }
        return {};
    }

    const repo::TableRow &row = table.rows.at(rowIndex);
    QStringList values;
    values.reserve(index.columnNames.size());
    for (const QString &columnName : index.columnNames) {
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

QString rowLocatorForIndex(const QStringList &rowIds, int rowIndex, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    if (rowIndex < 0 || rowIndex >= rowIds.size()) {
        if (error != nullptr) {
            *error = QStringLiteral("row id index %1 is out of range").arg(rowIndex);
        }
        return {};
    }

    return rowIds.at(rowIndex);
}

void logIndexMaintenance(const QString &message)
{
    qDebug().noquote() << QStringLiteral("[index-maintenance] %1").arg(message);
}

} // namespace

namespace service {

bool validateColumnDefinition(const ColumnDefinition &definition, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    if (definition.column.name.trimmed().isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("column name cannot be empty");
        }
        return false;
    }

    if (definition.referencedTable.trimmed().isEmpty()
        && !definition.referencedColumns.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("foreign key column '%1' is missing a referenced table")
                         .arg(definition.column.name);
        }
        return false;
    }

    if (!definition.referencedTable.trimmed().isEmpty()
        && definition.referencedColumns.size() != 1) {
        if (error != nullptr) {
            *error = QStringLiteral("foreign key column '%1' must reference exactly one column")
                         .arg(definition.column.name);
        }
        return false;
    }

    return true;
}

tabledef::Constraint makeConstraint(const QString &constraintName,
                                    tabledef::ConstraintType type,
                                    const QStringList &columns,
                                    const QString &referencedTable,
                                    const QStringList &referencedColumns,
                                    const QString &checkClause,
                                    const QString &indexName)
{
    return tabledef::Constraint{constraintName,
                                type,
                                columns,
                                referencedTable,
                                referencedColumns,
                                checkClause,
                                indexName};
}

QList<tabledef::Constraint> buildGeneratedConstraints(const ColumnDefinition &definition)
{
    QList<tabledef::Constraint> constraints;
    const QString columnName = definition.column.name;

    if (definition.primaryKey) {
        constraints.append(makeConstraint(generatedConstraintName(columnName, QStringLiteral("pk")),
                                          tabledef::ConstraintType::PrimaryKey,
                                          {columnName},
                                          QString(),
                                          {},
                                          QString(),
                                          generatedConstraintName(columnName, QStringLiteral("pk_idx"))));
    }
    if (definition.unique) {
        constraints.append(makeConstraint(generatedConstraintName(columnName, QStringLiteral("uq")),
                                          tabledef::ConstraintType::Unique,
                                          {columnName},
                                          QString(),
                                          {},
                                          QString(),
                                          generatedConstraintName(columnName, QStringLiteral("uq_idx"))));
    }
    if (!definition.referencedTable.trimmed().isEmpty()) {
        constraints.append(makeConstraint(generatedConstraintName(columnName, QStringLiteral("fk")),
                                          tabledef::ConstraintType::ForeignKey,
                                          {columnName},
                                          definition.referencedTable,
                                          definition.referencedColumns,
                                          QString(),
                                          QString()));
    }
    if (!definition.checkClause.trimmed().isEmpty()) {
        constraints.append(makeConstraint(generatedConstraintName(columnName, QStringLiteral("ck")),
                                          tabledef::ConstraintType::Check,
                                          {columnName},
                                          QString(),
                                          {},
                                          definition.checkClause,
                                          QString()));
    }

    return constraints;
}

QList<tabledef::IndexMeta> loadUserTableIndexes(const QString &tableName, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    const QString databaseName = normalizeDatabaseName(QString());
    repo::IndexRepo indexRepo(databaseName, tableName, currentDataRoot);
    return indexRepo.listIndexes(error);
}

tabledef::TableSchema loadUserTableSchema(const QString &tableName, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    tabledef::TableSchema schema;
    schema.tableName = tableName;

    const QString databaseName = normalizeDatabaseName(QString());
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

repo::TableData loadUserTableData(const QString &tableName, QString *error)
{
    const QString databaseName = normalizeDatabaseName(QString());
    repo::TableRepo tableRepo(databaseName, tableName, currentDataRoot);
    return tableRepo.readTable(error);
}

bool saveUserTableRowIds(const QString &tableName, const QStringList &rowIds, QString *error);

QStringList loadUserTableRowIds(const QString &tableName,
                                const repo::TableData &tableData,
                                bool *initialized,
                                QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    if (initialized != nullptr) {
        *initialized = false;
    }

    const QString databaseName = normalizeDatabaseName(QString());
    repo::FlatFileTableStore store(currentDataRoot);
    const QString path = store.getRowIdFilePath(databaseName, tableName);

    QString rowIdError;
    repo::TableData rowIdTable;
    if (store.exists(path)) {
        rowIdTable = store.readTable(path, &rowIdError);
    }

    const bool needsInitialization = !store.exists(path)
                                     || !rowIdError.isEmpty()
                                     || rowIdTable.columns != QStringList{QStringLiteral("row_id")}
                                     || rowIdTable.rows.size() != tableData.rows.size();
    if (!needsInitialization) {
        QStringList rowIds;
        rowIds.reserve(rowIdTable.rows.size());
        for (const repo::TableRow &row : rowIdTable.rows) {
            rowIds.append(row.value(0));
        }
        logIndexMaintenance(QStringLiteral("row id sidecar loaded for %1 (%2 rows)")
                                .arg(tableName)
                                .arg(rowIds.size()));
        return rowIds;
    }

    const QStringList rowIds = generateRowIds(tableData.rows.size());
    if (!saveUserTableRowIds(tableName, rowIds, error)) {
        return {};
    }
    if (initialized != nullptr) {
        *initialized = true;
    }
    logIndexMaintenance(QStringLiteral("row id sidecar initialized for %1 (%2 rows)")
                            .arg(tableName)
                            .arg(rowIds.size()));
    return rowIds;
}

bool saveUserTableRowIds(const QString &tableName, const QStringList &rowIds, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    const QString databaseName = normalizeDatabaseName(QString());
    repo::FlatFileTableStore store(currentDataRoot);
    const repo::RepositoryResult result = store.writeTable(store.getRowIdFilePath(databaseName, tableName), rowIdTable(rowIds));
    if (!result.ok && error != nullptr) {
        *error = result.error;
    }
    if (result.ok) {
        logIndexMaintenance(QStringLiteral("row id sidecar saved for %1 (%2 rows)")
                                .arg(tableName)
                                .arg(rowIds.size()));
    }
    return result.ok;
}

bool rebuildTableIndexes(const QString &tableName,
                         const tabledef::TableSchema &schema,
                         const repo::TableData &tableData,
                         const QStringList &rowIds,
                         QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    const QString databaseName = normalizeDatabaseName(QString());
    logIndexMaintenance(QStringLiteral("rebuild indexes for %1 (%2 indexes)")
                            .arg(tableName)
                            .arg(schema.indexes.size()));
    repo::IndexRepo indexRepo(databaseName, tableName, currentDataRoot);
    for (const tabledef::IndexMeta &index : schema.indexes) {
        repo::SortIndexRepo sortIndexRepo(databaseName, index.indexName, tableName, currentDataRoot);
        if (indexRepo.hasIndex(index.indexName, error)) {
            logIndexMaintenance(QStringLiteral("rebuild existing index %1 on %2")
                                    .arg(index.indexName, tableName));
            const repo::RepositoryResult rebuildResult = sortIndexRepo.rebuild(tableData, rowIds);
            if (!rebuildResult.ok) {
                if (error != nullptr) {
                    *error = rebuildResult.error;
                }
                return false;
            }
            continue;
        }

        logIndexMaintenance(QStringLiteral("create missing index %1 on %2")
                                .arg(index.indexName, tableName));
        const repo::RepositoryResult createResult = sortIndexRepo.createIndex(index, tableData, rowIds);
        if (!createResult.ok) {
            if (error != nullptr) {
                *error = createResult.error;
            }
            return false;
        }
    }

    return true;
}

bool insertTableIndexes(const QString &tableName,
                        const tabledef::TableSchema &schema,
                        const repo::TableData &tableData,
                        const QStringList &rowIds,
                        const QList<int> &insertedRowIndexes,
                        QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    const QString databaseName = normalizeDatabaseName(QString());
    for (const tabledef::IndexMeta &index : schema.indexes) {
        logIndexMaintenance(QStringLiteral("insert rows into index %1 for %2")
                                .arg(index.indexName, tableName));
        repo::SortIndexRepo sortIndexRepo(databaseName, index.indexName, tableName, currentDataRoot);
        for (int rowIndex : insertedRowIndexes) {
            QString keyError;
            const QStringList keyValues = keyValuesForIndexRow(tableData, index, rowIndex, &keyError);
            if (!keyError.isEmpty()) {
                if (error != nullptr) {
                    *error = keyError;
                }
                return false;
            }

            const QString rowLocator = rowLocatorForIndex(rowIds, rowIndex, &keyError);
            if (!keyError.isEmpty()) {
                if (error != nullptr) {
                    *error = keyError;
                }
                return false;
            }

            const repo::RepositoryResult result = sortIndexRepo.insertIndexEntry(keyValues, rowLocator);
            if (!result.ok) {
                if (error != nullptr) {
                    *error = result.error;
                }
                return false;
            }
        }
    }

    return true;
}

bool updateTableIndexes(const QString &tableName,
                        const tabledef::TableSchema &schema,
                        const repo::TableData &currentTable,
                        const repo::TableData &candidateTable,
                        const QStringList &rowIds,
                        const QList<int> &changedRowIndexes,
                        QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    const QString databaseName = normalizeDatabaseName(QString());
    for (const tabledef::IndexMeta &index : schema.indexes) {
        logIndexMaintenance(QStringLiteral("update rows in index %1 for %2")
                                .arg(index.indexName, tableName));
        repo::SortIndexRepo sortIndexRepo(databaseName, index.indexName, tableName, currentDataRoot);
        for (int rowIndex : changedRowIndexes) {
            QString keyError;
            const QStringList oldKeyValues = keyValuesForIndexRow(currentTable, index, rowIndex, &keyError);
            if (!keyError.isEmpty()) {
                if (error != nullptr) {
                    *error = keyError;
                }
                return false;
            }

            const QStringList newKeyValues = keyValuesForIndexRow(candidateTable, index, rowIndex, &keyError);
            if (!keyError.isEmpty()) {
                if (error != nullptr) {
                    *error = keyError;
                }
                return false;
            }

            const QString rowLocator = rowLocatorForIndex(rowIds, rowIndex, &keyError);
            if (!keyError.isEmpty()) {
                if (error != nullptr) {
                    *error = keyError;
                }
                return false;
            }

            const repo::RepositoryResult result = sortIndexRepo.updateIndexEntry(oldKeyValues,
                                                                                 newKeyValues,
                                                                                 rowLocator);
            if (!result.ok) {
                if (error != nullptr) {
                    *error = result.error;
                }
                return false;
            }
        }
    }

    return true;
}

bool deleteTableIndexes(const QString &tableName,
                        const tabledef::TableSchema &schema,
                        const repo::TableData &tableData,
                        const QStringList &rowIds,
                        const QList<int> &deletedRowIndexes,
                        QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    const QString databaseName = normalizeDatabaseName(QString());
    for (const tabledef::IndexMeta &index : schema.indexes) {
        logIndexMaintenance(QStringLiteral("delete rows from index %1 for %2")
                                .arg(index.indexName, tableName));
        repo::SortIndexRepo sortIndexRepo(databaseName, index.indexName, tableName, currentDataRoot);
        for (int rowIndex : deletedRowIndexes) {
            QString keyError;
            const QStringList keyValues = keyValuesForIndexRow(tableData, index, rowIndex, &keyError);
            if (!keyError.isEmpty()) {
                if (error != nullptr) {
                    *error = keyError;
                }
                return false;
            }

            const QString rowLocator = rowLocatorForIndex(rowIds, rowIndex, &keyError);
            if (!keyError.isEmpty()) {
                if (error != nullptr) {
                    *error = keyError;
                }
                return false;
            }

            const repo::RepositoryResult result = sortIndexRepo.deleteIndexEntry(keyValues, rowLocator);
            if (!result.ok) {
                if (error != nullptr) {
                    *error = result.error;
                }
                return false;
            }
        }
    }

    return true;
}

bool ensureConstraintBoundIndex(const QString &tableName,
                                const tabledef::Constraint &constraint,
                                const repo::TableData &tableData,
                                QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    if (!tabledef::isPrimaryKeyConstraint(constraint)
        && !tabledef::isUniqueConstraint(constraint)) {
        return true;
    }

    const QString databaseName = normalizeDatabaseName(QString());
    bool rowIdsInitialized = false;
    const QStringList rowIds = loadUserTableRowIds(tableName, tableData, &rowIdsInitialized, error);
    if (error != nullptr && !error->isEmpty()) {
        return false;
    }
    if (rowIdsInitialized) {
        const tabledef::TableSchema schema = loadUserTableSchema(tableName, error);
        if (error != nullptr && !error->isEmpty()) {
            return false;
        }
        if (!rebuildTableIndexes(tableName, schema, tableData, rowIds, error)) {
            return false;
        }
    }

    logIndexMaintenance(QStringLiteral("ensure bound index for %1")
                            .arg(constraint.name));
    repo::IndexRepo indexRepo(databaseName, tableName, currentDataRoot);
    const QString boundIndexName = constraint.indexName.trimmed().isEmpty()
                                       ? generatedConstraintName(constraint.name, QStringLiteral("idx"))
                                       : constraint.indexName;
    const tabledef::IndexMeta indexMeta{boundIndexName, constraint.columns, tabledef::isUniqueConstraint(constraint)};

    if (indexRepo.hasIndex(boundIndexName, error)) {
        logIndexMaintenance(QStringLiteral("bound index exists, rebuild %1")
                                .arg(boundIndexName));
        const repo::RepositoryResult rebuildResult = repo::SortIndexRepo(databaseName, boundIndexName, tableName, currentDataRoot)
                                                         .rebuild(tableData, rowIds);
        if (!rebuildResult.ok) {
            if (error != nullptr) {
                *error = rebuildResult.error;
            }
            return false;
        }
        return true;
    }

    const repo::RepositoryResult createResult = indexRepo.createIndex(indexMeta);
    if (!createResult.ok) {
        if (error != nullptr) {
            *error = createResult.error;
        }
        return false;
    }

    logIndexMaintenance(QStringLiteral("create bound index %1")
                            .arg(boundIndexName));
    const repo::RepositoryResult treeResult = repo::SortIndexRepo(databaseName, boundIndexName, tableName, currentDataRoot)
                                                 .createIndex(indexMeta, tableData, rowIds);
    if (!treeResult.ok) {
        indexRepo.deleteIndex(boundIndexName);
        if (error != nullptr) {
            *error = treeResult.error;
        }
        return false;
    }

    return true;
}

bool removeConstraintBoundIndex(const QString &tableName,
                               const QString &constraintName,
                               QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    const QString databaseName = normalizeDatabaseName(QString());
    QString schemaError;
    const tabledef::TableSchema schema = loadUserTableSchema(tableName, &schemaError);
    if (!schemaError.isEmpty()) {
        if (error != nullptr) {
            *error = schemaError;
        }
        return false;
    }

    for (const tabledef::Constraint &constraint : schema.constraints) {
        if (constraint.name != constraintName) {
            continue;
        }
        if (!tabledef::isPrimaryKeyConstraint(constraint)
            && !tabledef::isUniqueConstraint(constraint)) {
            return true;
        }

        const QString boundIndexName = constraint.indexName.trimmed().isEmpty()
                                           ? generatedConstraintName(constraint.name, QStringLiteral("idx"))
                                           : constraint.indexName;
        logIndexMaintenance(QStringLiteral("remove bound index %1")
                    .arg(boundIndexName));
        repo::IndexRepo indexRepo(databaseName, tableName, currentDataRoot);
        indexRepo.deleteIndex(boundIndexName);
        repo::SortIndexRepo(databaseName, boundIndexName, tableName, currentDataRoot).dropIndex();
        return true;
    }

    if (error != nullptr) {
        *error = QStringLiteral("constraint '%1' does not exist").arg(constraintName);
    }
    return false;
}

} // namespace service