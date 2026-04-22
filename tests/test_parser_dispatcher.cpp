#include "../service/service.h"
#include "../service/sql_dispatcher.h"
#include "../utils/sql_parser/sql_parser.h"

#include <QDir>
#include <QtTest>

#include "test_entry.h"

using namespace service;

namespace {

QString testDataRoot()
{
    return QDir(QDir::tempPath()).absoluteFilePath(QStringLiteral("DBMS_test_parser_dispatcher"));
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

void ensureDatabase(const QString &databaseName)
{
    TaskResult result = database_service::createDatabase(databaseName);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    result = database_service::useDatabase(databaseName);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
}

void ensureTable(const QString &tableName, const tabledef::TableSchema &schema)
{
    TaskResult result = table_service::createTable(tableName, schema);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
}

} // namespace

class ParserDispatcherTest : public QObject
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

    void test_parseCreateTableUsesQtBasePayload()
    {
        const QString sql = QStringLiteral(
            "CREATE TABLE child ("
            "id INT PRIMARY KEY, "
            "parent_id INT REFERENCES parent(id) ON DELETE CASCADE ON UPDATE SET NULL, "
            "CONSTRAINT uq_child_pair UNIQUE (id, parent_id), "
            "CONSTRAINT fk_child_parent FOREIGN KEY (parent_id) REFERENCES parent(id) ON DELETE NO ACTION ON UPDATE CASCADE"
            ")");

        const sqlparser::ParseResult result = sqlparser::parseSql(sql);
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.commandType, QStringLiteral("CREATE_TABLE"));
        QCOMPARE(result.payload.value(QStringLiteral("columns")).typeId(), QMetaType::QVariantList);
        QCOMPARE(result.payload.value(QStringLiteral("constraints")).typeId(), QMetaType::QVariantList);

        const QVariantList columns = result.payload.value(QStringLiteral("columns")).toList();
        QCOMPARE(columns.size(), 2);
        QCOMPARE(columns.at(0).typeId(), QMetaType::QVariantMap);
        QCOMPARE(columns.at(1).typeId(), QMetaType::QVariantMap);

        const QVariantMap foreignKeyColumn = columns.at(1).toMap();
        QCOMPARE(foreignKeyColumn.value(QStringLiteral("name")).toString(), QStringLiteral("parent_id"));
        QCOMPARE(foreignKeyColumn.value(QStringLiteral("referencesTable")).toString(), QStringLiteral("parent"));
        QCOMPARE(foreignKeyColumn.value(QStringLiteral("referencedColumns")).toStringList(),
                 QStringList({QStringLiteral("id")}));
        QCOMPARE(foreignKeyColumn.value(QStringLiteral("onDeleteAction")).toString(), QStringLiteral("CASCADE"));
        QCOMPARE(foreignKeyColumn.value(QStringLiteral("onUpdateAction")).toString(), QStringLiteral("SET NULL"));

