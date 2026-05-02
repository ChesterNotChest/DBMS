/*
 * 范围：只放 service_common 的通用辅助。
 * 这里放：校验、复合键、行匹配、索引/row_id 维护、约束构造。
 * 不放：schema 规则、DDL 编排、DML 级联。
 */

#include "service_common.h"
#include "../logic/logic.h"

#include <QDebug>
#include <QSet>
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

tabledef::ColumnType columnTypeForName(const tabledef::TableSchema &schema, const QString &columnName)
{
    for (const tabledef::Column &column : schema.columns) {
        if (column.name == columnName) {
            return column.type;
        }
    }
    return tabledef::ColumnType::Varchar;
}

logic::LogicRowContext buildRowContext(const tabledef::TableSchema &schema,
                                      const QStringList &tableColumns,
                                      const QStringList &rowValues)
{
    logic::LogicRowContext rowContext;
    rowContext.tableName = schema.tableName;

    for (int columnIndex = 0; columnIndex < tableColumns.size(); ++columnIndex) {
        const QString &columnName = tableColumns.at(columnIndex);
        const QString value = columnIndex < rowValues.size() ? rowValues.at(columnIndex) : QString();
        rowContext.cellsByName.insert(columnName,
                                      logic::LogicCellValue{value,
                                                            columnTypeForName(schema, columnName),
                                                            value.isEmpty()});
    }

    return rowContext;
}

void logIndexMaintenance(const QString &message)
{
    qDebug().noquote() << QStringLiteral("[index-maintenance] %1").arg(message);
}

} // namespace

namespace service {

// 字段与约束的基础校验。
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

    if (definition.referencedTable.trimmed().isEmpty()
        && (definition.onDeleteAction != tabledef::ForeignKeyAction::NoAction
            || definition.onUpdateAction != tabledef::ForeignKeyAction::NoAction)) {
        if (error != nullptr) {
            *error = QStringLiteral("foreign key actions can only be set when a referenced table is provided");
        }
        return false;
    }

    return true;
}

