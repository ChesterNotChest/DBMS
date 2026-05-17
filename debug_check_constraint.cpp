
#include &lt;iostream&gt;
#include "utils/logic/logic_tokenizer.h"
#include "utils/logic/logic_parser.h"

int main() {
    QString expr = "ssex IN ('男', '女')";
    std::cout &lt;&lt; "Testing expression: " &lt;&lt; expr.toStdString() &lt;&lt; std::endl;
    
    auto tokenResult = logic::tokenizeLogicExpression(expr);
    std::cout &lt;&lt; "Tokenization success: " &lt;&lt; tokenResult.success &lt;&lt; std::endl;
    if (!tokenResult.success) {
        std::cout &lt;&lt; "Error: " &lt;&lt; tokenResult.error.message.toStdString() &lt;&lt; std::endl;
        return 1;
    }
    
    std::cout &lt;&lt; "Number of tokens: " &lt;&lt; tokenResult.tokens.size() &lt;&lt; std::endl;
    for (int i=0; i&lt;tokenResult.tokens.size(); ++i) {
        const auto &amp;token = tokenResult.tokens[i];
        QString typeStr;
        switch (token.type) {
            case logic::LogicTokenType::Identifier: typeStr = "Identifier"; break;
            case logic::LogicTokenType::StringLiteral: typeStr = "StringLiteral"; break;
            case logic::LogicTokenType::NumberLiteral: typeStr = "NumberLiteral"; break;
            case logic::LogicTokenType::LeftParen: typeStr = "LeftParen"; break;
            case logic::LogicTokenType::RightParen: typeStr = "RightParen"; break;
            case logic::LogicTokenType::Comma: typeStr = "Comma"; break;
            case logic::LogicTokenType::Keyword: typeStr = "Keyword"; break;
            default: typeStr = "Other"; break;
        }
        std::cout &lt;&lt; "  Token " &lt;&lt; i &lt;&lt; ": " &lt;&lt; typeStr.toStdString() 
                  &lt;&lt; " = '" &lt;&lt; token.rawText.toStdString() &lt;&lt; "'" 
                  &lt;&lt; ", pos=" &lt;&lt; token.position &lt;&lt; std::endl;
    }
    
    logic::LogicParserState state;
    state.tokens = tokenResult.tokens;
    state.index = 0;
    auto parseResult = logic::parseLogicTokens(expr, tokenResult.tokens);
    std::cout &lt;&lt; "Parse success: " &lt;&lt; parseResult.success &lt;&lt; std::endl;
    if (!parseResult.success) {
        std::cout &lt;&lt; "Error: " &lt;&lt; parseResult.error.message.toStdString() &lt;&lt; std::endl;
    }
    
    return 0;
}
