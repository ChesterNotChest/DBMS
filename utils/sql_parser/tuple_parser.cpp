/**
 * tuple_parser.cpp — 元组级 SQL 解析器
 *
 * 仅做解析，不碰文件系统。
 * 支持：SELECT, INSERT, UPDATE, DELETE
 */
#include "sql_parser.h"
#include <QDebug>

namespace sqlparser {

// ============================================================
//  辅助：解析逗号分隔值列表（尊重引号和括号）
// ============================================================
static QStringList splitCommaList(const QVector<SqlToken>& tokens, int from, int to) {
    QStringList result;
    for (int i = from; i <= to; ++i) {
        if (tokens[i].type == TokenType::COMMA) continue;
        if (tokens[i].type == TokenType::INTEGER_LIT ||
            tokens[i].type == TokenType::FLOAT_LIT ||
            tokens[i].type == TokenType::STRING_LIT ||
            tokens[i].type == TokenType::IDENTIFIER ||
            tokens[i].type == TokenType::NULL_VAL ||
            tokens[i].type == TokenType::STAR ||
            tokens[i].type == TokenType::UNKNOWN) {
            result.append(tokens[i].lexeme);
        }
    }
    return result;
}

// ============================================================
//  找到匹配的右括号
// ============================================================
static int findMatchingParen(const QVector<SqlToken>& tokens, int lparen) {
    int depth = 0;
    for (int i = lparen; i < tokens.size(); ++i) {
        if (tokens[i].type == TokenType::LPAREN) depth++;
        else if (tokens[i].type == TokenType::RPAREN) depth--;
        if (depth == 0) return i;
    }
    return -1;
}

static bool hasWhereClause(const QVector<SqlToken>& tokens)
{
    for (int i = 0; i < tokens.size(); ++i) {
        if (tokens[i].type == TokenType::WHERE) {
            return true;
        }
    }
    return false;
}

static bool parseSelectLimit(const QVector<SqlToken>& tokens,
                             int from,
                             int *limit,
                             QString *error)
{
    if (limit != nullptr) *limit = -1;
    if (error != nullptr) error->clear();

    for (int i = from; i < tokens.size(); ++i) {
        if (tokens[i].type == TokenType::END_OF_INPUT || tokens[i].type == TokenType::SEMICOLON) {
            break;
        }

        if (tokens[i].type == TokenType::LIMIT) {
            if (limit != nullptr && *limit != -1) {
                if (error != nullptr) *error = QStringLiteral("SELECT: duplicate LIMIT clause");
                return false;
            }
            if (i + 1 >= tokens.size() || tokens[i + 1].type != TokenType::INTEGER_LIT) {
                if (error != nullptr) *error = QStringLiteral("SELECT: LIMIT requires a non-negative integer");
                return false;
            }

            bool ok = false;
            const int parsedLimit = tokens[i + 1].lexeme.toInt(&ok);
            if (!ok || parsedLimit < 0) {
                if (error != nullptr) *error = QStringLiteral("SELECT: LIMIT requires a non-negative integer");
                return false;
            }
            if (limit != nullptr) *limit = parsedLimit;
            ++i;
            continue;
        }

        if (error != nullptr) {
            *error = QStringLiteral("SELECT: unsupported clause '%1'").arg(tokens[i].lexeme);
        }
        return false;
    }

    return true;
}

// ============================================================
//  parseTupleSql
// ============================================================
ParseResult parseTupleSql(const QString& /*sql*/, const QVector<SqlToken>& tokens) {
    if (tokens.isEmpty()) return {false, "Empty input", "UNKNOWN", {}};

    auto [cmdType, payload] = classifySql("", tokens);

    // ── SELECT ──
    if (cmdType == "SELECT") {
        if (hasWhereClause(tokens)) {
            return {false, "WHERE is not supported yet", cmdType, {}};
        }

        // SELECT cols FROM table
        QStringList projection;
        QString table;
        int tableIndex = -1;

        int fromIdx = -1;
        for (int i = 1; i < tokens.size(); ++i) {
            if (tokens[i].type == TokenType::FROM) { fromIdx = i; break; }
            if (tokens[i].type == TokenType::END_OF_INPUT) break;
            if (tokens[i].type == TokenType::STAR ||
                tokens[i].type == TokenType::IDENTIFIER)
                projection.append(tokens[i].lexeme);
        }

        if (fromIdx >= 0) {
            for (int i = fromIdx + 1; i < tokens.size(); ++i) {
                if (tokens[i].type == TokenType::IDENTIFIER)
                    { table = tokens[i].lexeme; tableIndex = i; break; }
            }
        }

        if (table.isEmpty())
            return {false, "SELECT: expected FROM table", cmdType, {}};

        int limit = -1;
        QString limitError;
        if (!parseSelectLimit(tokens, tableIndex + 1, &limit, &limitError)) {
            return {false, limitError, cmdType, {}};
        }

        payload["projection"] = projection;
        payload["tableName"] = table;
        payload["limit"] = limit;

        return {true, "", cmdType, payload};
    }

    // ── INSERT INTO ──
    if (cmdType == "INSERT") {
        // INSERT INTO table (col1, col2) VALUES (v1, v2)
        QString table;
        QStringList columnNames;
        QVariantList rows;

        // 找 INTO 和表名
        int intoIdx = -1;
        for (int i = 0; i < tokens.size(); ++i) {
            if (tokens[i].type == TokenType::INTO) { intoIdx = i; break; }
        }
        if (intoIdx < 0)
            return {false, "INSERT: expected INTO", cmdType, {}};

        // 表名
        for (int i = intoIdx + 1; i < tokens.size(); ++i) {
            if (tokens[i].type == TokenType::IDENTIFIER)
                { table = tokens[i].lexeme; break; }
        }
        if (table.isEmpty())
            return {false, "INSERT: expected table name", cmdType, {}};

        // 找 VALUES 位置
        int valuesIdx = -1;
        for (int i = intoIdx + 1; i < tokens.size(); ++i) {
            if (tokens[i].type == TokenType::VALUES) { valuesIdx = i; break; }
        }

        if (valuesIdx < 0)
            return {false, "INSERT: expected VALUES", cmdType, {}};

        // 列名列表（VALUES 之前的括号部分）
        // 找第一个 LPAREN
        int firstLparen = -1, firstRparen = -1;
        for (int i = intoIdx + 1; i < valuesIdx; ++i) {
            if (tokens[i].type == TokenType::LPAREN && firstLparen < 0) firstLparen = i;
            if (tokens[i].type == TokenType::RPAREN && firstLparen >= 0 && firstRparen < 0) firstRparen = i;
        }

        if (firstLparen >= 0 && firstRparen >= 0) {
            columnNames = splitCommaList(tokens, firstLparen + 1, firstRparen - 1);
        }

        // 值列表（VALUES 之后的括号部分，支持多行）
        int i = valuesIdx + 1;
        while (i < tokens.size()) {
            if (tokens[i].type == TokenType::LPAREN) {
                int rp = findMatchingParen(tokens, i);
                if (rp < 0) break;

                QStringList vals = splitCommaList(tokens, i + 1, rp - 1);
                QVariantList row;
                for (const QString& v : vals) {
                    bool isInt;
                    int intVal = v.toInt(&isInt);
                    if (isInt) { row.append(intVal); }
                    else {
                        bool isFloat;
                        double floatVal = v.toDouble(&isFloat);
                        if (isFloat) { row.append(floatVal); }
                        else { row.append(v); }
                    }
                }
                rows.append(QVariant(row));
                i = rp + 1;
            } else {
                ++i;
            }
        }

        payload["tableName"] = table;
        payload["columnNames"] = columnNames;
        payload["rowCount"] = rows.size();
        payload["rows"] = rows;
        return {true, "", cmdType, payload};
    }

    // ── UPDATE ──
    if (cmdType == "UPDATE") {
        if (hasWhereClause(tokens)) {
            return {false, "WHERE is not supported yet", cmdType, {}};
        }

        // UPDATE table SET col=val
        QString table;
        QVariantMap assignments;

        int setIdx = -1;
        for (int i = 0; i < tokens.size(); ++i) {
            if (tokens[i].type == TokenType::SET) { setIdx = i; break; }
        }

        // 表名（SET 之前）
        if (setIdx > 0 && tokens[setIdx - 1].type == TokenType::IDENTIFIER)
            table = tokens[setIdx - 1].lexeme;

        if (table.isEmpty())
            return {false, "UPDATE: expected table name", cmdType, {}};

        // SET 子句
        for (int i = setIdx + 1; i < tokens.size(); ++i) {
            if (tokens[i].type == TokenType::WHERE) break;
            if (tokens[i].type == TokenType::END_OF_INPUT) break;

            if (tokens[i].type == TokenType::IDENTIFIER) {
                QString col = tokens[i].lexeme;
                if (i + 1 < tokens.size() && tokens[i + 1].type == TokenType::EQ && i + 2 < tokens.size()) {
                    QString val = tokens[i + 2].lexeme;
                    bool isInt; int intVal = val.toInt(&isInt);
                    if (isInt) assignments.insert(col, intVal);
                    else { bool isFloat; double fv = val.toDouble(&isFloat);
                           if (isFloat) assignments.insert(col, fv);
                           else assignments.insert(col, val); }
                    i += 2;
                }
            }
        }

        payload["tableName"] = table;
        payload["assignments"] = assignments;

        return {true, "", cmdType, payload};
    }

    // ── DELETE ──
    if (cmdType == "DELETE") {
        if (hasWhereClause(tokens)) {
            return {false, "WHERE is not supported yet", cmdType, {}};
        }

        // DELETE FROM table
        QString table;

        int fromIdx = -1;
        for (int i = 0; i < tokens.size(); ++i) {
            if (tokens[i].type == TokenType::FROM) { fromIdx = i; break; }
        }

        if (fromIdx >= 0 && fromIdx + 1 < tokens.size()) {
            table = tokens[fromIdx + 1].lexeme;
        }

        if (table.isEmpty())
            return {false, "DELETE: expected table name", cmdType, {}};

        payload["tableName"] = table;

        return {true, "", cmdType, payload};
    }

    return {false, "Not a tuple-level SQL statement", "UNKNOWN", {}};
}

// ============================================================
//  统一入口
// ============================================================
ParseResult parseSql(const QString& sql) {
    auto tokens = SqlTokenizer::tokenize(sql);
    auto [cmdType, _] = classifySql(sql, tokens);

    // 数据库级
    if (cmdType == "CREATE_DATABASE" || cmdType == "DROP_DATABASE" ||
        cmdType == "USE_DATABASE"   || cmdType == "SHOW_DATABASES")
        return parseDatabaseSql(sql, tokens);

    // 表级
    if (cmdType == "CREATE_TABLE" || cmdType == "DROP_TABLE" ||
        cmdType == "ALTER_TABLE"  || cmdType == "SHOW_TABLES" ||
        cmdType == "DESC_TABLE" || cmdType == "SHOW_CREATE_TABLE")
        return parseTableSql(sql, tokens);

    // 元组级
    if (cmdType == "SELECT" || cmdType == "INSERT" ||
        cmdType == "UPDATE"  || cmdType == "DELETE")
        return parseTupleSql(sql, tokens);

    return {false, "Unsupported SQL statement", "UNKNOWN", {}};
}

} // namespace sqlparser
