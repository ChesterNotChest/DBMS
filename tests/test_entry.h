#pragma once

namespace service_tests {

int runDatabaseServiceTests();
int runParserDispatcherTests();
int runLogicTests();
int runQueryExecutorTests();
int runTableServiceTests();
int runTupleServiceTests();
int runClientSessionTests();
int runCliClientTests();
int runAuthClientTests();
int runGuiClientRuntimeTests();

} // namespace service_tests
