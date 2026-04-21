#ifndef SQL_PARSER_SQL_PARSER_H
#define SQL_PARSER_SQL_PARSER_H

#include "sql_tokenizer.h"

#include <QPair>
#include <QString>
#include <QVariant>
#include <QVector>

namespace sqlparser {

struct ParseResult {
    bool success = false;
    QString errorMessage;
    QString commandType;
    // Payload must only contain Qt base types:
    // QString, QStringList, QVariantMap, QVariantList, int, bool.
    QVariantMap payload;
};

QPair<QString, QVariantMap> classifySql(const QString &sql, const QVector<SqlToken> &tokens);

QString extractDatabaseName(const QVector<SqlToken> &tokens);

QString extractTableName(const QVector<SqlToken> &tokens);

ParseResult parseDatabaseSql(const QString &sql, const QVector<SqlToken> &tokens);

ParseResult parseTableSql(const QString &sql, const QVector<SqlToken> &tokens);

ParseResult parseTupleSql(const QString &sql, const QVector<SqlToken> &tokens);

ParseResult parseSql(const QString &sql);

} // namespace sqlparser

#endif // SQL_PARSER_SQL_PARSER_H
