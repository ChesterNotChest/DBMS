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

    const int lockManagerTestResult = service_tests::runLockManagerTests();
    qDebug() << "Lock manager tests:" << (lockManagerTestResult == 0 ? "PASS" : "FAIL")
             << "(code=" << lockManagerTestResult << ")";

    const int threadedServiceTestResult = service_tests::runThreadedServiceTests();
    qDebug() << "Threaded service tests:" << (threadedServiceTestResult == 0 ? "PASS" : "FAIL")
             << "(code=" << threadedServiceTestResult << ")";

    const int catalogCacheTestResult = service_tests::runCatalogCacheTests();
    qDebug() << "Catalog cache tests:" << (catalogCacheTestResult == 0 ? "PASS" : "FAIL")
             << "(code=" << catalogCacheTestResult << ")";

    const int serviceCommonCacheTestResult = service_tests::runServiceCommonCacheTests();
    qDebug() << "Service common cache tests:" << (serviceCommonCacheTestResult == 0 ? "PASS" : "FAIL")
             << "(code=" << serviceCommonCacheTestResult << ")";

    const int tableRuntimePipelineTestResult = service_tests::runTableRuntimePipelineTests();
    qDebug() << "Table runtime pipeline tests:" << (tableRuntimePipelineTestResult == 0 ? "PASS" : "FAIL")
             << "(code=" << tableRuntimePipelineTestResult << ")";

    const int indexRuntimeRepairTestResult = service_tests::runIndexRuntimeRepairTests();
    qDebug() << "Index runtime repair tests:" << (indexRuntimeRepairTestResult == 0 ? "PASS" : "FAIL")
             << "(code=" << indexRuntimeRepairTestResult << ")";

    const int totalFailureCount = (databaseTestResult == 0 ? 0 : 1)
                                  + (parserDispatcherTestResult == 0 ? 0 : 1)
                                  + (logicTestResult == 0 ? 0 : 1)
                                  + (queryExecutorTestResult == 0 ? 0 : 1)
                                  + (tableTestResult == 0 ? 0 : 1)
                                  + (tupleTestResult == 0 ? 0 : 1)
                                  + (lockManagerTestResult == 0 ? 0 : 1)
                                  + (threadedServiceTestResult == 0 ? 0 : 1)
                                  + (catalogCacheTestResult == 0 ? 0 : 1)
                                  + (serviceCommonCacheTestResult == 0 ? 0 : 1)
                                  + (tableRuntimePipelineTestResult == 0 ? 0 : 1)
                                  + (indexRuntimeRepairTestResult == 0 ? 0 : 1);
    qDebug() << "=== Service Tests End ===";
    qDebug() << "Service test summary:" << (totalFailureCount == 0 ? "ALL PASS" : "SOME FAIL")
             << "(" << totalFailureCount << "failed groups)";

    MainWindow w;
    w.show();
    return a.exec();
}
