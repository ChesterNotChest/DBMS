#include "service.h"

namespace {

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

    const repo::RepositoryResult tableCreated = tableRepo.createTable(tabledef::schemaColumnNames(normalizedSchema));
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

    for (const tabledef::Constraint &constraint : normalizedSchema.constraints) {
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
            metaRepo.deleteColumn(definition.column.name);
            result.errorMessage = constraintResult.error;
            return result;
        }
    }

    const repo::RepositoryResult writeResult = tableRepo.replaceTable(table);
    if (!writeResult.ok) {
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

    const int columnIndex = tabledef::findColumnIndex(schema, columnName);
    if (columnIndex < 0) {
        result.errorMessage = QStringLiteral("column '%1' does not exist").arg(columnName);
        return result;
    }

    repo::MetaRepo metaRepo(normalizedDatabaseName, tableName, currentDataRoot);
    repo::ConstraintRepo constraintRepo(normalizedDatabaseName, tableName, currentDataRoot);
    repo::TableRepo tableRepo(normalizedDatabaseName, tableName, currentDataRoot);

    const QList<tabledef::Constraint> constraints = constraintRepo.listConstraints(&result.errorMessage);
    if (!result.errorMessage.isEmpty()) {
        return result;
    }

    repo::TableData table = loadUserTableData(tableName, &result.errorMessage);
    if (!result.errorMessage.isEmpty()) {
        return result;
    }

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

    const repo::RepositoryResult tableResult = tableRepo.replaceTable(table);
    if (!tableResult.ok) {
        result.errorMessage = tableResult.error;
        return result;
    }

    for (const tabledef::Constraint &constraint : constraints) {
        if (tabledef::constraintTouchesColumn(constraint, columnName)) {
            const repo::RepositoryResult deleteConstraintResult =
                constraintRepo.deleteConstraint(constraint.name);
            if (!deleteConstraintResult.ok) {
                result.errorMessage = deleteConstraintResult.error;
                return result;
            }
        }
    }

    const repo::RepositoryResult metaResult = metaRepo.deleteColumn(columnName);
    if (!metaResult.ok) {
        result.errorMessage = metaResult.error;
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

    const bool isRename = definition.column.name.trimmed() != columnName.trimmed();
    if (isRename && tabledef::hasColumn(schema, definition.column.name)) {
        result.errorMessage = QStringLiteral("column '%1' already exists").arg(definition.column.name);
        return result;
    }

    tabledef::TableSchema schemaWithoutTouchedConstraints = schema;
    schemaWithoutTouchedConstraints.columns[columnIndex] = definition.column;
    schemaWithoutTouchedConstraints.constraints.removeIf(
        [&](const tabledef::Constraint &constraint) {
            return tabledef::constraintTouchesColumn(constraint, columnName);
        });

    tabledef::TableSchema candidateSchema = schemaWithoutTouchedConstraints;
    for (const tabledef::Constraint &constraint : buildGeneratedConstraints(definition)) {
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

    const repo::RepositoryResult tableResult = tableRepo.replaceTable(table);
    if (!tableResult.ok) {
        result.errorMessage = tableResult.error;
        return result;
    }

    for (const tabledef::Constraint &constraint : schema.constraints) {
        if (!tabledef::constraintTouchesColumn(constraint, columnName)) {
            continue;
        }
        const repo::RepositoryResult deleteConstraintResult =
            constraintRepo.deleteConstraint(constraint.name);
        if (!deleteConstraintResult.ok) {
            result.errorMessage = deleteConstraintResult.error;
            return result;
        }
    }

    for (const tabledef::Constraint &constraint : buildGeneratedConstraints(definition)) {
        const repo::RepositoryResult createConstraintResult = constraintRepo.createConstraint(constraint);
        if (!createConstraintResult.ok) {
            result.errorMessage = createConstraintResult.error;
            return result;
        }
    }

    const repo::RepositoryResult updateResult = metaRepo.updateColumn(columnName, definition.column);
    if (!updateResult.ok) {
        result.errorMessage = updateResult.error;
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
    candidateSchema.constraints.append(constraint);
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
    const repo::RepositoryResult writeResult = constraintRepo.createConstraint(constraint);
    if (!writeResult.ok) {
        result.errorMessage = writeResult.error;
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
    candidateSchema.constraints[constraintIndex] = constraint;
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
    const repo::RepositoryResult writeResult = constraintRepo.updateConstraint(constraintName, constraint);
    if (!writeResult.ok) {
        result.errorMessage = writeResult.error;
        return result;
    }
    result.success = true;
    return result;
}

TaskResult deleteConstraint(const QString &tableName,
                         const QString &constraintName)
{
    TaskResult result;
    const QString normalizedDatabaseName = normalizeDatabaseName(QString());
    repo::ConstraintRepo constraintRepo(normalizedDatabaseName, tableName, currentDataRoot);
    const repo::RepositoryResult writeResult = constraintRepo.deleteConstraint(constraintName);
    if (!writeResult.ok) {
        result.errorMessage = writeResult.error;
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