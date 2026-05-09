#include "mainwindow.h"
#include "tests/test_entry.h"

#include <QApplication>
#include <QDebug>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    qDebug() << "=== Service Tests Start ===";

    const int databaseTestResult = service_tests::runDatabaseServiceTests();
    qDebug() << "Database service tests:" << (databaseTestResult == 0 ? "PASS" : "FAIL")
             << "(code=" << databaseTestResult << ")";

    const int parserDispatcherTestResult = service_tests::runParserDispatcherTests();
    qDebug() << "Parser/dispatcher tests:" << (parserDispatcherTestResult == 0 ? "PASS" : "FAIL")
             << "(code=" << parserDispatcherTestResult << ")";

    const int logicTestResult = service_tests::runLogicTests();
    qDebug() << "Logic tests:" << (logicTestResult == 0 ? "PASS" : "FAIL")
             << "(code=" << logicTestResult << ")";

    const int queryExecutorTestResult = service_tests::runQueryExecutorTests();
    qDebug() << "Query executor tests:" << (queryExecutorTestResult == 0 ? "PASS" : "FAIL")
             << "(code=" << queryExecutorTestResult << ")";

    const int tableTestResult = service_tests::runTableServiceTests();
    qDebug() << "Table service tests:" << (tableTestResult == 0 ? "PASS" : "FAIL")
             << "(code=" << tableTestResult << ")";

    const int tupleTestResult = service_tests::runTupleServiceTests();
    qDebug() << "Tuple service tests:" << (tupleTestResult == 0 ? "PASS" : "FAIL")
             << "(code=" << tupleTestResult << ")";

    const int clientSessionTestResult = service_tests::runClientSessionTests();
    qDebug() << "Client session tests:" << (clientSessionTestResult == 0 ? "PASS" : "FAIL")
             << "(code=" << clientSessionTestResult << ")";

    const int cliClientTestResult = service_tests::runCliClientTests();
    qDebug() << "CLI client tests:" << (cliClientTestResult == 0 ? "PASS" : "FAIL")
             << "(code=" << cliClientTestResult << ")";

    const int authClientTestResult = service_tests::runAuthClientTests();
    qDebug() << "Auth client tests:" << (authClientTestResult == 0 ? "PASS" : "FAIL")
             << "(code=" << authClientTestResult << ")";

    const int totalFailureCount = (databaseTestResult == 0 ? 0 : 1)
                                  + (parserDispatcherTestResult == 0 ? 0 : 1)
                                  + (logicTestResult == 0 ? 0 : 1)
                                  + (queryExecutorTestResult == 0 ? 0 : 1)
                                  + (tableTestResult == 0 ? 0 : 1)
                                  + (tupleTestResult == 0 ? 0 : 1)
                                  + (clientSessionTestResult == 0 ? 0 : 1)
                                  + (cliClientTestResult == 0 ? 0 : 1)
                                  + (authClientTestResult == 0 ? 0 : 1);
    qDebug() << "=== Service Tests End ===";
    qDebug() << "Service test summary:" << (totalFailureCount == 0 ? "ALL PASS" : "SOME FAIL")
             << "(" << totalFailureCount << "failed groups)";

    MainWindow w;
    w.show();
    return a.exec();
}
