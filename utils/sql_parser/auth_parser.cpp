#include "sql_parser.h"

namespace sqlparser {

namespace {

bool isValueToken(TokenType type)
{
    return type == TokenType::IDENTIFIER || type == TokenType::STRING_LIT;
}

QString tokenValue(const QVector<SqlToken> &tokens, int index)
{
    if (index < 0 || index >= tokens.size()) {
        return {};
    }
    return tokens.at(index).lexeme;
}

bool tokenIs(const QVector<SqlToken> &tokens, int index, TokenType type)
{
    return index >= 0 && index < tokens.size() && tokens.at(index).type == type;
}

bool parseScope(const QVector<SqlToken> &tokens,
                int databaseIndex,
                QString *databaseName,
                QString *tableName)
{
    if (!isValueToken(tokens.value(databaseIndex).type)
        || !tokenIs(tokens, databaseIndex + 1, TokenType::DOT)
        || !(isValueToken(tokens.value(databaseIndex + 2).type)
             || tokenIs(tokens, databaseIndex + 2, TokenType::STAR))) {
        return false;
    }
    if (databaseName != nullptr) {
        *databaseName = tokenValue(tokens, databaseIndex);
    }
    if (tableName != nullptr) {
        *tableName = tokenIs(tokens, databaseIndex + 2, TokenType::STAR)
                         ? QStringLiteral("*")
                         : tokenValue(tokens, databaseIndex + 2);
    }
    return true;
}

ParseResult parseUserPasswordCommand(const QString &commandType,
                                     const QVector<SqlToken> &tokens,
                                     int userIndex,
                                     int identifiedIndex)
{
    if (!tokenIs(tokens, userIndex, TokenType::IDENTIFIER)
        && !tokenIs(tokens, userIndex, TokenType::STRING_LIT)) {
        return {false, commandType + QStringLiteral(": expected user name"), commandType, {}};
    }
    if (!tokenIs(tokens, identifiedIndex, TokenType::IDENTIFIED)
        || !tokenIs(tokens, identifiedIndex + 1, TokenType::BY)
        || !isValueToken(tokens.value(identifiedIndex + 2).type)) {
        return {false, commandType + QStringLiteral(": expected IDENTIFIED BY password"), commandType, {}};
    }

    QVariantMap payload;
    payload.insert(QStringLiteral("userName"), tokenValue(tokens, userIndex));
    payload.insert(QStringLiteral("password"), tokenValue(tokens, identifiedIndex + 2));
    return {true, {}, commandType, payload};
}

} // namespace

ParseResult parseAuthSql(const QString &sql, const QVector<SqlToken> &tokens)
{
    auto [cmdType, payload] = classifySql(sql, tokens);
    if (cmdType == QStringLiteral("LOGIN")) {
        return parseUserPasswordCommand(cmdType, tokens, 1, 2);
    }

    if (cmdType == QStringLiteral("CREATE_USER")) {
        return parseUserPasswordCommand(cmdType, tokens, 2, 3);
    }

    if (cmdType == QStringLiteral("DROP_USER")) {
        if (!isValueToken(tokens.value(2).type)) {
            return {false, QStringLiteral("DROP_USER: expected user name"), cmdType, {}};
        }
        payload.insert(QStringLiteral("userName"), tokenValue(tokens, 2));
        return {true, {}, cmdType, payload};
    }

    if (cmdType == QStringLiteral("ALTER_USER")) {
        return parseUserPasswordCommand(cmdType, tokens, 2, 3);
    }

    if (cmdType == QStringLiteral("GRANT_ALL")) {
        if (!tokenIs(tokens, 1, TokenType::ALL)
            || !tokenIs(tokens, 2, TokenType::ON)
            || !tokenIs(tokens, 6, TokenType::TO)
            || !isValueToken(tokens.value(7).type)) {
            return {false, QStringLiteral("GRANT_ALL: expected GRANT ALL ON database.* TO user or GRANT ALL ON database.table TO user"), cmdType, {}};
        }
        QString databaseName;
        QString tableName;
        if (!parseScope(tokens, 3, &databaseName, &tableName)) {
            return {false, QStringLiteral("GRANT_ALL: expected GRANT ALL ON database.* TO user or GRANT ALL ON database.table TO user"), cmdType, {}};
        }
        payload.insert(QStringLiteral("databaseName"), databaseName);
        payload.insert(QStringLiteral("tableName"), tableName);
        payload.insert(QStringLiteral("userName"), tokenValue(tokens, 7));
        return {true, {}, cmdType, payload};
    }

    if (cmdType == QStringLiteral("REVOKE_ALL")) {
        if (!tokenIs(tokens, 1, TokenType::ALL)
            || !tokenIs(tokens, 2, TokenType::ON)
            || !tokenIs(tokens, 6, TokenType::FROM)
            || !isValueToken(tokens.value(7).type)) {
            return {false, QStringLiteral("REVOKE_ALL: expected REVOKE ALL ON database.* FROM user or REVOKE ALL ON database.table FROM user"), cmdType, {}};
        }
        QString databaseName;
        QString tableName;
        if (!parseScope(tokens, 3, &databaseName, &tableName)) {
            return {false, QStringLiteral("REVOKE_ALL: expected REVOKE ALL ON database.* FROM user or REVOKE ALL ON database.table FROM user"), cmdType, {}};
        }
        payload.insert(QStringLiteral("databaseName"), databaseName);
        payload.insert(QStringLiteral("tableName"), tableName);
        payload.insert(QStringLiteral("userName"), tokenValue(tokens, 7));
        return {true, {}, cmdType, payload};
    }

    return {false, QStringLiteral("Not an auth SQL statement"), QStringLiteral("UNKNOWN"), {}};
}

} // namespace sqlparser
