#ifndef CLIENT_SQL_CLIENT_RUNTIME_H
#define CLIENT_SQL_CLIENT_RUNTIME_H

#include "../controller/sql_dispatcher.h"

#include <QString>

namespace client {

class SqlClientRuntime
{
public:
    virtual ~SqlClientRuntime() = default;

    virtual QString createSession(const QString &dataRoot = QString(),
                                  const QString &userName = QString()) = 0;
    virtual bool closeSession(const QString &clientId) = 0;
    virtual int sessionCount() const = 0;

    virtual service::SqlExecResult login(const QString &clientId,
                                         const QString &userName,
                                         const QString &password) = 0;
    virtual service::SqlExecResult executeSql(const QString &clientId, const QString &sql) = 0;
    virtual service::SqlExecResult executeSqlPreservingDatabase(const QString &clientId,
                                                               const QString &sql) = 0;
};

} // namespace client

#endif // CLIENT_SQL_CLIENT_RUNTIME_H
