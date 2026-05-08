#include "sql_client_engine.h"

#include "../service/service.h"

namespace client {

ScopedServiceContext::ScopedServiceContext(ClientSession *session)
    : m_session(session)
    , m_previousDataRoot(service::getDataRoot())
    , m_previousDatabase(service::currentDatabase)
{
    if (m_session == nullptr) {
        return;
    }

    service::setDataRoot(m_session->dataRoot);
    service::currentDatabase = m_session->currentDatabase;
}

ScopedServiceContext::~ScopedServiceContext()
{
    if (m_session != nullptr) {
        m_session->dataRoot = service::getDataRoot();
        m_session->currentDatabase = service::currentDatabase;
    }

    service::setDataRoot(m_previousDataRoot);
    service::currentDatabase = m_previousDatabase;
}

SqlClientEngine::SqlClientEngine(ClientSessionPool *sessionPool)
    : m_sessionPool(sessionPool)
{
}

service::SqlExecResult SqlClientEngine::executeSql(const QString &clientId, const QString &sql)
{
    if (m_sessionPool == nullptr) {
        return {false, QStringLiteral("client session pool is not available")};
    }

    QString error;
    ClientSession *clientSession = m_sessionPool->session(clientId, &error);
    if (clientSession == nullptr) {
        return {false, error};
    }

    ScopedServiceContext context(clientSession);
    service::SqlDispatcher dispatcher;
    const QStringList statements = service::SqlDispatcher::splitStatements(sql);
    if (statements.isEmpty()) {
        return {false, QStringLiteral("SQL is empty")};
    }

    service::SqlExecResult lastResult;
    for (const QString &statement : statements) {
        lastResult = dispatcher.execute(statement);
        if (!lastResult.success) {
            return lastResult;
        }
    }
    return lastResult;
}

} // namespace client
