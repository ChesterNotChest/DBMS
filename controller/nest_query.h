#ifndef CONTROLLER_NEST_QUERY_H
#define CONTROLLER_NEST_QUERY_H

#include "../service/service.h"
#include "../utils/sql_parser/sql_parser.h"
#include "../utils/logic/subquery_logic.h"

namespace service {

struct QueryExecuteContext
{
    QString currentDatabase;
    QString dataRoot;
    QStringList skipSharedReadLockTables;
};

struct QueryExecuteResult
{
    bool success = false;
    QString errorMessage;
    QString text;
    int affectedRows = -1;
    SelectRowsResult selectResult;
};

class QueryExecutor
    : public logic::ISubqueryExecutor
{
public:
    // Accepts SQL text, but only SELECT is allowed.
    QueryExecuteResult executeSql(const QString &sql,
                                             const QueryExecuteContext &context = {});

    // Accepts parsed SQL, but only SELECT is allowed.
    QueryExecuteResult executeParsed(const sqlparser::ParseResult &parsed,
                                                 const QueryExecuteContext &context = {});

    QueryExecuteResult executeSelectSql(const QString &sql,
                                                     const QueryExecuteContext &context = {});

    QueryExecuteResult executeCorrelatedSelect(const QString &sql,
                                               const logic::CorrelationBindings &bindings,
                                                              const QueryExecuteContext &context = {}) override;

private:
    QueryExecuteResult execSelect(const sqlparser::ParseResult &parsed,
                                             const logic::CorrelationBindings *bindings = nullptr);
};

} // namespace service

#endif // CONTROLLER_NEST_QUERY_H
