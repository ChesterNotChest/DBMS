#include "service.h"

#include <QSet>

namespace {

using service::currentDataRoot;

bool valueFitsColumnDefinition(const tabledef::Column &column, const QString &value, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    if (value.isEmpty()) {
        if (column.notNull && column.defaultValue.isEmpty()) {
            if (error != nullptr) {
                *error = QStringLiteral("column '%1' cannot be null").arg(column.name);
            }
            return false;
        }
        return true;
    }

    switch (column.type) {
    case tabledef::ColumnType::Int: {
        bool ok = false;
        value.toLongLong(&ok);
        if (!ok) {
            if (error != nullptr) {
                *error = QStringLiteral("value '%1' cannot be converted to INT").arg(value);
            }
            return false;
        }
        return true;
    }
    case tabledef::ColumnType::Float: {
        bool ok = false;
        value.toDouble(&ok);
        if (!ok) {
            if (error != nullptr) {
                *error = QStringLiteral("value '%1' cannot be converted to FLOAT").arg(value);
            }
            return false;
        }
        return true;
    }
    case tabledef::ColumnType::Varchar:
        if (column.length > 0 && value.size() > column.length) {
            if (error != nullptr) {
                *error = QStringLiteral("value '%1' exceeds VARCHAR length %2")
                             .arg(value)
                             .arg(column.length);
            }
            return false;
        }
        return true;
    }

    return true;
}

bool rebuildModifiedColumnRows(const tabledef::Column &newColumn,
                               int columnIndex,
                               repo::TableData *table,
                               QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    if (table == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("table output pointer cannot be null");
        }
        return false;
    }

    for (repo::TableRow &row : table->rows) {
        if (columnIndex >= row.size()) {
            if (error != nullptr) {
                *error = QStringLiteral("row width does not match column count");
            }
            return false;
        }

        QString value = row.value(columnIndex);
        if (value.isEmpty() && !newColumn.defaultValue.isEmpty()) {
            value = newColumn.defaultValue;
        }

        if (!valueFitsColumnDefinition(newColumn, value, error)) {
            return false;
        }

        row[columnIndex] = value;
    }

    return true;
}

QString boundIndexNameForConstraint(const tabledef::Constraint &constraint)
{
    if (!constraint.indexName.trimmed().isEmpty()) {
        return constraint.indexName.trimmed();
    }
    return service::generatedConstraintName(constraint.name, QStringLiteral("idx"));
}

tabledef::IndexMeta indexMetaForConstraint(const tabledef::Constraint &constraint)
{
    return tabledef::IndexMeta{boundIndexNameForConstraint(constraint),
                               constraint.columns,
                               tabledef::isPrimaryKeyConstraint(constraint)
                                   || tabledef::isUniqueConstraint(constraint)};
}

QList<tabledef::IndexMeta> indexesTouchingColumn(const tabledef::TableSchema &schema,
                                                 const QString &columnName)
{
    QList<tabledef::IndexMeta> indexes;
    for (const tabledef::IndexMeta &index : schema.indexes) {
        if (index.columnNames.contains(columnName)) {
            indexes.append(index);
        }
    }
    return indexes;
}

bool hasIncomingForeignKeyReferenceToColumn(const QString &databaseName,
                                            const QString &targetTableName,
                                            const QString &targetColumnName,
                                            QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    repo::TabRepo tabRepo(databaseName, currentDataRoot);
    QString tabError;
    const QList<repo::TableEntry> tableEntries = tabRepo.listTables(&tabError);
    if (!tabError.isEmpty()) {
        if (error != nullptr) {
            *error = tabError;
        }
        return true;
    }

    for (const repo::TableEntry &tableEntry : tableEntries) {
        if (tableEntry.name == targetTableName) {
            continue;
        }

        repo::ConstraintRepo constraintRepo(databaseName, tableEntry.name, currentDataRoot);
        const QList<tabledef::Constraint> constraints = constraintRepo.listConstraints(&tabError);
        if (!tabError.isEmpty()) {
            if (error != nullptr) {
                *error = tabError;
            }
            return true;
        }

        for (const tabledef::Constraint &constraint : constraints) {
            if (!tabledef::isForeignKeyConstraint(constraint)
                || constraint.referencedTable != targetTableName) {
                continue;
            }

            if (constraint.referencedColumns.contains(targetColumnName)) {
                if (error != nullptr) {
                    *error = QStringLiteral("column '%1' is referenced by foreign key '%2' from table '%3'")
                                 .arg(targetColumnName, constraint.name, tableEntry.name);
                }
                return true;
            }
        }
    }

    return false;
}

bool rebuildIndexesForTable(const QString &tableName,
                            const tabledef::TableSchema &schema,
                            const repo::TableData &tableData,
                            QString *error)
{
    const QStringList rowIds = service::loadUserTableRowIds(tableName, tableData, nullptr, error);
    if (error != nullptr && !error->isEmpty()) {
        return false;
    }
    return service::rebuildTableIndexes(tableName, schema, tableData, rowIds, error);
}

} // namespace

