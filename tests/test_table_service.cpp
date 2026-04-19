#include "../service/service.h"

#include <QDir>
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
                                    const QStringList &referencedColumns)
{
    return tabledef::Constraint{name,
                                tabledef::ConstraintType::ForeignKey,
                                columns,
                                referencedTable,
                                referencedColumns,
                                QString()};
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

        TaskResult duplicateResult = table_service::addConstraint(tableName, uniqueConstraint);
        QVERIFY(!duplicateResult.success);
        QVERIFY(duplicateResult.errorMessage.contains(QStringLiteral("duplicate")));
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