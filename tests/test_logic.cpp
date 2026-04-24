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
};

int service_tests::runLogicTests()
{
    LogicTest test;
    return QTest::qExec(&test);
}

#include "test_logic.moc"