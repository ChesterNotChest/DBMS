#ifndef CLIENT_SQL_CLIENT_ENGINE_H
#define CLIENT_SQL_CLIENT_ENGINE_H

#include "client_session.h"
#include "client_session_pool.h"
#include "../controller/sql_dispatcher.h"

#include <QString>

namespace client {

class ScopedServiceContext
{
public:
    explicit ScopedServiceContext(ClientSession *session);
    ~ScopedServiceContext();

    ScopedServiceContext(const ScopedServiceContext &) = delete;
    ScopedServiceContext &operator=(const ScopedServiceContext &) = delete;

private:
    ClientSession *m_session = nullptr;
    QString m_previousDataRoot;
    QString m_previousDatabase;
};

class SqlClientEngine
{
public:
    explicit SqlClientEngine(ClientSessionPool *sessionPool);

    service::SqlExecResult executeSql(const QString &clientId, const QString &sql);

private:
    ClientSessionPool *m_sessionPool = nullptr;
};

} // namespace client

#endif // CLIENT_SQL_CLIENT_ENGINE_H
