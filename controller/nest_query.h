#ifndef CONTROLLER_NEST_QUERY_H
#define CONTROLLER_NEST_QUERY_H

#include "../service/service.h"
#include "../utils/sql_parser/sql_parser.h"

namespace service {

struct QueryExecuteContext
{
    QString currentDatabase;
    QString dataRoot;
};

struct QueryExecuteResult
{
    bool success = false;
    QString errorMessage;
    QString text;
    QString commandType;
    int affectedRows = -1;
    SelectRowsResult selectResult;
    QVariantMap payload;
};

class QueryExecutor
{
public:
    // Accepts SQL text, but only SELECT is allowed.
    QueryExecuteResult executeSql(const QString &sql,
                                  const QueryExecuteContext &context = {}) const;

    // Accepts parsed SQL, but only SELECT is allowed.
    QueryExecuteResult executeParsed(const sqlparser::ParseResult &parsed,
                                     const QueryExecuteContext &context = {}) const;

    QueryExecuteResult executeSelectSql(const QString &sql,
                                        const QueryExecuteContext &context = {}) const;

private:
    QueryExecuteResult execSelect(const sqlparser::ParseResult &parsed) const;
};

} // namespace service

#endif // CONTROLLER_NEST_QUERY_H
