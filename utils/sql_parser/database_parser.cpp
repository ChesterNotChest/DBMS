/**
 * database_parser.cpp — 数据库级 SQL 解析器
 *
 * 仅做解析，不碰文件系统、不调用 service。
 * 支持：CREATE DATABASE, DROP DATABASE, USE database, SHOW DATABASES
 */
#include "sql_parser.h"
#include <QDebug>

namespace sqlparser {

// ============================================================
//  辅助
// ============================================================

static QVector<SqlToken> tokenize(const QString& sql) {
    return SqlTokenizer::tokenize(sql);
}

// ============================================================
//  classifySql
// ============================================================
QPair<QString, QVariantMap> classifySql(const QString& sql, const QVector<SqlToken>& tokens) {
    if (tokens.isEmpty() || tokens[0].type == TokenType::END_OF_INPUT)
        return {"UNKNOWN", {}};

    QString kw = tokens[0].lexeme.toUpper();

    if (kw == "CREATE" && tokens.size() >= 2) {
        QString next = tokens[1].lexeme.toUpper();
        if (next == "DATABASE") return {"CREATE_DATABASE", {}};
        if (next == "TABLE")    return {"CREATE_TABLE", {}};
        if (next == "USER")     return {"CREATE_USER", {}};
        if (next == "INDEX")    return {"CREATE_INDEX", {}};
        if (next == "UNIQUE" && tokens.size() >= 3 && tokens[2].lexeme.toUpper() == "INDEX")
            return {"CREATE_INDEX", {}};
    }
    if (kw == "DROP" && tokens.size() >= 2) {
        QString next = tokens[1].lexeme.toUpper();
        if (next == "DATABASE") return {"DROP_DATABASE", {}};
        if (next == "TABLE")    return {"DROP_TABLE", {}};
        if (next == "USER")     return {"DROP_USER", {}};
        if (next == "INDEX")    return {"DROP_INDEX", {}};
    }
    if (kw == "USE" && tokens.size() >= 2 && tokens[1].type == TokenType::IDENTIFIER)
        return {"USE_DATABASE", {}};
    if (kw == "SHOW" && tokens.size() >= 2) {
        QString next = tokens[1].lexeme.toUpper();
        if (next == "DATABASES") return {"SHOW_DATABASES", {}};
        if (next == "TABLES")   return {"SHOW_TABLES", {}};
        if (next == "CREATE" && tokens.size() >= 3 && tokens[2].lexeme.toUpper() == "TABLE")
            return {"SHOW_CREATE_TABLE", {}};
    }
    if (kw == "DESC" || kw == "DESCRIBE") return {"DESC_TABLE", {}};
    if (kw == "SELECT") return {"SELECT", {}};
    if (kw == "INSERT") return {"INSERT", {}};
    if (kw == "UPDATE") return {"UPDATE", {}};
    if (kw == "DELETE") return {"DELETE", {}};
    if (kw == "ALTER" && tokens.size() >= 2) {
        QString next = tokens[1].lexeme.toUpper();
        if (next == "USER") return {"ALTER_USER", {}};
        return {"ALTER_TABLE", {}};
    }
    if (kw == "GRANT")  return {"GRANT_ALL", {}};
    if (kw == "REVOKE") return {"REVOKE_ALL", {}};
    if (kw == "LOGIN")  return {"LOGIN", {}};

    return {"UNKNOWN", {}};
}

QString extractDatabaseName(const QVector<SqlToken>& tokens) {
    // tokens[0] 应为 DATABASE
    for (int i = 1; i < tokens.size(); ++i) {
        if (tokens[i].type == TokenType::IDENTIFIER || tokens[i].type == TokenType::STRING_LIT)
            return tokens[i].lexeme;
    }
    return {};
}

// ============================================================
//  parseDatabaseSql
// ============================================================
ParseResult parseDatabaseSql(const QString& sql, const QVector<SqlToken>& tokens) {
    if (tokens.isEmpty()) return {false, "Empty input", "UNKNOWN", {}};

    auto [cmdType, payload] = classifySql(sql, tokens);

    if (cmdType == "CREATE_DATABASE") {
        QString name = extractDatabaseName(tokens);
        if (name.isEmpty())
            return {false, "CREATE DATABASE: expected database name", cmdType, {}};
        payload["databaseName"] = name;
        return {true, "", cmdType, payload};
    }

    if (cmdType == "DROP_DATABASE") {
        QString name = extractDatabaseName(tokens);
        if (name.isEmpty())
            return {false, "DROP DATABASE: expected database name", cmdType, {}};
        payload["databaseName"] = name;
        return {true, "", cmdType, payload};
    }

    if (cmdType == "USE_DATABASE") {
        QString name;
        for (int i = 1; i < tokens.size(); ++i) {
            if (tokens[i].type == TokenType::IDENTIFIER)
                { name = tokens[i].lexeme; break; }
        }
        if (name.isEmpty())
            return {false, "USE: expected database name", cmdType, {}};
        payload["databaseName"] = name;
        return {true, "", cmdType, payload};
    }

    if (cmdType == "SHOW_DATABASES") {
        return {true, "", cmdType, {}};
    }

    return {false, "Not a database-level SQL statement", "UNKNOWN", {}};
}

// ============================================================
//  统一入口（数据库级）
// ============================================================

} // namespace sqlparser
