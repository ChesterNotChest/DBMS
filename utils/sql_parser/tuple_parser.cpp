/**
 * tuple_parser.cpp — 元组级 SQL 解析器
 *
 * 仅做解析，不碰文件系统。
 * 支持：SELECT, INSERT, UPDATE, DELETE
 */
#include "sql_parser.h"
#include "../logic/logic.h"
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

static int lastMeaningfulTokenIndex(const QVector<SqlToken> &tokens)
{
    for (int i = tokens.size() - 1; i >= 0; --i) {
        if (tokens[i].type != TokenType::END_OF_INPUT && tokens[i].type != TokenType::SEMICOLON) {
            return i;
        }
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

static bool isLiteralToken(TokenType type)
{
    return type == TokenType::INTEGER_LIT
           || type == TokenType::FLOAT_LIT
           || type == TokenType::STRING_LIT
           || type == TokenType::IDENTIFIER
           || type == TokenType::NULL_VAL;
}

static QString sliceClauseText(const QString &sql,
                               const QVector<SqlToken> &tokens,
                               int from,
                               int to)
{
    if (from < 0 || to < from || from >= tokens.size() || to >= tokens.size()) {
        return {};
    }

    const int startPos = tokens[from].position;
    const int tokenLength = tokens[to].length > 0 ? tokens[to].length : tokens[to].lexeme.size();
    const int endPos = tokens[to].position + tokenLength;
    if (startPos < 0 || endPos < startPos || endPos > sql.size()) {
        return {};
    }
    return sql.mid(startPos, endPos - startPos);
}

static bool isSimpleWhereNode(const logic::LogicNode &node, QVariantList *conditions)
{
    if (node.type == logic::LogicNodeType::Binary
        && node.binaryOperator == logic::LogicBinaryOperator::And) {
        return isSimpleWhereNode(node.children.value(0), conditions)
               && isSimpleWhereNode(node.children.value(1), conditions);
    }

    if (node.type != logic::LogicNodeType::Comparison
        || node.compareOperator != logic::LogicCompareOperator::Eq
        || node.children.size() != 2) {
        return false;
    }

    const logic::LogicNode &lhs = node.children.at(0);
    const logic::LogicNode &rhs = node.children.at(1);
    if (lhs.type != logic::LogicNodeType::ColumnRef
        || lhs.reference.scope != logic::LogicReferenceScope::Local) {
        return false;
    }
    if (lhs.reference.name.contains(QLatin1Char('.'))) {
        return false;
    }
    if (rhs.type != logic::LogicNodeType::Literal || rhs.literalIsNull) {
        return false;
    }

    QVariantMap condition;
    condition.insert(QStringLiteral("columnName"), lhs.reference.name);
    condition.insert(QStringLiteral("value"), rhs.literalValue);
    if (conditions != nullptr) {
        conditions->append(condition);
    }
    return true;
}

static bool extractWherePayload(const QString &sql,
                                const QVector<SqlToken> &tokens,
                                int whereIdx,
                                int clauseEndIdx,
                                QVariantMap *payload,
                                QString *error)
{
    if (payload != nullptr) {
        payload->remove(QStringLiteral("conditions"));
        payload->remove(QStringLiteral("whereAst"));
        payload->remove(QStringLiteral("hasComplexWhere"));
    }

    if (whereIdx < 0) {
        if (payload != nullptr) {
            payload->insert(QStringLiteral("hasComplexWhere"), false);
        }
        return true;
    }

    const int whereStart = whereIdx + 1;
    const int whereEnd = clauseEndIdx >= 0 ? clauseEndIdx - 1 : lastMeaningfulTokenIndex(tokens);
    const QString whereText = sliceClauseText(sql, tokens, whereStart, whereEnd);
    if (whereText.trimmed().isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("WHERE: expected condition");
        }
        return false;
    }

    const logic::LogicTokenizeResult tokenized = logic::tokenizeLogicExpression(whereText);
    if (!tokenized.success) {
        if (error != nullptr) {
            *error = tokenized.error.message;
        }
        return false;
    }

    const logic::LogicParseResult parsed = logic::parseLogicTokens(whereText, tokenized.tokens);
    if (!parsed.success) {
        if (error != nullptr) {
            *error = parsed.error.message;
        }
        return false;
    }

    QVariantList conditions;
    const bool simpleWhere = isSimpleWhereNode(parsed.root, &conditions);
    if (payload != nullptr) {
        payload->insert(QStringLiteral("whereAst"), QVariant::fromValue(parsed.root));
        payload->insert(QStringLiteral("hasComplexWhere"), !simpleWhere);
        if (simpleWhere && !conditions.isEmpty()) {
            payload->insert(QStringLiteral("conditions"), conditions);
        }
    }
    return true;
}

static bool projectionIsSelectAll(const QStringList &projection)
{
    return projection.size() == 1 && projection.first() == QStringLiteral("*");
}

static bool parseSimpleConditions(const QVector<SqlToken> &tokens,
                                  int from,
                                  int to,
                                  QVariantList *conditions,
                                  QString *error)
{
    if (conditions != nullptr) conditions->clear();
    if (from > to) {
        if (error != nullptr) *error = QStringLiteral("WHERE: expected condition");
        return false;
    }

    int index = from;
    while (index <= to) {
        if (tokens[index].type != TokenType::IDENTIFIER) {
            if (error != nullptr) *error = QStringLiteral("WHERE: expected column name");
            return false;
        }
        if (index + 1 > to || tokens[index + 1].type != TokenType::EQ) {
            if (error != nullptr) *error = QStringLiteral("WHERE only supports '=' conditions");
            return false;
        }
        if (index + 2 > to || !isLiteralToken(tokens[index + 2].type)) {
            if (error != nullptr) *error = QStringLiteral("WHERE: expected literal value");
            return false;
        }

        QVariantMap condition;
        condition.insert(QStringLiteral("columnName"), tokens[index].lexeme);
        condition.insert(QStringLiteral("value"), tokens[index + 2].lexeme);
        if (conditions != nullptr) {
            conditions->append(condition);
        }

        index += 3;
        if (index > to) break;
        if (tokens[index].type != TokenType::AND) {
            if (error != nullptr) *error = QStringLiteral("WHERE only supports AND-combined equality conditions");
            return false;
        }
        ++index;
        if (index > to) {
            if (error != nullptr) *error = QStringLiteral("WHERE: expected condition after AND");
            return false;
        }
    }

    return true;
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

static bool parseOrderByClause(const QVector<SqlToken> &tokens,
                               int orderIdx,
                               QVariantMap *payload,
                               QString *error)
{
    if (payload != nullptr) {
        payload->remove(QStringLiteral("orderByColumn"));
        payload->remove(QStringLiteral("orderByDescending"));
    }
    if (error != nullptr) {
        error->clear();
    }
    if (orderIdx < 0) {
        return true;
    }
    if (orderIdx + 2 >= tokens.size()
        || tokens[orderIdx + 1].type != TokenType::BY
        || tokens[orderIdx + 2].type != TokenType::IDENTIFIER) {
        if (error != nullptr) {
            *error = QStringLiteral("ORDER BY: expected column name");
        }
        return false;
    }

    bool descending = false;
    const int directionIndex = orderIdx + 3;
    if (directionIndex < tokens.size()
        && tokens[directionIndex].type != TokenType::LIMIT
        && tokens[directionIndex].type != TokenType::END_OF_INPUT
        && tokens[directionIndex].type != TokenType::SEMICOLON) {
        if (tokens[directionIndex].type == TokenType::DESC) {
            descending = true;
        } else if (tokens[directionIndex].type == TokenType::ASC) {
            descending = false;
        } else {
            if (error != nullptr) {
                *error = QStringLiteral("ORDER BY: expected ASC or DESC");
            }
            return false;
        }
    }

    if (payload != nullptr) {
        payload->insert(QStringLiteral("orderByColumn"), tokens[orderIdx + 2].lexeme);
        payload->insert(QStringLiteral("orderByDescending"), descending);
    }
    return true;
}

// ============================================================
//  parseTupleSql
// ============================================================
ParseResult parseTupleSql(const QString& sql, const QVector<SqlToken>& tokens) {
    if (tokens.isEmpty()) return {false, "Empty input", "UNKNOWN", {}};

    auto [cmdType, payload] = classifySql(sql, tokens);

    // ── SELECT ──
    if (cmdType == "SELECT") {
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

        int whereIdx = -1;
        int orderIdx = -1;
        int limitIdx = -1;
        for (int i = tableIndex + 1; i < tokens.size(); ++i) {
            if (tokens[i].type == TokenType::WHERE && whereIdx < 0) {
                whereIdx = i;
            } else if (tokens[i].type == TokenType::ORDER && orderIdx < 0) {
                orderIdx = i;
            } else if (tokens[i].type == TokenType::LIMIT && limitIdx < 0) {
                limitIdx = i;
            }
        }

        if (whereIdx >= 0 && limitIdx >= 0 && limitIdx < whereIdx) {
            return {false, "SELECT: unsupported clause 'LIMIT'", cmdType, {}};
        }
        if (whereIdx >= 0 && orderIdx >= 0 && orderIdx < whereIdx) {
            return {false, "SELECT: unsupported clause 'ORDER'", cmdType, {}};
        }
        if (orderIdx >= 0 && limitIdx >= 0 && limitIdx < orderIdx) {
            return {false, "SELECT: unsupported clause 'LIMIT'", cmdType, {}};
        }

        QString whereError;
        const int whereEndClause = orderIdx >= 0 ? orderIdx : limitIdx;
        if (!extractWherePayload(sql, tokens, whereIdx, whereEndClause, &payload, &whereError)) {
            return {false, whereError, cmdType, {}};
        }

        QString orderError;
        if (!parseOrderByClause(tokens, orderIdx, &payload, &orderError)) {
            return {false, orderError, cmdType, {}};
        }

        int limit = -1;
        QString limitError;
        const int limitParseStart = limitIdx >= 0 ? limitIdx : tokens.size() - 1;
        if (!parseSelectLimit(tokens, limitParseStart, &limit, &limitError)) {
            return {false, limitError, cmdType, {}};
        }

        payload["selectAll"] = projectionIsSelectAll(projection);
        if (payload.value(QStringLiteral("selectAll")).toBool()) {
            projection.clear();
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
        int whereIdx = -1;
        for (int i = setIdx + 1; i < tokens.size(); ++i) {
            if (tokens[i].type == TokenType::WHERE) {
                whereIdx = i;
                break;
            }
        }

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

        if (whereIdx >= 0) {
            QString whereError;
            if (!extractWherePayload(sql, tokens, whereIdx, -1, &payload, &whereError)) {
                return {false, whereError, cmdType, {}};
            }
        }

        payload["tableName"] = table;
        payload["assignments"] = assignments;

        return {true, "", cmdType, payload};
    }

    // ── DELETE ──
    if (cmdType == "DELETE") {
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

        if (hasWhereClause(tokens)) {
            int whereIdx = -1;
            for (int i = fromIdx + 1; i < tokens.size(); ++i) {
                if (tokens[i].type == TokenType::WHERE) {
                    whereIdx = i;
                    break;
                }
            }
            QString whereError;
            if (!extractWherePayload(sql, tokens, whereIdx, -1, &payload, &whereError)) {
                return {false, whereError, cmdType, {}};
            }
        }

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
        cmdType == "ALTER_TABLE"  || cmdType == "CREATE_INDEX" ||
        cmdType == "DROP_INDEX"   || cmdType == "SHOW_TABLES" ||
        cmdType == "DESC_TABLE" || cmdType == "SHOW_CREATE_TABLE")
        return parseTableSql(sql, tokens);

    // 元组级
    if (cmdType == "SELECT" || cmdType == "INSERT" ||
        cmdType == "UPDATE"  || cmdType == "DELETE")
        return parseTupleSql(sql, tokens);

    if (cmdType == "LOGIN"
        || cmdType == "CREATE_USER"
        || cmdType == "DROP_USER"
        || cmdType == "ALTER_USER"
        || cmdType == "GRANT_ALL"
        || cmdType == "REVOKE_ALL")
        return parseAuthSql(sql, tokens);

    return {false, "Unsupported SQL statement", "UNKNOWN", {}};
}

} // namespace sqlparser
