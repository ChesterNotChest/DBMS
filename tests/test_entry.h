#pragma once

namespace service_tests {

int runDatabaseServiceTests();
int runParserDispatcherTests();
int runLogicTests();
int runQueryExecutorTests();
int runTableServiceTests();
int runTupleServiceTests();
int runLockManagerTests();
int runThreadedServiceTests();
int runCatalogCacheTests();
int runServiceCommonCacheTests();
int runTableRuntimePipelineTests();
int runIndexRuntimeRepairTests();
int runClientSessionTests();
int runCliClientTests();
int runAuthClientTests();
int runGuiClientRuntimeTests();
int runIntegrationTests();
int runStressTests();

} // namespace service_tests
