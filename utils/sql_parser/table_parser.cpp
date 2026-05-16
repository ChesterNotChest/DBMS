#include "sql_parser.h"

namespace sqlparser {

namespace {

bool isEndToken(const SqlToken &token)
{
    return token.type == TokenType::END_OF_INPUT || token.type == TokenType::SEMICOLON;
}

QString upperLexeme(const QVector<SqlToken> &tokens, int index)
{
    return index >= 0 && index < tokens.size() ? tokens[index].lexeme.toUpper() : QString();
}

bool lexemeIs(const QVector<SqlToken> &tokens, int index, const QString &value)
{
    return upperLexeme(tokens, index) == value;
}

int findToken(const QVector<SqlToken> &tokens, TokenType type, int from = 0)
{
    for (int i = from; i < tokens.size(); ++i) {
        if (tokens[i].type == type) return i;
    }
    return -1;
}

int findMatchingParen(const QVector<SqlToken> &tokens, int left, int limit)
{
    int depth = 0;
    for (int i = left; i <= limit && i < tokens.size(); ++i) {
        if (tokens[i].type == TokenType::LPAREN) {
            ++depth;
        } else if (tokens[i].type == TokenType::RPAREN) {
            --depth;
            if (depth == 0) return i;
        }
    }
    return -1;
}

int lastMeaningfulTokenIndex(const QVector<SqlToken> &tokens)
{
    for (int i = tokens.size() - 1; i >= 0; --i) {
        if (tokens[i].type != TokenType::END_OF_INPUT && tokens[i].type != TokenType::SEMICOLON) {
            return i;
        }
    }
    return -1;
}

QList<QPair<int, int>> splitTopLevelSegments(const QVector<SqlToken> &tokens, int from, int to)
{
    QList<QPair<int, int>> segments;
    int start = from;
    int depth = 0;
    for (int i = from; i <= to; ++i) {
        if (tokens[i].type == TokenType::LPAREN) {
            ++depth;
        } else if (tokens[i].type == TokenType::RPAREN) {
            --depth;
        } else if (tokens[i].type == TokenType::COMMA && depth == 0) {
            if (start <= i - 1) segments.append({start, i - 1});
            start = i + 1;
        }
    }
    if (start <= to) segments.append({start, to});
    return segments;
}

QStringList identifierListInParens(const QVector<SqlToken> &tokens, int left, int right)
{
    QStringList names;
    for (int i = left + 1; i < right; ++i) {
        if (tokens[i].type == TokenType::IDENTIFIER) {
            names.append(tokens[i].lexeme);
        }
    }
    return names;
}

QString joinedLexemes(const QVector<SqlToken> &tokens, int from, int to)
{
    QStringList parts;
    for (int i = from; i <= to && i < tokens.size(); ++i) {
        if (tokens[i].type == TokenType::END_OF_INPUT) break;
        parts.append(tokens[i].lexeme);
    }
    return parts.join(QLatin1Char(' '));
}

QString parseForeignKeyAction(const QVector<SqlToken> &tokens, int *index, int end)
{
    if (index == nullptr || *index > end) return QStringLiteral("NO ACTION");

    if (lexemeIs(tokens, *index, QStringLiteral("CASCADE"))) {
        ++(*index);
        return QStringLiteral("CASCADE");
    }
    if (lexemeIs(tokens, *index, QStringLiteral("RESTRICT"))) {
        ++(*index);
        return QStringLiteral("RESTRICT");
    }
    if (lexemeIs(tokens, *index, QStringLiteral("NO"))
        && *index + 1 <= end
        && lexemeIs(tokens, *index + 1, QStringLiteral("ACTION"))) {
        *index += 2;
        return QStringLiteral("NO ACTION");
    }
    if (lexemeIs(tokens, *index, QStringLiteral("SET"))
        && *index + 1 <= end
        && lexemeIs(tokens, *index + 1, QStringLiteral("NULL"))) {
        *index += 2;
        return QStringLiteral("SET NULL");
    }
    if (lexemeIs(tokens, *index, QStringLiteral("SET"))
        && *index + 1 <= end
        && lexemeIs(tokens, *index + 1, QStringLiteral("DEFAULT"))) {
        *index += 2;
        return QStringLiteral("SET DEFAULT");
    }

    return QStringLiteral("NO ACTION");
}

bool parseForeignKeyActions(const QVector<SqlToken> &tokens,
                            int from,
                            int end,
                            QString *onDeleteAction,
                            QString *onUpdateAction)
{
    if (onDeleteAction != nullptr) *onDeleteAction = QStringLiteral("NO ACTION");
    if (onUpdateAction != nullptr) *onUpdateAction = QStringLiteral("NO ACTION");

    int i = from;
    while (i <= end) {
        if (lexemeIs(tokens, i, QStringLiteral("ON")) && i + 1 <= end) {
            if (lexemeIs(tokens, i + 1, QStringLiteral("DELETE"))) {
                i += 2;
                if (onDeleteAction != nullptr) *onDeleteAction = parseForeignKeyAction(tokens, &i, end);
                continue;
            }
            if (lexemeIs(tokens, i + 1, QStringLiteral("UPDATE"))) {
                i += 2;
                if (onUpdateAction != nullptr) *onUpdateAction = parseForeignKeyAction(tokens, &i, end);
                continue;
            }
        }
        ++i;
    }
    return true;
}

QVariantMap makeConstraintMap(const QString &name,
                              const QString &type,
                              const QStringList &columns,
                              const QString &checkClause = QString(),
                              const QString &referencedTable = QString(),
                              const QStringList &referencedColumns = {},
                              const QString &onDeleteAction = QStringLiteral("NO ACTION"),
                              const QString &onUpdateAction = QStringLiteral("NO ACTION"))
{
    QVariantMap constraint;
    constraint.insert(QStringLiteral("name"), name);
    constraint.insert(QStringLiteral("type"), type);
    constraint.insert(QStringLiteral("columns"), columns);
    constraint.insert(QStringLiteral("checkClause"), checkClause);
    constraint.insert(QStringLiteral("referencedTable"), referencedTable);
    constraint.insert(QStringLiteral("referencedColumns"), referencedColumns);
    constraint.insert(QStringLiteral("onDeleteAction"), onDeleteAction);
    constraint.insert(QStringLiteral("onUpdateAction"), onUpdateAction);
    return constraint;
}

bool parseColumnSegment(const QVector<SqlToken> &tokens,
                        int start,
                        int end,
                        QVariantMap *column,
                        QString *error)
{
    if (column == nullptr) return false;
    if (start > end || tokens[start].type != TokenType::IDENTIFIER) {
        if (error != nullptr) *error = QStringLiteral("CREATE TABLE: expected column name");
        return false;
    }

    QVariantMap result;
    result.insert(QStringLiteral("name"), tokens[start].lexeme);
    result.insert(QStringLiteral("type"), QStringLiteral("VARCHAR"));
    result.insert(QStringLiteral("length"), -1);
    result.insert(QStringLiteral("notNull"), false);
    result.insert(QStringLiteral("primaryKey"), false);
    result.insert(QStringLiteral("autoIncrement"), false);
    result.insert(QStringLiteral("unique"), false);
    result.insert(QStringLiteral("defaultValue"), QString());
    result.insert(QStringLiteral("checkClause"), QString());
    result.insert(QStringLiteral("referencesTable"), QString());
    result.insert(QStringLiteral("referencedColumns"), QStringList());
    result.insert(QStringLiteral("onDeleteAction"), QStringLiteral("NO ACTION"));
    result.insert(QStringLiteral("onUpdateAction"), QStringLiteral("NO ACTION"));

    int i = start + 1;
    if (i <= end) {
        result.insert(QStringLiteral("type"), tokens[i].lexeme.toUpper());
        ++i;
        if (i <= end && tokens[i].type == TokenType::LPAREN) {
            const int right = findMatchingParen(tokens, i, end);
            if (right < 0) {
                if (error != nullptr) *error = QStringLiteral("CREATE TABLE: unmatched type length parenthesis");
                return false;
            }
            if (i + 1 < right && tokens[i + 1].type == TokenType::INTEGER_LIT) {
                result.insert(QStringLiteral("length"), tokens[i + 1].lexeme.toInt());
            }
            i = right + 1;
        }
    }

    while (i <= end) {
        if (lexemeIs(tokens, i, QStringLiteral("PRIMARY"))) {
            result.insert(QStringLiteral("primaryKey"), true);
            i += (i + 1 <= end && lexemeIs(tokens, i + 1, QStringLiteral("KEY"))) ? 2 : 1;
        } else if (lexemeIs(tokens, i, QStringLiteral("NOT"))
                   && i + 1 <= end
                   && lexemeIs(tokens, i + 1, QStringLiteral("NULL"))) {
            result.insert(QStringLiteral("notNull"), true);
            i += 2;
        } else if (lexemeIs(tokens, i, QStringLiteral("UNIQUE"))) {
            result.insert(QStringLiteral("unique"), true);
            ++i;
        } else if (lexemeIs(tokens, i, QStringLiteral("AUTO_INCREMENT"))) {
            result.insert(QStringLiteral("autoIncrement"), true);
            ++i;
        } else if (lexemeIs(tokens, i, QStringLiteral("DEFAULT")) && i + 1 <= end) {
            result.insert(QStringLiteral("defaultValue"), tokens[i + 1].lexeme);
            i += 2;
        } else if (lexemeIs(tokens, i, QStringLiteral("CHECK")) && i + 1 <= end && tokens[i + 1].type == TokenType::LPAREN) {
            const int right = findMatchingParen(tokens, i + 1, end);
            if (right < 0) {
                if (error != nullptr) *error = QStringLiteral("CREATE TABLE: unmatched CHECK parenthesis");
                return false;
            }
            result.insert(QStringLiteral("checkClause"), joinedLexemes(tokens, i + 2, right - 1));
            i = right + 1;
        } else if (lexemeIs(tokens, i, QStringLiteral("REFERENCES")) && i + 1 <= end) {
            result.insert(QStringLiteral("referencesTable"), tokens[i + 1].lexeme);
            i += 2;
            if (i <= end && tokens[i].type == TokenType::LPAREN) {
                const int right = findMatchingParen(tokens, i, end);
                if (right < 0) {
                    if (error != nullptr) *error = QStringLiteral("CREATE TABLE: unmatched REFERENCES parenthesis");
                    return false;
                }
                result.insert(QStringLiteral("referencedColumns"), identifierListInParens(tokens, i, right));
                i = right + 1;
            }
            QString onDeleteAction;
            QString onUpdateAction;
            parseForeignKeyActions(tokens, i, end, &onDeleteAction, &onUpdateAction);
            result.insert(QStringLiteral("onDeleteAction"), onDeleteAction);
            result.insert(QStringLiteral("onUpdateAction"), onUpdateAction);
            break;
        } else {
            if (error != nullptr) *error = QStringLiteral("CREATE TABLE: unsupported column token '%1'").arg(tokens[i].lexeme);
            return false;
        }
    }

    *column = result;
    return true;
}

bool parseTableConstraintSegment(const QVector<SqlToken> &tokens,
                                 int start,
                                 int end,
                                 QVariantMap *constraint,
                                 QString *error)
{
    if (constraint == nullptr) return false;

    QString constraintName;
    int i = start;
    if (lexemeIs(tokens, i, QStringLiteral("CONSTRAINT"))) {
        if (i + 1 > end || tokens[i + 1].type != TokenType::IDENTIFIER) {
            if (error != nullptr) *error = QStringLiteral("CREATE TABLE: expected constraint name");
            return false;
        }
        constraintName = tokens[i + 1].lexeme;
        i += 2;
    }

    if (lexemeIs(tokens, i, QStringLiteral("PRIMARY"))) {
        if (i + 1 <= end && lexemeIs(tokens, i + 1, QStringLiteral("KEY"))) ++i;
        const int left = findToken(tokens, TokenType::LPAREN, i);
        const int right = left >= 0 ? findMatchingParen(tokens, left, end) : -1;
        if (left < 0 || right < 0) {
            if (error != nullptr) *error = QStringLiteral("CREATE TABLE: invalid PRIMARY KEY constraint");
            return false;
        }
        *constraint = makeConstraintMap(constraintName, QStringLiteral("PRIMARY_KEY"), identifierListInParens(tokens, left, right));
        return true;
    }

    if (lexemeIs(tokens, i, QStringLiteral("UNIQUE"))) {
        const int left = findToken(tokens, TokenType::LPAREN, i);
        const int right = left >= 0 ? findMatchingParen(tokens, left, end) : -1;
        if (left < 0 || right < 0) {
            if (error != nullptr) *error = QStringLiteral("CREATE TABLE: invalid UNIQUE constraint");
            return false;
        }
        *constraint = makeConstraintMap(constraintName, QStringLiteral("UNIQUE"), identifierListInParens(tokens, left, right));
        return true;
    }

    if (lexemeIs(tokens, i, QStringLiteral("CHECK"))) {
        const int left = findToken(tokens, TokenType::LPAREN, i);
        const int right = left >= 0 ? findMatchingParen(tokens, left, end) : -1;
        if (left < 0 || right < 0) {
            if (error != nullptr) *error = QStringLiteral("CREATE TABLE: invalid CHECK constraint");
            return false;
        }
        *constraint = makeConstraintMap(constraintName,
                                        QStringLiteral("CHECK"),
                                        {},
                                        joinedLexemes(tokens, left + 1, right - 1));
        return true;
    }

    if (lexemeIs(tokens, i, QStringLiteral("FOREIGN"))) {
        if (i + 1 <= end && lexemeIs(tokens, i + 1, QStringLiteral("KEY"))) ++i;
        const int localLeft = findToken(tokens, TokenType::LPAREN, i);
        const int localRight = localLeft >= 0 ? findMatchingParen(tokens, localLeft, end) : -1;
        if (localLeft < 0 || localRight < 0) {
            if (error != nullptr) *error = QStringLiteral("CREATE TABLE: invalid FOREIGN KEY constraint");
            return false;
        }

        int referencesIndex = -1;
        for (int j = localRight + 1; j <= end; ++j) {
            if (lexemeIs(tokens, j, QStringLiteral("REFERENCES"))) {
                referencesIndex = j;
                break;
            }
        }
        if (referencesIndex < 0 || referencesIndex + 1 > end) {
            if (error != nullptr) *error = QStringLiteral("CREATE TABLE: FOREIGN KEY missing REFERENCES target");
            return false;
        }

        const QString referencedTable = tokens[referencesIndex + 1].lexeme;
        const int referencedLeft = findToken(tokens, TokenType::LPAREN, referencesIndex + 1);
        const int referencedRight = referencedLeft >= 0 ? findMatchingParen(tokens, referencedLeft, end) : -1;
        if (referencedLeft < 0 || referencedRight < 0) {
            if (error != nullptr) *error = QStringLiteral("CREATE TABLE: invalid REFERENCES column list");
            return false;
        }

        QString onDeleteAction;
        QString onUpdateAction;
        parseForeignKeyActions(tokens, referencedRight + 1, end, &onDeleteAction, &onUpdateAction);
        *constraint = makeConstraintMap(constraintName,
                                        QStringLiteral("FOREIGN_KEY"),
                                        identifierListInParens(tokens, localLeft, localRight),
                                        QString(),
                                        referencedTable,
                                        identifierListInParens(tokens, referencedLeft, referencedRight),
                                        onDeleteAction,
                                        onUpdateAction);
        return true;
    }

    if (error != nullptr) *error = QStringLiteral("CREATE TABLE: unsupported table constraint");
    return false;
}

bool startsTableConstraint(const QVector<SqlToken> &tokens, int index)
{
    return lexemeIs(tokens, index, QStringLiteral("CONSTRAINT"))
           || lexemeIs(tokens, index, QStringLiteral("PRIMARY"))
           || lexemeIs(tokens, index, QStringLiteral("UNIQUE"))
           || lexemeIs(tokens, index, QStringLiteral("CHECK"))
           || lexemeIs(tokens, index, QStringLiteral("FOREIGN"));
}

} // namespace

ParseResult parseTableSql(const QString& sql, const QVector<SqlToken>& tokens)
{
    if (tokens.isEmpty()) return {false, "Empty input", "UNKNOWN", {}};

    auto [cmdType, payload] = classifySql(sql, tokens);

    if (cmdType == "CREATE_TABLE") {
        int tableIdx = -1;
        for (int i = 1; i < tokens.size(); ++i) {
            if (tokens[i].type == TokenType::IDENTIFIER) {
                tableIdx = i;
                break;
            }
        }
        if (tableIdx < 0) return {false, "CREATE TABLE: expected table name", cmdType, {}};

        const int left = findToken(tokens, TokenType::LPAREN, tableIdx);
        if (left < 0) return {false, "CREATE TABLE: expected '('", cmdType, {}};

        const int right = findMatchingParen(tokens, left, tokens.size() - 1);
        if (right < 0) return {false, "CREATE TABLE: unmatched parenthesis", cmdType, {}};

        QVariantList columns;
        QVariantList constraints;
        const QList<QPair<int, int>> segments = splitTopLevelSegments(tokens, left + 1, right - 1);
        for (const auto &segment : segments) {
            if (segment.first > segment.second) continue;
            QString error;
            if (startsTableConstraint(tokens, segment.first)) {
                QVariantMap constraint;
                if (!parseTableConstraintSegment(tokens, segment.first, segment.second, &constraint, &error)) {
                    return {false, error, cmdType, {}};
                }
                constraints.append(constraint);
            } else {
                QVariantMap column;
                if (!parseColumnSegment(tokens, segment.first, segment.second, &column, &error)) {
                    return {false, error, cmdType, {}};
                }
                columns.append(column);
            }
        }

        payload.insert(QStringLiteral("tableName"), tokens[tableIdx].lexeme);
        payload.insert(QStringLiteral("columns"), columns);
        payload.insert(QStringLiteral("constraints"), constraints);
        return {true, "", cmdType, payload};
    }

    if (cmdType == "DROP_TABLE") {
        QString tableName = extractTableName(tokens);
        if (tableName.isEmpty()) return {false, "DROP TABLE: expected table name", cmdType, {}};
        payload.insert(QStringLiteral("tableName"), tableName);
        return {true, "", cmdType, payload};
    }

    if (cmdType == "ALTER_TABLE") {
        QString tableName = extractTableName(tokens);
        if (tableName.isEmpty()) return {false, "ALTER TABLE: expected table name", cmdType, {}};

        const int end = lastMeaningfulTokenIndex(tokens);
        if (end < 0) return {false, "ALTER TABLE: unsupported syntax", cmdType, {}};

        int actionSearchStart = 0;
        for (int i = 0; i <= end; ++i) {
            if (lexemeIs(tokens, i, QStringLiteral("TABLE"))) {
                actionSearchStart = i + 2;
                break;
            }
        }

        int actionIndex = -1;
        for (int i = actionSearchStart; i <= end; ++i) {
            if (lexemeIs(tokens, i, QStringLiteral("ADD"))
                || lexemeIs(tokens, i, QStringLiteral("ALTER"))
                || lexemeIs(tokens, i, QStringLiteral("MODIFY"))
                || lexemeIs(tokens, i, QStringLiteral("RENAME"))
                || lexemeIs(tokens, i, QStringLiteral("DROP"))) {
                actionIndex = i;
                break;
            }
        }
        if (actionIndex < 0 || actionIndex + 1 > end) {
            return {false, "ALTER TABLE: unsupported syntax", cmdType, {}};
        }

        payload.insert(QStringLiteral("tableName"), tableName);

        if (lexemeIs(tokens, actionIndex, QStringLiteral("ADD"))) {
            if (lexemeIs(tokens, actionIndex + 1, QStringLiteral("COLUMN"))
                || tokens[actionIndex + 1].type == TokenType::IDENTIFIER) {
                const int columnStart = lexemeIs(tokens, actionIndex + 1, QStringLiteral("COLUMN"))
                                            ? actionIndex + 2
                                            : actionIndex + 1;
                QVariantMap column;
                QString error;
                if (!parseColumnSegment(tokens, columnStart, end, &column, &error)) {
                    return {false, error, cmdType, {}};
                }
                payload.insert(QStringLiteral("alterAction"), QStringLiteral("ADD_COLUMN"));
                payload.insert(QStringLiteral("column"), column);
                return {true, "", cmdType, payload};
            }

            QVariantMap constraint;
            QString error;
            if (!parseTableConstraintSegment(tokens, actionIndex + 1, end, &constraint, &error)) {
                return {false, error, cmdType, {}};
            }
            payload.insert(QStringLiteral("alterAction"), QStringLiteral("ADD_CONSTRAINT"));
            payload.insert(QStringLiteral("constraint"), constraint);
            return {true, "", cmdType, payload};
        }

        if (lexemeIs(tokens, actionIndex, QStringLiteral("ALTER"))) {
            if (!lexemeIs(tokens, actionIndex + 1, QStringLiteral("COLUMN"))
                || actionIndex + 2 > end
                || tokens[actionIndex + 2].type != TokenType::IDENTIFIER) {
                return {false, "ALTER TABLE ALTER COLUMN: expected column name", cmdType, {}};
            }

            const QString columnName = tokens[actionIndex + 2].lexeme;
            const int operationIndex = actionIndex + 3;
            if (operationIndex > end) {
                return {false, "ALTER TABLE ALTER COLUMN: expected operation", cmdType, {}};
            }

            if (lexemeIs(tokens, operationIndex, QStringLiteral("SET"))) {
                if (operationIndex + 1 > end) {
                    return {false, "ALTER TABLE ALTER COLUMN SET: expected operation", cmdType, {}};
                }
                if (lexemeIs(tokens, operationIndex + 1, QStringLiteral("DEFAULT"))) {
                    if (operationIndex + 2 > end) {
                        return {false, "ALTER TABLE ALTER COLUMN SET DEFAULT: expected value", cmdType, {}};
                    }
                    if (operationIndex + 3 <= end) {
                        return {false, "ALTER TABLE ALTER COLUMN SET DEFAULT: unsupported trailing syntax", cmdType, {}};
                    }
                    payload.insert(QStringLiteral("alterAction"), QStringLiteral("ALTER_COLUMN_SET_DEFAULT"));
                    payload.insert(QStringLiteral("columnName"), columnName);
                    payload.insert(QStringLiteral("defaultValue"), tokens[operationIndex + 2].lexeme);
                    return {true, "", cmdType, payload};
                }
                if (lexemeIs(tokens, operationIndex + 1, QStringLiteral("NOT"))
                    && operationIndex + 2 <= end
                    && lexemeIs(tokens, operationIndex + 2, QStringLiteral("NULL"))) {
                    if (operationIndex + 3 <= end) {
                        return {false, "ALTER TABLE ALTER COLUMN SET NOT NULL: unsupported trailing syntax", cmdType, {}};
                    }
                    payload.insert(QStringLiteral("alterAction"), QStringLiteral("ALTER_COLUMN_SET_NOT_NULL"));
                    payload.insert(QStringLiteral("columnName"), columnName);
                    return {true, "", cmdType, payload};
                }
                return {false, "ALTER TABLE ALTER COLUMN SET: unsupported operation", cmdType, {}};
            }

            if (lexemeIs(tokens, operationIndex, QStringLiteral("DROP"))) {
                if (operationIndex + 1 > end) {
                    return {false, "ALTER TABLE ALTER COLUMN DROP: expected operation", cmdType, {}};
                }
                if (lexemeIs(tokens, operationIndex + 1, QStringLiteral("DEFAULT"))) {
                    if (operationIndex + 2 <= end) {
                        return {false, "ALTER TABLE ALTER COLUMN DROP DEFAULT: unsupported trailing syntax", cmdType, {}};
                    }
                    payload.insert(QStringLiteral("alterAction"), QStringLiteral("ALTER_COLUMN_DROP_DEFAULT"));
                    payload.insert(QStringLiteral("columnName"), columnName);
                    return {true, "", cmdType, payload};
                }
                if (lexemeIs(tokens, operationIndex + 1, QStringLiteral("NOT"))
                    && operationIndex + 2 <= end
                    && lexemeIs(tokens, operationIndex + 2, QStringLiteral("NULL"))) {
                    if (operationIndex + 3 <= end) {
                        return {false, "ALTER TABLE ALTER COLUMN DROP NOT NULL: unsupported trailing syntax", cmdType, {}};
                    }
                    payload.insert(QStringLiteral("alterAction"), QStringLiteral("ALTER_COLUMN_DROP_NOT_NULL"));
                    payload.insert(QStringLiteral("columnName"), columnName);
                    return {true, "", cmdType, payload};
                }
                return {false, "ALTER TABLE ALTER COLUMN DROP: unsupported operation", cmdType, {}};
            }

            if (lexemeIs(tokens, operationIndex, QStringLiteral("TYPE"))) {
                if (operationIndex + 1 > end) {
                    return {false, "ALTER TABLE ALTER COLUMN TYPE: expected type", cmdType, {}};
                }
                payload.insert(QStringLiteral("alterAction"), QStringLiteral("ALTER_COLUMN_SET_TYPE"));
                payload.insert(QStringLiteral("columnName"), columnName);
                payload.insert(QStringLiteral("type"), tokens[operationIndex + 1].lexeme.toUpper());
                if (operationIndex + 2 <= end && tokens[operationIndex + 2].type == TokenType::LPAREN) {
                    const int right = findMatchingParen(tokens, operationIndex + 2, end);
                    if (right < 0) {
                        return {false, "ALTER TABLE ALTER COLUMN TYPE: unmatched type length parenthesis", cmdType, {}};
                    }
                    if (right < end) {
                        return {false, "ALTER TABLE ALTER COLUMN TYPE: unsupported trailing syntax", cmdType, {}};
                    }
                    if (operationIndex + 3 < right && tokens[operationIndex + 3].type == TokenType::INTEGER_LIT) {
                        payload.insert(QStringLiteral("length"), tokens[operationIndex + 3].lexeme.toInt());
                    }
                } else if (operationIndex + 2 <= end) {
                    return {false, "ALTER TABLE ALTER COLUMN TYPE: unsupported trailing syntax", cmdType, {}};
                }
                return {true, "", cmdType, payload};
            }

            return {false, "ALTER TABLE ALTER COLUMN: unsupported operation", cmdType, {}};
        }

        if (lexemeIs(tokens, actionIndex, QStringLiteral("MODIFY"))) {
            if (lexemeIs(tokens, actionIndex + 1, QStringLiteral("COLUMN"))
                || tokens[actionIndex + 1].type == TokenType::IDENTIFIER) {
                const int columnStart = lexemeIs(tokens, actionIndex + 1, QStringLiteral("COLUMN"))
                                            ? actionIndex + 2
                                            : actionIndex + 1;
                QVariantMap column;
                QString error;
                if (!parseColumnSegment(tokens, columnStart, end, &column, &error)) {
                    return {false, error, cmdType, {}};
                }
                payload.insert(QStringLiteral("alterAction"), QStringLiteral("MODIFY_COLUMN"));
                payload.insert(QStringLiteral("column"), column);
                return {true, "", cmdType, payload};
            }

            if (!lexemeIs(tokens, actionIndex + 1, QStringLiteral("CONSTRAINT"))
                || actionIndex + 2 > end
                || tokens[actionIndex + 2].type != TokenType::IDENTIFIER) {
                return {false, "ALTER TABLE MODIFY CONSTRAINT: expected constraint name", cmdType, {}};
            }

            const QString constraintName = tokens[actionIndex + 2].lexeme;
            QVariantMap constraint;
            QString error;
            if (!parseTableConstraintSegment(tokens, actionIndex + 3, end, &constraint, &error)) {
                return {false, error, cmdType, {}};
            }
            payload.insert(QStringLiteral("alterAction"), QStringLiteral("MODIFY_CONSTRAINT"));
            payload.insert(QStringLiteral("constraintName"), constraintName);
            payload.insert(QStringLiteral("constraint"), constraint);
            return {true, "", cmdType, payload};
        }

        if (lexemeIs(tokens, actionIndex, QStringLiteral("RENAME"))) {
            if (!lexemeIs(tokens, actionIndex + 1, QStringLiteral("COLUMN"))
                || actionIndex + 4 > end
                || tokens[actionIndex + 2].type != TokenType::IDENTIFIER
                || !lexemeIs(tokens, actionIndex + 3, QStringLiteral("TO"))
                || tokens[actionIndex + 4].type != TokenType::IDENTIFIER) {
                return {false, "ALTER TABLE RENAME COLUMN: expected old column name TO new column name", cmdType, {}};
            }
            if (actionIndex + 5 <= end) {
                return {false, "ALTER TABLE RENAME COLUMN: unsupported trailing syntax", cmdType, {}};
            }
            payload.insert(QStringLiteral("alterAction"), QStringLiteral("RENAME_COLUMN"));
            payload.insert(QStringLiteral("columnName"), tokens[actionIndex + 2].lexeme);
            payload.insert(QStringLiteral("newColumnName"), tokens[actionIndex + 4].lexeme);
            return {true, "", cmdType, payload};
        }

        if (lexemeIs(tokens, actionIndex, QStringLiteral("DROP"))) {
            if (lexemeIs(tokens, actionIndex + 1, QStringLiteral("COLUMN"))) {
                if (actionIndex + 3 <= end) {
                    return {false, "ALTER TABLE DROP COLUMN: unsupported trailing syntax", cmdType, {}};
                }
                payload.insert(QStringLiteral("alterAction"), QStringLiteral("DROP_COLUMN"));
                if (actionIndex + 2 < tokens.size()) payload.insert(QStringLiteral("columnName"), tokens[actionIndex + 2].lexeme);
                return {true, "", cmdType, payload};
            }
            if (lexemeIs(tokens, actionIndex + 1, QStringLiteral("CONSTRAINT"))) {
                if (actionIndex + 3 <= end) {
                    return {false, "ALTER TABLE DROP CONSTRAINT: unsupported trailing syntax", cmdType, {}};
                }
                payload.insert(QStringLiteral("alterAction"), QStringLiteral("DROP_CONSTRAINT"));
                if (actionIndex + 2 < tokens.size()) payload.insert(QStringLiteral("constraintName"), tokens[actionIndex + 2].lexeme);
                return {true, "", cmdType, payload};
            }
        }
        return {false, "ALTER TABLE: unsupported syntax", cmdType, {}};
    }

    if (cmdType == "CREATE_INDEX") {
        int indexToken = -1;
        for (int i = 0; i < tokens.size(); ++i) {
            if (lexemeIs(tokens, i, QStringLiteral("INDEX"))) {
                indexToken = i;
                break;
            }
        }
        if (indexToken < 0 || indexToken + 1 >= tokens.size() || tokens[indexToken + 1].type != TokenType::IDENTIFIER) {
            return {false, "CREATE INDEX: expected index name", cmdType, {}};
        }

        int onIndex = -1;
        for (int i = indexToken + 2; i < tokens.size(); ++i) {
            if (lexemeIs(tokens, i, QStringLiteral("ON"))) {
                onIndex = i;
                break;
            }
        }
        if (onIndex < 0 || onIndex + 1 >= tokens.size() || tokens[onIndex + 1].type != TokenType::IDENTIFIER) {
            return {false, "CREATE INDEX: expected table name", cmdType, {}};
        }

        const int left = findToken(tokens, TokenType::LPAREN, onIndex + 1);
        const int right = left >= 0 ? findMatchingParen(tokens, left, tokens.size() - 1) : -1;
        if (left < 0 || right < 0) {
            return {false, "CREATE INDEX: expected column list", cmdType, {}};
        }

        payload.insert(QStringLiteral("indexName"), tokens[indexToken + 1].lexeme);
        payload.insert(QStringLiteral("tableName"), tokens[onIndex + 1].lexeme);
        payload.insert(QStringLiteral("columnNames"), identifierListInParens(tokens, left, right));
        payload.insert(QStringLiteral("isUnique"), lexemeIs(tokens, 1, QStringLiteral("UNIQUE")));
        return {true, "", cmdType, payload};
    }

    if (cmdType == "DROP_INDEX") {
        if (tokens.size() < 3 || tokens[2].type != TokenType::IDENTIFIER) {
            return {false, "DROP INDEX: expected index name", cmdType, {}};
        }

        int onIndex = -1;
        for (int i = 3; i < tokens.size(); ++i) {
            if (lexemeIs(tokens, i, QStringLiteral("ON"))) {
                onIndex = i;
                break;
            }
        }
        if (onIndex < 0 || onIndex + 1 >= tokens.size() || tokens[onIndex + 1].type != TokenType::IDENTIFIER) {
            return {false, "DROP INDEX: expected table name", cmdType, {}};
        }

        payload.insert(QStringLiteral("indexName"), tokens[2].lexeme);
        payload.insert(QStringLiteral("tableName"), tokens[onIndex + 1].lexeme);
        return {true, "", cmdType, payload};
    }

    if (cmdType == "SHOW_TABLES") {
        return {true, "", cmdType, {}};
    }

    if (cmdType == "DESC_TABLE") {
        QString tableName;
        for (int i = 1; i < tokens.size(); ++i) {
            if (tokens[i].type == TokenType::IDENTIFIER) {
                tableName = tokens[i].lexeme;
                break;
            }
        }
        if (tableName.isEmpty()) return {false, "DESC: expected table name", cmdType, {}};
        payload.insert(QStringLiteral("tableName"), tableName);
        return {true, "", cmdType, payload};
    }

    if (cmdType == "SHOW_CREATE_TABLE") {
        QString tableName;
        for (int i = 3; i < tokens.size(); ++i) {
            if (tokens[i].type == TokenType::IDENTIFIER) {
                tableName = tokens[i].lexeme;
                break;
            }
        }
        if (tableName.isEmpty()) return {false, "SHOW CREATE TABLE: expected table name", cmdType, {}};
        payload.insert(QStringLiteral("tableName"), tableName);
        return {true, "", cmdType, payload};
    }

    return {false, "Not a table-level SQL statement", "UNKNOWN", {}};
}

QString extractTableName(const QVector<SqlToken>& tokens)
{
    for (int i = 1; i < tokens.size(); ++i) {
        if (tokens[i].type == TokenType::IDENTIFIER) return tokens[i].lexeme;
        if (tokens[i].type == TokenType::STAR) break;
        if (tokens[i].type == TokenType::FROM) break;
        if (isEndToken(tokens[i])) break;
    }
    return {};
}

} // namespace sqlparser
