#include "remote_sql_client.h"

#include "sql_rpc_protocol.h"

#include <QJsonObject>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTcpSocket>
#include <QUuid>

namespace client {

namespace {

service::SqlExecResult transportFailure(const QString &message)
{
    return {false, message, message};
}

} // namespace

RemoteSqlClient::RemoteSqlClient(QString host, quint16 port, int timeoutMs)
    : m_host(std::move(host))
    , m_port(port)
    , m_timeoutMs(timeoutMs)
{
}

QString RemoteSqlClient::createSession(const QString &dataRoot, const QString &userName)
{
    QJsonObject params;
    params.insert(QStringLiteral("dataRoot"), dataRoot);
    params.insert(QStringLiteral("userName"), userName);

    bool ok = false;
    QString error;
    const QJsonObject payload = request(QStringLiteral("createSession"), params, &ok, &error);
    return ok ? payload.value(QStringLiteral("clientId")).toString() : QString();
}

bool RemoteSqlClient::closeSession(const QString &clientId)
{
    QJsonObject params;
    params.insert(QStringLiteral("clientId"), clientId);

    bool ok = false;
    QString error;
    request(QStringLiteral("closeSession"), params, &ok, &error);
    return ok;
}

int RemoteSqlClient::sessionCount() const
{
    bool ok = false;
    QString error;
    const QJsonObject payload = request(QStringLiteral("sessionCount"), {}, &ok, &error);
    return ok ? payload.value(QStringLiteral("count")).toInt() : 0;
}

service::SqlExecResult RemoteSqlClient::login(const QString &clientId,
                                              const QString &userName,
                                              const QString &password)
{
    QJsonObject params;
    params.insert(QStringLiteral("clientId"), clientId);
    params.insert(QStringLiteral("userName"), userName);
    params.insert(QStringLiteral("password"), password);
    return execResultRequest(QStringLiteral("login"), params);
}

service::SqlExecResult RemoteSqlClient::executeSql(const QString &clientId, const QString &sql)
{
    QJsonObject params;
    params.insert(QStringLiteral("clientId"), clientId);
    params.insert(QStringLiteral("sql"), sql);
    return execResultRequest(QStringLiteral("executeSql"), params);
}

service::SqlExecResult RemoteSqlClient::executeSqlPreservingDatabase(const QString &clientId,
                                                                     const QString &sql)
{
    QJsonObject params;
    params.insert(QStringLiteral("clientId"), clientId);
    params.insert(QStringLiteral("sql"), sql);
    return execResultRequest(QStringLiteral("executeSqlPreservingDatabase"), params);
}

service::SqlExecResult RemoteSqlClient::execResultRequest(const QString &op, const QJsonObject &params)
{
    bool ok = false;
    QString error;
    const QJsonObject payload = request(op, params, &ok, &error);
    if (!ok) {
        return transportFailure(error);
    }
    return rpc::sqlExecResultFromJson(payload.value(QStringLiteral("result")).toObject());
}

QJsonObject RemoteSqlClient::request(const QString &op,
                                     const QJsonObject &params,
                                     bool *ok,
                                     QString *errorMessage) const
{
    if (ok != nullptr) {
        *ok = false;
    }
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    QTcpSocket socket;
    socket.connectToHost(m_host, m_port);
    if (!socket.waitForConnected(m_timeoutMs)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to connect DBMS server at %1:%2: %3")
                                .arg(m_host)
                                .arg(m_port)
                                .arg(socket.errorString());
        }
        return {};
    }

    rpc::RpcRequest rpcRequest;
    rpcRequest.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    rpcRequest.op = op;
    rpcRequest.params = params;
    socket.write(rpc::encodeFrame(rpc::requestToJson(rpcRequest)));
    if (!socket.waitForBytesWritten(m_timeoutMs)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to send DBMS RPC request: %1").arg(socket.errorString());
        }
        return {};
    }

    QByteArray buffer;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < m_timeoutMs) {
        if (QCoreApplication::instance() != nullptr) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        if (!socket.waitForReadyRead(20) && socket.bytesAvailable() <= 0) {
            continue;
        }
        buffer.append(socket.readAll());
        QJsonObject object;
        QString frameError;
        if (!rpc::tryDecodeFrame(&buffer, &object, &frameError)) {
            if (!frameError.isEmpty()) {
                if (errorMessage != nullptr) {
                    *errorMessage = frameError;
                }
                return {};
            }
            continue;
        }

        QString responseError;
        const rpc::RpcResponse response = rpc::responseFromJson(object, &responseError);
        if (!responseError.isEmpty()) {
            if (errorMessage != nullptr) {
                *errorMessage = responseError;
            }
            return {};
        }
        if (!response.ok) {
            if (errorMessage != nullptr) {
                *errorMessage = response.errorMessage;
            }
            return response.payload;
        }
        if (ok != nullptr) {
            *ok = true;
        }
        return response.payload;
    }

    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("DBMS RPC request timed out: %1").arg(socket.errorString());
    }
    return {};
}

} // namespace client
