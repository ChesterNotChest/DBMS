#ifndef CLIENT_SQL_RESULT_FORMATTER_H
#define CLIENT_SQL_RESULT_FORMATTER_H

#include "../controller/sql_dispatcher.h"

#include <QString>
#include <QStringList>

namespace client {

QString formatResultTable(const QStringList &headers, const QList<QStringList> &rows);
QString formatSelectResult(const service::SelectRowsResult &result);
QString formatDescResult(const QString &text);
QString formatShowCreateTableResult(const service::SqlExecResult &result);
QString formatSqlExecResultForText(const service::SqlExecResult &result);
QString rowCountText(int rowCount);

} // namespace client

#endif // CLIENT_SQL_RESULT_FORMATTER_H
