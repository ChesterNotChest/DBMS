#ifndef CLIENT_SQL_RPC_PROTOCOL_H
#define CLIENT_SQL_RPC_PROTOCOL_H

#include "../controller/sql_dispatcher.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace client::rpc {

struct RpcRequest
{
    QString id;
    QString op;
    QJsonObject params;
};

struct RpcResponse
{
    QString id;
    bool ok = false;
    QString errorMessage;
    QJsonObject payload;
};

QJsonObject tableDataToJson(const repo::TableData &table);
repo::TableData tableDataFromJson(const QJsonObject &object);

QJsonObject selectRowsResultToJson(const service::SelectRowsResult &result);
service::SelectRowsResult selectRowsResultFromJson(const QJsonObject &object);

QJsonObject sqlExecResultToJson(const service::SqlExecResult &result);
service::SqlExecResult sqlExecResultFromJson(const QJsonObject &object);

QJsonObject requestToJson(const RpcRequest &request);
RpcRequest requestFromJson(const QJsonObject &object, QString *error = nullptr);

QJsonObject responseToJson(const RpcResponse &response);
RpcResponse responseFromJson(const QJsonObject &object, QString *error = nullptr);

QByteArray encodeFrame(const QJsonObject &object);
bool tryDecodeFrame(QByteArray *buffer, QJsonObject *object, QString *error = nullptr);

} // namespace client::rpc

#endif // CLIENT_SQL_RPC_PROTOCOL_H