bool validateScalarValue(const tabledef::Column &column, const QString &value, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    if (value.isEmpty()) {
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

// 复合键与 FK 行匹配。
QString compositeKeySignature(const QStringList &values)
{
    QString signature;
    for (const QString &value : values) {
        signature.append(QString::number(value.size()));
        signature.append(QLatin1Char(':'));
        signature.append(value);
        signature.append(QLatin1Char(';'));
    }
    return signature;
}

bool rowExistsInTable(const repo::TableData &table,
                      const QStringList &columnNames,
                      const QStringList &values,
                      QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    if (columnNames.size() != values.size()) {
        if (error != nullptr) {
            *error = QStringLiteral("foreign key column count does not match referenced column count");
        }
        return false;
    }

    for (const repo::TableRow &row : table.rows) {
        bool matches = true;
        for (int index = 0; index < columnNames.size(); ++index) {
            const int columnIndex = table.columns.indexOf(columnNames.at(index));
            if (columnIndex < 0) {
                if (error != nullptr) {
                    *error = QStringLiteral("column '%1' does not exist").arg(columnNames.at(index));
                }
                return false;
            }

            if (row.value(columnIndex) != values.at(index)) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return true;
        }
    }

    return false;
}

bool validateConstraintRows(const QString &databaseName,
                            const QString &dataRoot,
                            const tabledef::TableSchema &schema,
                            const QStringList &tableColumns,
                            const QList<QStringList> &tableRows,
                            QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    for (const tabledef::Constraint &constraint : schema.constraints) {
        if (tabledef::isCheckConstraint(constraint)) {
            if (constraint.checkClause.trimmed().isEmpty()) {
                if (error != nullptr) {
                    *error = QStringLiteral("check constraint '%1' is incomplete").arg(constraint.name);
                }
                return false;
            }

            const logic::LogicTokenizeResult tokenized = logic::tokenizeLogicExpression(constraint.checkClause);
            if (!tokenized.success) {
                if (error != nullptr) {
                    *error = QStringLiteral("check constraint '%1': %2").arg(constraint.name, tokenized.error.message);
                }
                return false;
            }

            const logic::LogicParseResult parsed = logic::parseLogicTokens(constraint.checkClause, tokenized.tokens);
            if (!parsed.success) {
                if (error != nullptr) {
                    *error = QStringLiteral("check constraint '%1': %2").arg(constraint.name, parsed.error.message);
                }
                return false;
            }

            const logic::LogicEvalContext evalContext;
            for (const QStringList &row : tableRows) {
                const logic::LogicRowContext rowContext = buildRowContext(schema, tableColumns, row);
                const logic::LogicEvalResult evalResult = logic::evaluateCheckConstraintForRow(parsed.root,
                                                                                            rowContext,
                                                                                            evalContext);
                if (!evalResult.success) {
                    if (error != nullptr) {
                        *error = QStringLiteral("check constraint '%1': %2")
                                     .arg(constraint.name, evalResult.error.message);
                    }
                    return false;
                }
                if (evalResult.truth != logic::LogicTruthValue::True) {
                    if (error != nullptr) {
                        *error = QStringLiteral("check constraint '%1' is violated").arg(constraint.name);
                    }
                    return false;
                }
            }
            continue;
        }

        if (tabledef::isPrimaryKeyConstraint(constraint) || tabledef::isUniqueConstraint(constraint)) {
            QSet<QString> seenKeys;
            for (const QStringList &row : tableRows) {
                QStringList values;
                values.reserve(constraint.columns.size());
                bool hasEmptyValue = false;

                for (const QString &columnName : constraint.columns) {
                    const int columnIndex = tableColumns.indexOf(columnName);
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
            continue;
        }

        if (!tabledef::isForeignKeyConstraint(constraint)) {
            continue;
        }
        if (!tabledef::isForeignKeyReferenceComplete(constraint)) {
            if (error != nullptr) {
                *error = QStringLiteral("foreign key '%1' is incomplete").arg(constraint.name);
            }
            return false;
        }

        QList<tabledef::Column> parentColumns;
        repo::TableData parentTable;
        QString tabError;
        if (constraint.referencedTable == schema.tableName) {
            parentColumns = schema.columns;
            parentTable.columns = tableColumns;
            parentTable.rows = tableRows;
        } else {
            repo::TabRepo tabRepo(databaseName, dataRoot);
            if (!tabRepo.hasTable(constraint.referencedTable, &tabError)) {
                if (error != nullptr) {
                    *error = tabError.isEmpty()
                                 ? QStringLiteral("referenced table '%1' does not exist").arg(constraint.referencedTable)
                                 : tabError;
                }
                return false;
            }

            repo::MetaRepo parentMeta(databaseName, constraint.referencedTable, dataRoot);
            parentColumns = parentMeta.listColumns(&tabError);
            if (!tabError.isEmpty()) {
                if (error != nullptr) {
                    *error = tabError;
                }
                return false;
            }

            parentTable = repo::TableRepo(databaseName, constraint.referencedTable, dataRoot)
                              .readTable(&tabError);
            if (!tabError.isEmpty()) {
                if (error != nullptr) {
                    *error = tabError;
                }
                return false;
            }
        }

        for (const QString &referencedColumn : constraint.referencedColumns) {
            if (!tabledef::hasColumn(tabledef::TableSchema{constraint.referencedTable, parentColumns, {}}, referencedColumn)) {
                if (error != nullptr) {
                    *error = QStringLiteral("referenced column '%1' does not exist").arg(referencedColumn);
                }
                return false;
            }
        }

        for (const QStringList &row : tableRows) {
            QStringList values;
            values.reserve(constraint.columns.size());
            bool hasEmptyValue = false;

            for (const QString &columnName : constraint.columns) {
                const int columnIndex = tableColumns.indexOf(columnName);
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
            if (!rowExistsInTable(parentTable, constraint.referencedColumns, values, &rowError)) {
                if (error != nullptr) {
                    *error = rowError.isEmpty()
                                 ? QStringLiteral("foreign key '%1' references missing parent row")
                                       .arg(constraint.name)
                                 : rowError;
                }
                return false;
            }
        }
    }

    return true;
}

bool validateNoIncomingForeignKeyReferences(const QString &databaseName,
                                            const QString &dataRoot,
                                            const QString &targetTableName,
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
        if (tableEntry.name == targetTableName) {
            continue;
        }

        repo::ConstraintRepo constraintRepo(databaseName, tableEntry.name, dataRoot);
        const QList<tabledef::Constraint> constraints = constraintRepo.listConstraints(&tabError);
        if (!tabError.isEmpty()) {
            if (error != nullptr) {
                *error = tabError;
            }
            return false;
        }

        for (const tabledef::Constraint &constraint : constraints) {
            if (tabledef::isForeignKeyConstraint(constraint) && constraint.referencedTable == targetTableName) {
                if (error != nullptr) {
                    *error = QStringLiteral("table '%1' is referenced by foreign key '%2' from table '%3'")
                                 .arg(targetTableName, constraint.name, tableEntry.name);
                }
                return false;
            }
        }
    }

    return true;
}

// 约束构造与派生约束生成。
tabledef::Constraint makeConstraint(const QString &constraintName,
                                    tabledef::ConstraintType type,
                                    const QStringList &columns,
                                    const QString &referencedTable,
                                    const QStringList &referencedColumns,
                                    const QString &checkClause,
                                    const QString &indexName,
                                    tabledef::ForeignKeyAction onDeleteAction,
                                    tabledef::ForeignKeyAction onUpdateAction)
{
    return tabledef::Constraint{constraintName,
                                type,
                                columns,
                                referencedTable,
                                referencedColumns,
                                checkClause,
                                indexName,
                                onDeleteAction,
                                onUpdateAction};
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
                                          QString(),
                                          definition.onDeleteAction,
                                          definition.onUpdateAction));
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

// 表/索引/row-id 的通用载入与维护。
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
            const repo::RepositoryResult rebuildResult = sortIndexRepo.rebuild(index, tableData, rowIds);
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
    const tabledef::IndexMeta indexMeta{boundIndexName,
                                        constraint.columns,
                                        tabledef::isPrimaryKeyConstraint(constraint)
                                            || tabledef::isUniqueConstraint(constraint)};

    const QList<tabledef::IndexMeta> existingIndexes = indexRepo.listIndexes(error);
    if (error != nullptr && !error->isEmpty()) {
        return false;
    }

    tabledef::IndexMeta existingIndex;
    bool indexExists = false;
    for (const tabledef::IndexMeta &candidate : existingIndexes) {
        if (candidate.indexName == boundIndexName) {
            existingIndex = candidate;
            indexExists = true;
            break;
        }
    }

    if (indexExists) {
        logIndexMaintenance(QStringLiteral("bound index exists, rebuild %1")
                                .arg(boundIndexName));
        const repo::RepositoryResult updateResult = indexRepo.updateIndex(boundIndexName, indexMeta);
        if (!updateResult.ok) {
            if (error != nullptr) {
                *error = updateResult.error;
            }
            return false;
        }

        const repo::RepositoryResult rebuildResult = repo::SortIndexRepo(databaseName, boundIndexName, tableName, currentDataRoot)
                                                         .rebuild(indexMeta, tableData, rowIds);
        if (!rebuildResult.ok) {
            const repo::RepositoryResult rollbackResult = indexRepo.updateIndex(boundIndexName, existingIndex);
            if (error != nullptr) {
                if (!rollbackResult.ok) {
                    *error = QStringLiteral("%1; rollback failed: %2").arg(rebuildResult.error, rollbackResult.error);
                } else {
                    *error = rebuildResult.error;
                }
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
        repo::SortIndexRepo(databaseName, boundIndexName, tableName, currentDataRoot).dropIndex();
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

    if (qEnvironmentVariableIsSet("DBMS_TEST_FAIL_BOUND_INDEX_REMOVE")) {
        if (error != nullptr) {
            *error = QStringLiteral("failed to remove file '%1'").arg(QStringLiteral("<test-injected>"));
        }
        return false;
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
        const repo::RepositoryResult metadataResult = indexRepo.deleteIndex(boundIndexName);
        if (!metadataResult.ok) {
            if (error != nullptr) {
                *error = metadataResult.error;
            }
            return false;
        }
        const repo::RepositoryResult treeResult =
            repo::SortIndexRepo(databaseName, boundIndexName, tableName, currentDataRoot).dropIndex();
        if (!treeResult.ok) {
            if (error != nullptr) {
                *error = treeResult.error;
            }
            return false;
        }
        return true;
    }

    if (error != nullptr) {
        *error = QStringLiteral("constraint '%1' does not exist").arg(constraintName);
    }
    return false;
}

} // namespace service
