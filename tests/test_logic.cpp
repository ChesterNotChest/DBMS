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
};

int service_tests::runLogicTests()
{
    LogicTest test;
    return QTest::qExec(&test);
}

#include "test_logic.moc"