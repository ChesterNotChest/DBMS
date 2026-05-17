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

static bool isClauseTerminator(TokenType type)
{
    return type == TokenType::WHERE
           || type == TokenType::ORDER
           || type == TokenType::LIMIT
           || type == TokenType::SEMICOLON
           || type == TokenType::END_OF_INPUT;
}

static bool isJoinStart(TokenType type)
{
    return type == TokenType::JOIN
           || type == TokenType::INNER
           || type == TokenType::LEFT
           || type == TokenType::RIGHT
           || type == TokenType::FULL
           || type == TokenType::NATURAL
           || type == TokenType::CROSS;
}

static bool isFromItemTerminator(TokenType type)
{
    return isClauseTerminator(type)
           || type == TokenType::COMMA
           || isJoinStart(type)
           || type == TokenType::ON;
}

static bool isOnExpressionTerminator(TokenType type)
{
    return isClauseTerminator(type)
           || type == TokenType::COMMA
           || isJoinStart(type);
}

static bool isAliasForbidden(TokenType type)
{
    return isFromItemTerminator(type)
           || type == TokenType::BY
           || type == TokenType::USING;
}

static bool isIdentifierLike(TokenType type)
{
    return type == TokenType::IDENTIFIER
           || type == TokenType::INT_TYPE
           || type == TokenType::FLOAT_TYPE
           || type == TokenType::CHAR_TYPE
           || type == TokenType::VARCHAR_TYPE
           || type == TokenType::TEXT_TYPE;
}

static bool parseQualifiedIdentifier(const QVector<SqlToken> &tokens,
                                     int *index,
                                     int endExclusive,
                                     QString *name)
{
    if (index == nullptr || name == nullptr || *index >= endExclusive) {
        return false;
    }
    if (!isIdentifierLike(tokens[*index].type)) {
        return false;
    }

    QString result = tokens[*index].lexeme;
    ++(*index);
    if (*index + 1 < endExclusive
        && tokens[*index].type == TokenType::DOT
        && isIdentifierLike(tokens[*index + 1].type)) {
        result += QLatin1Char('.');
        result += tokens[*index + 1].lexeme;
        *index += 2;
    }

    *name = result;
    return true;
}

static bool parseLogicAstFromTokenRange(const QString &sql,
                                        const QVector<SqlToken> &tokens,
                                        int from,
                                        int to,
                                        logic::LogicNode *ast,
                                        QString *error)
{
    const QString text = sliceClauseText(sql, tokens, from, to);
    if (text.trimmed().isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("JOIN: expected ON condition");
        }
        return false;
    }

    const logic::LogicTokenizeResult tokenized = logic::tokenizeLogicExpression(text);
    if (!tokenized.success) {
        if (error != nullptr) {
            *error = tokenized.error.message;
        }
        return false;
    }

    const logic::LogicParseResult parsed = logic::parseLogicTokens(text, tokenized.tokens);
    if (!parsed.success) {
        if (error != nullptr) {
            *error = parsed.error.message;
        }
        return false;
    }
    if (ast != nullptr) {
        *ast = parsed.root;
    }
    return true;
}

static bool parseTableSource(const QVector<SqlToken> &tokens,
                             int startIndex,
                             int endExclusive,
                             QVariantMap *source,
                             int *nextIndex,
                             QString *error)
{
    if (startIndex >= endExclusive || !isIdentifierLike(tokens[startIndex].type)) {
        if (error != nullptr) {
            *error = QStringLiteral("SELECT: expected table name");
        }
        return false;
    }
    if (startIndex + 1 < endExclusive && tokens[startIndex + 1].type == TokenType::DOT) {
        if (error != nullptr) {
            *error = QStringLiteral("SELECT: database-qualified table names are not supported");
        }
        return false;
    }

    QVariantMap parsedSource;
    parsedSource.insert(QStringLiteral("tableName"), tokens[startIndex].lexeme);
    parsedSource.insert(QStringLiteral("tableAlias"), QString());

    int index = startIndex + 1;
    if (index < endExclusive && !isFromItemTerminator(tokens[index].type)) {
        if (tokens[index].lexeme.compare(QStringLiteral("AS"), Qt::CaseInsensitive) == 0) {
            ++index;
            if (index >= endExclusive || !isIdentifierLike(tokens[index].type) || isAliasForbidden(tokens[index].type)) {
                if (error != nullptr) {
                    *error = QStringLiteral("SELECT: expected table alias after AS");
                }
                return false;
            }
            parsedSource.insert(QStringLiteral("tableAlias"), tokens[index].lexeme);
            ++index;
        } else if (isIdentifierLike(tokens[index].type) && !isAliasForbidden(tokens[index].type)) {
            parsedSource.insert(QStringLiteral("tableAlias"), tokens[index].lexeme);
            ++index;
        }
    }

    if (source != nullptr) {
        *source = parsedSource;
    }
    if (nextIndex != nullptr) {
        *nextIndex = index;
    }
    return true;
}

