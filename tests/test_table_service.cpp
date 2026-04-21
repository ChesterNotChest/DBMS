#include "../service/service.h"

#include <QDir>
#include <QJsonArray>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include "service_test_entry.h"

using namespace service;

namespace {

QString testDataRoot()
{
    return QDir(QDir::tempPath()).absoluteFilePath(QStringLiteral("DBMS_test_table_service"));
}

void removeTestDataRoot(const QString &path)
{
    QDir directory(path);
    if (directory.exists()) {
        directory.removeRecursively();
    }
}

tabledef::Column makeColumn(const QString &name,
                            tabledef::ColumnType type,
                            int length = 0,
                            bool notNull = false,
                            const QString &defaultValue = QString(),
                            bool autoIncrement = false)
{
    return tabledef::Column{name, type, length, notNull, defaultValue, autoIncrement, QString()};
}

tabledef::Constraint makePrimaryKey(const QString &name, const QStringList &columns)
{
    return tabledef::Constraint{name, tabledef::ConstraintType::PrimaryKey, columns, QString(), {}, QString()};
}

tabledef::Constraint makeUnique(const QString &name, const QStringList &columns)
{
    return tabledef::Constraint{name, tabledef::ConstraintType::Unique, columns, QString(), {}, QString()};
}

tabledef::Constraint makeForeignKey(const QString &name,
                                    const QStringList &columns,
                                    const QString &referencedTable,
                                    const QStringList &referencedColumns,
                                    tabledef::ForeignKeyAction onDeleteAction = tabledef::ForeignKeyAction::NoAction,
                                    tabledef::ForeignKeyAction onUpdateAction = tabledef::ForeignKeyAction::NoAction)
{
    return tabledef::Constraint{name,
                                tabledef::ConstraintType::ForeignKey,
                                columns,
                                referencedTable,
                                referencedColumns,
                                QString(),
                                QString(),
                                onDeleteAction,
                                onUpdateAction};
}

tabledef::TableSchema baseSchema(const QString &tableName)
{
    tabledef::TableSchema schema;
    schema.tableName = tableName;
    schema.columns = {
        makeColumn(QStringLiteral("id"), tabledef::ColumnType::Int, 0, true),
        makeColumn(QStringLiteral("name"), tabledef::ColumnType::Varchar, 64, true),
    };
    schema.constraints = {
        makePrimaryKey(QStringLiteral("pk_%1_id").arg(tableName), {QStringLiteral("id")}),

    };
    return schema;
}

tabledef::TableSchema tableWithForeignKeyColumn(const QString &tableName)
{
    tabledef::TableSchema schema = baseSchema(tableName);
    schema.columns.append(makeColumn(QStringLiteral("parent_id"), tabledef::ColumnType::Int, 0, true));
    return schema;
}

tabledef::TableSchema agedSchema(const QString &tableName)
{
    tabledef::TableSchema schema = baseSchema(tableName);
    schema.columns.append(makeColumn(QStringLiteral("age"), tabledef::ColumnType::Int, 0, false, QStringLiteral("22")));
    return schema;
}

void ensureDatabase(const QString &databaseName, const QString &dataRoot)
{
    Q_UNUSED(dataRoot);
    TaskResult result = database_service::createDatabase(databaseName);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    result = database_service::useDatabase(databaseName);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
}

void ensureTable(const QString &databaseName,
                 const QString &tableName,
                 const tabledef::TableSchema &schema,
                 const QString &dataRoot)
{
    Q_UNUSED(databaseName);
    Q_UNUSED(dataRoot);
    TaskResult result = table_service::createTable(tableName, schema);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
}

void seedRow(const QString &databaseName,
             const QString &tableName,
             const QStringList &row,
             const QString &dataRoot)
{
    repo::TableRepo tableRepo(databaseName, tableName, dataRoot);
    const repo::RepositoryResult result = tableRepo.insertRow(row);
    QVERIFY2(result.ok, qPrintable(result.error));
}

QStringList listedTables(const SelectRowsResult &result)
{
    QStringList names;
    for (const repo::TableRow &row : result.resultTable.rows) {
        if (!row.isEmpty()) {
            names.append(row.first());
        }
    }
    return names;
}

QStringList listedIndexes(const QString &databaseName,
                          const QString &tableName,
                          const QString &dataRoot,
                          QString *error = nullptr)
{
    repo::IndexRepo indexRepo(databaseName, tableName, dataRoot);
    const QList<tabledef::IndexMeta> indexes = indexRepo.listIndexes(error);
    QStringList names;
    for (const tabledef::IndexMeta &index : indexes) {
        names.append(index.indexName);
    }
    return names;
}

QString findIndexNameByColumns(const QString &databaseName,
                               const QString &tableName,
                               const QString &dataRoot,
                               const QStringList &columns,
                               QString *error = nullptr)
{
    repo::IndexRepo indexRepo(databaseName, tableName, dataRoot);
    const QList<tabledef::IndexMeta> indexes = indexRepo.listIndexes(error);
    for (const tabledef::IndexMeta &index : indexes) {
        if (index.columnNames == columns) {
            return index.indexName;
        }
    }
    return {};
}

QStringList searchIndex(const QString &databaseName,
                        const QString &tableName,
                        const QString &indexName,
                        const QStringList &keyValues,
                        const QString &dataRoot,
                        QString *error = nullptr)
{
    repo::SortIndexRepo sortIndexRepo(databaseName, indexName, tableName, dataRoot);
    return sortIndexRepo.search(keyValues, error);
}

QStringList readRowIds(const QString &databaseName,
                       const QString &tableName,
                       const QString &dataRoot,
                       QString *error = nullptr)
{
    repo::FlatFileTableStore store(dataRoot);
    const repo::TableData table = store.readTable(store.getRowIdFilePath(databaseName, tableName), error);
    QStringList rowIds;
    for (const repo::TableRow &row : table.rows) {
        if (!row.isEmpty()) {
            rowIds.append(row.first());
        }
    }
    return rowIds;
}

bool readSortIndexIsUnique(const QString &databaseName,
                           const QString &tableName,
                           const QString &indexName,
                           const QString &dataRoot,
                           QString *error = nullptr)
{
    repo::FlatFileTableStore store(dataRoot);
    QFile file(store.getSortIndexFilePath(databaseName, tableName, indexName));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error != nullptr) {
            *error = QStringLiteral("failed to open sort index file");
        }
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("sort index file is not a json object");
        }
        return false;
    }

    const QJsonObject meta = document.object().value(QStringLiteral("meta")).toObject();
    return meta.value(QStringLiteral("isUnique")).toBool(false);
}

