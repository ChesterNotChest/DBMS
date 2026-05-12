#ifndef CLIENT_CLIENT_SESSION_H
#define CLIENT_CLIENT_SESSION_H

#include <QString>

namespace client {

struct ClientSession
{
    QString clientId;
    QString dataRoot;
    QString currentDatabase;
    QString userName;
    bool authenticated = false;
};

} // namespace client

#endif // CLIENT_CLIENT_SESSION_H
