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

    // 如果用户明确指定 --run-tests 才运行测试
    if (runTestsOnly) {
        auto reportTestGroup = [&](const QString &name, int result) {
            testOutput << name << ": " << (result == 0 ? "PASS" : "FAIL")
                       << " (code=" << result << ")" << Qt::endl;
        };

        qDebug() << "=== Service Tests Start ===";

        const int databaseTestResult = service_tests::runDatabaseServiceTests();
        reportTestGroup(QStringLiteral("Database service tests"), databaseTestResult);

        const int parserDispatcherTestResult = service_tests::runParserDispatcherTests();
        reportTestGroup(QStringLiteral("Parser/dispatcher tests"), parserDispatcherTestResult);

        const int logicTestResult = service_tests::runLogicTests();
        reportTestGroup(QStringLiteral("Logic tests"), logicTestResult);

        const int queryExecutorTestResult = service_tests::runQueryExecutorTests();
        reportTestGroup(QStringLiteral("Query executor tests"), queryExecutorTestResult);

        const int tableTestResult = service_tests::runTableServiceTests();
        reportTestGroup(QStringLiteral("Table service tests"), tableTestResult);

        const int tupleTestResult = service_tests::runTupleServiceTests();
        reportTestGroup(QStringLiteral("Tuple service tests"), tupleTestResult);

        const int lockManagerTestResult = service_tests::runLockManagerTests();
        reportTestGroup(QStringLiteral("Lock manager tests"), lockManagerTestResult);

        const int threadedServiceTestResult = service_tests::runThreadedServiceTests();
        reportTestGroup(QStringLiteral("Threaded service tests"), threadedServiceTestResult);

        const int catalogCacheTestResult = service_tests::runCatalogCacheTests();
        reportTestGroup(QStringLiteral("Catalog cache tests"), catalogCacheTestResult);

        const int serviceCommonCacheTestResult = service_tests::runServiceCommonCacheTests();
        reportTestGroup(QStringLiteral("Service common cache tests"), serviceCommonCacheTestResult);

        const int tableRuntimePipelineTestResult = service_tests::runTableRuntimePipelineTests();
        reportTestGroup(QStringLiteral("Table runtime pipeline tests"), tableRuntimePipelineTestResult);

        const int indexRuntimeRepairTestResult = service_tests::runIndexRuntimeRepairTests();
        reportTestGroup(QStringLiteral("Index runtime repair tests"), indexRuntimeRepairTestResult);

        const int clientSessionTestResult = service_tests::runClientSessionTests();
        reportTestGroup(QStringLiteral("Client session tests"), clientSessionTestResult);

        const int cliClientTestResult = service_tests::runCliClientTests();
        reportTestGroup(QStringLiteral("CLI client tests"), cliClientTestResult);

        const int authClientTestResult = service_tests::runAuthClientTests();
        reportTestGroup(QStringLiteral("Auth client tests"), authClientTestResult);

        const int guiClientRuntimeTestResult = service_tests::runGuiClientRuntimeTests();
        reportTestGroup(QStringLiteral("GUI client runtime tests"), guiClientRuntimeTestResult);

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
                                      + (indexRuntimeRepairTestResult == 0 ? 0 : 1)
                                      + (clientSessionTestResult == 0 ? 0 : 1)
                                      + (cliClientTestResult == 0 ? 0 : 1)
                                      + (authClientTestResult == 0 ? 0 : 1)
                                      + (guiClientRuntimeTestResult == 0 ? 0 : 1);

        qDebug() << "=== Service Tests End ===";
        qDebug() << "Service test summary:" << (totalFailureCount == 0 ? "ALL PASS" : "SOME FAIL")
                 << "(" << totalFailureCount << "failed groups)";

        return totalFailureCount == 0 ? 0 : 1;
    }

    // 默认直接启动界面，不运行测试
    MainWindow w;
    w.show();
    return a.exec();
}
