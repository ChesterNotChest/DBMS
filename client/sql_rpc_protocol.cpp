#include "sql_rpc_protocol.h"

#include "../constants/cli_client_def.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace client::rpc {

namespace {

QJsonArray stringListToJson(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values) {
        array.append(value);
    }
    return array;
}

QStringList stringListFromJson(const QJsonArray &array)
{
    QStringList values;
    for (const QJsonValue &value : array) {
        values.append(value.toString());
    }
    return values;
}

QJsonObject variantMapToFlatJson(const QVariantMap &payload)
{
    QJsonObject object;
    for (auto it = payload.constBegin(); it != payload.constEnd(); ++it) {
        const QVariant &value = it.value();
        if (value.canConvert<QStringList>()) {
            object.insert(it.key(), stringListToJson(value.toStringList()));
        } else {
            object.insert(it.key(), QJsonValue::fromVariant(value));
        }
    }
    return object;
}

QVariantMap flatJsonToVariantMap(const QJsonObject &object)
{
    QVariantMap payload;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (it.value().isArray()) {
            payload.insert(it.key(), stringListFromJson(it.value().toArray()));
        } else {
            payload.insert(it.key(), it.value().toVariant());
        }
    }
    return payload;
}

} // namespace

QJsonObject tableDataToJson(const repo::TableData &table)
{
    QJsonObject object;
    object.insert(QStringLiteral("columns"), stringListToJson(table.columns));

    QJsonArray rows;
    for (const QStringList &row : table.rows) {
        rows.append(stringListToJson(row));
    }
    object.insert(QStringLiteral("rows"), rows);
    return object;
}

repo::TableData tableDataFromJson(const QJsonObject &object)
{
    repo::TableData table;
    table.columns = stringListFromJson(object.value(QStringLiteral("columns")).toArray());
    const QJsonArray rows = object.value(QStringLiteral("rows")).toArray();
    for (const QJsonValue &rowValue : rows) {
        table.rows.append(stringListFromJson(rowValue.toArray()));
    }
    return table;
}

QJsonObject selectRowsResultToJson(const service::SelectRowsResult &result)
{
    QJsonObject object;
    object.insert(QStringLiteral("success"), result.success);
    object.insert(QStringLiteral("errorMessage"), result.errorMessage);
    object.insert(QStringLiteral("affectedRowCount"), result.affectedRowCount);
    object.insert(QStringLiteral("resultTable"), tableDataToJson(result.resultTable));
    return object;
}

service::SelectRowsResult selectRowsResultFromJson(const QJsonObject &object)
{
    service::SelectRowsResult result;
    result.success = object.value(QStringLiteral("success")).toBool();
    result.errorMessage = object.value(QStringLiteral("errorMessage")).toString();
    result.affectedRowCount = object.value(QStringLiteral("affectedRowCount")).toInt();
    result.resultTable = tableDataFromJson(object.value(QStringLiteral("resultTable")).toObject());
    return result;
}

QJsonObject sqlExecResultToJson(const service::SqlExecResult &result)
{
    QJsonObject object;
    object.insert(QStringLiteral("success"), result.success);
    object.insert(QStringLiteral("errorMessage"), result.errorMessage);
    object.insert(QStringLiteral("text"), result.text);
    object.insert(QStringLiteral("affectedRows"), result.affectedRows);
    object.insert(QStringLiteral("selectResult"), selectRowsResultToJson(result.selectResult));
    object.insert(QStringLiteral("commandType"), result.commandType);
    object.insert(QStringLiteral("payload"), variantMapToFlatJson(result.payload));
    return object;
}

service::SqlExecResult sqlExecResultFromJson(const QJsonObject &object)
{
    service::SqlExecResult result;
    result.success = object.value(QStringLiteral("success")).toBool();
    result.errorMessage = object.value(QStringLiteral("errorMessage")).toString();
    result.text = object.value(QStringLiteral("text")).toString();
    result.affectedRows = object.value(QStringLiteral("affectedRows")).toInt(-1);
    result.selectResult = selectRowsResultFromJson(object.value(QStringLiteral("selectResult")).toObject());
    result.commandType = object.value(QStringLiteral("commandType")).toString();
    result.payload = flatJsonToVariantMap(object.value(QStringLiteral("payload")).toObject());
    return result;
}