        const QVariantList constraints = result.payload.value(QStringLiteral("constraints")).toList();
        QCOMPARE(constraints.size(), 2);
        QCOMPARE(constraints.at(0).typeId(), QMetaType::QVariantMap);
        QCOMPARE(constraints.at(1).typeId(), QMetaType::QVariantMap);
        QCOMPARE(constraints.at(0).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("UNIQUE"));
        QCOMPARE(constraints.at(1).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("FOREIGN_KEY"));
        QCOMPARE(constraints.at(1).toMap().value(QStringLiteral("onDeleteAction")).toString(), QStringLiteral("NO ACTION"));
        QCOMPARE(constraints.at(1).toMap().value(QStringLiteral("onUpdateAction")).toString(), QStringLiteral("CASCADE"));
    }

    void test_parseSelectLimitAndRejectUnsupportedClauses()
    {
        const sqlparser::ParseResult limited = sqlparser::parseSql(QStringLiteral("SELECT id FROM student LIMIT 3"));
        QVERIFY2(limited.success, qPrintable(limited.errorMessage));
        QCOMPARE(limited.commandType, QStringLiteral("SELECT"));
        QCOMPARE(limited.payload.value(QStringLiteral("tableName")).toString(), QStringLiteral("student"));
        QCOMPARE(limited.payload.value(QStringLiteral("projection")).toStringList(), QStringList({QStringLiteral("id")}));
        QCOMPARE(limited.payload.value(QStringLiteral("limit")).toInt(), 3);

        const sqlparser::ParseResult ordered = sqlparser::parseSql(QStringLiteral("SELECT * FROM student ORDER BY id"));
        QVERIFY(!ordered.success);
        QVERIFY(ordered.errorMessage.contains(QStringLiteral("unsupported clause")));
    }

    void test_parseUpdateAndDeleteRejectWhere()
    {
        const sqlparser::ParseResult updated = sqlparser::parseSql(
            QStringLiteral("UPDATE student SET name = 'alice' WHERE id = 1"));
        QVERIFY(!updated.success);
        QVERIFY(updated.errorMessage.contains(QStringLiteral("WHERE is not supported")));

        const sqlparser::ParseResult deleted = sqlparser::parseSql(
            QStringLiteral("DELETE FROM student WHERE id = 1"));
        QVERIFY(!deleted.success);
        QVERIFY(deleted.errorMessage.contains(QStringLiteral("WHERE is not supported")));
    }

    void test_parseInsertWithoutColumnListProducesSingleRowPayload()
    {
        const sqlparser::ParseResult result = sqlparser::parseSql(
            QStringLiteral("INSERT INTO student VALUES (1, 'alice')"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.commandType, QStringLiteral("INSERT"));
        QCOMPARE(result.payload.value(QStringLiteral("tableName")).toString(), QStringLiteral("student"));
        QCOMPARE(result.payload.value(QStringLiteral("columnNames")).toStringList().size(), 0);
        QCOMPARE(result.payload.value(QStringLiteral("rowCount")).toInt(), 1);

        const QVariantList rows = result.payload.value(QStringLiteral("rows")).toList();
        QCOMPARE(rows.size(), 1);
        const QVariantList values = rows.at(0).toList();
        QCOMPARE(values.size(), 2);
        QCOMPARE(values.at(0).toString(), QStringLiteral("1"));
        QCOMPARE(values.at(1).toString(), QStringLiteral("alice"));
    }

    void test_dispatcherInsertWithoutColumnListUsesSchemaOrder()
    {
        const QString databaseName = QStringLiteral("test_parser_dispatcher_insert_db");
        const QString tableName = QStringLiteral("test_parser_dispatcher_insert_table");
        ensureDatabase(databaseName);
        ensureTable(tableName, baseSchema(tableName));

        SqlDispatcher dispatcher;
        const SqlExecResult insertResult = dispatcher.execute(
            QStringLiteral("INSERT INTO %1 VALUES (1, 'alice')").arg(tableName));
        QVERIFY2(insertResult.success, qPrintable(insertResult.errorMessage));
        QCOMPARE(insertResult.affectedRows, 1);

        const SelectRowsResult selectResult = tuple_service::selectRows(tableName, {}, {}, -1);
        QVERIFY2(selectResult.success, qPrintable(selectResult.errorMessage));
        QCOMPARE(selectResult.resultTable.rows.size(), 1);
        QCOMPARE(selectResult.resultTable.rows.at(0),
                 QStringList({QStringLiteral("1"), QStringLiteral("alice")}));
    }

    void test_dispatcherShowCreateTableReturnsText()
    {
        const QString databaseName = QStringLiteral("test_parser_dispatcher_show_create_db");
        const QString tableName = QStringLiteral("test_parser_dispatcher_show_create_table");
        ensureDatabase(databaseName);
        ensureTable(tableName, baseSchema(tableName));

        SqlDispatcher dispatcher;
        const SqlExecResult result = dispatcher.execute(QStringLiteral("SHOW CREATE TABLE %1").arg(tableName));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.commandType, QStringLiteral("SHOW_CREATE_TABLE"));
        QVERIFY(result.text.startsWith(QStringLiteral("CREATE TABLE")));
        QVERIFY(result.text.contains(tableName));
    }

    void test_dispatcherAlterRejectsIncompletePayload()
    {
        const QString databaseName = QStringLiteral("test_parser_dispatcher_alter_db");
        const QString tableName = QStringLiteral("test_parser_dispatcher_alter_table");
        ensureDatabase(databaseName);
        ensureTable(tableName, baseSchema(tableName));

        SqlDispatcher dispatcher;

        const SqlExecResult addColumnResult = dispatcher.execute(
            QStringLiteral("ALTER TABLE %1 ADD COLUMN age INT").arg(tableName));
        QVERIFY(!addColumnResult.success);
        QVERIFY(addColumnResult.errorMessage.contains(QStringLiteral("complete column payload")));

        const SqlExecResult modifyColumnResult = dispatcher.execute(
            QStringLiteral("ALTER TABLE %1 MODIFY COLUMN age INT").arg(tableName));
        QVERIFY(!modifyColumnResult.success);
        QVERIFY(modifyColumnResult.errorMessage.contains(QStringLiteral("complete column payload")));

        const SqlExecResult addConstraintResult = dispatcher.execute(
            QStringLiteral("ALTER TABLE %1 ADD CONSTRAINT uq_%1 UNIQUE (name)").arg(tableName));
        QVERIFY(!addConstraintResult.success);
        QVERIFY(addConstraintResult.errorMessage.contains(QStringLiteral("complete constraint payload")));

        const SqlExecResult modifyConstraintResult = dispatcher.execute(
            QStringLiteral("ALTER TABLE %1 MODIFY CONSTRAINT uq_%1 CONSTRAINT uq_%1 UNIQUE (name)").arg(tableName));
        QVERIFY(!modifyConstraintResult.success);
        QVERIFY(modifyConstraintResult.errorMessage.contains(QStringLiteral("complete constraint payload")));
    }

    void test_dispatcherIndexSqlCurrentlyUnsupported()
    {
        const sqlparser::ParseResult createIndex = sqlparser::parseSql(
            QStringLiteral("CREATE INDEX idx_student_name ON student(name)"));
        QVERIFY(!createIndex.success);
        QVERIFY(createIndex.errorMessage.contains(QStringLiteral("Unsupported SQL statement")));

        const sqlparser::ParseResult dropIndex = sqlparser::parseSql(
            QStringLiteral("DROP INDEX idx_student_name ON student"));
        QVERIFY(!dropIndex.success);
        QVERIFY(dropIndex.errorMessage.contains(QStringLiteral("Unsupported SQL statement")));
    }

private:
    QString m_dataRoot;
};

int service_tests::runParserDispatcherTests()
{
    ParserDispatcherTest test;
    return QTest::qExec(&test);
}

#include "test_parser_dispatcher.moc"
