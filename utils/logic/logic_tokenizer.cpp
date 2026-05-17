#include "logic_tokenizer.h"

namespace logic {

namespace {

bool isIdentStart(QChar ch)
{
    return ch.isLetter() || ch == QLatin1Char('_');
}

bool isIdentChar(QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_');
}

LogicKeywordType keywordTypeForWord(const QString &word)
{
    const QString upper = word.toUpper();
    if (upper == QStringLiteral("AND")) return LogicKeywordType::And;
    if (upper == QStringLiteral("OR")) return LogicKeywordType::Or;
    if (upper == QStringLiteral("NOT")) return LogicKeywordType::Not;
    if (upper == QStringLiteral("IN")) return LogicKeywordType::In;
    if (upper == QStringLiteral("EXISTS")) return LogicKeywordType::Exists;
    if (upper == QStringLiteral("SELECT")) return LogicKeywordType::Select;
    if (upper == QStringLiteral("ANY")) return LogicKeywordType::Any;
    if (upper == QStringLiteral("ALL")) return LogicKeywordType::All;
    if (upper == QStringLiteral("IS")) return LogicKeywordType::Is;
    if (upper == QStringLiteral("LIKE")) return LogicKeywordType::Like;
    if (upper == QStringLiteral("NULL")) return LogicKeywordType::Null;
    return LogicKeywordType::None;
}

bool isKeywordWord(const QString &word)
{
    return keywordTypeForWord(word) != LogicKeywordType::None;
}

} // namespace

LogicTokenizeResult tokenizeLogicExpression(const QString &expressionText)
{
    ensureLogicMetaTypesRegistered();

    LogicTokenizeResult result;
    int index = 0;
    const int length = expressionText.size();

    auto appendToken = [&](LogicTokenType type,
                           const QString &rawText,
                           int position,
                           LogicKeywordType keywordType = LogicKeywordType::None) {
        result.tokens.append(LogicToken{type, rawText, position, keywordType});
    };

    while (index < length) {
        const QChar ch = expressionText.at(index);
        if (ch.isSpace()) {
            ++index;
            continue;
        }

        const int start = index;
        if (ch == QLatin1Char('\'')) {
            ++index;
            QString value;
            bool closed = false;
            while (index < length) {
                const QChar current = expressionText.at(index);
                if (current == QLatin1Char('\'')) {
                    if (index + 1 < length && expressionText.at(index + 1) == QLatin1Char('\'')) {
                        value.append(QLatin1Char('\''));
                        index += 2;
                        continue;
                    }
                    ++index;
                    closed = true;
                    break;
                }
                value.append(current);
                ++index;
            }
            if (!closed) {
                result.error.message = QStringLiteral("unterminated string literal");
                result.error.position = start;
                return result;
            }
            appendToken(LogicTokenType::StringLiteral, value, start);
            continue;
        }

        if (ch.isDigit()) {
            QString number;
            bool seenDot = false;
            while (index < length) {
                const QChar current = expressionText.at(index);
                if (current.isDigit()) {
                    number.append(current);
                    ++index;
                    continue;
                }
                if (current == QLatin1Char('.') && !seenDot && index + 1 < length && expressionText.at(index + 1).isDigit()) {
                    seenDot = true;
                    number.append(current);
                    ++index;
                    continue;
                }
                break;
            }
            appendToken(LogicTokenType::NumberLiteral, number, start);
            continue;
        }

        if (isIdentStart(ch)) {
            QString word;
            while (index < length) {
                const QChar current = expressionText.at(index);
                if (isIdentChar(current)) {
                    word.append(current);
                    ++index;
                    continue;
                }
                if (current == QLatin1Char('.')
                    && index + 1 < length
                    && isIdentChar(expressionText.at(index + 1))) {
                    word.append(current);
                    ++index;
                    continue;
                }
                break;
            }

            const LogicKeywordType keywordType = keywordTypeForWord(word);
            if (keywordType != LogicKeywordType::None) {
                appendToken(LogicTokenType::Keyword, word, start, keywordType);
            } else {
                appendToken(LogicTokenType::Identifier, word, start);
            }
            continue;
        }

        if (ch == QLatin1Char('(')) {
            appendToken(LogicTokenType::LeftParen, QStringLiteral("("), start);
            ++index;
            continue;
        }
        if (ch == QLatin1Char(')')) {
            appendToken(LogicTokenType::RightParen, QStringLiteral(")"), start);
            ++index;
            continue;
        }
        if (ch == QLatin1Char(',')) {
            appendToken(LogicTokenType::Comma, QStringLiteral(","), start);
            ++index;
            continue;
        }
        if (ch == QLatin1Char('*')) {
            appendToken(LogicTokenType::Asterisk, QStringLiteral("*"), start);
            ++index;
            continue;
        }
        if (ch == QLatin1Char('=')) {
            appendToken(LogicTokenType::CompareOperator, QStringLiteral("="), start);
            ++index;
            continue;
        }
        if (ch == QLatin1Char('!') && index + 1 < length && expressionText.at(index + 1) == QLatin1Char('=')) {
            appendToken(LogicTokenType::CompareOperator, QStringLiteral("!="), start);
            index += 2;
            continue;
        }
        if (ch == QLatin1Char('<')) {
            if (index + 1 < length && expressionText.at(index + 1) == QLatin1Char('=')) {
                appendToken(LogicTokenType::CompareOperator, QStringLiteral("<="), start);
                index += 2;
                continue;
            }
            if (index + 1 < length && expressionText.at(index + 1) == QLatin1Char('>')) {
                appendToken(LogicTokenType::CompareOperator, QStringLiteral("<>"), start);
                index += 2;
                continue;
            }
            appendToken(LogicTokenType::CompareOperator, QStringLiteral("<"), start);
            ++index;
            continue;
        }
        if (ch == QLatin1Char('>')) {
            if (index + 1 < length && expressionText.at(index + 1) == QLatin1Char('=')) {
                appendToken(LogicTokenType::CompareOperator, QStringLiteral(">="), start);
                index += 2;
                continue;
            }
            appendToken(LogicTokenType::CompareOperator, QStringLiteral(">"), start);
            ++index;
            continue;
        }

        result.error.message = QStringLiteral("unexpected character '%1'").arg(ch);
        result.error.position = start;
        return result;
    }

    appendToken(LogicTokenType::EndOfInput, QString(), expressionText.size());
    result.success = true;
    return result;
}

} // namespace logic
