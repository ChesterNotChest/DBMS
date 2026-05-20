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
    // 新增：允许通过 `--no-tests` 跳过启动时自动运行的单元测试（便于直接打开 GUI 进行调试）
    // 优先级：如果同时指定了 `--run-tests`，仍会运行测试。
    bool skipTests = arguments.contains(QStringLiteral("--no-tests")) && !runTestsOnly;
    if (arguments.contains(QStringLiteral("--skip-stress-tests"))
        || arguments.contains(QStringLiteral("--no-stress-tests"))) {
        qputenv("DBMS_SKIP_STRESS_TESTS", "1");
    }

    if (runTestsOnly) {
        QTextStream testOutput(stdout);

        auto reportTestGroup = [&](const QString &name, int result) {
            testOutput << name << ": " << (result == 0 ? "PASS" : "FAIL") << "\n";
        };

        int totalFailureCount = 0;
        if (!skipTests) {
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

                        const int lockManagerTestResult = service_tests::runLockManagerTests();
                        reportTestGroup(QStringLiteral("Lock manager tests"), lockManagerTestResult);
                        qDebug() << "Lock manager tests:" << (lockManagerTestResult == 0 ? "PASS" : "FAIL")
                                 << "(code=" << lockManagerTestResult << ")";

                        const int threadedServiceTestResult = service_tests::runThreadedServiceTests();
                        reportTestGroup(QStringLiteral("Threaded service tests"), threadedServiceTestResult);
                        qDebug() << "Threaded service tests:" << (threadedServiceTestResult == 0 ? "PASS" : "FAIL")
                                 << "(code=" << threadedServiceTestResult << ")";

                        const int catalogCacheTestResult = service_tests::runCatalogCacheTests();
                        reportTestGroup(QStringLiteral("Catalog cache tests"), catalogCacheTestResult);
                        qDebug() << "Catalog cache tests:" << (catalogCacheTestResult == 0 ? "PASS" : "FAIL")
                                 << "(code=" << catalogCacheTestResult << ")";

                        const int serviceCommonCacheTestResult = service_tests::runServiceCommonCacheTests();
                        reportTestGroup(QStringLiteral("Service common cache tests"), serviceCommonCacheTestResult);
                        qDebug() << "Service common cache tests:" << (serviceCommonCacheTestResult == 0 ? "PASS" : "FAIL")
                                 << "(code=" << serviceCommonCacheTestResult << ")";

                        const int tableRuntimePipelineTestResult = service_tests::runTableRuntimePipelineTests();
                        reportTestGroup(QStringLiteral("Table runtime pipeline tests"), tableRuntimePipelineTestResult);
                        qDebug() << "Table runtime pipeline tests:" << (tableRuntimePipelineTestResult == 0 ? "PASS" : "FAIL")
                                 << "(code=" << tableRuntimePipelineTestResult << ")";

                        const int indexRuntimeRepairTestResult = service_tests::runIndexRuntimeRepairTests();
                        reportTestGroup(QStringLiteral("Index runtime repair tests"), indexRuntimeRepairTestResult);
                        qDebug() << "Index runtime repair tests:" << (indexRuntimeRepairTestResult == 0 ? "PASS" : "FAIL")
                                 << "(code=" << indexRuntimeRepairTestResult << ")";
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

                        const int integrationTestResult = service_tests::runIntegrationTests();
                        reportTestGroup(QStringLiteral("Integration tests"), integrationTestResult);
                        qDebug() << "Integration tests:" << (integrationTestResult == 0 ? "PASS" : "FAIL")
                                 << "(code=" << integrationTestResult << ")";

                        const int stressTestResult = service_tests::runStressTests();
                        reportTestGroup(QStringLiteral("Stress tests"), stressTestResult);
                        qDebug() << "Stress tests:" << (stressTestResult == 0 ? "PASS" : "FAIL")
                                 << "(code=" << stressTestResult << ")";

                        totalFailureCount = (databaseTestResult == 0 ? 0 : 1)
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
                                                  + (guiClientRuntimeTestResult == 0 ? 0 : 1)
                                                  + (integrationTestResult == 0 ? 0 : 1)
                                                  + (stressTestResult == 0 ? 0 : 1);
                        qDebug() << "=== Service Tests End ===";
                        qDebug() << "Service test summary:" << (totalFailureCount == 0 ? "ALL PASS" : "SOME FAIL")
                                 << "(" << totalFailureCount << "failed groups)";
                    } else {
                        qDebug() << "Skipping service tests due to --no-tests flag.";
                    }

                    return totalFailureCount == 0 ? 0 : 1;
                }

                MainWindow w;
                w.show();
                return a.exec();
            }