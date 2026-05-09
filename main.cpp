#include "mainwindow.h"
#include "tests/test_entry.h"

#include <QApplication>
#include <QDebug>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    const QStringList arguments = a.arguments();
    const bool runTestsOnly = arguments.contains(QStringLiteral("--run-tests"));
    QTextStream testOutput(stdout);

    auto reportTestGroup = [&](const QString &name, int result) {
        if (runTestsOnly) {
            testOutput << name << ": " << (result == 0 ? "PASS" : "FAIL")
                       << " (code=" << result << ")" << Qt::endl;
        }
    };

    qDebug() << "=== Service Tests Start ===";

    const int databaseTestResult = service_tests::runDatabaseServiceTests();
    reportTestGroup(QStringLiteral("Database service tests"), databaseTestResult);
    qDebug() << "Database service tests:" << (databaseTestResult == 0 ? "PASS" : "FAIL")
             << "(code=" << databaseTestResult << ")";

    const int parserDispatcherTestResult = service_tests::runParserDispatcherTests();
    reportTestGroup(QStringLiteral("Parser/dispatcher tests"), parserDispatcherTestResult);
    qDebug() << "Parser/dispatcher tests:" << (parserDispatcherTestResult == 0 ? "PASS" : "FAIL")
             << "(code=" << parserDispatcherTestResult << ")";

    const int logicTestResult = service_tests::runLogicTests();
    reportTestGroup(QStringLiteral("Logic tests"), logicTestResult);
    qDebug() << "Logic tests:" << (logicTestResult == 0 ? "PASS" : "FAIL")
             << "(code=" << logicTestResult << ")";

    const int queryExecutorTestResult = service_tests::runQueryExecutorTests();
    reportTestGroup(QStringLiteral("Query executor tests"), queryExecutorTestResult);
    qDebug() << "Query executor tests:" << (queryExecutorTestResult == 0 ? "PASS" : "FAIL")
             << "(code=" << queryExecutorTestResult << ")";

    const int tableTestResult = service_tests::runTableServiceTests();
    reportTestGroup(QStringLiteral("Table service tests"), tableTestResult);
    qDebug() << "Table service tests:" << (tableTestResult == 0 ? "PASS" : "FAIL")
             << "(code=" << tableTestResult << ")";

    const int tupleTestResult = service_tests::runTupleServiceTests();
    reportTestGroup(QStringLiteral("Tuple service tests"), tupleTestResult);
    qDebug() << "Tuple service tests:" << (tupleTestResult == 0 ? "PASS" : "FAIL")
             << "(code=" << tupleTestResult << ")";

    const int clientSessionTestResult = service_tests::runClientSessionTests();
    reportTestGroup(QStringLiteral("Client session tests"), clientSessionTestResult);
    qDebug() << "Client session tests:" << (clientSessionTestResult == 0 ? "PASS" : "FAIL")
             << "(code=" << clientSessionTestResult << ")";

    const int cliClientTestResult = service_tests::runCliClientTests();
    reportTestGroup(QStringLiteral("CLI client tests"), cliClientTestResult);
    qDebug() << "CLI client tests:" << (cliClientTestResult == 0 ? "PASS" : "FAIL")
             << "(code=" << cliClientTestResult << ")";

    const int authClientTestResult = service_tests::runAuthClientTests();
    reportTestGroup(QStringLiteral("Auth client tests"), authClientTestResult);
    qDebug() << "Auth client tests:" << (authClientTestResult == 0 ? "PASS" : "FAIL")
             << "(code=" << authClientTestResult << ")";

    const int guiClientRuntimeTestResult = service_tests::runGuiClientRuntimeTests();
    reportTestGroup(QStringLiteral("GUI client runtime tests"), guiClientRuntimeTestResult);
    qDebug() << "GUI client runtime tests:" << (guiClientRuntimeTestResult == 0 ? "PASS" : "FAIL")
             << "(code=" << guiClientRuntimeTestResult << ")";

    const int totalFailureCount = (databaseTestResult == 0 ? 0 : 1)
                                  + (parserDispatcherTestResult == 0 ? 0 : 1)
                                  + (logicTestResult == 0 ? 0 : 1)
                                  + (queryExecutorTestResult == 0 ? 0 : 1)
                                  + (tableTestResult == 0 ? 0 : 1)
                                  + (tupleTestResult == 0 ? 0 : 1)
                                  + (clientSessionTestResult == 0 ? 0 : 1)
                                  + (cliClientTestResult == 0 ? 0 : 1)
                                  + (authClientTestResult == 0 ? 0 : 1)
                                  + (guiClientRuntimeTestResult == 0 ? 0 : 1);
    qDebug() << "=== Service Tests End ===";
    qDebug() << "Service test summary:" << (totalFailureCount == 0 ? "ALL PASS" : "SOME FAIL")
             << "(" << totalFailureCount << "failed groups)";

    if (runTestsOnly) {
        return totalFailureCount == 0 ? 0 : 1;
    }

    MainWindow w;
    w.show();
    return a.exec();
}
