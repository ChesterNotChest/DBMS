/**
 * table_parser.cpp — 表级 SQL 解析器
 *
 * 仅做解析，不碰文件系统。
 * 支持：CREATE TABLE, DROP TABLE, ALTER TABLE, SHOW TABLES, DESC
 */
#include "sql_parser.h"
#include <QDebug>

namespace sqlparser {

// ============================================================
//  辅助：从 token 流中查找关键字位置
// ============================================================
static int findKeyword(const QVector<SqlToken>& tokens, TokenType type, int from = 0) {
    for (int i = from; i < tokens.size(); ++i)
        if (tokens[i].type == type) return i;
    return -1;
}

// ============================================================
//  parseTableSql
// ============================================================
ParseResult parseTableSql(const QString& sql) {
    auto tokens = SqlTokenizer::tokenize(sql);
    if (tokens.isEmpty()) return {false, "Empty input", "UNKNOWN", {}};

    auto [cmdType, payload] = classifySql(sql, tokens);

    // ── CREATE TABLE ──
    if (cmdType == "CREATE_TABLE") {
        // CREATE TABLE name (...)
        int tableIdx = -1;
        for (int i = 1; i < tokens.size(); ++i) {
            if (tokens[i].type == TokenType::IDENTIFIER)
                { tableIdx = i; break; }
        }
        if (tableIdx < 0)
            return {false, "CREATE TABLE: expected table name", cmdType, {}};

        QString tableName = tokens[tableIdx].lexeme;

        // 找括号范围
        int lparen = findKeyword(tokens, TokenType::LPAREN, tableIdx);
        if (lparen < 0)
            return {false, "CREATE TABLE: expected '('", cmdType, {}};

        // 找到匹配的右括号
        int depth = 0, rparen = -1;
        for (int i = lparen; i < tokens.size(); ++i) {
            if (tokens[i].type == TokenType::LPAREN) depth++;
            else if (tokens[i].type == TokenType::RPAREN) depth--;
            if (depth == 0) { rparen = i; break; }
        }
        if (rparen < 0)
            return {false, "CREATE TABLE: unmatched parenthesis", cmdType, {}};

        // 解析列定义
        QVector<ColumnDef> columns;
        int i = lparen + 1;
        while (i < rparen) {
            if (tokens[i].type == TokenType::COMMA) { ++i; continue; }
            if (tokens[i].type == TokenType::RPAREN) break;

            // 检查是否为约束定义 (PRIMARY KEY, CONSTRAINT ...)
            QString upper = tokens[i].lexeme.toUpper();

            if (upper == "PRIMARY" && i + 1 < rparen) {
                // PRIMARY KEY (col1, col2, ...)
                int pkLparen = -1, pkRparen = -1;
                for (int j = i; j <= rparen; ++j) {
                    if (tokens[j].type == TokenType::LPAREN && pkLparen < 0) pkLparen = j;
                    if (tokens[j].type == TokenType::RPAREN) { pkRparen = j; break; }
                }
                if (pkLparen >= 0 && pkRparen >= 0) {
                    for (int j = pkLparen + 1; j < pkRparen; ++j) {
                        if (tokens[j].type == TokenType::IDENTIFIER) {
                            // 标记对应列为 PK
                            for (auto& c : columns) {
                                if (c.name.toUpper() == tokens[j].lexeme.toUpper())
                                    c.primaryKey = true;
                            }
                        }
                    }
                    i = pkRparen + 1;
                    continue;
                }
            }

            if (upper == "CONSTRAINT" || upper == "UNIQUE" || upper == "CHECK" || upper == "FOREIGN") {
                // 跳过约束定义直到括号结束
                while (i < rparen && tokens[i].type != TokenType::COMMA && tokens[i].type != TokenType::RPAREN)
                    ++i;
                continue;
            }

            // 列定义：name TYPE [constraints...]
            if (tokens[i].type != TokenType::IDENTIFIER) { ++i; continue; }

            ColumnDef col;
            col.name = tokens[i].lexeme;
            ++i;

            // 类型
            if (i >= rparen) break;
            if (tokens[i].type == TokenType::INT_TYPE ||
                tokens[i].type == TokenType::FLOAT_TYPE ||
                tokens[i].type == TokenType::CHAR_TYPE ||
                tokens[i].type == TokenType::VARCHAR_TYPE ||
                tokens[i].type == TokenType::TEXT_TYPE) {
                col.type = tokens[i].lexeme.toUpper();
                ++i;

                // 括号内长度 (e.g., VARCHAR(255))
                if (i < rparen && tokens[i].type == TokenType::LPAREN) {
                    ++i;
                    if (i < rparen && tokens[i].type == TokenType::INTEGER_LIT)
                        { col.length = tokens[i].lexeme.toInt(); ++i; }
                    if (i < rparen && tokens[i].type == TokenType::RPAREN) ++i;
                }
            } else {
                col.type = tokens[i].lexeme.toUpper();
                ++i;
            }

            // 约束
            while (i < rparen && tokens[i].type != TokenType::COMMA && tokens[i].type != TokenType::RPAREN) {
                QString cu = tokens[i].lexeme.toUpper();
                if (cu == "PRIMARY") {
                    col.primaryKey = true;
                    ++i;
                    if (i < rparen && tokens[i].lexeme.toUpper() == "KEY") ++i;
                } else if (cu == "NOT") {
                    ++i;
                    if (i < rparen && tokens[i].lexeme.toUpper() == "NULL") { col.notNull = true; ++i; }
                } else if (cu == "UNIQUE") {
                    col.unique = true; ++i;
                } else if (cu == "AUTO_INCREMENT") {
                    col.autoIncrement = true; ++i;
                } else if (cu == "DEFAULT") {
                    ++i;
                    if (i < rparen) { col.defaultValue = tokens[i].lexeme; ++i; }
                } else {
                    ++i; // 跳过未知
                }
            }

            columns.append(col);
        }

        payload["tableName"] = tableName;
        payload["columns"] = QVariant::fromValue(columns);
        return {true, "", cmdType, payload};
    }

    // ── DROP TABLE ──
    if (cmdType == "DROP_TABLE") {
        QString tableName = extractTableName(tokens);
        if (tableName.isEmpty())
            return {false, "DROP TABLE: expected table name", cmdType, {}};
        payload["tableName"] = tableName;
        return {true, "", cmdType, payload};
    }

    // ── ALTER TABLE ──
    if (cmdType == "ALTER_TABLE") {
        QString tableName = extractTableName(tokens);
        if (tableName.isEmpty())
            return {false, "ALTER TABLE: expected table name", cmdType, {}};

        // 找到动作关键字
        for (int i = 2; i < tokens.size() - 1; ++i) {
            QString kw = tokens[i].lexeme.toUpper();

            if (kw == "ADD" && i + 1 < tokens.size()) {
                QString next = tokens[i + 1].lexeme.toUpper();
                if (next == "COLUMN") {
                    // ALTER TABLE t ADD COLUMN col TYPE ...
                    payload["alterAction"] = "ADD_COLUMN";
                    if (i + 2 < tokens.size())
                        payload["columnName"] = tokens[i + 2].lexeme;
                    payload["tableName"] = tableName;
                    return {true, "", cmdType, payload};
                }
                if (next == "CONSTRAINT") {
                    payload["alterAction"] = "ADD_CONSTRAINT";
                    payload["tableName"] = tableName;
                    return {true, "", cmdType, payload};
                }
            }
            if (kw == "MODIFY" && i + 1 < tokens.size()) {
                QString next = tokens[i + 1].lexeme.toUpper();
                if (next == "COLUMN") {
                    payload["alterAction"] = "MODIFY_COLUMN";
                    if (i + 2 < tokens.size())
                        payload["columnName"] = tokens[i + 2].lexeme;
                    payload["tableName"] = tableName;
                    return {true, "", cmdType, payload};
                }
                if (next == "CONSTRAINT") {
                    payload["alterAction"] = "MODIFY_CONSTRAINT";
                    payload["tableName"] = tableName;
                    return {true, "", cmdType, payload};
                }
            }
            if (kw == "DROP") {
                if (i + 1 < tokens.size()) {
                    QString next = tokens[i + 1].lexeme.toUpper();
                    if (next == "COLUMN") {
                        payload["alterAction"] = "DROP_COLUMN";
                        if (i + 2 < tokens.size())
                            payload["columnName"] = tokens[i + 2].lexeme;
                        payload["tableName"] = tableName;
                        return {true, "", cmdType, payload};
                    }
                    if (next == "CONSTRAINT") {
                        payload["alterAction"] = "DROP_CONSTRAINT";
                        if (i + 2 < tokens.size())
                            payload["constraintName"] = tokens[i + 2].lexeme;
                        payload["tableName"] = tableName;
                        return {true, "", cmdType, payload};
                    }
                }
            }
        }
        return {false, "ALTER TABLE: unsupported syntax", cmdType, {}};
    }

    // ── SHOW TABLES ──
    if (cmdType == "SHOW_TABLES") {
        return {true, "", cmdType, {}};
    }

    // ── DESC / DESCRIBE ──
    if (cmdType == "DESC_TABLE") {
        // 找第一个标识符作为表名
        QString tableName;
        for (int i = 1; i < tokens.size(); ++i) {
            if (tokens[i].type == TokenType::IDENTIFIER)
                { tableName = tokens[i].lexeme; break; }
        }
        if (tableName.isEmpty())
            return {false, "DESC: expected table name", cmdType, {}};
        payload["tableName"] = tableName;
        return {true, "", cmdType, payload};
    }

    return {false, "Not a table-level SQL statement", "UNKNOWN", {}};
}

// ============================================================
//  extractTableName
// ============================================================
QString extractTableName(const QVector<SqlToken>& tokens) {
    for (int i = 1; i < tokens.size(); ++i) {
        if (tokens[i].type == TokenType::IDENTIFIER)
            return tokens[i].lexeme;
        if (tokens[i].type == TokenType::STAR) break; // SELECT *
        if (tokens[i].type == TokenType::FROM) break;
        if (tokens[i].type == TokenType::SEMICOLON) break;
    }
    return {};
}

} // namespace sqlparser
