#include "../controller/nest_query.h"
#include "../service/service.h"
#include "../utils/logic/logic.h"

#include <QDir>
#include <QtTest>

#include "test_entry.h"

using namespace service;

namespace {

logic::LogicRowContext makeOuterRowContext()
{
    logic::LogicRowContext rowContext;
    rowContext.tableName = QStringLiteral("parent");
    rowContext.cellsByName.insert(QStringLiteral("id"),
                                  logic::LogicCellValue{QStringLiteral("10"), tabledef::ColumnType::Int, false});
    rowContext.cellsByName.insert(QStringLiteral("name"),
                                  logic::LogicCellValue{QStringLiteral("alice"), tabledef::ColumnType::Varchar, false});
    return rowContext;
}

class FixedSubqueryExecutor : public logic::ISubqueryExecutor
{
public:
    explicit FixedSubqueryExecutor(const repo::TableData &tableData,
                                   const QList<tabledef::ColumnType> &columnTypes = {})
    {
        m_result.success = true;
        m_result.selectResult.success = true;
        m_result.selectResult.resultTable = tableData;
        m_result.selectResult.columnTypes = columnTypes;
    }

    QueryExecuteResult executeSelectSql(const QString &, const QueryExecuteContext &) override
    {
        return m_result;
    }

    QueryExecuteResult executeCorrelatedSelect(const QString &,
                                               const logic::CorrelationBindings &,
                                               const QueryExecuteContext &) override
    {
        return m_result;
    }

private:
    QueryExecuteResult m_result;
};

class BindingAwareSubqueryExecutor : public logic::ISubqueryExecutor
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
        QueryExecuteResult result;
        result.success = true;
        result.selectResult.success = true;
        result.selectResult.resultTable.columns = {QStringLiteral("parent_id")};
        result.selectResult.columnTypes = {tabledef::ColumnType::Int};

        if (!bindings.items.isEmpty() && bindings.items.first().value == QStringLiteral("10")) {
            result.selectResult.resultTable.rows.append({QStringLiteral("10")});
        }

        observedBindings = bindings;

        return result;
    }

    logic::CorrelationBindings observedBindings;
};

class NullAwareBindingSubqueryExecutor : public logic::ISubqueryExecutor
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
        QueryExecuteResult result;
        result.success = true;
        result.selectResult.success = true;
        result.selectResult.resultTable.columns = {QStringLiteral("parent_id")};
        result.selectResult.columnTypes = {tabledef::ColumnType::Int};

        if (!bindings.items.isEmpty()) {
            const logic::CorrelatedBinding &binding = bindings.items.first();
            if (binding.isNull) {
                result.selectResult.resultTable.rows.append({QString()});
            } else if (binding.value == QStringLiteral("10")) {
                result.selectResult.resultTable.rows.append({QStringLiteral("10")});
                result.selectResult.resultTable.rows.append({QString()});
            }
        }

        return result;
    }
};

class FakeQueryExecutor : public service::QueryExecutor
{
public:
    explicit FakeQueryExecutor(const QueryExecuteResult &res)
        : m_res(res)
    {
    }

    QueryExecuteResult executeSelectSql(const QString &,
                                        const QueryExecuteContext &) override
    {
        return m_res;
    }

    QueryExecuteResult executeCorrelatedSelect(const QString &,
                                               const logic::CorrelationBindings &,
                                               const QueryExecuteContext &) override
    {
        return m_res;
    }

private:
    QueryExecuteResult m_res;
};

} // namespace