static QString sourceAliasOrTable(const QVariantMap &source)
{
    const QString alias = source.value(QStringLiteral("tableAlias")).toString().trimmed();
    return alias.isEmpty() ? source.value(QStringLiteral("tableName")).toString().trimmed() : alias;
}

static bool validateSourceNames(const QVariantList &sources, QString *error)
{
    QSet<QString> visiblePrefixes;
    QSet<QString> unaliasedTables;
    for (const QVariant &value : sources) {
        const QVariantMap source = value.toMap();
        const QString tableName = source.value(QStringLiteral("tableName")).toString().trimmed();
        const QString alias = source.value(QStringLiteral("tableAlias")).toString().trimmed();
        const QString prefix = alias.isEmpty() ? tableName : alias;
        if (prefix.isEmpty()) {
            if (error != nullptr) {
                *error = QStringLiteral("SELECT: expected table name");
            }
            return false;
        }
        if (visiblePrefixes.contains(prefix)) {
            if (error != nullptr) {
                *error = QStringLiteral("SELECT: duplicate table alias '%1'").arg(prefix);
            }
            return false;
        }
        visiblePrefixes.insert(prefix);
        if (alias.isEmpty()) {
            if (unaliasedTables.contains(tableName)) {
                if (error != nullptr) {
                    *error = QStringLiteral("SELECT: duplicate table '%1' requires aliases").arg(tableName);
                }
                return false;
            }
            unaliasedTables.insert(tableName);
        }
    }
    return true;
}

