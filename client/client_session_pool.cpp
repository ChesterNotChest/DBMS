#include "client_session_pool.h"

#include "../constants/cli_client_def.h"
#include "../repo/repo.h"

#include <QDir>
#include <QUuid>

namespace client {

QString ClientSessionPool::createSession(const QString &dataRoot, const QString &userName)
{
    ClientSession session;
    session.clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    session.dataRoot = dataRoot.trimmed().isEmpty()
                           ? repo::FlatFileTableStore::defaultDataRoot()
                           : QDir::cleanPath(dataRoot);
    session.userName = userName.trimmed().isEmpty()
                           ? QString::fromLatin1(cliclient::kDefaultAnonymousUser)
                           : userName.trimmed();

    const QString clientId = session.clientId;
    m_sessions.insert(clientId, session);
    return clientId;
}

ClientSession *ClientSessionPool::session(const QString &clientId, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    auto it = m_sessions.find(clientId);
    if (it == m_sessions.end()) {
        if (error != nullptr) {
            *error = QStringLiteral("client session '%1' does not exist").arg(clientId);
        }
        return nullptr;
    }
    return &it.value();
}

const ClientSession *ClientSessionPool::session(const QString &clientId, QString *error) const
{
    if (error != nullptr) {
        error->clear();
    }

    auto it = m_sessions.constFind(clientId);
    if (it == m_sessions.constEnd()) {
        if (error != nullptr) {
            *error = QStringLiteral("client session '%1' does not exist").arg(clientId);
        }
        return nullptr;
    }
    return &it.value();
}

bool ClientSessionPool::closeSession(const QString &clientId)
{
    return m_sessions.remove(clientId) > 0;
}

int ClientSessionPool::sessionCount() const
{
    return m_sessions.size();
}

} // namespace client
