#ifndef CLIENT_SQL_CLIENT_ENGINE_H
#define CLIENT_SQL_CLIENT_ENGINE_H

#include "sql_client_runtime.h"
#include "client_session.h"
#include "client_session_pool.h"
#include "../controller/sql_dispatcher.h"

#include <QString>
#include <QStringList>

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
    QString m_previousUser;
};

class SqlClientEngine : public SqlClientRuntime
{
public:
    explicit SqlClientEngine(ClientSessionPool *sessionPool);

    QString createSession(const QString &dataRoot = QString(),
                          const QString &userName = QString()) override;
    bool closeSession(const QString &clientId) override;
    int sessionCount() const override;

    service::SqlExecResult login(const QString &clientId,
                                 const QString &userName,
                                 const QString &password) override;
    service::SqlExecResult executeSql(const QString &clientId, const QString &sql) override;
    service::SqlExecResult executeSqlPreservingDatabase(const QString &clientId, const QString &sql) override;

private:
    service::SqlExecResult executeParsedStatement(ClientSession *clientSession,
                                                  service::SqlDispatcher *dispatcher,
                                                  const sqlparser::ParseResult &parsed);
    service::SqlExecResult executeLogin(ClientSession *clientSession,
                                        const sqlparser::ParseResult &parsed);
    service::SqlExecResult authorizeStatement(const ClientSession &clientSession,
                                              const sqlparser::ParseResult &parsed) const;
    QString targetDatabaseForStatement(const ClientSession &clientSession,
                                       const sqlparser::ParseResult &parsed) const;
    QStringList targetTablesForStatement(const sqlparser::ParseResult &parsed) const;

    ClientSessionPool *m_sessionPool = nullptr;
};

} // namespace client

#endif // CLIENT_SQL_CLIENT_ENGINE_H