QJsonObject requestToJson(const RpcRequest &request)
{
    QJsonObject object;
    object.insert(QStringLiteral("version"), cliclient::kRpcProtocolVersion);
    object.insert(QStringLiteral("id"), request.id);
    object.insert(QStringLiteral("op"), request.op);
    object.insert(QStringLiteral("params"), request.params);
    return object;
}

RpcRequest requestFromJson(const QJsonObject &object, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    RpcRequest request;
    const int version = object.value(QStringLiteral("version")).toInt();
    if (version != cliclient::kRpcProtocolVersion) {
        if (error != nullptr) {
            *error = QStringLiteral("unsupported RPC protocol version");
        }
        return request;
    }
    request.id = object.value(QStringLiteral("id")).toString();
    request.op = object.value(QStringLiteral("op")).toString();
    request.params = object.value(QStringLiteral("params")).toObject();
    if (request.op.trimmed().isEmpty() && error != nullptr) {
        *error = QStringLiteral("RPC request op is empty");
    }
    return request;
}

QJsonObject responseToJson(const RpcResponse &response)
{
    QJsonObject object;
    object.insert(QStringLiteral("version"), cliclient::kRpcProtocolVersion);
    object.insert(QStringLiteral("id"), response.id);
    object.insert(QStringLiteral("ok"), response.ok);
    object.insert(QStringLiteral("errorMessage"), response.errorMessage);
    object.insert(QStringLiteral("payload"), response.payload);
    return object;
}

RpcResponse responseFromJson(const QJsonObject &object, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    RpcResponse response;
    const int version = object.value(QStringLiteral("version")).toInt();
    if (version != cliclient::kRpcProtocolVersion) {
        if (error != nullptr) {
            *error = QStringLiteral("unsupported RPC protocol version");
        }
        return response;
    }
    response.id = object.value(QStringLiteral("id")).toString();
    response.ok = object.value(QStringLiteral("ok")).toBool();
    response.errorMessage = object.value(QStringLiteral("errorMessage")).toString();
    response.payload = object.value(QStringLiteral("payload")).toObject();
    return response;
}

QByteArray encodeFrame(const QJsonObject &object)
{
    const QByteArray body = QJsonDocument(object).toJson(QJsonDocument::Compact);
    QByteArray frame;
    frame.resize(4);
    const quint32 size = static_cast<quint32>(body.size());
    frame[0] = static_cast<char>((size >> 24) & 0xff);
    frame[1] = static_cast<char>((size >> 16) & 0xff);
    frame[2] = static_cast<char>((size >> 8) & 0xff);
    frame[3] = static_cast<char>(size & 0xff);
    frame.append(body);
    return frame;
}

bool tryDecodeFrame(QByteArray *buffer, QJsonObject *object, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    if (buffer == nullptr || object == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("frame decode buffer is null");
        }
        return false;
    }
    if (buffer->size() < 4) {
        return false;
    }

    const auto byteAt = [&](int index) -> quint32 {
        return static_cast<quint8>(buffer->at(index));
    };
    const quint32 size = (byteAt(0) << 24) | (byteAt(1) << 16) | (byteAt(2) << 8) | byteAt(3);
    if (size > static_cast<quint32>(cliclient::kRpcMaxFrameBytes)) {
        if (error != nullptr) {
            *error = QStringLiteral("RPC frame exceeds max size");
        }
        buffer->clear();
        return false;
    }
    if (buffer->size() < 4 + static_cast<int>(size)) {
        return false;
    }

    const QByteArray body = buffer->mid(4, static_cast<int>(size));
    buffer->remove(0, 4 + static_cast<int>(size));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("invalid RPC JSON frame");
        }
        return false;
    }
    *object = document.object();
    return true;
}

} // namespace client::rpc
