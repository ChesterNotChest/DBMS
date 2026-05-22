#ifndef SERVER_SQL_SERVER_H
#define SERVER_SQL_SERVER_H

#include "../client/client_session_pool.h"
#include "../client/sql_client_engine.h"
#include "../client/sql_rpc_protocol.h"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTcpServer>

namespace server {

class DbmsServer : public QObject
{
public:
    explicit DbmsServer(QObject *parent = nullptr);
    ~DbmsServer() override;

    bool start(const QString &host, quint16 port, QString *error = nullptr);
    void stop();
    quint16 serverPort() const;

    client::rpc::RpcResponse handleRequest(const client::rpc::RpcRequest &request);

private:
    void acceptConnection();
    void readConnection(QTcpSocket *socket);
    void writeResponse(QTcpSocket *socket, const client::rpc::RpcResponse &response);
    client::rpc::RpcResponse failure(const QString &id, const QString &message) const;

    QTcpServer m_tcpServer;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    bool m_stopping = false;
    client::ClientSessionPool m_sessionPool;
    client::SqlClientEngine m_engine;
};

} // namespace server

#endif // SERVER_SQL_SERVER_H
