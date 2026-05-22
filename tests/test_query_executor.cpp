#include "../controller/nest_query.h"
#include "../service/service.h"
#include "../utils/logic/logic.h"

#include <QDir>
#include <QtTest>

#include "test_entry.h"

using namespace service;

namespace {

QString testDataRoot()
{
    return QDir(QDir::tempPath()).absoluteFilePath(QStringLiteral("DBMS_test_query_executor"));
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
                            bool notNull = false)
{
    return tabledef::Column{name, type, length, notNull, QString(), false, QString()};
}

tabledef::TableSchema childSchema(const QString &tableName)
{
    tabledef::TableSchema schema;
    schema.tableName = tableName;
    schema.columns = {
        makeColumn(QStringLiteral("id"), tabledef::ColumnType::Int, 0, true),
        makeColumn(QStringLiteral("parent_id"), tabledef::ColumnType::Int, 0, false),
    };
    return schema;
}

tabledef::TableSchema relationSchema(const QString &tableName, const QString &foreignKeyColumn)
{
    tabledef::TableSchema schema;
    schema.tableName = tableName;
    schema.columns = {
        makeColumn(QStringLiteral("id"), tabledef::ColumnType::Int, 0, true),
        makeColumn(foreignKeyColumn, tabledef::ColumnType::Int, 0, false),
    };
    return schema;
}

tabledef::TableSchema aggregateStudentSchema(const QString &tableName)
{
    tabledef::TableSchema schema;
    schema.tableName = tableName;
    schema.columns = {
        makeColumn(QStringLiteral("id"), tabledef::ColumnType::Int, 0, true),
        makeColumn(QStringLiteral("class_id"), tabledef::ColumnType::Int),
        makeColumn(QStringLiteral("gender"), tabledef::ColumnType::Varchar, 16),
        makeColumn(QStringLiteral("score"), tabledef::ColumnType::Float),
        makeColumn(QStringLiteral("name"), tabledef::ColumnType::Varchar, 64),
    };
    return schema;
}

tabledef::TableSchema aggregateClassSchema(const QString &tableName)
{
    tabledef::TableSchema schema;
    schema.tableName = tableName;
    schema.columns = {
        makeColumn(QStringLiteral("id"), tabledef::ColumnType::Int, 0, true),
        makeColumn(QStringLiteral("name"), tabledef::ColumnType::Varchar, 64),
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

void seedRow(const QString &databaseName,
             const QString &tableName,
             const QStringList &row,
             const QString &dataRoot)
{
    repo::TableRepo tableRepo(databaseName, tableName, dataRoot);
    const repo::RepositoryResult result = tableRepo.insertRow(row);
    QVERIFY2(result.ok, qPrintable(result.error));
}

logic::LogicEvalContext makeEvalContext(QueryExecutor *executor,
                                        const QString &databaseName,
                                        const QString &dataRoot)
{
    logic::LogicEvalContext evalContext;
    evalContext.subqueryExecutor = executor;
    evalContext.currentDatabase = databaseName;
    evalContext.dataRoot = dataRoot;
    evalContext.allowSubquery = true;
    return evalContext;
}

logic::LogicRowContext makeOuterRow(const QString &idValue)
{
    logic::LogicRowContext rowContext;
    rowContext.tableName = QStringLiteral("parent");
    rowContext.cellsByName.insert(QStringLiteral("id"),
                                  logic::LogicCellValue{idValue, tabledef::ColumnType::Int, false});
    rowContext.cellsByName.insert(QStringLiteral("parent.id"),
                                  logic::LogicCellValue{idValue, tabledef::ColumnType::Int, false});
    return rowContext;
}

class CountingSubqueryExecutor : public logic::ISubqueryExecutor
{
public:
    QueryExecuteResult executeSelectSql(const QString &, const QueryExecuteContext &) override
    {
        QueryExecuteResult result;
        result.success = true;
        result.selectResult.success = true;
        return result;
    }

    QueryExecuteResult executeCorrelatedSelect(const QString &,
                                               const logic::CorrelationBindings &bindings,
                                               const QueryExecuteContext &) override
    {
        ++correlatedCallCount;
        if (!bindings.items.isEmpty()) {
            observedValues.append(bindings.items.first().value);
        }

        QueryExecuteResult result;
        result.success = true;
        result.selectResult.success = true;
        if (!bindings.items.isEmpty() && bindings.items.first().value == QStringLiteral("10")) {
            result.selectResult.resultTable.rows.append({QStringLiteral("1")});
        }
        return result;
    }

    int correlatedCallCount = 0;
    QStringList observedValues;
};

} // namespace

class QueryExecutorTest : public QObject
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

    void test_evaluateCorrelatedExistsReturnsTrueWhenRowsExist()
    {
        const QString databaseName = QStringLiteral("test_query_executor_exists_true_db");
        const QString tableName = QStringLiteral("child");
        ensureDatabase(databaseName);
        ensureTable(tableName, childSchema(tableName));
        seedRow(databaseName, tableName, {QStringLiteral("1"), QStringLiteral("10")}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("2"), QStringLiteral("20")}, m_dataRoot);

        QueryExecutor executor;
        const logic::LogicTokenizeResult tokenized = logic::tokenizeLogicExpression(
            QStringLiteral("EXISTS (SELECT id FROM child WHERE child.parent_id = parent.id)"));
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));
        const logic::LogicParseResult parsed = logic::parseLogicTokens(
            QStringLiteral("EXISTS (SELECT id FROM child WHERE child.parent_id = parent.id)"),
            tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));

        const logic::LogicEvalResult result = logic::evaluateLogicExpression(parsed.root,
                                                                            makeOuterRow(QStringLiteral("10")),
                                                                            makeEvalContext(&executor,
                                                                                            databaseName,
                                                                                            m_dataRoot));
        QVERIFY2(result.success, qPrintable(result.error.message));
        QCOMPARE(result.truth, logic::LogicTruthValue::True);
    }

    void test_evaluateCorrelatedExistsReturnsFalseWhenRowsDoNotExist()
    {
        const QString databaseName = QStringLiteral("test_query_executor_exists_false_db");
        const QString tableName = QStringLiteral("child");
        ensureDatabase(databaseName);
        ensureTable(tableName, childSchema(tableName));
        seedRow(databaseName, tableName, {QStringLiteral("1"), QStringLiteral("10")}, m_dataRoot);

        QueryExecutor executor;
        const QString expression = QStringLiteral("EXISTS (SELECT id FROM child WHERE child.parent_id = parent.id)");
        const logic::LogicTokenizeResult tokenized = logic::tokenizeLogicExpression(expression);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));
        const logic::LogicParseResult parsed = logic::parseLogicTokens(expression, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));

        const logic::LogicEvalResult result = logic::evaluateLogicExpression(parsed.root,
                                                                            makeOuterRow(QStringLiteral("30")),
                                                                            makeEvalContext(&executor,
                                                                                            databaseName,
                                                                                            m_dataRoot));
        QVERIFY2(result.success, qPrintable(result.error.message));
        QCOMPARE(result.truth, logic::LogicTruthValue::False);
    }

    void test_evaluateCorrelatedInSubqueryUsesOuterBinding()
    {
        const QString databaseName = QStringLiteral("test_query_executor_in_db");
        const QString tableName = QStringLiteral("child");
        ensureDatabase(databaseName);
        ensureTable(tableName, childSchema(tableName));
        seedRow(databaseName, tableName, {QStringLiteral("1"), QStringLiteral("10")}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("2"), QStringLiteral("20")}, m_dataRoot);

        QueryExecutor executor;
        const QString expression = QStringLiteral(
            "id IN (SELECT parent_id FROM child WHERE child.parent_id = parent.id)");
        const logic::LogicTokenizeResult tokenized = logic::tokenizeLogicExpression(expression);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));
        const logic::LogicParseResult parsed = logic::parseLogicTokens(expression, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));

        logic::LogicRowContext outerRow = makeOuterRow(QStringLiteral("10"));
        outerRow.cellsByName.insert(QStringLiteral("id"),
                                    logic::LogicCellValue{QStringLiteral("10"), tabledef::ColumnType::Int, false});
        const logic::LogicEvalResult trueResult = logic::evaluateLogicExpression(parsed.root,
                                                                                outerRow,
                                                                                makeEvalContext(&executor,
                                                                                                databaseName,
                                                                                                m_dataRoot));
        QVERIFY2(trueResult.success, qPrintable(trueResult.error.message));
        QCOMPARE(trueResult.truth, logic::LogicTruthValue::True);

        logic::LogicRowContext falseOuterRow = makeOuterRow(QStringLiteral("30"));
        falseOuterRow.cellsByName.insert(QStringLiteral("id"),
                                         logic::LogicCellValue{QStringLiteral("30"), tabledef::ColumnType::Int, false});
        const logic::LogicEvalResult falseResult = logic::evaluateLogicExpression(parsed.root,
                                                                                 falseOuterRow,
                                                                                 makeEvalContext(&executor,
                                                                                                 databaseName,
                                                                                                 m_dataRoot));
        QVERIFY2(falseResult.success, qPrintable(falseResult.error.message));
        QCOMPARE(falseResult.truth, logic::LogicTruthValue::False);
    }

    void test_executeSelectSqlAppliesWhereAst()
    {
        const QString databaseName = QStringLiteral("test_query_executor_select_db");
        const QString tableName = QStringLiteral("child");
        ensureDatabase(databaseName);
        ensureTable(tableName, childSchema(tableName));
        seedRow(databaseName, tableName, {QStringLiteral("1"), QStringLiteral("10")}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("2"), QStringLiteral("20")}, m_dataRoot);

        QueryExecutor executor;
        const service::QueryExecuteContext context{databaseName, m_dataRoot};
        const QueryExecuteResult result = executor.executeSelectSql(
            QStringLiteral("SELECT id FROM child WHERE parent_id = 10 OR id = 999"),
            context);

        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.columns, QStringList({QStringLiteral("id")}));
        QCOMPARE(result.selectResult.columnTypes.size(), 1);
        QCOMPARE(result.selectResult.columnTypes.first(), tabledef::ColumnType::Int);
        QCOMPARE(result.selectResult.resultTable.rows.size(), 1);
        QCOMPARE(result.selectResult.resultTable.rows.first().value(0), QStringLiteral("1"));
    }

    void test_executeSelectSqlFiltersUnknownWhereRows()
    {
        const QString databaseName = QStringLiteral("test_query_executor_unknown_where_db");
        const QString tableName = QStringLiteral("child");
        ensureDatabase(databaseName);
        ensureTable(tableName, childSchema(tableName));
        seedRow(databaseName, tableName, {QStringLiteral("1"), QStringLiteral("10")}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("2"), QStringLiteral("20")}, m_dataRoot);

        QueryExecutor executor;
        const service::QueryExecuteContext context{databaseName, m_dataRoot};
        const QueryExecuteResult result = executor.executeSelectSql(
            QStringLiteral("SELECT id FROM child WHERE parent_id = NULL OR id = 999"),
            context);

        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.columns, QStringList({QStringLiteral("id")}));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 0);
    }

    void test_executeSelectSqlAllowsWhereOnUnprojectedColumns()
    {
        const QString databaseName = QStringLiteral("test_query_executor_unprojected_where_db");
        const QString tableName = QStringLiteral("child");
        ensureDatabase(databaseName);
        ensureTable(tableName, childSchema(tableName));
        seedRow(databaseName, tableName, {QStringLiteral("1"), QStringLiteral("10")}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("2"), QStringLiteral("20")}, m_dataRoot);

        QueryExecutor executor;
        const service::QueryExecuteContext context{databaseName, m_dataRoot};
        const QueryExecuteResult result = executor.executeSelectSql(
            QStringLiteral("SELECT id FROM child WHERE parent_id = 20 OR id = 999"),
            context);

        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.columns, QStringList({QStringLiteral("id")}));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 1);
        QCOMPARE(result.selectResult.resultTable.rows.first().value(0), QStringLiteral("2"));
    }

    void test_executeSelectSqlAppliesLikeWhere()
    {
        const QString databaseName = QStringLiteral("test_query_executor_like_db");
        const QString tableName = QStringLiteral("child");
        ensureDatabase(databaseName);
        ensureTable(tableName, childSchema(tableName));
        seedRow(databaseName, tableName, {QStringLiteral("1"), QStringLiteral("10")}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("2"), QStringLiteral("20")}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("10"), QStringLiteral("30")}, m_dataRoot);

        QueryExecutor executor;
        const service::QueryExecuteContext context{databaseName, m_dataRoot};
        const QueryExecuteResult result = executor.executeSelectSql(
            QStringLiteral("SELECT id FROM child WHERE id LIKE '1%' ORDER BY id ASC"),
            context);

        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.columns, QStringList({QStringLiteral("id")}));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 2);
        QCOMPARE(result.selectResult.resultTable.rows.at(0).value(0), QStringLiteral("1"));
        QCOMPARE(result.selectResult.resultTable.rows.at(1).value(0), QStringLiteral("10"));
    }

    void test_executeSelectSqlAppliesBetweenWhere()
    {
        const QString databaseName = QStringLiteral("test_query_executor_between_db");
        const QString tableName = QStringLiteral("student");
        ensureDatabase(databaseName);
        ensureTable(tableName, aggregateStudentSchema(tableName));
        seedRow(databaseName, tableName, {QStringLiteral("1"), QStringLiteral("1"), QStringLiteral("F"), QStringLiteral("90"), QStringLiteral("alice")}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("2"), QStringLiteral("1"), QStringLiteral("M"), QStringLiteral("70"), QStringLiteral("bob")}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("3"), QStringLiteral("2"), QStringLiteral("F"), QStringLiteral("50"), QStringLiteral("cathy")}, m_dataRoot);

        QueryExecutor executor;
        const service::QueryExecuteContext context{databaseName, m_dataRoot};
        const QueryExecuteResult result = executor.executeSelectSql(
            QStringLiteral("SELECT id FROM student WHERE score BETWEEN 60 AND 90 ORDER BY id ASC"),
            context);

        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.columns, QStringList({QStringLiteral("id")}));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 2);
        QCOMPARE(result.selectResult.resultTable.rows.at(0).value(0), QStringLiteral("1"));
        QCOMPARE(result.selectResult.resultTable.rows.at(1).value(0), QStringLiteral("2"));
    }

    void test_executeAggregateSelectSupportsGroupHavingAndOrderAlias()
    {
        const QString databaseName = QStringLiteral("test_query_executor_aggregate_db");
        const QString tableName = QStringLiteral("student");
        ensureDatabase(databaseName);
        ensureTable(tableName, aggregateStudentSchema(tableName));
        seedRow(databaseName, tableName, {QStringLiteral("1"), QStringLiteral("1"), QStringLiteral("F"), QStringLiteral("90"), QStringLiteral("alice")}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("2"), QStringLiteral("1"), QStringLiteral("M"), QStringLiteral("70"), QStringLiteral("bob")}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("3"), QStringLiteral("2"), QStringLiteral("F"), QStringLiteral("50"), QStringLiteral("cathy")}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("4"), QStringLiteral("2"), QStringLiteral("M"), QString(), QStringLiteral("dan")}, m_dataRoot);

        QueryExecutor executor;
        const service::QueryExecuteContext context{databaseName, m_dataRoot};

        QueryExecuteResult result = executor.executeSelectSql(
            QStringLiteral("SELECT COUNT(*) AS n, COUNT(score) AS scored, SUM(score) AS total, AVG(score) AS avg_score FROM student"),
            context);
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.columns,
                 QStringList({QStringLiteral("n"), QStringLiteral("scored"), QStringLiteral("total"), QStringLiteral("avg_score")}));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 1);
        QCOMPARE(result.selectResult.resultTable.rows.first().at(0), QStringLiteral("4"));
        QCOMPARE(result.selectResult.resultTable.rows.first().at(1), QStringLiteral("3"));
        QCOMPARE(result.selectResult.resultTable.rows.first().at(2), QStringLiteral("210"));
        QCOMPARE(result.selectResult.resultTable.rows.first().at(3), QStringLiteral("70"));

        result = executor.executeSelectSql(
            QStringLiteral("SELECT class_id, COUNT(*) AS n FROM student GROUP BY class_id HAVING n >= 2 ORDER BY n DESC LIMIT 1"),
            context);
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.columns, QStringList({QStringLiteral("class_id"), QStringLiteral("n")}));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 1);
        QCOMPARE(result.selectResult.resultTable.rows.first().at(1), QStringLiteral("2"));

        result = executor.executeSelectSql(
            QStringLiteral("SELECT class_id, gender, AVG(score) AS avg_score FROM student GROUP BY class_id, gender ORDER BY avg_score DESC"),
            context);
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 4);
        QCOMPARE(result.selectResult.resultTable.rows.first().at(2), QStringLiteral("90"));
    }

    void test_executeAggregateSelectSupportsJoinWhereHavingOrderAliasLimit()
    {
        const QString databaseName = QStringLiteral("test_query_executor_aggregate_join_db");
        const QString studentTable = QStringLiteral("student");
        const QString classTable = QStringLiteral("class");
        ensureDatabase(databaseName);
        ensureTable(studentTable, aggregateStudentSchema(studentTable));
        ensureTable(classTable, aggregateClassSchema(classTable));
        seedRow(databaseName, classTable, {QStringLiteral("1"), QStringLiteral("alpha")}, m_dataRoot);
        seedRow(databaseName, classTable, {QStringLiteral("2"), QStringLiteral("beta")}, m_dataRoot);
        seedRow(databaseName, studentTable, {QStringLiteral("1"), QStringLiteral("1"), QStringLiteral("F"), QStringLiteral("90"), QStringLiteral("alice")}, m_dataRoot);
        seedRow(databaseName, studentTable, {QStringLiteral("2"), QStringLiteral("1"), QStringLiteral("M"), QStringLiteral("80"), QStringLiteral("bob")}, m_dataRoot);
        seedRow(databaseName, studentTable, {QStringLiteral("3"), QStringLiteral("2"), QStringLiteral("F"), QStringLiteral("70"), QStringLiteral("cathy")}, m_dataRoot);
        seedRow(databaseName, studentTable, {QStringLiteral("4"), QStringLiteral("2"), QStringLiteral("M"), QStringLiteral("55"), QStringLiteral("dan")}, m_dataRoot);

        QueryExecutor executor;
        const service::QueryExecuteContext context{databaseName, m_dataRoot};
        const QueryExecuteResult result = executor.executeSelectSql(
            QStringLiteral("SELECT c.name AS class_name, COUNT(s.id) AS n, AVG(s.score) AS avg_score "
                           "FROM student s JOIN class c ON s.class_id = c.id "
                           "WHERE s.score >= 60 "
                           "GROUP BY c.name "
                           "HAVING n >= 2 "
                           "ORDER BY avg_score DESC "
                           "LIMIT 1"),
            context);

        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.columns,
                 QStringList({QStringLiteral("class_name"), QStringLiteral("n"), QStringLiteral("avg_score")}));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 1);
        QCOMPARE(result.selectResult.resultTable.rows.first().at(0), QStringLiteral("alpha"));
        QCOMPARE(result.selectResult.resultTable.rows.first().at(1), QStringLiteral("2"));
        QCOMPARE(result.selectResult.resultTable.rows.first().at(2), QStringLiteral("85"));
    }

    void test_executeAggregateSelectRejectsInvalidQueries()
    {
        const QString databaseName = QStringLiteral("test_query_executor_aggregate_invalid_db");
        const QString tableName = QStringLiteral("student");
        ensureDatabase(databaseName);
        ensureTable(tableName, aggregateStudentSchema(tableName));
        seedRow(databaseName, tableName, {QStringLiteral("1"), QStringLiteral("1"), QStringLiteral("F"), QStringLiteral("90"), QStringLiteral("alice")}, m_dataRoot);

        QueryExecutor executor;
        const service::QueryExecuteContext context{databaseName, m_dataRoot};

        QueryExecuteResult result = executor.executeSelectSql(QStringLiteral("SELECT * FROM student GROUP BY class_id"), context);
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("*")));

        result = executor.executeSelectSql(QStringLiteral("SELECT id, COUNT(*) FROM student"), context);
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("must appear in GROUP BY")));

        result = executor.executeSelectSql(QStringLiteral("SELECT SUM(name) FROM student"), context);
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("numeric")));

        result = executor.executeSelectSql(QStringLiteral("SELECT class_id, COUNT(*) AS n FROM student GROUP BY class_id HAVING score > 60"), context);
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("HAVING column")));

        result = executor.executeSelectSql(QStringLiteral("SELECT class_id, COUNT(*) AS n FROM student GROUP BY class_id ORDER BY unknown_alias"), context);
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("ORDER BY column")));

        result = executor.executeSelectSql(QStringLiteral("SELECT COUNT(*) AS n, SUM(score) AS n FROM student"), context);
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("duplicate output alias")));
    }

    void test_executeAggregateSelectHandlesEmptyInputAndHavingAggregateFunction()
    {
        const QString databaseName = QStringLiteral("test_query_executor_aggregate_empty_db");
        const QString tableName = QStringLiteral("student");
        ensureDatabase(databaseName);
        ensureTable(tableName, aggregateStudentSchema(tableName));

        QueryExecutor executor;
        const service::QueryExecuteContext context{databaseName, m_dataRoot};
        QueryExecuteResult result = executor.executeSelectSql(
            QStringLiteral("SELECT COUNT(*) AS n, SUM(score) AS total FROM student"),
            context);
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 1);
        QCOMPARE(result.selectResult.resultTable.rows.first().at(0), QStringLiteral("0"));
        QCOMPARE(result.selectResult.resultTable.rows.first().at(1), QString());

        seedRow(databaseName, tableName, {QStringLiteral("1"), QStringLiteral("1"), QStringLiteral("F"), QStringLiteral("90"), QStringLiteral("alice")}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("2"), QStringLiteral("1"), QStringLiteral("M"), QString(), QStringLiteral("bob")}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("3"), QStringLiteral("2"), QStringLiteral("F"), QStringLiteral("70"), QStringLiteral("cathy")}, m_dataRoot);

        result = executor.executeSelectSql(
            QStringLiteral("SELECT class_id FROM student GROUP BY class_id HAVING COUNT(*) > 1"),
            context);
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 1);
        QCOMPARE(result.selectResult.resultTable.rows.first().at(0), QStringLiteral("1"));

        result = executor.executeSelectSql(
            QStringLiteral("SELECT class_id, COUNT(*) AS n FROM student GROUP BY class_id ORDER BY COUNT(*) DESC"),
            context);
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 2);
        QCOMPARE(result.selectResult.resultTable.rows.first().at(0), QStringLiteral("1"));
        QCOMPARE(result.selectResult.resultTable.rows.first().at(1), QStringLiteral("2"));

        result = executor.executeSelectSql(
            QStringLiteral("SELECT class_id FROM student GROUP BY class_id ORDER BY COUNT(*) DESC"),
            context);
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 2);
        QCOMPARE(result.selectResult.resultTable.rows.first().at(0), QStringLiteral("1"));

        result = executor.executeSelectSql(
            QStringLiteral("SELECT class_id, COUNT(score) AS scored FROM student GROUP BY class_id ORDER BY class_id ASC"),
            context);
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 2);
        QCOMPARE(result.selectResult.resultTable.rows.at(0).at(1), QStringLiteral("1"));
        QCOMPARE(result.selectResult.resultTable.rows.at(1).at(1), QStringLiteral("1"));
    }

    void test_executeSelectSqlAppliesWhereAstBeforeProjectionAndLimit()
    {
        const QString databaseName = QStringLiteral("test_query_executor_select_limit_db");
        const QString tableName = QStringLiteral("child");
        ensureDatabase(databaseName);
        ensureTable(tableName, childSchema(tableName));
        seedRow(databaseName, tableName, {QStringLiteral("1"), QStringLiteral("30")}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("2"), QStringLiteral("10")}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("3"), QStringLiteral("20")}, m_dataRoot);

        QueryExecutor executor;
        const service::QueryExecuteContext context{databaseName, m_dataRoot};
        const QueryExecuteResult result = executor.executeSelectSql(
            QStringLiteral("SELECT id FROM child WHERE parent_id = 10 OR parent_id = 20 LIMIT 1"),
            context);

        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.columns, QStringList({QStringLiteral("id")}));
        QCOMPARE(result.selectResult.columnTypes.size(), 1);
        QCOMPARE(result.selectResult.columnTypes.first(), tabledef::ColumnType::Int);
        QCOMPARE(result.selectResult.resultTable.rows.size(), 1);
        QCOMPARE(result.selectResult.resultTable.rows.first().value(0), QStringLiteral("2"));
    }

    void test_evaluateCorrelatedExistsRejectsMissingBinding()
    {
        const QString databaseName = QStringLiteral("test_query_executor_missing_binding_db");
        const QString tableName = QStringLiteral("child");
        ensureDatabase(databaseName);
        ensureTable(tableName, childSchema(tableName));
        seedRow(databaseName, tableName, {QStringLiteral("1"), QStringLiteral("10")}, m_dataRoot);

        QueryExecutor executor;
        const logic::CorrelationBindings bindings;
        const service::QueryExecuteContext context{databaseName, m_dataRoot};
        const QueryExecuteResult result = executor.executeCorrelatedSelect(
            QStringLiteral("SELECT id FROM child WHERE child.parent_id = parent.id"),
            bindings,
            context);

        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("missing correlated binding")));
    }

    void test_executeCorrelatedSelectFiltersRowsUsingOuterBinding()
    {
        const QString databaseName = QStringLiteral("test_query_executor_correlated_select_db");
        const QString tableName = QStringLiteral("child");
        ensureDatabase(databaseName);
        ensureTable(tableName, childSchema(tableName));
        seedRow(databaseName, tableName, {QStringLiteral("1"), QStringLiteral("10")}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("2"), QStringLiteral("20")}, m_dataRoot);

        QueryExecutor executor;
        logic::CorrelationBindings bindings;
        bindings.items.append(logic::CorrelatedBinding{QStringLiteral("parent.id"),
                                                       QStringLiteral("10"),
                                                       tabledef::ColumnType::Int,
                                                       false});

        const service::QueryExecuteContext context{databaseName, m_dataRoot};
        const QueryExecuteResult result = executor.executeCorrelatedSelect(
            QStringLiteral("SELECT id FROM child WHERE child.parent_id = parent.id"),
            bindings,
            context);

        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.columns, QStringList({QStringLiteral("id")}));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 1);
        QCOMPARE(result.selectResult.resultTable.rows.first().value(0), QStringLiteral("1"));
    }

    void test_executeNestedCorrelatedSelectPreservesOuterBindingName()
    {
        const QString databaseName = QStringLiteral("test_query_executor_nested_correlated_db");
        const QString parentTable = QStringLiteral("parent");
        const QString childTable = QStringLiteral("child");
        const QString grandchildTable = QStringLiteral("grandchild");

        ensureDatabase(databaseName);
        ensureTable(parentTable, relationSchema(parentTable, QStringLiteral("parent_id")));
        ensureTable(childTable, relationSchema(childTable, QStringLiteral("parent_id")));
        ensureTable(grandchildTable, relationSchema(grandchildTable, QStringLiteral("child_id")));

        seedRow(databaseName, parentTable, {QStringLiteral("10"), QStringLiteral("0")}, m_dataRoot);
        seedRow(databaseName, childTable, {QStringLiteral("1"), QStringLiteral("10")}, m_dataRoot);
        seedRow(databaseName, grandchildTable, {QStringLiteral("100"), QStringLiteral("10")}, m_dataRoot);

        QueryExecutor executor;
        const QueryExecuteContext context{databaseName, m_dataRoot};
        const QueryExecuteResult result = executor.executeSql(
            QStringLiteral(
                "SELECT id FROM parent WHERE EXISTS ("
                "SELECT c.id FROM child c WHERE EXISTS ("
                "SELECT id FROM grandchild WHERE grandchild.child_id = c.parent_id))"),
            context);

        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 1);
        QCOMPARE(result.selectResult.resultTable.rows.first().value(0), QStringLiteral("10"));
    }

    void test_executeSqlRejectsNonSelect()
    {
        QueryExecutor executor;
        const QueryExecuteResult result = executor.executeSql(QStringLiteral("UPDATE child SET id = 1"));
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("only supports SELECT subqueries")));
    }

    void test_executeCorrelatedSelectRejectsMissingBinding()
    {
        const QString databaseName = QStringLiteral("test_query_executor_missing_binding_db");
        const QString tableName = QStringLiteral("child");
        ensureDatabase(databaseName);
        ensureTable(tableName, childSchema(tableName));
        seedRow(databaseName, tableName, {QStringLiteral("1"), QStringLiteral("10")}, m_dataRoot);

        QueryExecutor executor;
        const QString expression = QStringLiteral("EXISTS (SELECT id FROM child WHERE child.parent_id = parent.id)");
        const logic::LogicTokenizeResult tokenized = logic::tokenizeLogicExpression(expression);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));
        const logic::LogicParseResult parsed = logic::parseLogicTokens(expression, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));

        logic::LogicRowContext outerRow;
        outerRow.tableName = QStringLiteral("parent");

        const logic::LogicEvalResult result = logic::evaluateLogicExpression(parsed.root,
                                                                            outerRow,
                                                                            makeEvalContext(&executor,
                                                                                            databaseName,
                                                                                            m_dataRoot));
        QVERIFY(!result.success);
        QVERIFY(result.error.message.contains(QStringLiteral("missing correlated binding")));
    }

    void test_evaluateCorrelatedExistsWithNullOuterBindingReturnsFalse()
    {
        const QString databaseName = QStringLiteral("test_query_executor_exists_null_binding_db");
        const QString tableName = QStringLiteral("child");
        ensureDatabase(databaseName);
        ensureTable(tableName, childSchema(tableName));
        seedRow(databaseName, tableName, {QStringLiteral("1"), QStringLiteral("10")}, m_dataRoot);

        QueryExecutor executor;
        const QString expression = QStringLiteral("EXISTS (SELECT id FROM child WHERE child.parent_id = parent.id)");
        const logic::LogicTokenizeResult tokenized = logic::tokenizeLogicExpression(expression);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));
        const logic::LogicParseResult parsed = logic::parseLogicTokens(expression, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));

        logic::LogicRowContext outerRow;
        outerRow.tableName = QStringLiteral("parent");
        outerRow.cellsByName.insert(QStringLiteral("id"),
                                    logic::LogicCellValue{QString(), tabledef::ColumnType::Int, true});
        outerRow.cellsByName.insert(QStringLiteral("parent.id"),
                                    logic::LogicCellValue{QString(), tabledef::ColumnType::Int, true});

        const logic::LogicEvalResult result = logic::evaluateLogicExpression(parsed.root,
                                                                            outerRow,
                                                                            makeEvalContext(&executor,
                                                                                            databaseName,
                                                                                            m_dataRoot));
        QVERIFY2(result.success, qPrintable(result.error.message));
        QCOMPARE(result.truth, logic::LogicTruthValue::False);
    }

    void test_correlatedSubqueryExecutesPerRowWithoutCache()
    {
        CountingSubqueryExecutor executor;
        const QString expression = QStringLiteral("EXISTS (SELECT id FROM child WHERE child.parent_id = parent.id)");
        const logic::LogicTokenizeResult tokenized = logic::tokenizeLogicExpression(expression);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));
        const logic::LogicParseResult parsed = logic::parseLogicTokens(expression, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));

        logic::LogicEvalContext evalContext;
        evalContext.subqueryExecutor = &executor;
        evalContext.allowSubquery = true;

        const logic::LogicEvalResult first = logic::evaluateLogicExpression(parsed.root,
                                                                            makeOuterRow(QStringLiteral("10")),
                                                                            evalContext);
        QVERIFY2(first.success, qPrintable(first.error.message));
        QCOMPARE(first.truth, logic::LogicTruthValue::True);

        const logic::LogicEvalResult second = logic::evaluateLogicExpression(parsed.root,
                                                                             makeOuterRow(QStringLiteral("20")),
                                                                             evalContext);
        QVERIFY2(second.success, qPrintable(second.error.message));
        QCOMPARE(second.truth, logic::LogicTruthValue::False);

        QCOMPARE(executor.correlatedCallCount, 2);
        QCOMPARE(executor.observedValues, QStringList({QStringLiteral("10"), QStringLiteral("20")}));
    }

private:
    QString m_dataRoot;
};

int service_tests::runQueryExecutorTests()
{
    QueryExecutorTest test;
    return QTest::qExec(&test);
}

#include "test_query_executor.moc"

