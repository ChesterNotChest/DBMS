#include "../service/service.h"
#include "../service/sql_dispatcher.h"
#include "../utils/service_common/service_common.h"
#include "../utils/sql_parser/sql_parser.h"

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
        QVERIFY(duplicateResult.errorMessage.contains(QStringLiteral("already exists")));
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

    void test_parseCreateTableWithCompositeConstraints()
    {
        const QString sql = QStringLiteral(
            "CREATE TABLE student_score ("
            "id INT, "
            "student_id INT, "
            "course_id INT, "
            "CONSTRAINT uq_student_course UNIQUE (student_id, course_id), "
            "FOREIGN KEY (student_id, course_id) REFERENCES course(id, id)"
            ")");

        const sqlparser::ParseResult result = sqlparser::parseSql(sql);
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.commandType, QStringLiteral("CREATE_TABLE"));

        const auto columns = result.payload.value<QVector<sqlparser::ColumnDef>>(QStringLiteral("columns"));
        QCOMPARE(columns.size(), 3);
        QCOMPARE(columns.at(0).name, QStringLiteral("id"));
        QCOMPARE(columns.at(1).name, QStringLiteral("student_id"));
        QCOMPARE(columns.at(2).name, QStringLiteral("course_id"));
    }

    void test_dispatcherCreateTablePreservesSchemaConstraints()
    {
        const QString databaseName = QStringLiteral("test_table_service_dispatcher_create_db");
        const QString tableName = QStringLiteral("test_table_service_dispatcher_create_table");
        ensureDatabase(databaseName, m_dataRoot);

        SqlDispatcher dispatcher;
        const QString sql = QStringLiteral(
            "CREATE TABLE %1 ("
            "id INT AUTO_INCREMENT, "
            "code VARCHAR(32) NOT NULL DEFAULT 7 UNIQUE, "
            "name VARCHAR(64), "
            "PRIMARY KEY (id)"
            ")").arg(tableName);

        const SqlExecResult execResult = dispatcher.execute(sql);
        QVERIFY2(execResult.success, qPrintable(execResult.errorMessage));

        QString error;
        const tabledef::TableSchema schema = loadUserTableSchema(tableName, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(schema.columns.size(), 3);
        QCOMPARE(schema.columns.at(0).autoIncrement, true);
        QCOMPARE(schema.columns.at(1).notNull, true);
        QCOMPARE(schema.columns.at(1).defaultValue, QStringLiteral("7"));

        int primaryKeyCount = 0;
        int uniqueCount = 0;
        for (const tabledef::Constraint &constraint : schema.constraints) {
            if (constraint.type == tabledef::ConstraintType::PrimaryKey) {
                ++primaryKeyCount;
            } else if (constraint.type == tabledef::ConstraintType::Unique) {
                ++uniqueCount;
            }
        }
        QCOMPARE(primaryKeyCount, 1);
        QCOMPARE(uniqueCount, 1);

        TextResult createText = table_service::showCreateTable(tableName);
        QVERIFY2(createText.success, qPrintable(createText.errorMessage));
        QVERIFY(createText.text.contains(QStringLiteral("AUTO_INCREMENT")));
        QVERIFY(createText.text.contains(QStringLiteral("DEFAULT 7")));
        QVERIFY(createText.text.contains(QStringLiteral("UNIQUE")));
        QVERIFY(createText.text.contains(QStringLiteral("PRIMARY KEY")));
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