namespace service::table_service {

TaskResult createTable(const QString &tableName,
                       const tabledef::TableSchema &schema)
{
    TaskResult result;
    const QString normalizedDatabaseName = normalizeDatabaseName(QString());
    if (normalizedDatabaseName.isEmpty()) {
        result.errorMessage = QStringLiteral("database name cannot be empty");
        return result;
    }
    if (tableName.trimmed().isEmpty()) {
        result.errorMessage = QStringLiteral("table name cannot be empty");
        return result;
    }

    tabledef::TableSchema normalizedSchema = schema;
    normalizedSchema.tableName = tableName;

    QString error;
    if (!tabledef::validateConstraintDefinitions(normalizedSchema, QString(), &error)) {
        result.errorMessage = error;
        return result;
    }
    if (!tabledef::validateConstraintRows(normalizedDatabaseName,
                                          currentDataRoot,
                                          normalizedSchema,
                                          tabledef::schemaColumnNames(normalizedSchema),
                                          QList<QStringList>(),
                                          &error)) {
        result.errorMessage = error;
        return result;
    }

    repo::TabRepo tabRepo(normalizedDatabaseName, currentDataRoot);
    const repo::RepositoryResult tabReady = tabRepo.initialize();
    if (!tabReady.ok) {
        result.errorMessage = tabReady.error;
        return result;
    }

    if (tabRepo.hasTable(tableName, &error)) {
        result.errorMessage = QStringLiteral("table '%1' already exists").arg(tableName);
        return result;
    }
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    repo::MetaRepo metaRepo(normalizedDatabaseName, tableName, currentDataRoot);
    repo::ConstraintRepo constraintRepo(normalizedDatabaseName, tableName, currentDataRoot);
    repo::TableRepo tableRepo(normalizedDatabaseName, tableName, currentDataRoot);

    tabledef::TableSchema storedSchema = normalizedSchema;
    for (tabledef::Constraint &constraint : storedSchema.constraints) {
        if ((tabledef::isPrimaryKeyConstraint(constraint) || tabledef::isUniqueConstraint(constraint))
            && constraint.indexName.trimmed().isEmpty()) {
            constraint.indexName = boundIndexNameForConstraint(constraint);
        }
    }

    const repo::RepositoryResult tableCreated = tableRepo.createTable(tabledef::schemaColumnNames(storedSchema));
    if (!tableCreated.ok) {
        result.errorMessage = tableCreated.error;
        return result;
    }

    for (const tabledef::Column &column : normalizedSchema.columns) {
        const repo::RepositoryResult columnResult = metaRepo.createColumn(column);
        if (!columnResult.ok) {
            tableRepo.dropTable();
            result.errorMessage = columnResult.error;
            return result;
        }
    }

    for (const tabledef::Constraint &constraint : storedSchema.constraints) {
        const repo::RepositoryResult constraintResult = constraintRepo.createConstraint(constraint);
        if (!constraintResult.ok) {
            tableRepo.dropTable();
            result.errorMessage = constraintResult.error;
            return result;
        }
    }

    const repo::RepositoryResult tabResult = tabRepo.createTableEntry(tableName);
    if (!tabResult.ok) {
        tableRepo.dropTable();
        result.errorMessage = tabResult.error;
        return result;
    }

    if (!saveUserTableRowIds(tableName, QStringList(), &error)) {
        tableRepo.dropTable();
        result.errorMessage = error;
        return result;
    }

    repo::TableData emptyTable;
    emptyTable.columns = tabledef::schemaColumnNames(storedSchema);
    for (const tabledef::Constraint &constraint : storedSchema.constraints) {
        if (!tabledef::isPrimaryKeyConstraint(constraint) && !tabledef::isUniqueConstraint(constraint)) {
            continue;
        }

        if (!ensureConstraintBoundIndex(tableName, constraint, emptyTable, &error)) {
            tableRepo.dropTable();
            result.errorMessage = error;
            return result;
        }
    }

    result.success = true;
    return result;
}

TaskResult dropTable(const QString &tableName)
{
    TaskResult result;
    const QString normalizedDatabaseName = normalizeDatabaseName(QString());
    if (normalizedDatabaseName.isEmpty()) {
        result.errorMessage = QStringLiteral("database name cannot be empty");
        return result;
    }
    if (tableName.trimmed().isEmpty()) {
        result.errorMessage = QStringLiteral("table name cannot be empty");
        return result;
    }

    repo::TabRepo tabRepo(normalizedDatabaseName, currentDataRoot);
    QString error;
    if (!tabRepo.hasTable(tableName, &error)) {
        result.errorMessage = error.isEmpty()
                                 ? QStringLiteral("table '%1' does not exist").arg(tableName)
                                 : error;
        return result;
    }

    if (!tabledef::validateNoIncomingForeignKeyReferences(normalizedDatabaseName,
                                                           currentDataRoot,
                                                           tableName,
                                                           &error)) {
        result.errorMessage = error;
        return result;
    }

    repo::TableRepo tableRepo(normalizedDatabaseName, tableName, currentDataRoot);
    repo::MetaRepo metaRepo(normalizedDatabaseName, tableName, currentDataRoot);
    repo::ConstraintRepo constraintRepo(normalizedDatabaseName, tableName, currentDataRoot);

    const repo::RepositoryResult removeTabEntry = tabRepo.deleteTableEntry(tableName);
    if (!removeTabEntry.ok) {
        result.errorMessage = removeTabEntry.error;
        return result;
    }

    metaRepo.deleteColumn(QString());
    constraintRepo.deleteConstraint(QString());

    const repo::RepositoryResult dropResult = tableRepo.dropTable();
    if (!dropResult.ok) {
        result.errorMessage = dropResult.error;
        return result;
    }

    result.success = true;
    return result;
}

TaskResult addColumn(const QString &tableName,
                     const ColumnDefinition &definition)
{
    TaskResult result;
    const QString normalizedDatabaseName = normalizeDatabaseName(QString());
    if (normalizedDatabaseName.isEmpty()) {
        result.errorMessage = QStringLiteral("database name cannot be empty");
        return result;
    }

    QString error;
    if (!validateColumnDefinition(definition, &error)) {
        result.errorMessage = error;
        return result;
    }

    tabledef::TableSchema schema = loadUserTableSchema(tableName, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }
    if (tabledef::hasColumn(schema, definition.column.name)) {
        result.errorMessage = QStringLiteral("column '%1' already exists").arg(definition.column.name);
        return result;
    }

    const QList<tabledef::Constraint> generatedConstraints = buildGeneratedConstraints(definition);
    tabledef::TableSchema candidateSchema = schema;
    candidateSchema.columns.append(definition.column);
    for (const tabledef::Constraint &constraint : generatedConstraints) {
        candidateSchema.constraints.append(constraint);
    }

    repo::TableData table = loadUserTableData(tableName, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    table.columns.append(definition.column.name);
    for (repo::TableRow &row : table.rows) {
        row.append(definition.column.defaultValue);
    }

    if (!tabledef::validateConstraintDefinitions(candidateSchema, QString(), &error)) {
        result.errorMessage = error;
        return result;
    }
    if (!tabledef::validateConstraintRows(normalizedDatabaseName,
                                          currentDataRoot,
                                          candidateSchema,
                                          table.columns,
                                          table.rows,
                                          &error)) {
        result.errorMessage = error;
        return result;
    }

    repo::MetaRepo metaRepo(normalizedDatabaseName, tableName, currentDataRoot);
    repo::ConstraintRepo constraintRepo(normalizedDatabaseName, tableName, currentDataRoot);
    repo::TableRepo tableRepo(normalizedDatabaseName, tableName, currentDataRoot);

    const repo::RepositoryResult columnResult = metaRepo.createColumn(definition.column);
    if (!columnResult.ok) {
        result.errorMessage = columnResult.error;
        return result;
    }

    for (const tabledef::Constraint &constraint : generatedConstraints) {
        const repo::RepositoryResult constraintResult = constraintRepo.createConstraint(constraint);
        if (!constraintResult.ok) {
            for (const tabledef::Constraint &createdConstraint : generatedConstraints) {
                if (createdConstraint.name == constraint.name) {
                    break;
                }
                if (tabledef::isPrimaryKeyConstraint(createdConstraint)
                    || tabledef::isUniqueConstraint(createdConstraint)) {
                    removeConstraintBoundIndex(tableName, createdConstraint.name, nullptr);
                }
                constraintRepo.deleteConstraint(createdConstraint.name);
            }
            metaRepo.deleteColumn(definition.column.name);
            result.errorMessage = constraintResult.error;
            return result;
        }
    }

    for (const tabledef::Constraint &constraint : generatedConstraints) {
        if (!tabledef::isPrimaryKeyConstraint(constraint)
            && !tabledef::isUniqueConstraint(constraint)) {
            continue;
        }
        if (!ensureConstraintBoundIndex(tableName, constraint, table, &error)) {
            for (const tabledef::Constraint &createdConstraint : generatedConstraints) {
                if (tabledef::isPrimaryKeyConstraint(createdConstraint)
                    || tabledef::isUniqueConstraint(createdConstraint)) {
                    removeConstraintBoundIndex(tableName, createdConstraint.name, nullptr);
                }
                constraintRepo.deleteConstraint(createdConstraint.name);
            }
            metaRepo.deleteColumn(definition.column.name);
            result.errorMessage = error;
            return result;
        }
    }

    const repo::RepositoryResult writeResult = tableRepo.replaceTable(table);
    if (!writeResult.ok) {
        for (const tabledef::Constraint &createdConstraint : generatedConstraints) {
            if (tabledef::isPrimaryKeyConstraint(createdConstraint)
                || tabledef::isUniqueConstraint(createdConstraint)) {
                removeConstraintBoundIndex(tableName, createdConstraint.name, nullptr);
            }
            constraintRepo.deleteConstraint(createdConstraint.name);
        }
        metaRepo.deleteColumn(definition.column.name);
        result.errorMessage = writeResult.error;
        return result;
    }

    result.success = true;
    return result;
}

TaskResult deleteColumn(const QString &tableName,
                        const QString &columnName)
{
    TaskResult result;
    const QString normalizedDatabaseName = normalizeDatabaseName(QString());
    if (normalizedDatabaseName.isEmpty()) {
        result.errorMessage = QStringLiteral("database name cannot be empty");
        return result;
    }

    tabledef::TableSchema schema = loadUserTableSchema(tableName, &result.errorMessage);
    if (!result.errorMessage.isEmpty()) {
        return result;
    }
    repo::ConstraintRepo constraintRepo(normalizedDatabaseName, tableName, currentDataRoot);
    const QList<tabledef::Constraint> constraints = constraintRepo.listConstraints(&result.errorMessage);
    if (!result.errorMessage.isEmpty()) {
        return result;
    }

    const int columnIndex = tabledef::findColumnIndex(schema, columnName);
    if (columnIndex < 0) {
        result.errorMessage = QStringLiteral("column '%1' does not exist").arg(columnName);
        return result;
    }

    const tabledef::Column originalColumn = schema.columns.at(columnIndex);

    for (const tabledef::Constraint &constraint : constraints) {
        if (!tabledef::constraintTouchesColumn(constraint, columnName)) {
            continue;
        }

        if (tabledef::isPrimaryKeyConstraint(constraint)) {
            result.errorMessage = QStringLiteral("column '%1' is part of primary key '%2'")
                                      .arg(columnName, constraint.name);
            return result;
        }
        if (tabledef::isUniqueConstraint(constraint)) {
            result.errorMessage = QStringLiteral("column '%1' is part of unique constraint '%2'")
                                      .arg(columnName, constraint.name);
            return result;
        }
        if (tabledef::isForeignKeyConstraint(constraint)) {
            result.errorMessage = QStringLiteral("column '%1' is part of foreign key '%2'")
                                      .arg(columnName, constraint.name);
            return result;
        }
    }

    const QList<tabledef::IndexMeta> indexes = schema.indexes;
    for (const tabledef::IndexMeta &index : indexes) {
        if (!index.columnNames.contains(columnName)) {
            continue;
        }
        result.errorMessage = QStringLiteral("column '%1' is referenced by index '%2'")
                                  .arg(columnName, index.indexName);
        return result;
    }

    QString incomingFkError;
    if (hasIncomingForeignKeyReferenceToColumn(normalizedDatabaseName,
                                               tableName,
                                               columnName,
                                               &incomingFkError)) {
        result.errorMessage = incomingFkError;
        return result;
    }

    repo::MetaRepo metaRepo(normalizedDatabaseName, tableName, currentDataRoot);
    repo::TableRepo tableRepo(normalizedDatabaseName, tableName, currentDataRoot);
    repo::TableData table = loadUserTableData(tableName, &result.errorMessage);
    if (!result.errorMessage.isEmpty()) {
        return result;
    }
    const repo::TableData originalTable = table;

    if (columnIndex >= table.columns.size()) {
        result.errorMessage = QStringLiteral("column '%1' does not exist").arg(columnName);
        return result;
    }

    table.columns.removeAt(columnIndex);
    for (repo::TableRow &row : table.rows) {
        if (columnIndex < row.size()) {
            row.removeAt(columnIndex);
        }
    }

    const repo::RepositoryResult metaResult = metaRepo.deleteColumn(columnName);
    if (!metaResult.ok) {
        result.errorMessage = metaResult.error;
        return result;
    }

    const repo::RepositoryResult tableResult = tableRepo.replaceTable(table);
    if (!tableResult.ok) {
        metaRepo.createColumn(originalColumn);
        result.errorMessage = tableResult.error;
        return result;
    }

    tabledef::TableSchema updatedSchema = loadUserTableSchema(tableName, &result.errorMessage);
    if (!result.errorMessage.isEmpty()) {
        return result;
    }
    if (!rebuildIndexesForTable(tableName, updatedSchema, table, &result.errorMessage)) {
        tableRepo.replaceTable(originalTable);
        metaRepo.createColumn(originalColumn);
        rebuildIndexesForTable(tableName, schema, originalTable, nullptr);
        return result;
    }

    result.success = true;
    return result;
}

TaskResult modifyColumn(const QString &tableName,
                        const QString &columnName,
                        const ColumnDefinition &definition)
{
    TaskResult result;
    const QString normalizedDatabaseName = normalizeDatabaseName(QString());
    if (normalizedDatabaseName.isEmpty()) {
        result.errorMessage = QStringLiteral("database name cannot be empty");
        return result;
    }

    QString error;
    if (!validateColumnDefinition(definition, &error)) {
        result.errorMessage = error;
        return result;
    }

    tabledef::TableSchema schema = loadUserTableSchema(tableName, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    const int columnIndex = tabledef::findColumnIndex(schema, columnName);
    if (columnIndex < 0) {
        result.errorMessage = QStringLiteral("column '%1' does not exist").arg(columnName);
        return result;
    }

    const tabledef::Column originalColumn = schema.columns.at(columnIndex);
    const bool isRename = definition.column.name.trimmed() != columnName.trimmed();
    if (isRename && tabledef::hasColumn(schema, definition.column.name)) {
        result.errorMessage = QStringLiteral("column '%1' already exists").arg(definition.column.name);
        return result;
    }

    tabledef::TableSchema candidateSchema = schema;
    candidateSchema.columns[columnIndex] = definition.column;
    candidateSchema.constraints.removeIf(
        [&](const tabledef::Constraint &constraint) {
            return tabledef::constraintTouchesColumn(constraint, columnName);
        });

    const QList<tabledef::Constraint> generatedConstraints = buildGeneratedConstraints(definition);
    for (const tabledef::Constraint &constraint : generatedConstraints) {
        candidateSchema.constraints.append(constraint);
    }

    if (!tabledef::validateConstraintDefinitions(candidateSchema, QString(), &error)) {
        result.errorMessage = error;
        return result;
    }

    repo::MetaRepo metaRepo(normalizedDatabaseName, tableName, currentDataRoot);
    repo::ConstraintRepo constraintRepo(normalizedDatabaseName, tableName, currentDataRoot);
    repo::TableRepo tableRepo(normalizedDatabaseName, tableName, currentDataRoot);

    repo::TableData table = loadUserTableData(tableName, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }
    const repo::TableData originalTable = table;

    const QStringList rowIds = loadUserTableRowIds(tableName, table, nullptr, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    if (columnIndex >= table.columns.size()) {
        result.errorMessage = QStringLiteral("column '%1' does not exist").arg(columnName);
        return result;
    }

    if (!rebuildModifiedColumnRows(definition.column, columnIndex, &table, &error)) {
        result.errorMessage = error;
        return result;
    }

    table.columns[columnIndex] = definition.column.name;

    if (!tabledef::validateConstraintRows(normalizedDatabaseName,
                                          currentDataRoot,
                                          candidateSchema,
                                          table.columns,
                                          table.rows,
                                          &error)) {
        result.errorMessage = error;
        return result;
    }

    const repo::RepositoryResult updateMetaResult = metaRepo.updateColumn(columnName, definition.column);
    if (!updateMetaResult.ok) {
        result.errorMessage = updateMetaResult.error;
        return result;
    }

    const QList<tabledef::Constraint> touchedConstraints = schema.constraints;
    QList<tabledef::Constraint> removedConstraints;
    auto restoreModifiedColumnState = [&](const QString &message) {
        tableRepo.replaceTable(originalTable);
        metaRepo.updateColumn(definition.column.name, originalColumn);
        for (const tabledef::Constraint &removedConstraint : removedConstraints) {
            constraintRepo.createConstraint(removedConstraint);
            if (tabledef::isPrimaryKeyConstraint(removedConstraint)
                || tabledef::isUniqueConstraint(removedConstraint)) {
                ensureConstraintBoundIndex(tableName, removedConstraint, originalTable, nullptr);
            }
        }
        result.errorMessage = message;
    };
    for (const tabledef::Constraint &constraint : touchedConstraints) {
        if (!tabledef::constraintTouchesColumn(constraint, columnName)) {
            continue;
        }
        const repo::RepositoryResult deleteConstraintResult = constraintRepo.deleteConstraint(constraint.name);
        if (!deleteConstraintResult.ok) {
            tableRepo.replaceTable(originalTable);
            metaRepo.updateColumn(definition.column.name, originalColumn);
            for (const tabledef::Constraint &removedConstraint : removedConstraints) {
                constraintRepo.createConstraint(removedConstraint);
                if (tabledef::isPrimaryKeyConstraint(removedConstraint)
                    || tabledef::isUniqueConstraint(removedConstraint)) {
                    ensureConstraintBoundIndex(tableName, removedConstraint, originalTable, nullptr);
                }
            }
            result.errorMessage = deleteConstraintResult.error;
            return result;
        }
        removedConstraints.append(constraint);
    }

    const QList<tabledef::IndexMeta> indexes = indexesTouchingColumn(schema, columnName);
    repo::IndexRepo indexRepo(normalizedDatabaseName, tableName, currentDataRoot);
    for (const tabledef::IndexMeta &index : indexes) {
        tabledef::IndexMeta updatedIndex = index;
        for (QString &column : updatedIndex.columnNames) {
            if (column == columnName) {
                column = definition.column.name;
            }
        }

        QString indexError;
        if (!indexRepo.hasIndex(index.indexName, &indexError)) {
            if (!indexError.isEmpty()) {
                restoreModifiedColumnState(indexError);
                return result;
            }
            continue;
        }

        repo::SortIndexRepo sortIndexRepo(normalizedDatabaseName, index.indexName, tableName, currentDataRoot);
        if (isRename) {
            if (updatedIndex.indexName == index.indexName) {
                const repo::RepositoryResult dropResult = sortIndexRepo.dropIndex();
                if (!dropResult.ok) {
                    restoreModifiedColumnState(dropResult.error);
                    return result;
                }

                const repo::RepositoryResult recreateResult = sortIndexRepo.createIndex(updatedIndex, table, rowIds);
                if (!recreateResult.ok) {
                    sortIndexRepo.createIndex(index, table, rowIds);
                    restoreModifiedColumnState(recreateResult.error);
                    return result;
                }

                const repo::RepositoryResult updateIndexResult = indexRepo.updateIndex(index.indexName, updatedIndex);
                if (!updateIndexResult.ok) {
                    sortIndexRepo.dropIndex();
                    sortIndexRepo.createIndex(index, table, rowIds);
                    restoreModifiedColumnState(updateIndexResult.error);
                    return result;
                }
            } else {
                repo::SortIndexRepo renamedSortIndexRepo(normalizedDatabaseName,
                                                         updatedIndex.indexName,
                                                         tableName,
                                                         currentDataRoot);
                const repo::RepositoryResult recreateResult = renamedSortIndexRepo.createIndex(updatedIndex, table, rowIds);
                if (!recreateResult.ok) {
                    restoreModifiedColumnState(recreateResult.error);
                    return result;
                }

                const repo::RepositoryResult updateIndexResult = indexRepo.updateIndex(index.indexName, updatedIndex);
                if (!updateIndexResult.ok) {
                    renamedSortIndexRepo.dropIndex();
                    restoreModifiedColumnState(updateIndexResult.error);
                    return result;
                }

                const repo::RepositoryResult dropResult = sortIndexRepo.dropIndex();
                if (!dropResult.ok) {
                    renamedSortIndexRepo.dropIndex();
                    indexRepo.updateIndex(updatedIndex.indexName, index);
                    restoreModifiedColumnState(dropResult.error);
                    return result;
                }
            }
            continue;
        }

        const repo::RepositoryResult updateIndexResult = indexRepo.updateIndex(index.indexName, updatedIndex);
        if (!updateIndexResult.ok) {
            restoreModifiedColumnState(updateIndexResult.error);
            return result;
        }
    }

    for (const tabledef::Constraint &constraint : generatedConstraints) {
        const repo::RepositoryResult createConstraintResult = constraintRepo.createConstraint(constraint);
        if (!createConstraintResult.ok) {
            tableRepo.replaceTable(originalTable);
            metaRepo.updateColumn(definition.column.name, originalColumn);
            for (const tabledef::Constraint &removedConstraint : removedConstraints) {
                constraintRepo.createConstraint(removedConstraint);
                if (tabledef::isPrimaryKeyConstraint(removedConstraint)
                    || tabledef::isUniqueConstraint(removedConstraint)) {
                    ensureConstraintBoundIndex(tableName, removedConstraint, originalTable, nullptr);
                }
            }
            result.errorMessage = createConstraintResult.error;
            return result;
        }
        if ((tabledef::isPrimaryKeyConstraint(constraint) || tabledef::isUniqueConstraint(constraint))
            && !ensureConstraintBoundIndex(tableName, constraint, table, &error)) {
            tableRepo.replaceTable(originalTable);
            metaRepo.updateColumn(definition.column.name, originalColumn);
            for (const tabledef::Constraint &createdConstraint : generatedConstraints) {
                if (createdConstraint.name == constraint.name) {
                    break;
                }
                if (tabledef::isPrimaryKeyConstraint(createdConstraint)
                    || tabledef::isUniqueConstraint(createdConstraint)) {
                    removeConstraintBoundIndex(tableName, createdConstraint.name, nullptr);
                }
                constraintRepo.deleteConstraint(createdConstraint.name);
            }
            for (const tabledef::Constraint &removedConstraint : removedConstraints) {
                constraintRepo.createConstraint(removedConstraint);
                if (tabledef::isPrimaryKeyConstraint(removedConstraint)
                    || tabledef::isUniqueConstraint(removedConstraint)) {
                    ensureConstraintBoundIndex(tableName, removedConstraint, originalTable, nullptr);
                }
            }
            result.errorMessage = error;
            return result;
        }
    }

    const repo::RepositoryResult tableResult = tableRepo.replaceTable(table);
    if (!tableResult.ok) {
        metaRepo.updateColumn(definition.column.name, originalColumn);

        for (const tabledef::Constraint &constraint : generatedConstraints) {
            if (tabledef::isPrimaryKeyConstraint(constraint)
                || tabledef::isUniqueConstraint(constraint)) {
                removeConstraintBoundIndex(tableName, constraint.name, nullptr);
            }
            constraintRepo.deleteConstraint(constraint.name);
        }

        for (const tabledef::Constraint &removedConstraint : removedConstraints) {
            constraintRepo.createConstraint(removedConstraint);
            if (tabledef::isPrimaryKeyConstraint(removedConstraint)
                || tabledef::isUniqueConstraint(removedConstraint)) {
                ensureConstraintBoundIndex(tableName, removedConstraint, originalTable, nullptr);
            }
        }

        repo::IndexRepo rollbackIndexRepo(normalizedDatabaseName, tableName, currentDataRoot);
        QString rollbackError;
        const QList<tabledef::IndexMeta> currentIndexes = rollbackIndexRepo.listIndexes(&rollbackError);
        if (rollbackError.isEmpty()) {
            for (const tabledef::IndexMeta &currentIndex : currentIndexes) {
                if (tabledef::hasIndex(schema, currentIndex.indexName)) {
                    continue;
                }
                repo::SortIndexRepo rollbackSortIndexRepo(normalizedDatabaseName,
                                                         currentIndex.indexName,
                                                         tableName,
                                                         currentDataRoot);
                rollbackSortIndexRepo.dropIndex();
                rollbackIndexRepo.deleteIndex(currentIndex.indexName);
            }
        }
        rebuildIndexesForTable(tableName, schema, originalTable, nullptr);

        result.errorMessage = tableResult.error;
        return result;
    }

    result.success = true;
    return result;
}

TaskResult addConstraint(const QString &tableName,
                         const tabledef::Constraint &constraint)
{
    TaskResult result;
    const QString normalizedDatabaseName = normalizeDatabaseName(QString());
    QString error;
    const tabledef::TableSchema schema = loadUserTableSchema(tableName, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    tabledef::TableSchema candidateSchema = schema;
    tabledef::Constraint storedConstraint = constraint;
    if ((tabledef::isPrimaryKeyConstraint(storedConstraint) || tabledef::isUniqueConstraint(storedConstraint))
        && storedConstraint.indexName.trimmed().isEmpty()) {
        storedConstraint.indexName = boundIndexNameForConstraint(storedConstraint);
    }
    candidateSchema.constraints.append(storedConstraint);
    if (!tabledef::validateConstraintDefinitions(candidateSchema, QString(), &error)) {
        result.errorMessage = error;
        return result;
    }

    repo::TableData table = loadUserTableData(tableName, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    if (!tabledef::validateConstraintRows(normalizedDatabaseName,
                                          currentDataRoot,
                                          candidateSchema,
                                          table.columns,
                                          table.rows,
                                          &error)) {
        result.errorMessage = error;
        return result;
    }

    repo::ConstraintRepo constraintRepo(normalizedDatabaseName, tableName, currentDataRoot);
    const repo::RepositoryResult writeResult = constraintRepo.createConstraint(storedConstraint);
    if (!writeResult.ok) {
        result.errorMessage = writeResult.error;
        return result;
    }

    if (!ensureConstraintBoundIndex(tableName, storedConstraint, table, &error)) {
        const repo::RepositoryResult rollbackResult = constraintRepo.deleteConstraint(storedConstraint.name);
        if (!rollbackResult.ok) {
            result.errorMessage = QStringLiteral("%1; rollback failed: %2").arg(error, rollbackResult.error);
            return result;
        }
        result.errorMessage = error;
        return result;
    }
    result.success = true;
    return result;
}

TaskResult modifyConstraint(const QString &tableName,
                            const QString &constraintName,
                            const tabledef::Constraint &constraint)
{
    TaskResult result;
    const QString normalizedDatabaseName = normalizeDatabaseName(QString());
    QString error;
    const tabledef::TableSchema schema = loadUserTableSchema(tableName, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    if (tabledef::findConstraintIndex(schema, constraintName) < 0) {
        result.errorMessage = QStringLiteral("constraint '%1' does not exist").arg(constraintName);
        return result;
    }

    tabledef::TableSchema candidateSchema = schema;
    const int constraintIndex = tabledef::findConstraintIndex(candidateSchema, constraintName);
    const tabledef::Constraint existingConstraint = candidateSchema.constraints.at(constraintIndex);
    tabledef::Constraint storedConstraint = constraint;
    if ((tabledef::isPrimaryKeyConstraint(storedConstraint) || tabledef::isUniqueConstraint(storedConstraint))
        && storedConstraint.indexName.trimmed().isEmpty()) {
        storedConstraint.indexName = existingConstraint.indexName.trimmed().isEmpty()
                                         ? boundIndexNameForConstraint(storedConstraint)
                                         : existingConstraint.indexName;
    }
    candidateSchema.constraints[constraintIndex] = storedConstraint;
    if (!tabledef::validateConstraintDefinitions(candidateSchema, QString(), &error)) {
        result.errorMessage = error;
        return result;
    }

    repo::TableData table = loadUserTableData(tableName, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    if (!tabledef::validateConstraintRows(normalizedDatabaseName,
                                          currentDataRoot,
                                          candidateSchema,
                                          table.columns,
                                          table.rows,
                                          &error)) {
        result.errorMessage = error;
        return result;
    }

    const QString oldBoundIndexName = boundIndexNameForConstraint(existingConstraint);
    const QString newBoundIndexName = boundIndexNameForConstraint(storedConstraint);
    const bool oldIsBound = tabledef::isPrimaryKeyConstraint(existingConstraint)
                            || tabledef::isUniqueConstraint(existingConstraint);
    const bool newIsBound = tabledef::isPrimaryKeyConstraint(storedConstraint)
                            || tabledef::isUniqueConstraint(storedConstraint);
    const bool removeOldBoundIndex = oldIsBound && (!newIsBound || oldBoundIndexName != newBoundIndexName);

    if (removeOldBoundIndex) {
        QString removeError;
        if (!removeConstraintBoundIndex(tableName, constraintName, &removeError)) {
            result.errorMessage = removeError;
            return result;
        }
    }

    repo::ConstraintRepo constraintRepo(normalizedDatabaseName, tableName, currentDataRoot);
    const repo::RepositoryResult writeResult = constraintRepo.updateConstraint(constraintName, storedConstraint);
    if (!writeResult.ok) {
        if (removeOldBoundIndex) {
            ensureConstraintBoundIndex(tableName, existingConstraint, table, nullptr);
        }
        result.errorMessage = writeResult.error;
        return result;
    }

    if (newIsBound) {
        if (!ensureConstraintBoundIndex(tableName, storedConstraint, table, &error)) {
            if (removeOldBoundIndex) {
                constraintRepo.updateConstraint(constraintName, existingConstraint);
                ensureConstraintBoundIndex(tableName, existingConstraint, table, nullptr);
            }
            result.errorMessage = error;
            return result;
        }
    }
    result.success = true;
    return result;
}

TaskResult deleteConstraint(const QString &tableName,
                         const QString &constraintName)
{
    TaskResult result;
    const QString normalizedDatabaseName = normalizeDatabaseName(QString());
    QString error;
    const tabledef::TableSchema schema = loadUserTableSchema(tableName, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    const int constraintIndex = tabledef::findConstraintIndex(schema, constraintName);
    if (constraintIndex < 0) {
        result.errorMessage = QStringLiteral("constraint '%1' does not exist").arg(constraintName);
        return result;
    }

    const tabledef::Constraint existingConstraint = schema.constraints.at(constraintIndex);
    if (tabledef::isPrimaryKeyConstraint(existingConstraint)
        || tabledef::isUniqueConstraint(existingConstraint)) {
        QString removeError;
        if (!removeConstraintBoundIndex(tableName, constraintName, &removeError)) {
            result.errorMessage = removeError;
            return result;
        }
    }

    repo::ConstraintRepo constraintRepo(normalizedDatabaseName, tableName, currentDataRoot);
    const repo::RepositoryResult writeResult = constraintRepo.deleteConstraint(constraintName);
    if (!writeResult.ok) {
        result.errorMessage = writeResult.error;
        return result;
    }
    result.success = true;
    return result;
}

TaskResult createIndex(const QString &tableName,
                      const QString &indexName,
                      const QStringList &columnNames,
                      bool isUnique)
{
    TaskResult result;
    const QString normalizedDatabaseName = normalizeDatabaseName(QString());
    QString error;
    const tabledef::TableSchema schema = loadUserTableSchema(tableName, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    const tabledef::IndexMeta indexMeta{indexName, columnNames, isUnique};
    if (!tabledef::validateIndexDefinition(schema, indexMeta, QString(), &error)) {
        result.errorMessage = error;
        return result;
    }

    repo::TableData table = loadUserTableData(tableName, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    bool rowIdsInitialized = false;
    const QStringList rowIds = loadUserTableRowIds(tableName, table, &rowIdsInitialized, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }
    if (rowIdsInitialized) {
        if (!rebuildIndexesForTable(tableName, schema, table, &error)) {
            result.errorMessage = error;
            return result;
        }
    }

    if (isUnique) {
        QSet<QString> seen;
        for (const repo::TableRow &row : table.rows) {
            QStringList values;
            values.reserve(columnNames.size());
            bool hasEmptyValue = false;
            for (const QString &columnName : columnNames) {
                const int columnIndex = table.columns.indexOf(columnName);
                if (columnIndex < 0) {
                    result.errorMessage = QStringLiteral("column '%1' does not exist").arg(columnName);
                    return result;
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
            QString key;
            for (const QString &value : values) {
                key.append(QString::number(value.size()));
                key.append(QLatin1Char(':'));
                key.append(value);
                key.append(QLatin1Char(';'));
            }
            if (seen.contains(key)) {
                result.errorMessage = QStringLiteral("index '%1' contains duplicate key values").arg(indexName);
                return result;
            }
            seen.insert(key);
        }
    }

    repo::IndexRepo indexRepo(normalizedDatabaseName, tableName, currentDataRoot);
    const repo::RepositoryResult metadataResult = indexRepo.createIndex(indexMeta);
    if (!metadataResult.ok) {
        result.errorMessage = metadataResult.error;
        return result;
    }

    const repo::RepositoryResult treeResult = repo::SortIndexRepo(normalizedDatabaseName, indexName, tableName, currentDataRoot)
                                                  .createIndex(indexMeta, table, rowIds);
    if (!treeResult.ok) {
        repo::SortIndexRepo(normalizedDatabaseName, indexName, tableName, currentDataRoot).dropIndex();
        indexRepo.deleteIndex(indexName);
        result.errorMessage = treeResult.error;
        return result;
    }

    result.success = true;
    return result;
}

TaskResult dropIndex(const QString &tableName,
                    const QString &indexName)
{
    TaskResult result;
    const QString normalizedDatabaseName = normalizeDatabaseName(QString());
    QString error;
    const tabledef::TableSchema schema = loadUserTableSchema(tableName, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    for (const tabledef::Constraint &constraint : schema.constraints) {
        if ((tabledef::isPrimaryKeyConstraint(constraint) || tabledef::isUniqueConstraint(constraint))
            && boundIndexNameForConstraint(constraint) == indexName) {
            result.errorMessage = QStringLiteral("index '%1' is bound to constraint '%2'")
                                     .arg(indexName, constraint.name);
            return result;
        }
    }

    if (!tabledef::hasIndex(schema, indexName)) {
        result.errorMessage = QStringLiteral("index '%1' does not exist").arg(indexName);
        return result;
    }

    repo::IndexRepo indexRepo(normalizedDatabaseName, tableName, currentDataRoot);
    const repo::RepositoryResult metadataResult = indexRepo.deleteIndex(indexName);
    if (!metadataResult.ok) {
        result.errorMessage = metadataResult.error;
        return result;
    }

    const repo::RepositoryResult treeResult =
        repo::SortIndexRepo(normalizedDatabaseName, indexName, tableName, currentDataRoot).dropIndex();
    if (!treeResult.ok) {
        result.errorMessage = treeResult.error;
        return result;
    }

    result.success = true;
    return result;
}

SelectRowsResult showTables()
{
    TableDmlService dmlService;
    const QString databaseName = normalizeDatabaseName(QString());
    return dmlService.selectRows(QString(),
                                 QString(),
                                 TargetTableKind::DatabaseTab,
                                 tabledef::buildDatabaseTableCatalogSchema(databaseName),
                                 {QStringLiteral("*")},
                                 {});
}

TextResult describeTable(const QString &tableName)
{
    TextResult result;
    QString error;
    const tabledef::TableSchema schema = loadUserTableSchema(tableName, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    QStringList lines;
    for (const tabledef::Column &column : schema.columns) {
        lines.append(formatColumnDefinition(column));
    }
    for (const tabledef::Constraint &constraint : schema.constraints) {
        lines.append(formatConstraintDefinition(constraint));
    }

    result.success = true;
    result.text = lines.join(QStringLiteral("\n"));
    return result;
}

TextResult showCreateTable(const QString &tableName)
{
    TextResult result;
    QString error;
    const tabledef::TableSchema schema = loadUserTableSchema(tableName, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    result.success = true;
    result.text = buildCreateTableText(schema);
    return result;
}

} // namespace service::table_service