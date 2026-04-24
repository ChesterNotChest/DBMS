#ifndef UTILS_LOGIC_LOGIC_PARSER_H
#define UTILS_LOGIC_LOGIC_PARSER_H

#include "logic_ast.h"
#include "logic_tokenizer.h"

namespace logic {

struct LogicParserState {
    QList<LogicToken> tokens;
    int index = 0;
};

const LogicToken &peekToken(const LogicParserState &state);
bool isAtEnd(const LogicParserState &state);
bool matchToken(LogicParserState &state, LogicTokenType type);
LogicToken consumeToken(LogicParserState &state, LogicTokenType type, const QString &errorMessage);
LogicParseResult makeParseError(const QString &message, int position);

QString captureSubquerySql(const QString &expressionText, LogicParserState &state);
LogicParseResult parseLogicTokens(const QString &expressionText, const QList<LogicToken> &tokens);

} // namespace logic

#endif // UTILS_LOGIC_LOGIC_PARSER_H