static bool parseFromClause(const QString &sql,
                            const QVector<SqlToken> &tokens,
                            int fromIndex,
                            int clauseEndIndex,
                            QVariantList *fromSources,
                            QVariantList *joins,
                            QString *singleTableName,
                            QString *singleTableAlias,
                            bool *isMultiTable,
                            QString *error)
{
    if (fromSources != nullptr) {
        fromSources->clear();
    }
    if (joins != nullptr) {
        joins->clear();
    }

    const int endExclusive = clauseEndIndex;
    int index = fromIndex + 1;
    QVariantMap firstSource;
    if (!parseTableSource(tokens, index, endExclusive, &firstSource, &index, error)) {
        return false;
    }

    QVariantList parsedSources;
    QVariantList parsedJoins;
    parsedSources.append(firstSource);

    enum class FromMode { None, Comma, Join };
    FromMode mode = FromMode::None;

    while (index < endExclusive) {
        if (tokens[index].type == TokenType::COMMA) {
            if (mode == FromMode::Join) {
                if (error != nullptr) {
                    *error = QStringLiteral("SELECT: cannot mix comma FROM and JOIN in the same FROM clause");
                }
                return false;
            }
            mode = FromMode::Comma;
            ++index;
            QVariantMap source;
            if (!parseTableSource(tokens, index, endExclusive, &source, &index, error)) {
                return false;
            }
            parsedSources.append(source);
            continue;
        }

        QString joinType;
        if (tokens[index].type == TokenType::JOIN) {
            if (mode == FromMode::Comma) {
                if (error != nullptr) {
                    *error = QStringLiteral("SELECT: cannot mix comma FROM and JOIN in the same FROM clause");
                }
                return false;
            }
            joinType = QStringLiteral("inner");
            ++index;
        } else if (tokens[index].type == TokenType::INNER
                   || tokens[index].type == TokenType::LEFT
                   || tokens[index].type == TokenType::RIGHT
                   || tokens[index].type == TokenType::FULL) {
            if (mode == FromMode::Comma) {
                if (error != nullptr) {
                    *error = QStringLiteral("SELECT: cannot mix comma FROM and JOIN in the same FROM clause");
                }
                return false;
            }
            if (tokens[index].type == TokenType::INNER) joinType = QStringLiteral("inner");
            if (tokens[index].type == TokenType::LEFT) joinType = QStringLiteral("left");
            if (tokens[index].type == TokenType::RIGHT) joinType = QStringLiteral("right");
            if (tokens[index].type == TokenType::FULL) joinType = QStringLiteral("full");
            ++index;
            if (index >= endExclusive || tokens[index].type != TokenType::JOIN) {
                if (error != nullptr) {
                    *error = QStringLiteral("SELECT: expected JOIN after join type");
                }
                return false;
            }
            ++index;
        } else if (tokens[index].type == TokenType::NATURAL || tokens[index].type == TokenType::CROSS) {
            if (error != nullptr) {
                *error = QStringLiteral("SELECT: unsupported JOIN type '%1'").arg(tokens[index].lexeme);
            }
            return false;
        } else {
            if (error != nullptr) {
                *error = QStringLiteral("SELECT: unsupported trailing token '%1'").arg(tokens[index].lexeme);
            }
            return false;
        }

        mode = FromMode::Join;
        QVariantMap rightSource;
        const int rightSourceIndex = parsedSources.size();
        if (!parseTableSource(tokens, index, endExclusive, &rightSource, &index, error)) {
            return false;
        }
        parsedSources.append(rightSource);

        if (index >= endExclusive || tokens[index].type != TokenType::ON) {
            if (index < endExclusive && tokens[index].type == TokenType::USING) {
                if (error != nullptr) {
                    *error = QStringLiteral("SELECT: JOIN USING is not supported");
                }
                return false;
            }
            if (error != nullptr) {
                *error = QStringLiteral("SELECT: JOIN requires ON condition");
            }
            return false;
        }

        const int onStart = index + 1;
        int onEndExclusive = onStart;
        while (onEndExclusive < endExclusive && !isOnExpressionTerminator(tokens[onEndExclusive].type)) {
            ++onEndExclusive;
        }
        if (onEndExclusive < endExclusive && tokens[onEndExclusive].type == TokenType::COMMA) {
            if (error != nullptr) {
                *error = QStringLiteral("SELECT: cannot mix comma FROM and JOIN in the same FROM clause");
            }
            return false;
        }
        logic::LogicNode onAst;
        QString onError;
        if (!parseLogicAstFromTokenRange(sql, tokens, onStart, onEndExclusive - 1, &onAst, &onError)) {
            if (error != nullptr) {
                *error = onError;
            }
            return false;
        }

        QVariantMap join;
        join.insert(QStringLiteral("joinType"), joinType);
        join.insert(QStringLiteral("leftSourceIndex"), rightSourceIndex - 1);
        join.insert(QStringLiteral("rightSourceIndex"), rightSourceIndex);
        join.insert(QStringLiteral("onAst"), QVariant::fromValue(onAst));
        parsedJoins.append(join);
        index = onEndExclusive;
    }

    if (!validateSourceNames(parsedSources, error)) {
        return false;
    }

    if (fromSources != nullptr) {
        *fromSources = parsedSources;
    }
    if (joins != nullptr) {
        *joins = parsedJoins;
    }
    if (singleTableName != nullptr) {
        *singleTableName = firstSource.value(QStringLiteral("tableName")).toString();
    }
    if (singleTableAlias != nullptr) {
        *singleTableAlias = firstSource.value(QStringLiteral("tableAlias")).toString();
    }
    if (isMultiTable != nullptr) {
        *isMultiTable = parsedSources.size() > 1;
    }
    return true;
}

