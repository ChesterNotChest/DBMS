#ifndef CLIENT_REMOTE_SQL_CLIENT_H
#define CLIENT_REMOTE_SQL_CLIENT_H

#include "sql_client_runtime.h"

#include <QHostAddress>
#include <QString>

namespace client {

class RemoteSqlClient final : public SqlClientRuntime
{
public:
    RemoteSqlClient(QString host, quint16 port, int timeoutMs);

    QString createSession(const QString &dataRoot = QString(),
                          const QString &userName = QString()) override;
    bool closeSession(const QString &clientId) override;
    int sessionCount() const override;

    service::SqlExecResult login(const QString &clientId,
                                 const QString &userName,
                                 const QString &password) override;
    service::SqlExecResult executeSql(const QString &clientId, const QString &sql) override;
    service::SqlExecResult executeSqlPreservingDatabase(const QString &clientId,
                                                       const QString &sql) override;

private:
    service::SqlExecResult execResultRequest(const QString &op, const QJsonObject &params);
    QJsonObject request(const QString &op,
                        const QJsonObject &params,
                        bool *ok,
                        QString *errorMessage) const;

    QString m_host;
    quint16 m_port = 0;
    int m_timeoutMs = 0;
};

} // namespace client

#endif // CLIENT_REMOTE_SQL_CLIENT_H
