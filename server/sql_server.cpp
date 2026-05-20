#include "sql_server.h"

#include <QHostAddress>
#include <QJsonObject>
#include <QTcpSocket>

namespace server {

DbmsServer::DbmsServer(QObject *parent)
    : QObject(parent)
    , m_engine(&m_sessionPool)
{
    connect(&m_tcpServer, &QTcpServer::newConnection, this, [this]() {
        acceptConnection();
    });
}

DbmsServer::~DbmsServer()
{
    stop();
}

bool DbmsServer::start(const QString &host, quint16 port, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    QHostAddress address(host);
    if (address.isNull() && host != QStringLiteral("0.0.0.0")) {
        address = QHostAddress::LocalHost;
    }
    if (!m_tcpServer.listen(address, port)) {
        if (error != nullptr) {
            *error = m_tcpServer.errorString();
        }
        return false;
    }
    m_stopping = false;
    return true;
}

void DbmsServer::stop()
{
    if (m_stopping) {
        return;
    }
    m_stopping = true;
    m_tcpServer.close();
    const QList<QTcpSocket *> sockets = m_buffers.keys();
    for (QTcpSocket *socket : sockets) {
        socket->disconnect(this);
        socket->disconnectFromHost();
        delete socket;
    }
    m_buffers.clear();
}

quint16 DbmsServer::serverPort() const
{
    return m_tcpServer.serverPort();
}

client::rpc::RpcResponse DbmsServer::handleRequest(const client::rpc::RpcRequest &request)
{
    client::rpc::RpcResponse response;
    response.id = request.id;
    response.ok = true;

    if (request.op == QStringLiteral("createSession")) {
        const QString clientId = m_engine.createSession(
            request.params.value(QStringLiteral("dataRoot")).toString(),
            request.params.value(QStringLiteral("userName")).toString());
        if (clientId.isEmpty()) {
            return failure(request.id, QStringLiteral("failed to create server session"));
        }
        response.payload.insert(QStringLiteral("clientId"), clientId);
        return response;
    }

    if (request.op == QStringLiteral("closeSession")) {
        response.payload.insert(QStringLiteral("closed"),
                                m_engine.closeSession(request.params.value(QStringLiteral("clientId")).toString()));
        return response;
    }

    if (request.op == QStringLiteral("sessionCount")) {
        response.payload.insert(QStringLiteral("count"), m_engine.sessionCount());
        return response;
    }

    service::SqlExecResult result;
    const QString clientId = request.params.value(QStringLiteral("clientId")).toString();
    if (request.op == QStringLiteral("login")) {
        result = m_engine.login(clientId,
                                request.params.value(QStringLiteral("userName")).toString(),
                                request.params.value(QStringLiteral("password")).toString());
    } else if (request.op == QStringLiteral("executeSql")) {
        result = m_engine.executeSql(clientId, request.params.value(QStringLiteral("sql")).toString());
    } else if (request.op == QStringLiteral("executeSqlPreservingDatabase")) {
        result = m_engine.executeSqlPreservingDatabase(clientId,
                                                       request.params.value(QStringLiteral("sql")).toString());
    } else {
        return failure(request.id, QStringLiteral("unknown RPC op '%1'").arg(request.op));
    }

    response.payload.insert(QStringLiteral("result"), client::rpc::sqlExecResultToJson(result));
    return response;
}

void DbmsServer::acceptConnection()
{
    while (QTcpSocket *socket = m_tcpServer.nextPendingConnection()) {
        m_buffers.insert(socket, {});
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            readConnection(socket);
        });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            if (m_stopping) {
                return;
            }
            m_buffers.remove(socket);
            socket->deleteLater();
        });
    }
}

void DbmsServer::readConnection(QTcpSocket *socket)
{
    if (socket == nullptr) {
        return;
    }

    QByteArray &buffer = m_buffers[socket];
    buffer.append(socket->readAll());
    while (true) {
        QJsonObject object;
        QString frameError;
        if (!client::rpc::tryDecodeFrame(&buffer, &object, &frameError)) {
            if (!frameError.isEmpty()) {
                writeResponse(socket, failure({}, frameError));
                socket->disconnectFromHost();
            }
            return;
        }

        QString requestError;
        const client::rpc::RpcRequest request = client::rpc::requestFromJson(object, &requestError);
        if (!requestError.isEmpty()) {
            writeResponse(socket, failure(request.id, requestError));
            continue;
        }
        writeResponse(socket, handleRequest(request));
    }
}

void DbmsServer::writeResponse(QTcpSocket *socket, const client::rpc::RpcResponse &response)
{
    if (socket == nullptr) {
        return;
    }
    socket->write(client::rpc::encodeFrame(client::rpc::responseToJson(response)));
}

client::rpc::RpcResponse DbmsServer::failure(const QString &id, const QString &message) const
{
    client::rpc::RpcResponse response;
    response.id = id;
    response.ok = false;
    response.errorMessage = message;
    return response;
}

} // namespace server