bool readSortIndexLeftmostLeafNext(const QString &databaseName,
                                   const QString &tableName,
                                   const QString &indexName,
                                   const QString &dataRoot,
                                   int *nextLeafId,
                                   bool *nextLeafExists,
                                   QString *error = nullptr)
{
    if (nextLeafId != nullptr) {
        *nextLeafId = -1;
    }
    if (nextLeafExists != nullptr) {
        *nextLeafExists = false;
    }

    repo::FlatFileTableStore store(dataRoot);
    QFile file(store.getSortIndexFilePath(databaseName, tableName, indexName));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error != nullptr) {
            *error = QStringLiteral("failed to open sort index file");
        }
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("sort index file is not a json object");
        }
        return false;
    }

    const QJsonObject root = document.object();
    const QJsonArray nodes = root.value(QStringLiteral("nodes")).toArray();
    QMap<int, QJsonObject> nodesById;
    for (const QJsonValue &nodeValue : nodes) {
        const QJsonObject nodeObject = nodeValue.toObject();
        nodesById.insert(nodeObject.value(QStringLiteral("id")).toInt(-1), nodeObject);
    }

    int nodeId = root.value(QStringLiteral("rootId")).toInt(-1);
    if (!nodesById.contains(nodeId)) {
        if (error != nullptr) {
            *error = QStringLiteral("sort index root is missing");
        }
        return false;
    }

    while (!nodesById.value(nodeId).value(QStringLiteral("leaf")).toBool(true)) {
        const QJsonArray children = nodesById.value(nodeId).value(QStringLiteral("children")).toArray();
        if (children.isEmpty()) {
            if (error != nullptr) {
                *error = QStringLiteral("sort index tree is malformed");
            }
            return false;
        }
        nodeId = children.first().toInt(-1);
        if (!nodesById.contains(nodeId)) {
            if (error != nullptr) {
                *error = QStringLiteral("sort index tree is malformed");
            }
            return false;
        }
    }

    if (nextLeafId != nullptr) {
        *nextLeafId = nodesById.value(nodeId).value(QStringLiteral("next")).toInt(-1);
    }
    if (nextLeafExists != nullptr && nextLeafId != nullptr && *nextLeafId >= 0) {
        *nextLeafExists = nodesById.contains(*nextLeafId)
                           && nodesById.value(*nextLeafId).value(QStringLiteral("leaf")).toBool(true);
    }
    return true;
}

QString readSortIndexSourceTable(const QString &databaseName,
                                 const QString &tableName,
                                 const QString &indexName,
                                 const QString &dataRoot,
                                 QString *error = nullptr)
{
    repo::FlatFileTableStore store(dataRoot);
    QFile file(store.getSortIndexFilePath(databaseName, tableName, indexName));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error != nullptr) {
            *error = QStringLiteral("failed to open sort index file");
        }
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("sort index file is not a json object");
        }
        return {};
    }

    const QJsonObject meta = document.object().value(QStringLiteral("meta")).toObject();
    return meta.value(QStringLiteral("sourceTable")).toString();
}

QStringList readSortIndexColumns(const QString &databaseName,
                                const QString &tableName,
                                const QString &indexName,
                                const QString &dataRoot,
                                QString *error = nullptr)
{
    repo::FlatFileTableStore store(dataRoot);
    QFile file(store.getSortIndexFilePath(databaseName, tableName, indexName));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error != nullptr) {
            *error = QStringLiteral("failed to open sort index file");
        }
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("sort index file is not a json object");
        }
        return {};
    }

    const QJsonObject meta = document.object().value(QStringLiteral("meta")).toObject();
    QStringList columns;
    const QJsonArray jsonColumns = meta.value(QStringLiteral("columnNames")).toArray();
    columns.reserve(jsonColumns.size());
    for (const QJsonValue &value : jsonColumns) {
        columns.append(value.toString());
    }
    return columns;
}

} // namespace

class TableServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_dataRoot = testDataRoot();
        removeTestDataRoot(m_dataRoot);
        setDataRoot(m_dataRoot);
        currentDatabase.clear();
    }

    void cleanup()
    {
        currentDatabase.clear();
        removeTestDataRoot(m_dataRoot);
        setDataRoot(QString());
    }

    void test_createTable()
    {
        const QString databaseName = QStringLiteral("test_table_service_create_db");
        ensureDatabase(databaseName, m_dataRoot);

        currentDatabase.clear();
        TaskResult emptyDatabaseResult = table_service::createTable(QStringLiteral("tbl"),
                                                                   baseSchema(QStringLiteral("tbl")));
        QVERIFY(!emptyDatabaseResult.success);
        QVERIFY(emptyDatabaseResult.errorMessage.contains(QStringLiteral("database name cannot be empty")));

        QVERIFY(database_service::useDatabase(databaseName).success);

        TaskResult emptyTableResult = table_service::createTable(QStringLiteral("   "),
                                                                 baseSchema(QStringLiteral("tbl")));
        QVERIFY(!emptyTableResult.success);
        QVERIFY(emptyTableResult.errorMessage.contains(QStringLiteral("table name cannot be empty")));

        const QString tableName = QStringLiteral("test_table_service_create_table_main");
        TaskResult createResult = table_service::createTable(tableName, baseSchema(tableName));
        QVERIFY2(createResult.success, qPrintable(createResult.errorMessage));

        TaskResult duplicateResult = table_service::createTable(tableName, baseSchema(tableName));
        QVERIFY(!duplicateResult.success);
        QVERIFY(duplicateResult.errorMessage.contains(QStringLiteral("already exists")));

        SelectRowsResult tables = table_service::showTables();
        QVERIFY2(tables.success, qPrintable(tables.errorMessage));
        const QStringList expectedTables{tableName};
        QCOMPARE(listedTables(tables), expectedTables);

        repo::TableRepo tableRepo(databaseName, tableName, m_dataRoot);
        QString error;
        repo::TableData table = tableRepo.readTable(&error);
        QVERIFY(error.isEmpty());
        const QStringList expectedColumns{QStringLiteral("id"), QStringLiteral("name")};
        QCOMPARE(table.columns, expectedColumns);
        QCOMPARE(table.rows.size(), 0);

        tabledef::TableSchema brokenFkSchema = baseSchema(QStringLiteral("test_table_service_create_fk_table"));
        brokenFkSchema.constraints.append(makeForeignKey(QStringLiteral("fk_test_table_service_create_fk"),
                                                        {QStringLiteral("id")},
                                                        QStringLiteral("missing_parent_table"),
                                                        {QStringLiteral("id")}));
        TaskResult brokenFkResult = table_service::createTable(QStringLiteral("test_table_service_create_fk_table"),
                                                               brokenFkSchema);
        QVERIFY(!brokenFkResult.success);
        QVERIFY(brokenFkResult.errorMessage.contains(QStringLiteral("does not exist")));
    }

    void test_dropTable()
    {
        const QString databaseName = QStringLiteral("test_table_service_drop_db");
        const QString tableName = QStringLiteral("test_table_service_drop_table_main");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, baseSchema(tableName), m_dataRoot);

        TaskResult missingResult = table_service::dropTable(QStringLiteral("test_table_service_drop_missing"));
        QVERIFY(!missingResult.success);
        QVERIFY(missingResult.errorMessage.contains(QStringLiteral("does not exist")));

        // Clear currentDatabase to test empty database case
        currentDatabase.clear();
        QVERIFY(database_service::useDatabase(databaseName).success);

        TaskResult dropResult = table_service::dropTable(tableName);
        QVERIFY2(dropResult.success, qPrintable(dropResult.errorMessage));

        SelectRowsResult tables = table_service::showTables();
        QVERIFY2(tables.success, qPrintable(tables.errorMessage));
        QCOMPARE(tables.resultTable.rows.size(), 0);

        repo::TabRepo tabRepo(databaseName, m_dataRoot);
        QString error;
        QVERIFY(!tabRepo.hasTable(tableName, &error));
        QVERIFY(error.isEmpty());

        const QString parentTableName = QStringLiteral("test_table_service_drop_parent_table");
        const QString childTableName = QStringLiteral("test_table_service_drop_child_table");
        ensureTable(databaseName, parentTableName, baseSchema(parentTableName), m_dataRoot);
        ensureTable(databaseName,
                    childTableName,
                    [&]() {
                        tabledef::TableSchema schema = baseSchema(childTableName);
                        schema.columns.append(makeColumn(QStringLiteral("parent_id"), tabledef::ColumnType::Int, 0, true));
                        schema.constraints.append(makeForeignKey(QStringLiteral("fk_%1_parent").arg(childTableName),
                                                                 {QStringLiteral("parent_id")},
                                                                 parentTableName,
                                                                 {QStringLiteral("id")}));
                        return schema;
                    }(),
                    m_dataRoot);

        TaskResult restrictedDrop = table_service::dropTable(parentTableName);
        QVERIFY(!restrictedDrop.success);
        QVERIFY(restrictedDrop.errorMessage.contains(QStringLiteral("referenced by foreign key")));

        TaskResult childDrop = table_service::dropTable(childTableName);
        QVERIFY2(childDrop.success, qPrintable(childDrop.errorMessage));

        TaskResult parentDrop = table_service::dropTable(parentTableName);
        QVERIFY2(parentDrop.success, qPrintable(parentDrop.errorMessage));
    }

    void test_addColumn()
    {
        const QString databaseName = QStringLiteral("test_table_service_add_column_db");
        const QString tableName = QStringLiteral("test_table_service_add_column_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, baseSchema(tableName), m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("1"), QStringLiteral("alice")}, m_dataRoot);

        ColumnDefinition addDefinition;
        addDefinition.column = makeColumn(QStringLiteral("age"), tabledef::ColumnType::Int, 0, false, QStringLiteral("18"));

        TaskResult addResult = table_service::addColumn(tableName, addDefinition);
        QVERIFY2(addResult.success, qPrintable(addResult.errorMessage));

        TextResult describeResult = table_service::describeTable(tableName);
        QVERIFY2(describeResult.success, qPrintable(describeResult.errorMessage));
        QVERIFY(describeResult.text.contains(QStringLiteral("age")));
        QVERIFY(describeResult.text.contains(QStringLiteral("DEFAULT 18")));

        SelectRowsResult rows = tuple_service::selectRows(tableName,
                                 {QStringLiteral("*")},
                                 {});
        QVERIFY2(rows.success, qPrintable(rows.errorMessage));
        const QStringList expectedColumns{QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("age")};
        QCOMPARE(rows.resultTable.columns, expectedColumns);
        QCOMPARE(rows.resultTable.rows.size(), 1);
        const QStringList expectedRow{QStringLiteral("1"), QStringLiteral("alice"), QStringLiteral("18")};
        QCOMPARE(rows.resultTable.rows.first(), expectedRow);

        TaskResult duplicateResult = table_service::addColumn(tableName, addDefinition);
        QVERIFY(!duplicateResult.success);
        QVERIFY(duplicateResult.errorMessage.contains(QStringLiteral("already exists")));
    }

    void test_addColumnRejectsGeneratedConstraintViolation()
    {
        const QString databaseName = QStringLiteral("test_table_service_add_column_violation_db");
        const QString tableName = QStringLiteral("test_table_service_add_column_violation_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, baseSchema(tableName), m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("1"), QStringLiteral("alice")}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("2"), QStringLiteral("bob")}, m_dataRoot);

        ColumnDefinition addDefinition;
        addDefinition.column = makeColumn(QStringLiteral("code"), tabledef::ColumnType::Varchar, 16, false, QStringLiteral("dup"));
        addDefinition.unique = true;

        TaskResult addResult = table_service::addColumn(tableName, addDefinition);
        QVERIFY(!addResult.success);
        QVERIFY(addResult.errorMessage.contains(QStringLiteral("duplicate")));
    }

    void test_addColumnCreatesBoundIndex()
    {
        const QString databaseName = QStringLiteral("test_table_service_add_column_bound_index_db");
        const QString tableName = QStringLiteral("test_table_service_add_column_bound_index_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, baseSchema(tableName), m_dataRoot);

        ColumnDefinition addDefinition;
        addDefinition.column = makeColumn(QStringLiteral("code"), tabledef::ColumnType::Varchar, 16, false);
        addDefinition.unique = true;

        TaskResult addResult = table_service::addColumn(tableName, addDefinition);
        QVERIFY2(addResult.success, qPrintable(addResult.errorMessage));

        QString error;
        const QString indexName = findIndexNameByColumns(databaseName,
                                                        tableName,
                                                        m_dataRoot,
                                                        {QStringLiteral("code")},
                                                        &error);
        QVERIFY(error.isEmpty());
        QVERIFY(!indexName.isEmpty());

        const bool isUnique = readSortIndexIsUnique(databaseName, tableName, indexName, m_dataRoot, &error);
        QVERIFY(error.isEmpty());
        QVERIFY(isUnique);
    }

    void test_deleteColumn()
    {
        const QString databaseName = QStringLiteral("test_table_service_delete_column_db");
        const QString tableName = QStringLiteral("test_table_service_delete_column_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, agedSchema(tableName), m_dataRoot);
        seedRow(databaseName,
                tableName,
                {QStringLiteral("1"), QStringLiteral("alice"), QStringLiteral("22")},
                m_dataRoot);

        TaskResult deleteResult = table_service::deleteColumn(tableName, QStringLiteral("age"));
        QVERIFY2(deleteResult.success, qPrintable(deleteResult.errorMessage));

        SelectRowsResult rows = tuple_service::selectRows(tableName,
                                 {QStringLiteral("*")},
                                 {});
        QVERIFY2(rows.success, qPrintable(rows.errorMessage));
        const QStringList expectedColumns{QStringLiteral("id"), QStringLiteral("name")};
        const QStringList expectedRow{QStringLiteral("1"), QStringLiteral("alice")};
        QCOMPARE(rows.resultTable.columns, expectedColumns);
        QCOMPARE(rows.resultTable.rows.first(), expectedRow);

        TextResult describeResult = table_service::describeTable(tableName);
        QVERIFY2(describeResult.success, qPrintable(describeResult.errorMessage));
        QVERIFY(!describeResult.text.contains(QStringLiteral("age")));

        TaskResult missingResult = table_service::deleteColumn(tableName, QStringLiteral("missing"));
        QVERIFY(!missingResult.success);
        QVERIFY(missingResult.errorMessage.contains(QStringLiteral("does not exist")));
    }

    void test_modifyColumn()
    {
        const QString databaseName = QStringLiteral("test_table_service_modify_column_db");
        const QString tableName = QStringLiteral("test_table_service_modify_column_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, baseSchema(tableName), m_dataRoot);

        ColumnDefinition modifyDefinition;
        modifyDefinition.column = makeColumn(QStringLiteral("name"), tabledef::ColumnType::Varchar, 64, true, QStringLiteral("guest"));

        TaskResult modifyResult = table_service::modifyColumn(tableName,
                                      QStringLiteral("name"),
                                      modifyDefinition);
        QVERIFY2(modifyResult.success, qPrintable(modifyResult.errorMessage));

        repo::MetaRepo metaRepo(databaseName, tableName, m_dataRoot);
        QString error;
        const QList<tabledef::Column> columns = metaRepo.listColumns(&error);
        QVERIFY(error.isEmpty());
        QCOMPARE(columns.size(), 2);
        QCOMPARE(columns.at(1).name, QStringLiteral("name"));
        QCOMPARE(columns.at(1).defaultValue, QStringLiteral("guest"));
        QVERIFY(columns.at(1).notNull);

        ColumnDefinition missingDefinition;
        missingDefinition.column = makeColumn(QStringLiteral("missing_new"), tabledef::ColumnType::Varchar, 64, true, QStringLiteral("guest"));
        TaskResult missingResult = table_service::modifyColumn(tableName,
                           QStringLiteral("missing"),
                           missingDefinition);
        QVERIFY(!missingResult.success);
        QVERIFY(missingResult.errorMessage.contains(QStringLiteral("does not exist")));
    }

    void test_modifyColumnRejectsEmptyDefinitionName()
    {
        const QString databaseName = QStringLiteral("test_table_service_modify_column_empty_name_db");
        const QString tableName = QStringLiteral("test_table_service_modify_column_empty_name_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, baseSchema(tableName), m_dataRoot);

        ColumnDefinition modifyDefinition;
        modifyDefinition.column = makeColumn(QString(), tabledef::ColumnType::Varchar, 64, true, QStringLiteral("guest"));

        TaskResult modifyResult = table_service::modifyColumn(tableName,
                                                             QStringLiteral("name"),
                                                             modifyDefinition);
        QVERIFY(!modifyResult.success);
        QVERIFY(modifyResult.errorMessage.contains(QStringLiteral("column name cannot be empty")));
    }

    void test_modifyColumnRenamesIndexedColumn()
    {
        const QString databaseName = QStringLiteral("test_table_service_modify_column_rename_index_db");
        const QString tableName = QStringLiteral("test_table_service_modify_column_rename_index_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, baseSchema(tableName), m_dataRoot);

        seedRow(databaseName, tableName, {QStringLiteral("1"), QStringLiteral("alice")}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("2"), QStringLiteral("bob")}, m_dataRoot);

        TaskResult createIndexResult = table_service::createIndex(tableName,
                                                                  QStringLiteral("idx_test_table_service_name"),
                                                                  {QStringLiteral("name")},
                                                                  false);
        QVERIFY2(createIndexResult.success, qPrintable(createIndexResult.errorMessage));

        ColumnDefinition modifyDefinition;
        modifyDefinition.column = makeColumn(QStringLiteral("full_name"), tabledef::ColumnType::Varchar, 64, true);

        TaskResult modifyResult = table_service::modifyColumn(tableName,
                                                             QStringLiteral("name"),
                                                             modifyDefinition);
        QVERIFY2(modifyResult.success, qPrintable(modifyResult.errorMessage));

        QString error;
        const QString indexName = findIndexNameByColumns(databaseName,
                                                        tableName,
                                                        m_dataRoot,
                                                        {QStringLiteral("full_name")},
                                                        &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(indexName, QStringLiteral("idx_test_table_service_name"));

        const QStringList matches = searchIndex(databaseName,
                                               tableName,
                                               indexName,
                                               {QStringLiteral("alice")},
                                               m_dataRoot,
                                               &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(matches.size(), 1);
    }

    void test_modifyColumnRejectsTypeConversionFailure()
    {
        const QString databaseName = QStringLiteral("test_table_service_modify_column_convert_db");
        const QString tableName = QStringLiteral("test_table_service_modify_column_convert_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, baseSchema(tableName), m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("1"), QStringLiteral("alice")}, m_dataRoot);

        ColumnDefinition modifyDefinition;
        modifyDefinition.column = makeColumn(QStringLiteral("name"), tabledef::ColumnType::Int, 0, true);

        TaskResult modifyResult = table_service::modifyColumn(tableName,
                                      QStringLiteral("name"),
                                      modifyDefinition);
        QVERIFY(!modifyResult.success);
        QVERIFY(modifyResult.errorMessage.contains(QStringLiteral("cannot be converted to INT")));
    }

    void test_modifyColumnCreatesBoundIndex()
    {
        const QString databaseName = QStringLiteral("test_table_service_modify_column_bound_index_db");
        const QString tableName = QStringLiteral("test_table_service_modify_column_bound_index_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, agedSchema(tableName), m_dataRoot);

        ColumnDefinition modifyDefinition;
        modifyDefinition.column = makeColumn(QStringLiteral("age"), tabledef::ColumnType::Int, 0, false);
        modifyDefinition.unique = true;

        TaskResult modifyResult = table_service::modifyColumn(tableName,
                                                             QStringLiteral("age"),
                                                             modifyDefinition);
        QVERIFY2(modifyResult.success, qPrintable(modifyResult.errorMessage));

        QString error;
        const QString indexName = findIndexNameByColumns(databaseName,
                                                        tableName,
                                                        m_dataRoot,
                                                        {QStringLiteral("age")},
                                                        &error);
        QVERIFY(error.isEmpty());
        QVERIFY(!indexName.isEmpty());

        const bool isUnique = readSortIndexIsUnique(databaseName, tableName, indexName, m_dataRoot, &error);
        QVERIFY(error.isEmpty());
        QVERIFY(isUnique);
    }

    void test_addConstraint()
    {
        const QString databaseName = QStringLiteral("test_table_service_add_constraint_db");
        const QString tableName = QStringLiteral("test_table_service_add_constraint_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, baseSchema(tableName), m_dataRoot);

        const tabledef::Constraint uniqueConstraint = makeUnique(QStringLiteral("uq_test_table_service_name"), {QStringLiteral("name")});
        TaskResult addResult = table_service::addConstraint(tableName, uniqueConstraint);
        QVERIFY2(addResult.success, qPrintable(addResult.errorMessage));

        TextResult createText = table_service::showCreateTable(tableName);
        QVERIFY2(createText.success, qPrintable(createText.errorMessage));
        QVERIFY(createText.text.contains(QStringLiteral("UNIQUE")));
        QVERIFY(createText.text.contains(QStringLiteral("uq_test_table_service_name")));

        const tabledef::Constraint duplicateSemanticConstraint = makeUnique(QStringLiteral("uq_test_table_service_name_dup"),
                                           {QStringLiteral("name")});
        TaskResult duplicateResult = table_service::addConstraint(tableName, duplicateSemanticConstraint);
        QVERIFY(!duplicateResult.success);
        QVERIFY(duplicateResult.errorMessage.contains(QStringLiteral("duplicate")));
    }

    void test_addForeignKeyConstraintWithActions()
    {
        const QString databaseName = QStringLiteral("test_table_service_fk_action_db");
        const QString parentTableName = QStringLiteral("test_table_service_fk_action_parent");
        const QString childTableName = QStringLiteral("test_table_service_fk_action_child");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, parentTableName, baseSchema(parentTableName), m_dataRoot);
        ensureTable(databaseName, childTableName, tableWithForeignKeyColumn(childTableName), m_dataRoot);

        const tabledef::Constraint foreignKey = makeForeignKey(QStringLiteral("fk_test_table_service_parent"),
                                                               {QStringLiteral("parent_id")},
                                                               parentTableName,
                                                               {QStringLiteral("id")},
                                                               tabledef::ForeignKeyAction::Cascade,
                                                               tabledef::ForeignKeyAction::SetNull);
        TaskResult addResult = table_service::addConstraint(childTableName, foreignKey);
        QVERIFY2(addResult.success, qPrintable(addResult.errorMessage));

        QString error;
        repo::ConstraintRepo constraintRepo(databaseName, childTableName, m_dataRoot);
        const QList<tabledef::Constraint> constraints = constraintRepo.listConstraints(&error);
        QVERIFY(error.isEmpty());

        bool foundActionConstraint = false;
        for (const tabledef::Constraint &constraint : constraints) {
            if (constraint.name == QStringLiteral("fk_test_table_service_parent")) {
                QCOMPARE(constraint.onDeleteAction, tabledef::ForeignKeyAction::Cascade);
                QCOMPARE(constraint.onUpdateAction, tabledef::ForeignKeyAction::SetNull);
                foundActionConstraint = true;
            }
        }
        QVERIFY(foundActionConstraint);

        TextResult createText = table_service::showCreateTable(childTableName);
        QVERIFY2(createText.success, qPrintable(createText.errorMessage));
        QVERIFY(createText.text.contains(QStringLiteral("ON DELETE CASCADE")));
        QVERIFY(createText.text.contains(QStringLiteral("ON UPDATE SET NULL")));
    }

    void test_addConstraintRejectsDuplicateConstraintName()
    {
        const QString databaseName = QStringLiteral("test_table_service_add_constraint_name_dup_db");
        const QString tableName = QStringLiteral("test_table_service_add_constraint_name_dup_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, baseSchema(tableName), m_dataRoot);

        const tabledef::Constraint conflictingConstraint = makeUnique(QStringLiteral("pk_%1_id").arg(tableName),
                                                                      {QStringLiteral("name")});
        TaskResult addResult = table_service::addConstraint(tableName, conflictingConstraint);
        QVERIFY(!addResult.success);
        QVERIFY(addResult.errorMessage.contains(QStringLiteral("already exists")));
    }

    void test_addConstraintRejectsExistingDataViolations()
    {
        const QString databaseName = QStringLiteral("test_table_service_add_constraint_data_db");
        const QString tableName = QStringLiteral("test_table_service_add_constraint_data_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, baseSchema(tableName), m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("1"), QStringLiteral("alice")}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("2"), QStringLiteral("alice")}, m_dataRoot);

        const tabledef::Constraint uniqueConstraint = makeUnique(QStringLiteral("uq_test_table_service_name_data"),
                                                                {QStringLiteral("name")});
        TaskResult addResult = table_service::addConstraint(tableName, uniqueConstraint);
        QVERIFY(!addResult.success);
        QVERIFY(addResult.errorMessage.contains(QStringLiteral("duplicate values")));
    }

    void test_addConstraintRejectsBrokenForeignKey()
    {
        const QString databaseName = QStringLiteral("test_table_service_add_constraint_fk_db");
        const QString tableName = QStringLiteral("test_table_service_add_constraint_fk_table");
        const QString parentTableName = QStringLiteral("test_table_service_add_constraint_fk_parent");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, tableWithForeignKeyColumn(tableName), m_dataRoot);

        const tabledef::Constraint missingParentConstraint = makeForeignKey(QStringLiteral("fk_test_table_service_missing_parent"),
                                                                            {QStringLiteral("parent_id")},
                                                                            QStringLiteral("missing_parent_table"),
                                                                            {QStringLiteral("id")});
        TaskResult missingParentResult = table_service::addConstraint(tableName, missingParentConstraint);
        QVERIFY(!missingParentResult.success);
        QVERIFY(missingParentResult.errorMessage.contains(QStringLiteral("does not exist")));

        ensureTable(databaseName, parentTableName, baseSchema(parentTableName), m_dataRoot);
        const tabledef::Constraint missingColumnConstraint = makeForeignKey(QStringLiteral("fk_test_table_service_missing_column"),
                                                                            {QStringLiteral("parent_id")},
                                                                            parentTableName,
                                                                            {QStringLiteral("missing_id")} );
        TaskResult missingColumnResult = table_service::addConstraint(tableName, missingColumnConstraint);
        QVERIFY(!missingColumnResult.success);
        QVERIFY(missingColumnResult.errorMessage.contains(QStringLiteral("referenced column")));
    }

    void test_modifyConstraint()
    {
        const QString databaseName = QStringLiteral("test_table_service_modify_constraint_db");
        const QString tableName = QStringLiteral("test_table_service_modify_constraint_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, baseSchema(tableName), m_dataRoot);

        const tabledef::Constraint originalConstraint = makeUnique(QStringLiteral("uq_test_table_service_name"), {QStringLiteral("name")});
        QVERIFY(table_service::addConstraint(tableName, originalConstraint).success);

        const tabledef::Constraint modifiedConstraint = makeUnique(QStringLiteral("uq_test_table_service_renamed"), {QStringLiteral("name")});
        TaskResult modifyResult = table_service::modifyConstraint(tableName,
                                      QStringLiteral("uq_test_table_service_name"),
                                      modifiedConstraint);
        QVERIFY2(modifyResult.success, qPrintable(modifyResult.errorMessage));

        TextResult createText = table_service::showCreateTable(tableName);
        QVERIFY2(createText.success, qPrintable(createText.errorMessage));
        QVERIFY(createText.text.contains(QStringLiteral("uq_test_table_service_renamed")));
        QVERIFY(!createText.text.contains(QStringLiteral("uq_test_table_service_name")));

        const tabledef::Constraint missingConstraint = makeUnique(QStringLiteral("uq_test_table_service_missing"), {QStringLiteral("name")});
        TaskResult missingResult = table_service::modifyConstraint(tableName,
                                       QStringLiteral("missing"),
                                       missingConstraint);
        QVERIFY(!missingResult.success);
        QVERIFY(missingResult.errorMessage.contains(QStringLiteral("does not exist")));
    }

    void test_modifyConstraintUpdatesExistingBoundIndexMetadata()
    {
        const QString databaseName = QStringLiteral("test_table_service_modify_constraint_meta_db");
        const QString tableName = QStringLiteral("test_table_service_modify_constraint_meta_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, agedSchema(tableName), m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("1"), QStringLiteral("alice"), QStringLiteral("22")}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("2"), QStringLiteral("bob"), QStringLiteral("33")}, m_dataRoot);

        const tabledef::Constraint originalConstraint = makeUnique(QStringLiteral("uq_test_table_service_meta"),
                                                                  {QStringLiteral("name")});
        tabledef::Constraint originalConstraintWithIndex = originalConstraint;
        originalConstraintWithIndex.indexName = QStringLiteral("uq_test_table_service_meta_idx");
        QVERIFY(table_service::addConstraint(tableName, originalConstraintWithIndex).success);

        const tabledef::Constraint modifiedConstraint = [&]() {
            tabledef::Constraint constraint = makeUnique(QStringLiteral("uq_test_table_service_meta"),
                                                        {QStringLiteral("age")});
            constraint.indexName = QStringLiteral("uq_test_table_service_meta_idx");
            return constraint;
        }();

        TaskResult modifyResult = table_service::modifyConstraint(tableName,
                                                                  QStringLiteral("uq_test_table_service_meta"),
                                                                  modifiedConstraint);
        QVERIFY2(modifyResult.success, qPrintable(modifyResult.errorMessage));

        QString error;
        repo::IndexRepo indexRepo(databaseName, tableName, m_dataRoot);
        const QList<tabledef::IndexMeta> indexes = indexRepo.listIndexes(&error);
        QVERIFY(error.isEmpty());

        bool foundIndex = false;
        for (const tabledef::IndexMeta &index : indexes) {
            if (index.indexName == QStringLiteral("uq_test_table_service_meta_idx")) {
                QCOMPARE(index.columnNames, QStringList{QStringLiteral("age")});
                foundIndex = true;
            }
        }
        QVERIFY(foundIndex);

        const QStringList sortIndexColumns = readSortIndexColumns(databaseName,
                                                                  tableName,
                                                                  QStringLiteral("uq_test_table_service_meta_idx"),
                                                                  m_dataRoot,
                                                                  &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(sortIndexColumns, QStringList{QStringLiteral("age")});
    }

    void test_modifyConstraintSurfacesBoundIndexDeletionFailure()
    {
        const QString databaseName = QStringLiteral("test_table_service_modify_constraint_delete_fail_db");
        const QString tableName = QStringLiteral("test_table_service_modify_constraint_delete_fail_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, baseSchema(tableName), m_dataRoot);

        const tabledef::Constraint originalConstraint = makeUnique(QStringLiteral("uq_test_table_service_name"),
                                                                  {QStringLiteral("name")});
        QVERIFY(table_service::addConstraint(tableName, originalConstraint).success);

        QString error;
        const QString indexName = findIndexNameByColumns(databaseName,
                                                        tableName,
                                                        m_dataRoot,
                                                        {QStringLiteral("name")},
                                                        &error);
        QVERIFY(error.isEmpty());
        QVERIFY(!indexName.isEmpty());

        qputenv("DBMS_TEST_FAIL_BOUND_INDEX_REMOVE", QByteArrayLiteral("1"));

        const tabledef::Constraint modifiedConstraint = makeUnique(QStringLiteral("uq_test_table_service_name_renamed"),
                                                                  {QStringLiteral("name")});
        const tabledef::Constraint modifiedConstraintWithNewIndex = [&]() {
            tabledef::Constraint constraint = modifiedConstraint;
            constraint.indexName = QStringLiteral("uq_test_table_service_name_renamed_idx");
            return constraint;
        }();
        TaskResult modifyResult = table_service::modifyConstraint(tableName,
                                                                  QStringLiteral("uq_test_table_service_name"),
                                                                  modifiedConstraintWithNewIndex);
        qunsetenv("DBMS_TEST_FAIL_BOUND_INDEX_REMOVE");
        QVERIFY(!modifyResult.success);
        QVERIFY(modifyResult.errorMessage.contains(QStringLiteral("failed to remove file")));

        TextResult createText = table_service::showCreateTable(tableName);
        QVERIFY2(createText.success, qPrintable(createText.errorMessage));
        QVERIFY(createText.text.contains(QStringLiteral("uq_test_table_service_name")));
        QVERIFY(!createText.text.contains(QStringLiteral("uq_test_table_service_name_renamed")));
    }

    void test_modifyConstraintRejectsBrokenForeignKey()
    {
        const QString databaseName = QStringLiteral("test_table_service_modify_constraint_fk_db");
        const QString tableName = QStringLiteral("test_table_service_modify_constraint_fk_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, tableWithForeignKeyColumn(tableName), m_dataRoot);

        const tabledef::Constraint uniqueConstraint = makeUnique(QStringLiteral("uq_test_table_service_modify_constraint_fk"),
                                                                {QStringLiteral("parent_id")});
        QVERIFY(table_service::addConstraint(tableName, uniqueConstraint).success);

        const tabledef::Constraint foreignKeyConstraint = makeForeignKey(QStringLiteral("uq_test_table_service_modify_constraint_fk"),
                                                                         {QStringLiteral("parent_id")},
                                                                         QStringLiteral("missing_parent_table"),
                                                                         {QStringLiteral("id")} );
        TaskResult modifyResult = table_service::modifyConstraint(tableName,
                                        QStringLiteral("uq_test_table_service_modify_constraint_fk"),
                                        foreignKeyConstraint);
        QVERIFY(!modifyResult.success);
        QVERIFY(modifyResult.errorMessage.contains(QStringLiteral("does not exist")));
    }

    void test_deleteConstraint()
    {
        const QString databaseName = QStringLiteral("test_table_service_delete_constraint_db");
        const QString tableName = QStringLiteral("test_table_service_delete_constraint_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, baseSchema(tableName), m_dataRoot);

        const tabledef::Constraint uniqueConstraint = makeUnique(QStringLiteral("uq_test_table_service_name"), {QStringLiteral("name")});
        QVERIFY(table_service::addConstraint(tableName, uniqueConstraint).success);

        TaskResult deleteResult = table_service::deleteConstraint(tableName,
                                      QStringLiteral("uq_test_table_service_name"));
        QVERIFY2(deleteResult.success, qPrintable(deleteResult.errorMessage));

        TextResult createText = table_service::showCreateTable(tableName);
        QVERIFY2(createText.success, qPrintable(createText.errorMessage));
        QVERIFY(!createText.text.contains(QStringLiteral("uq_test_table_service_name")));

        TaskResult missingResult = table_service::deleteConstraint(tableName,
                                       QStringLiteral("missing"));
        QVERIFY(!missingResult.success);
        QVERIFY(missingResult.errorMessage.contains(QStringLiteral("does not exist")));
    }

    void test_deleteConstraintSurfacesBoundIndexDeletionFailure()
    {
        const QString databaseName = QStringLiteral("test_table_service_delete_constraint_delete_fail_db");
        const QString tableName = QStringLiteral("test_table_service_delete_constraint_delete_fail_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, baseSchema(tableName), m_dataRoot);

        const tabledef::Constraint uniqueConstraint = makeUnique(QStringLiteral("uq_test_table_service_name"),
                                                                 {QStringLiteral("name")});
        QVERIFY(table_service::addConstraint(tableName, uniqueConstraint).success);

        QString error;
        const QString indexName = findIndexNameByColumns(databaseName,
                                                        tableName,
                                                        m_dataRoot,
                                                        {QStringLiteral("name")},
                                                        &error);
        QVERIFY(error.isEmpty());
        QVERIFY(!indexName.isEmpty());

        qputenv("DBMS_TEST_FAIL_BOUND_INDEX_REMOVE", QByteArrayLiteral("1"));

        TaskResult deleteResult = table_service::deleteConstraint(tableName,
                                                                  QStringLiteral("uq_test_table_service_name"));
        qunsetenv("DBMS_TEST_FAIL_BOUND_INDEX_REMOVE");
        QVERIFY(!deleteResult.success);
        QVERIFY(deleteResult.errorMessage.contains(QStringLiteral("failed to remove file")));

        TextResult createText = table_service::showCreateTable(tableName);
        QVERIFY2(createText.success, qPrintable(createText.errorMessage));
        QVERIFY(createText.text.contains(QStringLiteral("uq_test_table_service_name")));
    }

    void test_showTables()
    {
        const QString databaseName = QStringLiteral("test_table_service_show_tables_db");
        const QString firstTableName = QStringLiteral("test_table_service_show_tables_a");
        const QString secondTableName = QStringLiteral("test_table_service_show_tables_b");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, firstTableName, baseSchema(firstTableName), m_dataRoot);
        ensureTable(databaseName, secondTableName, baseSchema(secondTableName), m_dataRoot);

        SelectRowsResult tables = table_service::showTables();
        QVERIFY2(tables.success, qPrintable(tables.errorMessage));
        const QStringList expectedTables{firstTableName, secondTableName};
        QCOMPARE(listedTables(tables), expectedTables);
    }

    void test_describeTable()
    {
        const QString databaseName = QStringLiteral("test_table_service_describe_db");
        const QString tableName = QStringLiteral("test_table_service_describe_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, agedSchema(tableName), m_dataRoot);
        QVERIFY(table_service::addConstraint(tableName,
                             makeUnique(QStringLiteral("uq_test_table_service_name"), {QStringLiteral("name")})).success);

        TextResult describeResult = table_service::describeTable(tableName);
        QVERIFY2(describeResult.success, qPrintable(describeResult.errorMessage));
        QVERIFY(describeResult.text.contains(QStringLiteral("id")));
        QVERIFY(describeResult.text.contains(QStringLiteral("name")));
        QVERIFY(describeResult.text.contains(QStringLiteral("age")));
        QVERIFY(describeResult.text.contains(QStringLiteral("UNIQUE")));
    }

    void test_showCreateTable()
    {
        const QString databaseName = QStringLiteral("test_table_service_show_create_db");
        const QString tableName = QStringLiteral("test_table_service_show_create_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, agedSchema(tableName), m_dataRoot);
        QVERIFY(table_service::addConstraint(tableName,
                             makeUnique(QStringLiteral("uq_test_table_service_name"), {QStringLiteral("name")})).success);

        TextResult createText = table_service::showCreateTable(tableName);
        QVERIFY2(createText.success, qPrintable(createText.errorMessage));
        QVERIFY(createText.text.startsWith(QStringLiteral("CREATE TABLE")));
        QVERIFY(createText.text.contains(QStringLiteral("age")));
        QVERIFY(createText.text.contains(QStringLiteral("CONSTRAINT")));
    }

    void test_createIndexAndDropIndex()
    {
        const QString databaseName = QStringLiteral("test_table_service_create_index_db");
        const QString tableName = QStringLiteral("test_table_service_create_index_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, baseSchema(tableName), m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("1"), QStringLiteral("alice")}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("2"), QStringLiteral("bob")}, m_dataRoot);

        TaskResult createResult = table_service::createIndex(tableName,
                                                             QStringLiteral("idx_test_table_service_name"),
                                                             {QStringLiteral("name")},
                                                             false);
        QVERIFY2(createResult.success, qPrintable(createResult.errorMessage));

        QString error;
        const QString indexName = findIndexNameByColumns(databaseName,
                                                        tableName,
                                                        m_dataRoot,
                                                        {QStringLiteral("name")},
                                                        &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(indexName, QStringLiteral("idx_test_table_service_name"));

        const QStringList matches = searchIndex(databaseName,
                                               tableName,
                                               indexName,
                                               {QStringLiteral("alice")},
                                               m_dataRoot,
                                               &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(matches.size(), 1);

        TaskResult duplicateResult = table_service::createIndex(tableName,
                                                                QStringLiteral("idx_test_table_service_name"),
                                                                {QStringLiteral("name")},
                                                                false);
        QVERIFY(!duplicateResult.success);
        QVERIFY(duplicateResult.errorMessage.contains(QStringLiteral("already exists")));

        TaskResult dropResult = table_service::dropIndex(tableName, indexName);
        QVERIFY2(dropResult.success, qPrintable(dropResult.errorMessage));

        const QStringList indexes = listedIndexes(databaseName, tableName, m_dataRoot, &error);
        QVERIFY(error.isEmpty());
        QVERIFY(!indexes.contains(indexName));

        repo::FlatFileTableStore store(m_dataRoot);
        QVERIFY(!QFile(store.getSortIndexFilePath(databaseName, tableName, indexName)).exists());
    }

    void test_createIndexCleansUpOnTreeFailure()
    {
        const QString databaseName = QStringLiteral("test_table_service_create_index_cleanup_db");
        const QString tableName = QStringLiteral("test_table_service_create_index_cleanup_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, baseSchema(tableName), m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("1"), QStringLiteral("alice")}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("2"), QStringLiteral("bob")}, m_dataRoot);

        qputenv("DBMS_TEST_FAIL_SORT_INDEX_CREATE_AFTER_WRITE", QByteArrayLiteral("1"));
        TaskResult createResult = table_service::createIndex(tableName,
                                                             QStringLiteral("idx_test_table_service_cleanup"),
                                                             {QStringLiteral("name")},
                                                             false);
        qunsetenv("DBMS_TEST_FAIL_SORT_INDEX_CREATE_AFTER_WRITE");
        QVERIFY(!createResult.success);

        QString error;
        const QString indexName = findIndexNameByColumns(databaseName,
                                                        tableName,
                                                        m_dataRoot,
                                                        {QStringLiteral("name")},
                                                        &error);
        QVERIFY(error.isEmpty());
        QVERIFY(indexName.isEmpty());

        repo::FlatFileTableStore store(m_dataRoot);
        QVERIFY(!QFile(store.getSortIndexFilePath(databaseName, tableName, QStringLiteral("idx_test_table_service_cleanup"))).exists());
    }

    void test_sortIndexLeafNextChain()
    {
        const QString databaseName = QStringLiteral("test_table_service_sort_index_leaf_next_db");
        const QString tableName = QStringLiteral("test_table_service_sort_index_leaf_next_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, baseSchema(tableName), m_dataRoot);

        seedRow(databaseName, tableName, {QStringLiteral("1"), QStringLiteral("alice")}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("2"), QStringLiteral("bob")}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("3"), QStringLiteral("carol")}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("4"), QStringLiteral("diana")}, m_dataRoot);

        TaskResult createResult = table_service::createIndex(tableName,
                                                             QStringLiteral("idx_test_table_service_leaf_next"),
                                                             {QStringLiteral("name")},
                                                             false);
        QVERIFY2(createResult.success, qPrintable(createResult.errorMessage));

        QString error;
        int nextLeafId = -1;
        bool nextLeafExists = false;
        QVERIFY2(readSortIndexLeftmostLeafNext(databaseName,
                                               tableName,
                                               QStringLiteral("idx_test_table_service_leaf_next"),
                                               m_dataRoot,
                                               &nextLeafId,
                                               &nextLeafExists,
                                               &error),
                 qPrintable(error));
        QVERIFY(nextLeafId >= 0);
        QVERIFY(nextLeafExists);
    }

    void test_primaryKeyBoundIndexIsUnique()
    {
        const QString databaseName = QStringLiteral("test_table_service_pk_index_unique_db");
        const QString tableName = QStringLiteral("test_table_service_pk_index_unique_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, baseSchema(tableName), m_dataRoot);

        QString error;
        const QString pkIndexName = findIndexNameByColumns(databaseName,
                                                           tableName,
                                                           m_dataRoot,
                                                           {QStringLiteral("id")},
                                                           &error);
        QVERIFY(error.isEmpty());
        QVERIFY(!pkIndexName.isEmpty());

        const bool isUnique = readSortIndexIsUnique(databaseName, tableName, pkIndexName, m_dataRoot, &error);
        QVERIFY(error.isEmpty());
        QVERIFY(isUnique);
    }

    void test_sortIndexPersistsSourceTable()
    {
        const QString databaseName = QStringLiteral("test_table_service_sort_index_source_table_db");
        const QString tableName = QStringLiteral("test_table_service_sort_index_source_table_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, baseSchema(tableName), m_dataRoot);

        const QString indexName = QStringLiteral("pk_%1_id__idx").arg(tableName);
        QString error;
        const QString sourceTable = readSortIndexSourceTable(databaseName,
                                                             tableName,
                                                             indexName,
                                                             m_dataRoot,
                                                             &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(sourceTable, tableName);
    }

    void test_createUniqueIndexRejectsDuplicateData()
    {
        const QString databaseName = QStringLiteral("test_table_service_create_unique_index_db");
        const QString tableName = QStringLiteral("test_table_service_create_unique_index_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, baseSchema(tableName), m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("1"), QStringLiteral("alice")}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("2"), QStringLiteral("alice")}, m_dataRoot);

        TaskResult createResult = table_service::createIndex(tableName,
                                                             QStringLiteral("uq_test_table_service_name_idx"),
                                                             {QStringLiteral("name")},
                                                             true);
        QVERIFY(!createResult.success);
        QVERIFY(createResult.errorMessage.contains(QStringLiteral("duplicate")));
    }

    void test_createUniqueIndexHandlesSeparatorLikeValues()
    {
        const QString databaseName = QStringLiteral("test_table_service_create_unique_index_separator_db");
        const QString tableName = QStringLiteral("test_table_service_create_unique_index_separator_table");
        const QString separator(1, QChar(0x1f));
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, agedSchema(tableName), m_dataRoot);
        seedRow(databaseName,
                tableName,
            {QStringLiteral("1"), QStringLiteral("a") + separator + QStringLiteral("b"), QStringLiteral("c")},
                m_dataRoot);
        seedRow(databaseName,
                tableName,
            {QStringLiteral("2"), QStringLiteral("a"), QStringLiteral("b") + separator + QStringLiteral("c")},
                m_dataRoot);

        TaskResult createResult = table_service::createIndex(tableName,
                                                             QStringLiteral("uq_test_table_service_separator_idx"),
                                                             {QStringLiteral("name"), QStringLiteral("age")},
                                                             true);
        QVERIFY2(createResult.success, qPrintable(createResult.errorMessage));

        QString error;
        const QString indexName = findIndexNameByColumns(databaseName,
                                                        tableName,
                                                        m_dataRoot,
                                                        {QStringLiteral("name"), QStringLiteral("age")},
                                                        &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(indexName, QStringLiteral("uq_test_table_service_separator_idx"));

        const QStringList firstMatches = searchIndex(databaseName,
                                                     tableName,
                                                     indexName,
                                                     {QStringLiteral("a") + separator + QStringLiteral("b"), QStringLiteral("c")},
                                                     m_dataRoot,
                                                     &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(firstMatches.size(), 1);

        const QStringList secondMatches = searchIndex(databaseName,
                                                      tableName,
                                                      indexName,
                                                      {QStringLiteral("a"), QStringLiteral("b") + separator + QStringLiteral("c")},
                                                      m_dataRoot,
                                                      &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(secondMatches.size(), 1);
    }

    void test_createUniqueIndexIgnoresEmptyValues()
    {
        const QString databaseName = QStringLiteral("test_table_service_create_unique_index_empty_db");
        const QString tableName = QStringLiteral("test_table_service_create_unique_index_empty_table");
        ensureDatabase(databaseName, m_dataRoot);

        tabledef::TableSchema schema;
        schema.tableName = tableName;
        schema.columns = {
            makeColumn(QStringLiteral("id"), tabledef::ColumnType::Int, 0, true),
            makeColumn(QStringLiteral("name"), tabledef::ColumnType::Varchar, 64, false),
        };
        schema.constraints = {
            makePrimaryKey(QStringLiteral("pk_%1_id").arg(tableName), {QStringLiteral("id")}),
        };
        ensureTable(databaseName, tableName, schema, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("1"), QString()}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("2"), QString()}, m_dataRoot);

        TaskResult createResult = table_service::createIndex(tableName,
                                                             QStringLiteral("uq_test_table_service_empty_name_idx"),
                                                             {QStringLiteral("name")},
                                                             true);
        QVERIFY2(createResult.success, qPrintable(createResult.errorMessage));
    }

    void test_boundIndexLifecycle()
    {
        const QString databaseName = QStringLiteral("test_table_service_bound_index_db");
        const QString tableName = QStringLiteral("test_table_service_bound_index_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, baseSchema(tableName), m_dataRoot);

        tabledef::Constraint uniqueConstraint = makeUnique(QStringLiteral("uq_test_table_service_name"),
                                                           {QStringLiteral("name")});
        TaskResult addResult = table_service::addConstraint(tableName, uniqueConstraint);
        QVERIFY2(addResult.success, qPrintable(addResult.errorMessage));

        QString error;
        QString boundIndexName = findIndexNameByColumns(databaseName,
                                                        tableName,
                                                        m_dataRoot,
                                                        {QStringLiteral("name")},
                                                        &error);
        QVERIFY(error.isEmpty());
        QVERIFY(!boundIndexName.isEmpty());

        uniqueConstraint.name = QStringLiteral("uq_test_table_service_name_renamed");
        uniqueConstraint.indexName = QStringLiteral("uq_test_table_service_name_renamed_idx");
        TaskResult modifyResult = table_service::modifyConstraint(tableName,
                                                                  QStringLiteral("uq_test_table_service_name"),
                                                                  uniqueConstraint);
        QVERIFY2(modifyResult.success, qPrintable(modifyResult.errorMessage));

        repo::ConstraintRepo modifiedConstraintRepo(databaseName, tableName, m_dataRoot);
        const QList<tabledef::Constraint> modifiedConstraints = modifiedConstraintRepo.listConstraints(&error);
        QVERIFY(error.isEmpty());
        bool foundRenamedConstraint = false;
        for (const tabledef::Constraint &constraint : modifiedConstraints) {
            if (constraint.name == QStringLiteral("uq_test_table_service_name_renamed")) {
                QCOMPARE(constraint.indexName, QStringLiteral("uq_test_table_service_name_renamed_idx"));
                foundRenamedConstraint = true;
            }
        }
        QVERIFY(foundRenamedConstraint);

        const QString renamedIndexName = findIndexNameByColumns(databaseName,
                                                                tableName,
                                                                m_dataRoot,
                                                                {QStringLiteral("name")},
                                                                &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(renamedIndexName, QStringLiteral("uq_test_table_service_name_renamed_idx"));

        TaskResult deleteResult = table_service::deleteConstraint(tableName,
                                                                  QStringLiteral("uq_test_table_service_name_renamed"));
        QVERIFY2(deleteResult.success, qPrintable(deleteResult.errorMessage));

        const QStringList remainingIndexes = listedIndexes(databaseName, tableName, m_dataRoot, &error);
        QVERIFY(error.isEmpty());
        QVERIFY(!remainingIndexes.contains(QStringLiteral("uq_test_table_service_name_renamed_idx")));
        QCOMPARE(findIndexNameByColumns(databaseName,
                                        tableName,
                                        m_dataRoot,
                                        {QStringLiteral("name")},
                                        &error),
                 QString());
    }

private:
    QString m_dataRoot;
};

int service_tests::runTableServiceTests()
{
    TableServiceTest test;
    return QTest::qExec(&test);
}

#include "test_table_service.moc"