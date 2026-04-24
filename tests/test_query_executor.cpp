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
            QStringLiteral("EXISTS (SELECT id FROM child WHERE child.parent_id = outer.id)"));
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));
        const logic::LogicParseResult parsed = logic::parseLogicTokens(
            QStringLiteral("EXISTS (SELECT id FROM child WHERE child.parent_id = outer.id)"),
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
        const QString expression = QStringLiteral("EXISTS (SELECT id FROM child WHERE child.parent_id = outer.id)");
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
            "id IN (SELECT parent_id FROM child WHERE child.parent_id = outer.id)");
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
            QStringLiteral("SELECT id FROM child WHERE parent_id = 10"),
            context);

        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.columns, QStringList({QStringLiteral("id")}));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 1);
        QCOMPARE(result.selectResult.resultTable.rows.first().value(0), QStringLiteral("1"));
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
            QStringLiteral("SELECT id FROM child WHERE child.parent_id = outer.id"),
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
        bindings.items.append(logic::CorrelatedBinding{QStringLiteral("outer.id"),
                                                       QStringLiteral("10"),
                                                       tabledef::ColumnType::Int,
                                                       false});

        const service::QueryExecuteContext context{databaseName, m_dataRoot};
        const QueryExecuteResult result = executor.executeCorrelatedSelect(
            QStringLiteral("SELECT id FROM child WHERE child.parent_id = outer.id"),
            bindings,
            context);

        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.columns, QStringList({QStringLiteral("id")}));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 1);
        QCOMPARE(result.selectResult.resultTable.rows.first().value(0), QStringLiteral("1"));
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
        const QString expression = QStringLiteral("EXISTS (SELECT id FROM child WHERE child.parent_id = outer.id)");
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

    void test_correlatedSubqueryExecutesPerRowWithoutCache()
    {
        CountingSubqueryExecutor executor;
        const QString expression = QStringLiteral("EXISTS (SELECT id FROM child WHERE child.parent_id = outer.id)");
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