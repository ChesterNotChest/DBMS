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

    const int tableTestResult = service_tests::runTableServiceTests();
    qDebug() << "Table service tests:" << (tableTestResult == 0 ? "PASS" : "FAIL")
             << "(code=" << tableTestResult << ")";

    const int tupleTestResult = service_tests::runTupleServiceTests();
    qDebug() << "Tuple service tests:" << (tupleTestResult == 0 ? "PASS" : "FAIL")
             << "(code=" << tupleTestResult << ")";

    const int totalFailureCount = (databaseTestResult == 0 ? 0 : 1)
                                  + (parserDispatcherTestResult == 0 ? 0 : 1)
                                  + (tableTestResult == 0 ? 0 : 1)
                                  + (tupleTestResult == 0 ? 0 : 1);
    qDebug() << "=== Service Tests End ===";
    qDebug() << "Service test summary:" << (totalFailureCount == 0 ? "ALL PASS" : "SOME FAIL")
             << "(" << totalFailureCount << "failed groups)";

    MainWindow w;
    w.show();
    return a.exec();
}