static QString defaultOutputNameForSource(const QString &sourceName)
{
    const int dotIndex = sourceName.lastIndexOf(QLatin1Char('.'));
    return dotIndex >= 0 && dotIndex + 1 < sourceName.size()
               ? sourceName.mid(dotIndex + 1)
               : sourceName;
}

static bool parseProjectionItems(const QVector<SqlToken> &tokens,
                                 int from,
                                 int to,
                                 QStringList *projection,
                                 QVariantList *projectionItems,
                                 QString *error)
{
    if (projection != nullptr) {
        projection->clear();
    }
    if (projectionItems != nullptr) {
        projectionItems->clear();
    }
    if (from > to) {
        if (error != nullptr) {
            *error = QStringLiteral("SELECT: expected projection columns");
        }
        return false;
    }

    int index = from;
    while (index <= to) {
        if (tokens[index].type == TokenType::COMMA) {
            if (error != nullptr) {
                *error = QStringLiteral("SELECT: expected projection column");
            }
            return false;
        }

        QString sourceColumn;
        if (tokens[index].type == TokenType::STAR) {
            sourceColumn = QStringLiteral("*");
            ++index;
            if (index <= to && !isClauseTerminator(tokens[index].type) && tokens[index].type != TokenType::COMMA) {
                if (error != nullptr) {
                    *error = QStringLiteral("SELECT: '*' cannot have an alias");
                }
                return false;
            }
        } else if (tokens[index].type == TokenType::LPAREN) {
            const int right = findMatchingParen(tokens, index);
            if (right < 0 || right > to) {
                if (error != nullptr) {
                    *error = QStringLiteral("SELECT: unmatched projection parenthesis");
                }
                return false;
            }
            if (index + 1 >= right || !parseQualifiedIdentifier(tokens, &(++index), right, &sourceColumn)) {
                if (error != nullptr) {
                    *error = QStringLiteral("SELECT: expected projection column");
                }
                return false;
            }
            if (index != right) {
                if (error != nullptr) {
                    *error = QStringLiteral("SELECT: unsupported projection expression");
                }
                return false;
            }
            index = right + 1;
        } else if (!parseQualifiedIdentifier(tokens, &index, to + 1, &sourceColumn)) {
            if (error != nullptr) {
                *error = QStringLiteral("SELECT: expected projection column");
            }
            return false;
        }

        QString outputColumn = defaultOutputNameForSource(sourceColumn);
        if (index <= to && tokens[index].type != TokenType::COMMA) {
            if (tokens[index].lexeme.compare(QStringLiteral("AS"), Qt::CaseInsensitive) == 0) {
                ++index;
                if (index > to || !isIdentifierLike(tokens[index].type)) {
                    if (error != nullptr) {
                        *error = QStringLiteral("SELECT: expected projection alias after AS");
                    }
                    return false;
                }
                outputColumn = tokens[index].lexeme;
                ++index;
            } else if (isIdentifierLike(tokens[index].type)) {
                outputColumn = tokens[index].lexeme;
                ++index;
            } else {
                if (error != nullptr) {
                    *error = QStringLiteral("SELECT: unsupported projection token '%1'").arg(tokens[index].lexeme);
                }
                return false;
            }
        }

        if (projection != nullptr) {
            projection->append(sourceColumn);
        }
        if (projectionItems != nullptr) {
            QVariantMap item;
            item.insert(QStringLiteral("sourceColumn"), sourceColumn);
            item.insert(QStringLiteral("outputColumn"), outputColumn);
            projectionItems->append(item);
        }

        if (index > to) {
            break;
        }
        if (tokens[index].type != TokenType::COMMA) {
            if (error != nullptr) {
                *error = QStringLiteral("SELECT: expected ',' between projection columns");
            }
            return false;
        }
        ++index;
        if (index > to) {
            if (error != nullptr) {
                *error = QStringLiteral("SELECT: expected projection column after ','");
            }
            return false;
        }
    }

    return true;
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
        || tokens[orderIdx + 1].type != TokenType::BY) {
        if (error != nullptr) {
            *error = QStringLiteral("ORDER BY: expected column name");
        }
        return false;
    }

    int itemIndex = orderIdx + 2;
    QString orderByColumn;
    if (!parseQualifiedIdentifier(tokens, &itemIndex, tokens.size(), &orderByColumn)) {
        if (error != nullptr) {
            *error = QStringLiteral("ORDER BY: expected column name");
        }
        return false;
    }

    bool descending = false;
    const int directionIndex = itemIndex;
    int nextIndex = directionIndex;
    if (directionIndex < tokens.size()
        && tokens[directionIndex].type != TokenType::LIMIT
        && tokens[directionIndex].type != TokenType::END_OF_INPUT
        && tokens[directionIndex].type != TokenType::SEMICOLON) {
        if (tokens[directionIndex].type == TokenType::DESC) {
            descending = true;
            nextIndex = directionIndex + 1;
        } else if (tokens[directionIndex].type == TokenType::ASC) {
            descending = false;
            nextIndex = directionIndex + 1;
        } else {
            if (error != nullptr) {
                *error = QStringLiteral("ORDER BY: expected ASC or DESC");
            }
            return false;
        }
    }
    if (nextIndex < tokens.size()
        && tokens[nextIndex].type != TokenType::LIMIT
        && tokens[nextIndex].type != TokenType::END_OF_INPUT
        && tokens[nextIndex].type != TokenType::SEMICOLON) {
        if (error != nullptr) {
            *error = QStringLiteral("ORDER BY: unsupported trailing token '%1'").arg(tokens[nextIndex].lexeme);
        }
        return false;
    }

    if (payload != nullptr) {
        payload->insert(QStringLiteral("orderByColumn"), orderByColumn);
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
        QVariantList projectionItems;
        QVariantList fromSources;
        QVariantList joins;
        QString table;
        QString tableAlias;

        int fromIdx = -1;
        for (int i = 1; i < tokens.size(); ++i) {
            if (tokens[i].type == TokenType::FROM) { fromIdx = i; break; }
            if (tokens[i].type == TokenType::END_OF_INPUT) break;
        }

        if (fromIdx < 0) {
            return {false, "SELECT: expected FROM table", cmdType, {}};
        }

        QString projectionError;
        if (!parseProjectionItems(tokens, 1, fromIdx - 1, &projection, &projectionItems, &projectionError)) {
            return {false, projectionError, cmdType, {}};
        }

        int whereIdx = -1;
        int orderIdx = -1;
        int limitIdx = -1;
        for (int i = fromIdx + 1; i < tokens.size(); ++i) {
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

        const int tableTailEnd = whereIdx >= 0
                                     ? whereIdx
                                     : (orderIdx >= 0
                                            ? orderIdx
                                            : (limitIdx >= 0 ? limitIdx : lastMeaningfulTokenIndex(tokens) + 1));

        bool isMultiTable = false;
        QString fromError;
        if (!parseFromClause(sql,
                             tokens,
                             fromIdx,
                             tableTailEnd,
                             &fromSources,
                             &joins,
                             &table,
                             &tableAlias,
                             &isMultiTable,
                             &fromError)) {
            return {false, fromError, cmdType, {}};
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
        const int limitParseStart = limitIdx >= 0
                                        ? limitIdx
                                        : ((orderIdx >= 0 || whereIdx >= 0) ? tokens.size() - 1 : tableTailEnd);
        if (!parseSelectLimit(tokens, limitParseStart, &limit, &limitError)) {
            return {false, limitError, cmdType, {}};
        }

        payload["selectAll"] = projectionIsSelectAll(projection);
        if (payload.value(QStringLiteral("selectAll")).toBool()) {
            projection.clear();
            projectionItems.clear();
        }
        payload["projection"] = projection;
        payload["projectionItems"] = projectionItems;
        payload["tableName"] = table;
        payload["tableAlias"] = tableAlias;
        payload["fromSources"] = fromSources;
        payload["joins"] = joins;
        payload["isMultiTable"] = isMultiTable;
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
