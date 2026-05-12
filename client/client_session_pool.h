#ifndef CLIENT_CLIENT_SESSION_POOL_H
#define CLIENT_CLIENT_SESSION_POOL_H

#include "client_session.h"

#include <QMap>
#include <QString>

namespace client {

class ClientSessionPool
{
public:
    QString createSession(const QString &dataRoot = QString(),
                          const QString &userName = QString());

    ClientSession *session(const QString &clientId, QString *error = nullptr);
    const ClientSession *session(const QString &clientId, QString *error = nullptr) const;

    bool closeSession(const QString &clientId);
    int sessionCount() const;

private:
    QMap<QString, ClientSession> m_sessions;
};

} // namespace client

#endif // CLIENT_CLIENT_SESSION_POOL_H
