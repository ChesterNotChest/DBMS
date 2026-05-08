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

} // namespace service_tests