class LogicTest : public QObject
{
    Q_OBJECT

private slots:
    void test_parseCorrelatedExistsCollectsOuterReferences()
    {
        const QString expression = QStringLiteral(
            "EXISTS (SELECT id FROM child WHERE child.parent_id = outer.id)");

        const logic::LogicTokenizeResult tokenized = logic::tokenizeLogicExpression(expression);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));

        const logic::LogicParseResult parsed = logic::parseLogicTokens(expression, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));
        QCOMPARE(parsed.root.type, logic::LogicNodeType::ExistsSubquery);
        QCOMPARE(parsed.root.subquerySql,
                 QStringLiteral("SELECT id FROM child WHERE child.parent_id = outer.id"));
        QCOMPARE(parsed.root.referencedOuterNames, QStringList({QStringLiteral("outer.id")}));
    }

    void test_parseSubqueryPreservesRawText()
    {
        const QString expression = QStringLiteral(
            "EXISTS (  SELECT id FROM child WHERE child.parent_id = outer.id  )");

        const logic::LogicTokenizeResult tokenized = logic::tokenizeLogicExpression(expression);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));

        const logic::LogicParseResult parsed = logic::parseLogicTokens(expression, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));
        QCOMPARE(parsed.root.subquerySql,
                 QStringLiteral("  SELECT id FROM child WHERE child.parent_id = outer.id  "));
    }

    void test_parseCorrelatedReferenceRejectsTableNamePrefix()
    {
        const QString expression = QStringLiteral(
            "EXISTS (SELECT id FROM child WHERE child.parent_id = parent.id)");

        const logic::LogicTokenizeResult tokenized = logic::tokenizeLogicExpression(expression);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));

        const logic::LogicParseResult parsed = logic::parseLogicTokens(expression, tokenized.tokens);
        QVERIFY(!parsed.success);
        QVERIFY(parsed.error.message.contains(QStringLiteral("outer.xxx")));
    }

    void test_parseCorrelatedReferenceRejectsAliasPrefix()
    {
        const QString expression = QStringLiteral(
            "EXISTS (SELECT id FROM child c WHERE c.parent_id = outer.id)");

        const logic::LogicTokenizeResult tokenized = logic::tokenizeLogicExpression(expression);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));

        const logic::LogicParseResult parsed = logic::parseLogicTokens(expression, tokenized.tokens);
        QVERIFY(!parsed.success);
        QVERIFY(parsed.error.message.contains(QStringLiteral("outer.xxx")));
    }

    void test_buildCorrelationBindingsExtractsTypedOuterValues()
    {
        const logic::LogicRowContext outerRowContext = makeOuterRowContext();
        const logic::CorrelationBindings bindings = logic::buildCorrelationBindings(outerRowContext,
                                                                                   {QStringLiteral("outer.id")});

        QCOMPARE(bindings.items.size(), 1);
        QCOMPARE(bindings.items.first().name, QStringLiteral("outer.id"));
        QCOMPARE(bindings.items.first().value, QStringLiteral("10"));
        QCOMPARE(bindings.items.first().type, tabledef::ColumnType::Int);
        QCOMPARE(bindings.items.first().isNull, false);
    }

    void test_correlatedSubqueryPrefersExactOuterBindingName()
    {
        logic::LogicRowContext outerRowContext = makeOuterRowContext();
        outerRowContext.cellsByName.insert(QStringLiteral("outer.id"),
                                           logic::LogicCellValue{QStringLiteral("10"), tabledef::ColumnType::Int, false});
        outerRowContext.cellsByName.insert(QStringLiteral("id"),
                                           logic::LogicCellValue{QStringLiteral("1"), tabledef::ColumnType::Int, false});

        const logic::CorrelationBindings bindings = logic::buildCorrelationBindings(outerRowContext,
                                                                                   {QStringLiteral("outer.id")});

        QCOMPARE(bindings.items.size(), 1);
        QCOMPARE(bindings.items.first().name, QStringLiteral("outer.id"));
        QCOMPARE(bindings.items.first().value, QStringLiteral("10"));
    }

    void test_correlatedSubqueryUsesExactOuterBindingDuringEvaluation()
    {
        const QString expression = QStringLiteral(
            "EXISTS (SELECT id FROM child WHERE child.parent_id = outer.id)");
        const logic::LogicTokenizeResult tokenized = logic::tokenizeLogicExpression(expression);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));
        const logic::LogicParseResult parsed = logic::parseLogicTokens(expression, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));

        BindingAwareSubqueryExecutor executor;
        logic::LogicEvalContext evalContext;
        evalContext.subqueryExecutor = &executor;
        evalContext.allowSubquery = true;

        logic::LogicRowContext outerRowContext = makeOuterRowContext();
        outerRowContext.cellsByName.insert(QStringLiteral("outer.id"),
                                           logic::LogicCellValue{QStringLiteral("10"), tabledef::ColumnType::Int, false});
        outerRowContext.cellsByName.insert(QStringLiteral("id"),
                                           logic::LogicCellValue{QStringLiteral("1"), tabledef::ColumnType::Int, false});

        const logic::LogicEvalResult result = logic::evaluateLogicExpression(parsed.root, outerRowContext, evalContext);
        QVERIFY2(result.success, qPrintable(result.error.message));
        QCOMPARE(result.truth, logic::LogicTruthValue::True);
        QCOMPARE(executor.observedBindings.items.size(), 1);
        QCOMPARE(executor.observedBindings.items.first().name, QStringLiteral("outer.id"));
        QCOMPARE(executor.observedBindings.items.first().value, QStringLiteral("10"));
    }

    void test_parseInLiteralListRejectsColumnReference()
    {
        const QString expression = QStringLiteral("a IN (b, 2)");
        const logic::LogicTokenizeResult tokenized = logic::tokenizeLogicExpression(expression);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));

        const logic::LogicParseResult parsed = logic::parseLogicTokens(expression, tokenized.tokens);
        QVERIFY(!parsed.success);
        QVERIFY(parsed.error.message.contains(QStringLiteral("literal value")));
    }

    void test_evaluateNullComparisonReturnsUnknown()
    {
        const QString expression = QStringLiteral("id = NULL");
        const logic::LogicTokenizeResult tokenized = logic::tokenizeLogicExpression(expression);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));

        const logic::LogicParseResult parsed = logic::parseLogicTokens(expression, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));

        logic::LogicRowContext rowContext;
        rowContext.cellsByName.insert(QStringLiteral("id"),
                                      logic::LogicCellValue{QStringLiteral("1"), tabledef::ColumnType::Int, false});

        const logic::LogicEvalResult result = logic::evaluateLogicExpression(parsed.root, rowContext, {});
        QVERIFY2(result.success, qPrintable(result.error.message));
        QCOMPARE(result.truth, logic::LogicTruthValue::Unknown);

        const logic::LogicEvalResult checkResult = logic::evaluateCheckConstraintForRow(parsed.root,
                                                                                       rowContext,
                                                                                       {});
        QVERIFY(!checkResult.success);
        QCOMPARE(checkResult.truth, logic::LogicTruthValue::Unknown);
        QVERIFY(checkResult.error.message.contains(QStringLiteral("CHECK constraint failed")));
    }

    void test_checkConstraintRejectsSubqueries()
    {
        const QString expression = QStringLiteral("EXISTS (SELECT id FROM child WHERE child.parent_id = outer.id)");
        const logic::LogicTokenizeResult tokenized = logic::tokenizeLogicExpression(expression);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));

        const logic::LogicParseResult parsed = logic::parseLogicTokens(expression, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));

        const logic::LogicEvalResult result = logic::evaluateCheckConstraintForRow(parsed.root,
                                                                                  makeOuterRowContext(),
                                                                                  {});
        QVERIFY(!result.success);
        QVERIFY(result.error.message.contains(QStringLiteral("CHECK does not allow subqueries")));
    }

    void test_quantifiedComparisonSupportsAnyAndAll()
    {
        repo::TableData subqueryTable;
        subqueryTable.columns = {QStringLiteral("score")};
        subqueryTable.rows = {{QStringLiteral("1")}, {QStringLiteral("5")}};

        FixedSubqueryExecutor executor(subqueryTable, {tabledef::ColumnType::Int});
        logic::LogicEvalContext evalContext;
        evalContext.subqueryExecutor = &executor;
        evalContext.allowSubquery = true;

        const QString anyExpression = QStringLiteral("name > ANY (SELECT score FROM scores)");
        const logic::LogicTokenizeResult anyTokenized = logic::tokenizeLogicExpression(anyExpression);
        QVERIFY2(anyTokenized.success, qPrintable(anyTokenized.error.message));
        const logic::LogicParseResult anyParsed = logic::parseLogicTokens(anyExpression, anyTokenized.tokens);
        QVERIFY2(anyParsed.success, qPrintable(anyParsed.error.message));
        QCOMPARE(anyParsed.root.type, logic::LogicNodeType::QuantifiedSubquery);

        logic::LogicRowContext anyRowContext;
        anyRowContext.cellsByName.insert(QStringLiteral("name"),
                                         logic::LogicCellValue{QStringLiteral("10"), tabledef::ColumnType::Varchar, false});
        const logic::LogicEvalResult anyResult = logic::evaluateLogicExpression(anyParsed.root, anyRowContext, evalContext);
        QVERIFY2(anyResult.success, qPrintable(anyResult.error.message));
        QCOMPARE(anyResult.truth, logic::LogicTruthValue::True);

        anyRowContext.cellsByName.insert(QStringLiteral("name"),
                                         logic::LogicCellValue{QStringLiteral("0"), tabledef::ColumnType::Varchar, false});
        const logic::LogicEvalResult anyFalseResult = logic::evaluateLogicExpression(anyParsed.root, anyRowContext, evalContext);
        QVERIFY2(anyFalseResult.success, qPrintable(anyFalseResult.error.message));
        QCOMPARE(anyFalseResult.truth, logic::LogicTruthValue::False);

        const QString allExpression = QStringLiteral("name > ALL (SELECT score FROM scores)");
        const logic::LogicTokenizeResult allTokenized = logic::tokenizeLogicExpression(allExpression);
        QVERIFY2(allTokenized.success, qPrintable(allTokenized.error.message));
        const logic::LogicParseResult allParsed = logic::parseLogicTokens(allExpression, allTokenized.tokens);
        QVERIFY2(allParsed.success, qPrintable(allParsed.error.message));
        QCOMPARE(allParsed.root.type, logic::LogicNodeType::QuantifiedSubquery);

        logic::LogicRowContext allRowContext;
        allRowContext.cellsByName.insert(QStringLiteral("name"),
                                         logic::LogicCellValue{QStringLiteral("10"), tabledef::ColumnType::Varchar, false});
        const logic::LogicEvalResult allResult = logic::evaluateLogicExpression(allParsed.root, allRowContext, evalContext);
        QVERIFY2(allResult.success, qPrintable(allResult.error.message));
        QCOMPARE(allResult.truth, logic::LogicTruthValue::True);

        allRowContext.cellsByName.insert(QStringLiteral("name"),
                                         logic::LogicCellValue{QStringLiteral("3"), tabledef::ColumnType::Varchar, false});
        const logic::LogicEvalResult allFalseResult = logic::evaluateLogicExpression(allParsed.root, allRowContext, evalContext);
        QVERIFY2(allFalseResult.success, qPrintable(allFalseResult.error.message));
        QCOMPARE(allFalseResult.truth, logic::LogicTruthValue::False);
    }

    void test_correlatedQuantifiedComparisonSupportsAnyAndAll()
    {
        BindingAwareSubqueryExecutor executor;
        logic::LogicEvalContext evalContext;
        evalContext.subqueryExecutor = &executor;
        evalContext.allowSubquery = true;

        const QString anyExpression = QStringLiteral(
            "score = ANY (SELECT parent_id FROM child WHERE child.parent_id = outer.id)");
        const logic::LogicTokenizeResult anyTokenized = logic::tokenizeLogicExpression(anyExpression);
        QVERIFY2(anyTokenized.success, qPrintable(anyTokenized.error.message));
        const logic::LogicParseResult anyParsed = logic::parseLogicTokens(anyExpression, anyTokenized.tokens);
        QVERIFY2(anyParsed.success, qPrintable(anyParsed.error.message));
        QCOMPARE(anyParsed.root.type, logic::LogicNodeType::QuantifiedSubquery);
        QCOMPARE(anyParsed.root.referencedOuterNames, QStringList({QStringLiteral("outer.id")}));

        logic::LogicRowContext anyTrueRow;
        anyTrueRow.tableName = QStringLiteral("parent");
        anyTrueRow.cellsByName.insert(QStringLiteral("id"),
                                      logic::LogicCellValue{QStringLiteral("10"), tabledef::ColumnType::Int, false});
        anyTrueRow.cellsByName.insert(QStringLiteral("score"),
                                      logic::LogicCellValue{QStringLiteral("10"), tabledef::ColumnType::Int, false});
        const logic::LogicEvalResult anyTrueResult = logic::evaluateLogicExpression(anyParsed.root,
                                                                                   anyTrueRow,
                                                                                   evalContext);
        QVERIFY2(anyTrueResult.success, qPrintable(anyTrueResult.error.message));
        QCOMPARE(anyTrueResult.truth, logic::LogicTruthValue::True);

        logic::LogicRowContext anyFalseRow = anyTrueRow;
        anyFalseRow.cellsByName.insert(QStringLiteral("score"),
                                       logic::LogicCellValue{QStringLiteral("30"), tabledef::ColumnType::Int, false});
        const logic::LogicEvalResult anyFalseResult = logic::evaluateLogicExpression(anyParsed.root,
                                                                                    anyFalseRow,
                                                                                    evalContext);
        QVERIFY2(anyFalseResult.success, qPrintable(anyFalseResult.error.message));
        QCOMPARE(anyFalseResult.truth, logic::LogicTruthValue::False);

        const QString allExpression = QStringLiteral(
            "score > ALL (SELECT parent_id FROM child WHERE child.parent_id = outer.id)");
        const logic::LogicTokenizeResult allTokenized = logic::tokenizeLogicExpression(allExpression);
        QVERIFY2(allTokenized.success, qPrintable(allTokenized.error.message));
        const logic::LogicParseResult allParsed = logic::parseLogicTokens(allExpression, allTokenized.tokens);
        QVERIFY2(allParsed.success, qPrintable(allParsed.error.message));
        QCOMPARE(allParsed.root.type, logic::LogicNodeType::QuantifiedSubquery);
        QCOMPARE(allParsed.root.referencedOuterNames, QStringList({QStringLiteral("outer.id")}));

        logic::LogicRowContext allTrueRow = anyTrueRow;
        allTrueRow.cellsByName.insert(QStringLiteral("score"),
                                      logic::LogicCellValue{QStringLiteral("20"), tabledef::ColumnType::Int, false});
        const logic::LogicEvalResult allTrueResult = logic::evaluateLogicExpression(allParsed.root,
                                                                                   allTrueRow,
                                                                                   evalContext);
        QVERIFY2(allTrueResult.success, qPrintable(allTrueResult.error.message));
        QCOMPARE(allTrueResult.truth, logic::LogicTruthValue::True);

        logic::LogicRowContext allFalseRow = anyTrueRow;
        allFalseRow.cellsByName.insert(QStringLiteral("score"),
                                       logic::LogicCellValue{QStringLiteral("5"), tabledef::ColumnType::Int, false});
        const logic::LogicEvalResult allFalseResult = logic::evaluateLogicExpression(allParsed.root,
                                                                                    allFalseRow,
                                                                                    evalContext);
        QVERIFY2(allFalseResult.success, qPrintable(allFalseResult.error.message));
        QCOMPARE(allFalseResult.truth, logic::LogicTruthValue::False);
    }

    void test_correlatedQuantifiedComparisonReturnsUnknownWhenSubqueryContainsNull()
    {
        NullAwareBindingSubqueryExecutor executor;
        logic::LogicEvalContext evalContext;
        evalContext.subqueryExecutor = &executor;
        evalContext.allowSubquery = true;

        const QString anyExpression = QStringLiteral(
            "score = ANY (SELECT parent_id FROM child WHERE child.parent_id = outer.id)");
        const auto anyTokenized = logic::tokenizeLogicExpression(anyExpression);
        QVERIFY2(anyTokenized.success, qPrintable(anyTokenized.error.message));
        const auto anyParsed = logic::parseLogicTokens(anyExpression, anyTokenized.tokens);
        QVERIFY2(anyParsed.success, qPrintable(anyParsed.error.message));

        logic::LogicRowContext anyRow;
        anyRow.tableName = QStringLiteral("parent");
        anyRow.cellsByName.insert(QStringLiteral("id"),
                                  logic::LogicCellValue{QStringLiteral("10"), tabledef::ColumnType::Int, false});
        anyRow.cellsByName.insert(QStringLiteral("score"),
                                  logic::LogicCellValue{QStringLiteral("30"), tabledef::ColumnType::Int, false});

        const auto anyResult = logic::evaluateLogicExpression(anyParsed.root, anyRow, evalContext);
        QVERIFY2(anyResult.success, qPrintable(anyResult.error.message));
        QCOMPARE(anyResult.truth, logic::LogicTruthValue::Unknown);

        const QString allExpression = QStringLiteral(
            "score > ALL (SELECT parent_id FROM child WHERE child.parent_id = outer.id)");
        const auto allTokenized = logic::tokenizeLogicExpression(allExpression);
        QVERIFY2(allTokenized.success, qPrintable(allTokenized.error.message));
        const auto allParsed = logic::parseLogicTokens(allExpression, allTokenized.tokens);
        QVERIFY2(allParsed.success, qPrintable(allParsed.error.message));

        logic::LogicRowContext allRow;
        allRow.tableName = QStringLiteral("parent");
        allRow.cellsByName.insert(QStringLiteral("id"),
                                  logic::LogicCellValue{QStringLiteral("10"), tabledef::ColumnType::Int, false});
        allRow.cellsByName.insert(QStringLiteral("score"),
                                  logic::LogicCellValue{QStringLiteral("20"), tabledef::ColumnType::Int, false});

        const auto allResult = logic::evaluateLogicExpression(allParsed.root, allRow, evalContext);
        QVERIFY2(allResult.success, qPrintable(allResult.error.message));
        QCOMPARE(allResult.truth, logic::LogicTruthValue::Unknown);
    }

    void test_parseAndNotEvaluation()
    {
        const QString expr = QStringLiteral("a = 1 AND b = 2");

        const auto tokenized = logic::tokenizeLogicExpression(expr);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));

        const auto parsed = logic::parseLogicTokens(expr, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));
        QCOMPARE(parsed.root.type, logic::LogicNodeType::Binary);

        logic::LogicRowContext row;
        row.cellsByName.insert(QStringLiteral("a"),
                              logic::LogicCellValue{QStringLiteral("1"), tabledef::ColumnType::Int, false});
        row.cellsByName.insert(QStringLiteral("b"),
                              logic::LogicCellValue{QStringLiteral("2"), tabledef::ColumnType::Int, false});

        const auto res = logic::evaluateLogicExpression(parsed.root, row, {});
        QVERIFY2(res.success, qPrintable(res.error.message));
        QCOMPARE(res.truth, logic::LogicTruthValue::True);

        row.cellsByName.insert(QStringLiteral("b"),
                              logic::LogicCellValue{QStringLiteral("3"), tabledef::ColumnType::Int, false});
        const auto res2 = logic::evaluateLogicExpression(parsed.root, row, {});
        QVERIFY2(res2.success, qPrintable(res2.error.message));
        QCOMPARE(res2.truth, logic::LogicTruthValue::False);
    }

    void test_notOrExpression()
    {
        const QString expr = QStringLiteral("NOT (a = 1 OR b = 2)");

        const auto tokenized = logic::tokenizeLogicExpression(expr);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));

        const auto parsed = logic::parseLogicTokens(expr, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));
        QCOMPARE(parsed.root.type, logic::LogicNodeType::Unary);

        logic::LogicRowContext row;
        row.cellsByName.insert(QStringLiteral("a"),
                              logic::LogicCellValue{QStringLiteral("0"), tabledef::ColumnType::Int, false});
        row.cellsByName.insert(QStringLiteral("b"),
                              logic::LogicCellValue{QStringLiteral("0"), tabledef::ColumnType::Int, false});

        const auto res = logic::evaluateLogicExpression(parsed.root, row, {});
        QVERIFY2(res.success, qPrintable(res.error.message));
        QCOMPARE(res.truth, logic::LogicTruthValue::True);

        row.cellsByName.insert(QStringLiteral("a"),
                              logic::LogicCellValue{QStringLiteral("1"), tabledef::ColumnType::Int, false});
        const auto res2 = logic::evaluateLogicExpression(parsed.root, row, {});
        QVERIFY2(res2.success, qPrintable(res2.error.message));
        QCOMPARE(res2.truth, logic::LogicTruthValue::False);
    }

    void test_isNullAndIsNotNull()
    {
        const QString isNullExpr = QStringLiteral("a IS NULL");
        auto tokenized = logic::tokenizeLogicExpression(isNullExpr);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));
        auto parsed = logic::parseLogicTokens(isNullExpr, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));

        logic::LogicRowContext row;
        row.cellsByName.insert(QStringLiteral("a"),
                              logic::LogicCellValue{QString(), tabledef::ColumnType::Varchar, true});
        auto res = logic::evaluateLogicExpression(parsed.root, row, {});
        QVERIFY2(res.success, qPrintable(res.error.message));
        QCOMPARE(res.truth, logic::LogicTruthValue::True);

        const QString isNotNullExpr = QStringLiteral("a IS NOT NULL");
        tokenized = logic::tokenizeLogicExpression(isNotNullExpr);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));
        parsed = logic::parseLogicTokens(isNotNullExpr, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));

        row.cellsByName.insert(QStringLiteral("a"),
                              logic::LogicCellValue{QStringLiteral("x"), tabledef::ColumnType::Varchar, false});
        res = logic::evaluateLogicExpression(parsed.root, row, {});
        QVERIFY2(res.success, qPrintable(res.error.message));
        QCOMPARE(res.truth, logic::LogicTruthValue::True);

        tokenized = logic::tokenizeLogicExpression(isNullExpr);
        parsed = logic::parseLogicTokens(isNullExpr, tokenized.tokens);
        row.cellsByName.insert(QStringLiteral("a"),
                              logic::LogicCellValue{QStringLiteral("x"), tabledef::ColumnType::Varchar, false});
        res = logic::evaluateLogicExpression(parsed.root, row, {});
        QVERIFY2(res.success, qPrintable(res.error.message));
        QCOMPARE(res.truth, logic::LogicTruthValue::False);
    }

    void test_inListEmptyAndNullRules()
    {
        const QString expr = QStringLiteral("id IN (1, 2, NULL)");
        auto tokenized = logic::tokenizeLogicExpression(expr);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));
        auto parsed = logic::parseLogicTokens(expr, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));
        QCOMPARE(parsed.root.type, logic::LogicNodeType::InList);

        logic::LogicRowContext row;
        row.cellsByName.insert(QStringLiteral("id"),
                              logic::LogicCellValue{QStringLiteral("1"), tabledef::ColumnType::Int, false});
        auto res = logic::evaluateLogicExpression(parsed.root, row, {});
        QVERIFY2(res.success, qPrintable(res.error.message));
        QCOMPARE(res.truth, logic::LogicTruthValue::True);

        row.cellsByName.insert(QStringLiteral("id"),
                              logic::LogicCellValue{QStringLiteral("3"), tabledef::ColumnType::Int, false});
        res = logic::evaluateLogicExpression(parsed.root, row, {});
        QVERIFY2(res.success, qPrintable(res.error.message));
        QCOMPARE(res.truth, logic::LogicTruthValue::Unknown);

        const QString emptyExpr = QStringLiteral("id IN ()");
        tokenized = logic::tokenizeLogicExpression(emptyExpr);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));
        parsed = logic::parseLogicTokens(emptyExpr, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));
        row.cellsByName.insert(QStringLiteral("id"),
                              logic::LogicCellValue{QStringLiteral("1"), tabledef::ColumnType::Int, false});
        res = logic::evaluateLogicExpression(parsed.root, row, {});
        QVERIFY2(res.success, qPrintable(res.error.message));
        QCOMPARE(res.truth, logic::LogicTruthValue::False);

        const QString notEmptyExpr = QStringLiteral("id NOT IN ()");
        tokenized = logic::tokenizeLogicExpression(notEmptyExpr);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));
        parsed = logic::parseLogicTokens(notEmptyExpr, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));
        res = logic::evaluateLogicExpression(parsed.root, row, {});
        QVERIFY2(res.success, qPrintable(res.error.message));
        QCOMPARE(res.truth, logic::LogicTruthValue::True);
    }

    void test_quantifiedEmptySubquerySemantics()
    {
        const QString anyExpr = QStringLiteral("id = ANY (SELECT score FROM scores)");
        auto tokenized = logic::tokenizeLogicExpression(anyExpr);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));
        auto parsed = logic::parseLogicTokens(anyExpr, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));
        QCOMPARE(parsed.root.type, logic::LogicNodeType::QuantifiedSubquery);

        repo::TableData emptyTable;
        emptyTable.columns = {QStringLiteral("score")};
        FixedSubqueryExecutor executor(emptyTable, {tabledef::ColumnType::Int});
        logic::LogicEvalContext evalContext;
        evalContext.subqueryExecutor = &executor;
        evalContext.allowSubquery = true;

        logic::LogicRowContext row;
        row.cellsByName.insert(QStringLiteral("id"),
                              logic::LogicCellValue{QStringLiteral("1"), tabledef::ColumnType::Int, false});
        auto res = logic::evaluateLogicExpression(parsed.root, row, evalContext);
        QVERIFY2(res.success, qPrintable(res.error.message));
        QCOMPARE(res.truth, logic::LogicTruthValue::False);

        const QString allExpr = QStringLiteral("id > ALL (SELECT score FROM scores)");
        tokenized = logic::tokenizeLogicExpression(allExpr);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));
        parsed = logic::parseLogicTokens(allExpr, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));
        QCOMPARE(parsed.root.type, logic::LogicNodeType::QuantifiedSubquery);

        res = logic::evaluateLogicExpression(parsed.root, row, evalContext);
        QVERIFY2(res.success, qPrintable(res.error.message));
        QCOMPARE(res.truth, logic::LogicTruthValue::True);
    }

    void test_threeValueLogicPropagation()
    {
        const QString expr = QStringLiteral("a = 1 AND b = NULL");
        auto tokenized = logic::tokenizeLogicExpression(expr);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));
        auto parsed = logic::parseLogicTokens(expr, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));

        logic::LogicRowContext row;
        row.cellsByName.insert(QStringLiteral("a"),
                              logic::LogicCellValue{QStringLiteral("1"), tabledef::ColumnType::Int, false});
        row.cellsByName.insert(QStringLiteral("b"),
                              logic::LogicCellValue{QString(), tabledef::ColumnType::Int, true});
        auto res = logic::evaluateLogicExpression(parsed.root, row, {});
        QVERIFY2(res.success, qPrintable(res.error.message));
        QCOMPARE(res.truth, logic::LogicTruthValue::Unknown);

        const QString exprOr = QStringLiteral("a = 2 OR b = NULL");
        tokenized = logic::tokenizeLogicExpression(exprOr);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));
        parsed = logic::parseLogicTokens(exprOr, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));

        row.cellsByName.insert(QStringLiteral("a"),
                              logic::LogicCellValue{QStringLiteral("2"), tabledef::ColumnType::Int, false});
        res = logic::evaluateLogicExpression(parsed.root, row, {});
        QVERIFY2(res.success, qPrintable(res.error.message));
        QCOMPARE(res.truth, logic::LogicTruthValue::True);

        row.cellsByName.insert(QStringLiteral("a"),
                              logic::LogicCellValue{QStringLiteral("3"), tabledef::ColumnType::Int, false});
        res = logic::evaluateLogicExpression(parsed.root, row, {});
        QVERIFY2(res.success, qPrintable(res.error.message));
        QCOMPARE(res.truth, logic::LogicTruthValue::Unknown);
    }

    void test_tokenizerPopulatesFieldsAndKeywordTypes()
    {
        const QString expr = QStringLiteral("a AND outer.id IS NULL");
        const logic::LogicTokenizeResult tokenized = logic::tokenizeLogicExpression(expr);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));

        bool sawAnd = false;
        bool sawIs = false;
        bool sawNull = false;
        for (const logic::LogicToken &token : tokenized.tokens) {
            if (token.type == logic::LogicTokenType::EndOfInput) break;
            QVERIFY(token.position >= 0);
            QVERIFY(!token.rawText.isNull());
            if (token.type == logic::LogicTokenType::Keyword) {
                if (token.keywordType == logic::LogicKeywordType::And) sawAnd = true;
                if (token.keywordType == logic::LogicKeywordType::Is) sawIs = true;
                if (token.keywordType == logic::LogicKeywordType::Null) sawNull = true;
            }
        }
        QVERIFY(sawAnd);
        QVERIFY(sawIs);
        QVERIFY(sawNull);
    }

    void test_captureSubqueryWithNestedParentheses()
    {
        const QString expression = QStringLiteral(
            "EXISTS (SELECT (id) FROM child WHERE child.parent_id = outer.id)");

        const logic::LogicTokenizeResult tokenized = logic::tokenizeLogicExpression(expression);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));

        const logic::LogicParseResult parsed = logic::parseLogicTokens(expression, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));
        QCOMPARE(parsed.root.type, logic::LogicNodeType::ExistsSubquery);
        QVERIFY(parsed.root.subquerySql.contains(QStringLiteral("(id)")));
        QCOMPARE(parsed.root.referencedOuterNames, QStringList({QStringLiteral("outer.id")}));
    }

    void test_notInLiteralListNegation()
    {
        const QString expr = QStringLiteral("id NOT IN (1, 2)");
        const auto tokenized = logic::tokenizeLogicExpression(expr);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));

        const auto parsed = logic::parseLogicTokens(expr, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));
        QCOMPARE(parsed.root.type, logic::LogicNodeType::InList);
        QVERIFY(parsed.root.negated);

        logic::LogicRowContext row;
        row.cellsByName.insert(QStringLiteral("id"),
                              logic::LogicCellValue{QStringLiteral("1"), tabledef::ColumnType::Int, false});
        auto res = logic::evaluateLogicExpression(parsed.root, row, {});
        QVERIFY2(res.success, qPrintable(res.error.message));
        QCOMPARE(res.truth, logic::LogicTruthValue::False);

        row.cellsByName.insert(QStringLiteral("id"),
                              logic::LogicCellValue{QStringLiteral("3"), tabledef::ColumnType::Int, false});
        res = logic::evaluateLogicExpression(parsed.root, row, {});
        QVERIFY2(res.success, qPrintable(res.error.message));
        QCOMPARE(res.truth, logic::LogicTruthValue::True);
    }

    void test_missingColumnComparison()
    {
        const QString expr = QStringLiteral("no_such = 1");
        const auto tokenized = logic::tokenizeLogicExpression(expr);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));

        const auto parsed = logic::parseLogicTokens(expr, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));

        logic::LogicRowContext row; // empty context
        const auto res = logic::evaluateLogicExpression(parsed.root, row, {});
        QVERIFY(!res.success);
        QVERIFY(res.error.message.contains(QStringLiteral("missing column")) || res.error.message.contains(QStringLiteral("missing column 'no_such'")));
    }

    void test_inSubqueryRejectsMultiColumnResult()
    {
        const QString expr = QStringLiteral("id IN (SELECT a, b FROM t)");
        const auto tokenized = logic::tokenizeLogicExpression(expr);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));

        const auto parsed = logic::parseLogicTokens(expr, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));
        QCOMPARE(parsed.root.type, logic::LogicNodeType::InSubquery);

        repo::TableData multi;
        multi.columns = {QStringLiteral("a"), QStringLiteral("b")};
        multi.rows = {{QStringLiteral("1"), QStringLiteral("2")}};

        FixedSubqueryExecutor executor(multi, {tabledef::ColumnType::Int, tabledef::ColumnType::Int});
        logic::LogicEvalContext evalContext;
        evalContext.subqueryExecutor = &executor;
        evalContext.allowSubquery = true;

        logic::LogicRowContext row;
        row.cellsByName.insert(QStringLiteral("id"), logic::LogicCellValue{QStringLiteral("1"), tabledef::ColumnType::Int, false});

        const auto res = logic::evaluateLogicExpression(parsed.root, row, evalContext);
        QVERIFY(!res.success);
        QVERIFY(res.error.message.contains(QStringLiteral("subquery must return a single column")));
    }

    void test_normalizeSelectResultToSet_handlesEmptyRowAsNull()
    {
        service::SelectRowsResult selectRes;
        selectRes.success = true;
        selectRes.resultTable.columns = {QStringLiteral("score")};
        selectRes.resultTable.rows.append(QStringList()); // empty row

        const QList<setdef::SetValue> values = logic::normalizeSelectResultToSet(selectRes);
        QCOMPARE(values.size(), 1);
        QCOMPARE(values.first().isNull, true);
    }

    void test_stringLiteralEscaping()
    {
        const QString expr = QStringLiteral("name = 'O''Reilly'");
        const auto tokenized = logic::tokenizeLogicExpression(expr);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));

        const auto parsed = logic::parseLogicTokens(expr, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));

        logic::LogicRowContext row;
        row.cellsByName.insert(QStringLiteral("name"), logic::LogicCellValue{QStringLiteral("O'Reilly"), tabledef::ColumnType::Varchar, false});
        const auto res = logic::evaluateLogicExpression(parsed.root, row, {});
        QVERIFY2(res.success, qPrintable(res.error.message));
        QCOMPARE(res.truth, logic::LogicTruthValue::True);
    }

    void test_unterminatedStringLiteralReturnsError()
    {
        const QString expr = QStringLiteral("name = 'O");
        const auto tokenized = logic::tokenizeLogicExpression(expr);
        QVERIFY(!tokenized.success);
        QCOMPARE(tokenized.error.message, QStringLiteral("unterminated string literal"));
        QCOMPARE(tokenized.error.position, expr.indexOf(QLatin1Char('\'')));
    }

    void test_parseCorrelatedExistsAllowsSelectStarSubquery()
    {
        const QString expression = QStringLiteral(
            "EXISTS (SELECT * FROM child WHERE child.parent_id = outer.id)");

        const auto tokenized = logic::tokenizeLogicExpression(expression);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));

        const auto parsed = logic::parseLogicTokens(expression, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));
        QCOMPARE(parsed.root.type, logic::LogicNodeType::ExistsSubquery);
        QCOMPARE(parsed.root.subquerySql, QStringLiteral("SELECT * FROM child WHERE child.parent_id = outer.id"));
        QCOMPARE(parsed.root.referencedOuterNames, QStringList({QStringLiteral("outer.id")}));
    }

    void test_parseCorrelatedReferenceErrorPosition()
    {
        const QString expression = QStringLiteral(
            "EXISTS (SELECT id FROM child WHERE child.parent_id = parent.id)");

        const logic::LogicTokenizeResult tokenized = logic::tokenizeLogicExpression(expression);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));

        const logic::LogicParseResult parsed = logic::parseLogicTokens(expression, tokenized.tokens);
        QVERIFY(!parsed.success);
        QVERIFY(parsed.error.position >= 0);
    }

    void test_existsSubqueryWithoutExecutorReturnsError()
    {
        const QString expr = QStringLiteral("EXISTS (SELECT id FROM t)");
        const auto tokenized = logic::tokenizeLogicExpression(expr);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));
        const auto parsed = logic::parseLogicTokens(expr, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));

        logic::LogicEvalContext evalContext; // subqueryExecutor == nullptr
        const auto res = logic::evaluateExistsSubqueryNode(parsed.root, makeOuterRowContext(), evalContext);
        QVERIFY(!res.success);
        QVERIFY(res.error.message.contains(QStringLiteral("subquery executor is not configured")));
    }

    void test_existsSubqueryIgnoresColumnCount()
    {
        repo::TableData tableData;
        tableData.columns = {QStringLiteral("a"), QStringLiteral("b")};
        tableData.rows = {{QStringLiteral("1"), QStringLiteral("2")}};

        FixedSubqueryExecutor executor(tableData,
                                       {tabledef::ColumnType::Int, tabledef::ColumnType::Int});
        logic::LogicEvalContext evalContext;
        evalContext.subqueryExecutor = &executor;
        evalContext.allowSubquery = true;

        const QString expr = QStringLiteral("EXISTS (SELECT a, b FROM t)");
        const auto tokenized = logic::tokenizeLogicExpression(expr);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));
        const auto parsed = logic::parseLogicTokens(expr, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));

        const auto res = logic::evaluateLogicExpression(parsed.root, makeOuterRowContext(), evalContext);
        QVERIFY2(res.success, qPrintable(res.error.message));
        QCOMPARE(res.truth, logic::LogicTruthValue::True);
    }

    void test_existsSubqueryIgnoresNullValues()
    {
        repo::TableData tableData;
        tableData.columns = {QStringLiteral("a")};
        tableData.rows = {{QString()}};

        FixedSubqueryExecutor executor(tableData, {tabledef::ColumnType::Varchar});
        logic::LogicEvalContext evalContext;
        evalContext.subqueryExecutor = &executor;
        evalContext.allowSubquery = true;

        const QString expr = QStringLiteral("EXISTS (SELECT a FROM t)");
        const auto tokenized = logic::tokenizeLogicExpression(expr);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));
        const auto parsed = logic::parseLogicTokens(expr, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));

        const auto res = logic::evaluateLogicExpression(parsed.root, makeOuterRowContext(), evalContext);
        QVERIFY2(res.success, qPrintable(res.error.message));
        QCOMPARE(res.truth, logic::LogicTruthValue::True);
    }

    void test_logicSubqueryExecutorAdapter_forwardsCalls()
    {
        QueryExecuteResult expected;
        expected.success = true;
        expected.selectResult.success = true;
        expected.selectResult.resultTable.columns = {QStringLiteral("c")};
        expected.selectResult.resultTable.rows = {{QStringLiteral("1")}};

        FakeQueryExecutor executor(expected);
        logic::LogicSubqueryExecutorAdapter adapter(&executor);

        QueryExecuteContext ctx;
        const QueryExecuteResult r1 = adapter.executeSelectSql(QStringLiteral("SELECT 1"), ctx);
        QVERIFY2(r1.success, qPrintable(r1.errorMessage));
        QCOMPARE(r1.selectResult.resultTable.rows.size(), 1);

        logic::CorrelationBindings bindings;
        const QueryExecuteResult r2 = adapter.executeCorrelatedSelect(QStringLiteral("SELECT 1"), bindings, ctx);
        QVERIFY2(r2.success, qPrintable(r2.errorMessage));
        QCOMPARE(r2.selectResult.resultTable.rows.size(), 1);
    }

    void test_logicSubqueryExecutorAdapter_nullExecutorReturnsDefault()
    {
        logic::LogicSubqueryExecutorAdapter adapter(nullptr);
        QueryExecuteContext ctx;
        const QueryExecuteResult r1 = adapter.executeSelectSql(QStringLiteral("SELECT 1"), ctx);
        QVERIFY(!r1.success);
        const QueryExecuteResult r2 = adapter.executeCorrelatedSelect(QStringLiteral("SELECT 1"), {}, ctx);
        QVERIFY(!r2.success);
    }

    void test_localeUnicodeComparison_matchesQStringLocaleAwareCompare()
    {
        const QString s1 = QString::fromUtf8("straße");
        const QString expr = QStringLiteral("name = 'strasse'");
        const auto tokenized = logic::tokenizeLogicExpression(expr);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));
        const auto parsed = logic::parseLogicTokens(expr, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));
        logic::LogicRowContext row;
        row.cellsByName.insert(QStringLiteral("name"), logic::LogicCellValue{s1, tabledef::ColumnType::Varchar, false});
        const auto res = logic::evaluateLogicExpression(parsed.root, row, {});
        QVERIFY2(res.success, qPrintable(res.error.message));
        const int cmp = QString::localeAwareCompare(s1, QStringLiteral("strasse"));
        const logic::LogicTruthValue expected = (cmp == 0) ? logic::LogicTruthValue::True : logic::LogicTruthValue::False;
        QCOMPARE(res.truth, expected);
    }

    void test_extremeIntegerPrecision()
    {
        const QString expr = QStringLiteral("a = 9007199254740992");
        const auto tokenized = logic::tokenizeLogicExpression(expr);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));
        const auto parsed = logic::parseLogicTokens(expr, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));
        logic::LogicRowContext row;
        row.cellsByName.insert(QStringLiteral("a"), logic::LogicCellValue{QStringLiteral("9007199254740993"), tabledef::ColumnType::Int, false});
        const auto res = logic::evaluateLogicExpression(parsed.root, row, {});
        QVERIFY2(res.success, qPrintable(res.error.message));
        bool ok1 = false, ok2 = false;
        const double left = QStringLiteral("9007199254740993").toDouble(&ok1);
        const double right = QStringLiteral("9007199254740992").toDouble(&ok2);
        QVERIFY(ok1 && ok2);
        const logic::LogicTruthValue expected = (left == right) ? logic::LogicTruthValue::True : logic::LogicTruthValue::False;
        QCOMPARE(res.truth, expected);
    }

    void test_numericParsingFailureUnknown()
    {
        const QString expr = QStringLiteral("a = 123.45");
        const auto tokenized = logic::tokenizeLogicExpression(expr);
        QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));
        const auto parsed = logic::parseLogicTokens(expr, tokenized.tokens);
        QVERIFY2(parsed.success, qPrintable(parsed.error.message));
        logic::LogicRowContext row;
        row.cellsByName.insert(QStringLiteral("a"), logic::LogicCellValue{QStringLiteral("not_a_number"), tabledef::ColumnType::Int, false});
        const auto res = logic::evaluateLogicExpression(parsed.root, row, {});
        QVERIFY2(res.success, qPrintable(res.error.message));
        QCOMPARE(res.truth, logic::LogicTruthValue::Unknown);
    }

    void test_errorMessagesAndPositions()
    {
        // Correlated prefix error
        {
            const QString expr = QStringLiteral("EXISTS (SELECT id FROM child WHERE child.parent_id = parent.id)");
            const auto tokenized = logic::tokenizeLogicExpression(expr);
            QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));
            const auto parsed = logic::parseLogicTokens(expr, tokenized.tokens);
            QVERIFY(!parsed.success);
            QCOMPARE(parsed.error.message, QStringLiteral("only outer.xxx is allowed in correlated subqueries"));
            const int start = expr.indexOf('(');
            const int end = expr.lastIndexOf(')');
            const QString sub = expr.mid(start + 1, end - start - 1);
            const int expectedPos = sub.indexOf(QStringLiteral("parent.id"));
            QCOMPARE(parsed.error.position, expectedPos);
        }

        // EXISTS missing parentheses
        {
            const QString expr = QStringLiteral("EXISTS SELECT id");
            const auto tokenized = logic::tokenizeLogicExpression(expr);
            QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));
            const auto parsed = logic::parseLogicTokens(expr, tokenized.tokens);
            QVERIFY(!parsed.success);
            QCOMPARE(parsed.error.message, QStringLiteral("EXISTS requires subquery parentheses"));
            int expectedPos = -1;
            for (const logic::LogicToken &t : tokenized.tokens) {
                if (t.type == logic::LogicTokenType::Keyword && t.keywordType == logic::LogicKeywordType::Exists) { expectedPos = t.position; break; }
            }
            QCOMPARE(parsed.error.position, expectedPos);
        }

        // IN requires parentheses
        {
            const QString expr = QStringLiteral("a IN 1");
            const auto tokenized = logic::tokenizeLogicExpression(expr);
            QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));
            const auto parsed = logic::parseLogicTokens(expr, tokenized.tokens);
            QVERIFY(!parsed.success);
            QCOMPARE(parsed.error.message, QStringLiteral("IN requires parentheses"));
            int expectedPos = -1;
            for (const logic::LogicToken &t : tokenized.tokens) {
                if (t.type == logic::LogicTokenType::Keyword && t.keywordType == logic::LogicKeywordType::In) { expectedPos = t.position; break; }
            }
            QCOMPARE(parsed.error.position, expectedPos);
        }

        // IN inner literal expected
        {
            const QString expr = QStringLiteral("a IN (b)");
            const auto tokenized = logic::tokenizeLogicExpression(expr);
            QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));
            const auto parsed = logic::parseLogicTokens(expr, tokenized.tokens);
            QVERIFY(!parsed.success);
            QCOMPARE(parsed.error.message, QStringLiteral("expected literal value"));
            const int left = expr.indexOf('(');
            const int right = expr.lastIndexOf(')');
            const QString inner = expr.mid(left + 1, right - left - 1);
            const auto innerTok = logic::tokenizeLogicExpression(inner);
            QVERIFY2(innerTok.success, qPrintable(innerTok.error.message));
            int expectedPos = -1;
            for (const logic::LogicToken &t : innerTok.tokens) {
                if (t.type == logic::LogicTokenType::Identifier) { expectedPos = t.position; break; }
            }
            QCOMPARE(parsed.error.position, expectedPos);
        }

        // Quantified requires parentheses
        {
            const QString expr = QStringLiteral("a = ANY SELECT 1");
            const auto tokenized = logic::tokenizeLogicExpression(expr);
            QVERIFY2(tokenized.success, qPrintable(tokenized.error.message));
            const auto parsed = logic::parseLogicTokens(expr, tokenized.tokens);
            QVERIFY(!parsed.success);
            QCOMPARE(parsed.error.message, QStringLiteral("quantified comparison requires subquery parentheses"));
            int expectedPos = -1;
            for (const logic::LogicToken &t : tokenized.tokens) {
                if (t.type == logic::LogicTokenType::CompareOperator) { expectedPos = t.position; break; }
            }
            QCOMPARE(parsed.error.position, expectedPos);
        }
    }
};

int service_tests::runLogicTests()
{
    LogicTest test;
    return QTest::qExec(&test);
}

#include "test_logic.moc"
