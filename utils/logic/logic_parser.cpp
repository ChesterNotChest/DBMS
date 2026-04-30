#include "logic_parser.h"

#include "../../utils/sql_parser/sql_parser.h"

#include <stdexcept>

namespace logic {

namespace {
QString captureParenthesizedText(const QString &expressionText, LogicParserState &state);
QString captureSubquerySqlTextInternal(const QString &expressionText, LogicParserState &state);

bool isComparisonOperatorToken(const LogicToken &token)
{
    return token.type == LogicTokenType::CompareOperator;
}

LogicCompareOperator compareOperatorFromToken(const LogicToken &token)
{
    const QString text = token.rawText.toUpper();
    if (text == QStringLiteral("=")) return LogicCompareOperator::Eq;
    if (text == QStringLiteral("!=") || text == QStringLiteral("<>")) return LogicCompareOperator::NotEq;
    if (text == QStringLiteral("<")) return LogicCompareOperator::Lt;
    if (text == QStringLiteral("<=")) return LogicCompareOperator::Lte;
    if (text == QStringLiteral(">")) return LogicCompareOperator::Gt;
    return LogicCompareOperator::Gte;
}

QString sliceByTokenPositions(const QString &expressionText,
                             const LogicToken &leftToken,
                             const LogicToken &rightToken)
{
    const int start = leftToken.position + leftToken.rawText.size();
    const int end = rightToken.position;
    if (start >= end || start < 0 || end > expressionText.size()) {
        return QString();
    }
    return expressionText.mid(start, end - start);
}

bool appendOuterName(QStringList *names, const QString &name)
{
    if (names == nullptr || names->contains(name)) {
        return true;
    }
    names->append(name);
    return true;
}

bool collectOuterNamesFromNode(const LogicNode &node,
                               const QString &allowedTablePrefix,
                               QStringList *names,
                               LogicError *error,
                               const QString &expressionText)
{
    for (const QString &referencedName : node.referencedOuterNames) {
        appendOuterName(names, referencedName);
    }

    if (node.type == LogicNodeType::ColumnRef) {
        if (node.reference.scope == LogicReferenceScope::Outer) {
            appendOuterName(names, node.reference.name);
        } else if (node.reference.name.contains(QLatin1Char('.'))) {
            if (!allowedTablePrefix.isEmpty() && node.reference.name.startsWith(allowedTablePrefix)) {
                return true;
            }
            if (error != nullptr) {
                error->message = QStringLiteral("only outer.xxx is allowed in correlated subqueries");
                const int position = expressionText.indexOf(node.reference.name);
                error->position = position >= 0 ? position : -1;
            }
            return false;
        }
    }

    for (const LogicNode &child : node.children) {
        if (!collectOuterNamesFromNode(child, allowedTablePrefix, names, error, expressionText)) {
            return false;
        }
    }
    return true;
}

QString captureParenthesizedText(const QString &expressionText, LogicParserState &state)
{
    if (isAtEnd(state) || peekToken(state).type != LogicTokenType::LeftParen) {
        return {};
    }

    const int startIndex = state.index;
    int depth = 0;
    for (int i = state.index; i < state.tokens.size(); ++i) {
        const LogicToken &token = state.tokens.at(i);
        if (token.type == LogicTokenType::LeftParen) {
            ++depth;
        } else if (token.type == LogicTokenType::RightParen) {
            --depth;
            if (depth == 0) {
                const QString captured = sliceByTokenPositions(expressionText,
                                                              state.tokens.at(startIndex),
                                                              token);
                state.index = i + 1;
                return captured;
            }
        }
    }

    return {};
}

bool tokenBeginsSubquery(const LogicParserState &state)
{
    return state.index + 1 < state.tokens.size()
           && state.tokens.at(state.index).type == LogicTokenType::LeftParen
           && state.tokens.at(state.index + 1).type == LogicTokenType::Keyword
           && state.tokens.at(state.index + 1).keywordType == LogicKeywordType::Select;
}

bool collectOuterNamesFromText(const QString &text, QStringList *names, LogicError *error)
{
    const sqlparser::ParseResult parsedSql = sqlparser::parseSql(text);
    if (!parsedSql.success) {
        if (error != nullptr) {
            error->message = parsedSql.errorMessage;
            error->position = -1;
        }
        return false;
    }
    if (parsedSql.commandType != QStringLiteral("SELECT")) {
        if (error != nullptr) {
            error->message = QStringLiteral("subquery must be SELECT");
            error->position = -1;
        }
        return false;
    }

    if (names != nullptr) {
        names->clear();
    }

    const QString allowedTablePrefix = parsedSql.payload.value(QStringLiteral("tableName")).toString().trimmed().isEmpty()
                                           ? QString()
                                           : parsedSql.payload.value(QStringLiteral("tableName")).toString().trimmed() + QLatin1Char('.');

    if (parsedSql.payload.contains(QStringLiteral("whereAst"))) {
        const LogicNode whereAst = parsedSql.payload.value(QStringLiteral("whereAst")).value<LogicNode>();
        if (!collectOuterNamesFromNode(whereAst, allowedTablePrefix, names, error, text)) {
            return false;
        }
    }

    return true;
}

LogicNode makeLiteralNode(const LogicToken &token)
{
    LogicNode node;
    node.type = LogicNodeType::Literal;
    node.rawText = token.rawText;
    if (token.type == LogicTokenType::NullLiteral) {
        node.literalIsNull = true;
    } else if (token.type == LogicTokenType::NumberLiteral) {
        node.literalType = token.rawText.contains(QLatin1Char('.'))
                               ? tabledef::ColumnType::Float
                               : tabledef::ColumnType::Int;
        node.literalValue = token.rawText;
    } else {
        node.literalValue = token.rawText;
    }
    return node;
}

LogicNode makeColumnNode(const LogicToken &token)
{
    LogicNode node;
    node.type = LogicNodeType::ColumnRef;
    node.rawText = token.rawText;
    if (token.rawText.startsWith(QStringLiteral("outer."))) {
        node.reference.scope = LogicReferenceScope::Outer;
        node.reference.name = token.rawText;
    } else if (token.rawText.contains(QLatin1Char('.'))) {
        node.reference.scope = LogicReferenceScope::Local;
        node.reference.name = token.rawText;
    } else {
        node.reference.scope = LogicReferenceScope::Local;
        node.reference.name = token.rawText;
    }
    return node;
}

bool parseLiteralOrReference(LogicParserState &state, LogicNode *node, LogicError *error)
{
    if (node == nullptr || isAtEnd(state)) {
        return false;
    }

    const LogicToken &token = peekToken(state);
    if (token.type == LogicTokenType::Identifier) {
        *node = makeColumnNode(token);
        ++state.index;
        return true;
    }

    if (token.type == LogicTokenType::NumberLiteral
        || token.type == LogicTokenType::StringLiteral
        || token.type == LogicTokenType::NullLiteral) {
        *node = makeLiteralNode(token);
        ++state.index;
        return true;
    }

    if (token.type == LogicTokenType::Keyword && token.keywordType == LogicKeywordType::Null) {
        *node = makeLiteralNode(token);
        node->literalIsNull = true;
        ++state.index;
        return true;
    }

    if (error != nullptr) {
        error->message = QStringLiteral("expected literal or column reference");
        error->position = token.position;
    }
    return false;
}

bool parseLiteralOnly(LogicParserState &state, LogicNode *node, LogicError *error)
{
    if (node == nullptr || isAtEnd(state)) {
        return false;
    }

    const LogicToken &token = peekToken(state);
    if (token.type == LogicTokenType::NumberLiteral
        || token.type == LogicTokenType::StringLiteral
        || token.type == LogicTokenType::NullLiteral
        || (token.type == LogicTokenType::Keyword && token.keywordType == LogicKeywordType::Null)) {
        *node = makeLiteralNode(token);
        if (token.type == LogicTokenType::Keyword && token.keywordType == LogicKeywordType::Null) {
            node->literalIsNull = true;
        }
        ++state.index;
        return true;
    }

    if (error != nullptr) {
        error->message = QStringLiteral("expected literal value");
        error->position = token.position;
    }
    return false;
}

LogicParseResult parseExpression(LogicParserState &state, const QString &expressionText);
LogicParseResult parseOrExpression(LogicParserState &state, const QString &expressionText);
LogicParseResult parseAndExpression(LogicParserState &state, const QString &expressionText);
LogicParseResult parseNotExpression(LogicParserState &state, const QString &expressionText);
LogicParseResult parsePredicateExpression(LogicParserState &state, const QString &expressionText);
LogicParseResult parsePrimaryExpression(LogicParserState &state, const QString &expressionText);

LogicParseResult parsePrimaryExpression(LogicParserState &state, const QString &expressionText)
{
    if (isAtEnd(state)) {
        return makeParseError(QStringLiteral("expected expression"), -1);
    }

    const LogicToken &token = peekToken(state);
    if (token.type == LogicTokenType::LeftParen) {
        if (tokenBeginsSubquery(state)) {
            LogicParseResult result;
            result.success = false;
            result.error.message = QStringLiteral("unexpected subquery parenthesis");
            result.error.position = token.position;
            return result;
        }

        ++state.index;
        LogicParseResult nested = parseExpression(state, expressionText);
        if (!nested.success) {
            return nested;
        }
        if (isAtEnd(state) || peekToken(state).type != LogicTokenType::RightParen) {
            return makeParseError(QStringLiteral("expected ')'"), token.position);
        }
        ++state.index;
        return nested;
    }

    if (token.type == LogicTokenType::Keyword && token.keywordType == LogicKeywordType::Exists) {
        ++state.index;
        if (isAtEnd(state) || peekToken(state).type != LogicTokenType::LeftParen) {
            return makeParseError(QStringLiteral("EXISTS requires subquery parentheses"), token.position);
        }
        const QString subquerySql = captureSubquerySqlTextInternal(expressionText, state);
        if (subquerySql.isEmpty()) {
            return makeParseError(QStringLiteral("EXISTS: invalid subquery"), token.position);
        }

        LogicNode node;
        node.type = LogicNodeType::ExistsSubquery;
        node.rawText = subquerySql;
        node.subquerySql = subquerySql;
        LogicError outerError;
        if (!collectOuterNamesFromText(node.subquerySql, &node.referencedOuterNames, &outerError)) {
            return makeParseError(outerError.message, outerError.position);
        }
        return {true, node, {}};
    }

    LogicNode node;
    if (!parseLiteralOrReference(state, &node, nullptr)) {
        return makeParseError(QStringLiteral("expected primary expression"), token.position);
    }
    return {true, node, {}};
}

LogicParseResult parsePredicateExpression(LogicParserState &state, const QString &expressionText)
{
    LogicParseResult leftResult = parsePrimaryExpression(state, expressionText);
    if (!leftResult.success) {
        return leftResult;
    }

    LogicNode lhs = leftResult.root;
    if (isAtEnd(state)) {
        return {true, lhs, {}};
    }

    const LogicToken &token = peekToken(state);
    if (token.type == LogicTokenType::Keyword && token.keywordType == LogicKeywordType::Is) {
        ++state.index;
        bool notNull = false;
        if (!isAtEnd(state) && peekToken(state).type == LogicTokenType::Keyword
            && peekToken(state).keywordType == LogicKeywordType::Not) {
            notNull = true;
            ++state.index;
        }
        if (isAtEnd(state) || peekToken(state).type != LogicTokenType::Keyword
            || peekToken(state).keywordType != LogicKeywordType::Null) {
            return makeParseError(QStringLiteral("IS requires NULL"), token.position);
        }
        ++state.index;

        LogicNode node;
        node.type = LogicNodeType::NullTest;
        node.isNotNullTest = notNull;
        node.children.append(lhs);
        node.rawText = token.rawText;
        return {true, node, {}};
    }

    if (token.type == LogicTokenType::Keyword && token.keywordType == LogicKeywordType::Not
        && state.index + 1 < state.tokens.size()
        && state.tokens.at(state.index + 1).type == LogicTokenType::Keyword
        && state.tokens.at(state.index + 1).keywordType == LogicKeywordType::In) {
        state.index += 2;
        if (isAtEnd(state) || peekToken(state).type != LogicTokenType::LeftParen) {
            return makeParseError(QStringLiteral("NOT IN requires parentheses"), token.position);
        }

        const int leftParenIndex = state.index;
        const QString setText = captureParenthesizedText(expressionText, state);
        const bool emptySet = state.index == leftParenIndex + 2
                              && leftParenIndex + 1 < state.tokens.size()
                              && state.tokens.at(leftParenIndex + 1).type == LogicTokenType::RightParen;
        if (setText.isEmpty() && !emptySet) {
            return makeParseError(QStringLiteral("NOT IN: invalid set expression"), token.position);
        }

        LogicNode node;
        node.negated = true;
        node.children.append(lhs);
        const QString innerText = setText.trimmed();
        if (innerText.startsWith(QStringLiteral("SELECT"), Qt::CaseInsensitive)) {
            node.type = LogicNodeType::InSubquery;
            node.subquerySql = setText;
            LogicError outerError;
            if (!collectOuterNamesFromText(node.subquerySql, &node.referencedOuterNames, &outerError)) {
                return makeParseError(outerError.message, outerError.position);
            }
            return {true, node, {}};
        }

        node.type = LogicNodeType::InList;
        const LogicTokenizeResult listTokenized = tokenizeLogicExpression(innerText);
        if (!listTokenized.success) {
            return makeParseError(listTokenized.error.message, listTokenized.error.position);
        }
        LogicParserState listState;
        listState.tokens = listTokenized.tokens;
        listState.index = 0;
        while (!isAtEnd(listState) && peekToken(listState).type != LogicTokenType::EndOfInput) {
            LogicNode valueNode;
            LogicError parseError;
            if (!parseLiteralOnly(listState, &valueNode, &parseError)) {
                return makeParseError(parseError.message, parseError.position);
            }
            node.children.append(valueNode);
            if (!isAtEnd(listState) && peekToken(listState).type == LogicTokenType::Comma) {
                ++listState.index;
                continue;
            }
            break;
        }
        return {true, node, {}};
    }

    if (isComparisonOperatorToken(token)) {
        const LogicCompareOperator op = compareOperatorFromToken(token);
        ++state.index;
        if (isAtEnd(state)) {
            return makeParseError(QStringLiteral("expected right-hand expression"), token.position);
        }

        if (peekToken(state).type == LogicTokenType::Keyword
            && (peekToken(state).keywordType == LogicKeywordType::Any
                || peekToken(state).keywordType == LogicKeywordType::All)) {
            const LogicKeywordType quantifierToken = peekToken(state).keywordType;
            ++state.index;
            if (isAtEnd(state) || peekToken(state).type != LogicTokenType::LeftParen) {
                return makeParseError(QStringLiteral("quantified comparison requires subquery parentheses"), token.position);
            }
            const QString subquerySql = captureSubquerySqlTextInternal(expressionText, state);
            if (subquerySql.isEmpty()) {
                return makeParseError(QStringLiteral("quantified comparison: invalid subquery"), token.position);
            }

            LogicNode node;
            node.type = LogicNodeType::QuantifiedSubquery;
            node.compareOperator = op;
            node.quantifier = quantifierToken == LogicKeywordType::Any ? LogicQuantifier::Any : LogicQuantifier::All;
            node.children.append(lhs);
            node.subquerySql = subquerySql;
            LogicError outerError;
            if (!collectOuterNamesFromText(node.subquerySql, &node.referencedOuterNames, &outerError)) {
                return makeParseError(outerError.message, outerError.position);
            }
            return {true, node, {}};
        }

        LogicNode rhs;
        LogicError parseError;
        if (!parseLiteralOrReference(state, &rhs, &parseError)) {
            return makeParseError(parseError.message, parseError.position);
        }

        LogicNode node;
        node.type = LogicNodeType::Comparison;
        node.compareOperator = op;
        node.children.append(lhs);
        node.children.append(rhs);
        return {true, node, {}};
    }

    if (token.type == LogicTokenType::Keyword && token.keywordType == LogicKeywordType::In) {
        ++state.index;
        if (isAtEnd(state) || peekToken(state).type != LogicTokenType::LeftParen) {
            return makeParseError(QStringLiteral("IN requires parentheses"), token.position);
        }

        const int leftParenIndex = state.index;
        const QString setText = captureParenthesizedText(expressionText, state);
        const bool emptySet = state.index == leftParenIndex + 2
                              && leftParenIndex + 1 < state.tokens.size()
                              && state.tokens.at(leftParenIndex + 1).type == LogicTokenType::RightParen;
        if (setText.isEmpty() && !emptySet) {
            return makeParseError(QStringLiteral("IN: invalid set expression"), token.position);
        }

        LogicNode node;
        node.children.append(lhs);
        const QString innerText = setText.trimmed();
        if (innerText.startsWith(QStringLiteral("SELECT"), Qt::CaseInsensitive)) {
            node.type = LogicNodeType::InSubquery;
            node.subquerySql = setText;
            LogicError outerError;
            if (!collectOuterNamesFromText(node.subquerySql, &node.referencedOuterNames, &outerError)) {
                return makeParseError(outerError.message, outerError.position);
            }
            return {true, node, {}};
        }

        node.type = LogicNodeType::InList;
        const LogicTokenizeResult listTokenized = tokenizeLogicExpression(innerText);
        if (!listTokenized.success) {
            return makeParseError(listTokenized.error.message, listTokenized.error.position);
        }
        LogicParserState listState;
        listState.tokens = listTokenized.tokens;
        listState.index = 0;
        while (!isAtEnd(listState) && peekToken(listState).type != LogicTokenType::EndOfInput) {
            LogicNode valueNode;
            LogicError parseError;
            if (!parseLiteralOnly(listState, &valueNode, &parseError)) {
                return makeParseError(parseError.message, parseError.position);
            }
            node.children.append(valueNode);
            if (!isAtEnd(listState) && peekToken(listState).type == LogicTokenType::Comma) {
                ++listState.index;
                continue;
            }
            break;
        }
        return {true, node, {}};
    }

    return {true, lhs, {}};
}

LogicParseResult parseNotExpression(LogicParserState &state, const QString &expressionText)
{
    if (!isAtEnd(state) && peekToken(state).type == LogicTokenType::Keyword
        && peekToken(state).keywordType == LogicKeywordType::Not) {
        const LogicToken token = peekToken(state);
        ++state.index;
        LogicParseResult child = parseNotExpression(state, expressionText);
        if (!child.success) {
            return child;
        }

        LogicNode node;
        node.type = LogicNodeType::Unary;
        node.unaryOperator = LogicUnaryOperator::Not;
        node.children.append(child.root);
        node.rawText = token.rawText;
        return {true, node, {}};
    }
    return parsePredicateExpression(state, expressionText);
}

LogicParseResult parseAndExpression(LogicParserState &state, const QString &expressionText)
{
    LogicParseResult leftResult = parseNotExpression(state, expressionText);
    if (!leftResult.success) {
        return leftResult;
    }

    LogicNode lhs = leftResult.root;
    while (!isAtEnd(state) && peekToken(state).type == LogicTokenType::Keyword
           && peekToken(state).keywordType == LogicKeywordType::And) {
        ++state.index;
        LogicParseResult rightResult = parseNotExpression(state, expressionText);
        if (!rightResult.success) {
            return rightResult;
        }

        LogicNode node;
        node.type = LogicNodeType::Binary;
        node.binaryOperator = LogicBinaryOperator::And;
        node.children.append(lhs);
        node.children.append(rightResult.root);
        lhs = node;
    }

    return {true, lhs, {}};
}

LogicParseResult parseOrExpression(LogicParserState &state, const QString &expressionText)
{
    LogicParseResult leftResult = parseAndExpression(state, expressionText);
    if (!leftResult.success) {
        return leftResult;
    }

    LogicNode lhs = leftResult.root;
    while (!isAtEnd(state) && peekToken(state).type == LogicTokenType::Keyword
           && peekToken(state).keywordType == LogicKeywordType::Or) {
        ++state.index;
        LogicParseResult rightResult = parseAndExpression(state, expressionText);
        if (!rightResult.success) {
            return rightResult;
        }

        LogicNode node;
        node.type = LogicNodeType::Binary;
        node.binaryOperator = LogicBinaryOperator::Or;
        node.children.append(lhs);
        node.children.append(rightResult.root);
        lhs = node;
    }

    return {true, lhs, {}};
}

LogicParseResult parseExpression(LogicParserState &state, const QString &expressionText)
{
    return parseOrExpression(state, expressionText);
}

QString captureSubquerySqlTextInternal(const QString &expressionText, LogicParserState &state)
{
    const int startIndex = state.index;
    const QString captured = captureParenthesizedText(expressionText, state);
    if (captured.isEmpty()) {
        return {};
    }
    const QString trimmed = captured.trimmed();
    if (!trimmed.startsWith(QStringLiteral("SELECT"), Qt::CaseInsensitive)) {
        state.index = startIndex;
        return {};
    }
    return captured;
}

} // namespace

const LogicToken &peekToken(const LogicParserState &state)
{
    return state.tokens.at(state.index);
}

bool isAtEnd(const LogicParserState &state)
{
    return state.index >= state.tokens.size()
           || state.tokens.at(state.index).type == LogicTokenType::EndOfInput;
}

bool matchToken(LogicParserState &state, LogicTokenType type)
{
    if (!isAtEnd(state) && peekToken(state).type == type) {
        ++state.index;
        return true;
    }
    return false;
}

LogicToken consumeToken(LogicParserState &state, LogicTokenType type, const QString &errorMessage)
{
    if (!isAtEnd(state) && peekToken(state).type == type) {
        return state.tokens.at(state.index++);
    }
    throw std::runtime_error(errorMessage.toStdString());
}

LogicParseResult makeParseError(const QString &message, int position)
{
    LogicParseResult result;
    result.success = false;
    result.error.message = message;
    result.error.position = position;
    return result;
}

LogicParseResult parseLogicTokens(const QString &expressionText, const QList<LogicToken> &tokens)
{
    ensureLogicMetaTypesRegistered();

    LogicParserState state;
    state.tokens = tokens;
    state.index = 0;

    if (state.tokens.isEmpty()) {
        return makeParseError(QStringLiteral("empty expression"), -1);
    }

    LogicParseResult result = parseExpression(state, expressionText);
    if (!result.success) {
        return result;
    }

    if (!isAtEnd(state) && peekToken(state).type != LogicTokenType::EndOfInput) {
        return makeParseError(QStringLiteral("unexpected trailing tokens"), peekToken(state).position);
    }

    return result;
}

} // namespace logic