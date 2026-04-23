/**
 * sql_tokenizer.h — SQL 词法分析器（纯解析，不碰文件系统）
 *
 * 职责：将 SQL 字符串拆分为 Token 流。
 * 不涉及任何数据库文件读写、不维护数据库状态。
 */
#ifndef SQL_PARSER_SQL_TOKENIZER_H
#define SQL_PARSER_SQL_TOKENIZER_H

#include <QString>
#include <QVector>
#include <QRegularExpression>

namespace sqlparser {

enum class TokenType {
    // 关键字
    CREATE, DROP, TABLE, DATABASE, DATABASES,
    USE, SHOW, DESC, SELECT, FROM, WHERE, LIMIT,
    INSERT, INTO, VALUES, UPDATE, SET, DELETE,
    ALTER, ADD, COLUMN, MODIFY, CONSTRAINT, INDEX, ON,
    PRIMARY_KEY, FOREIGN_KEY, REFERENCES, KEY,
    UNIQUE, CHECK, DEFAULT, NULL_VAL, NOT, AUTO_INCREMENT,
    INT_TYPE, FLOAT_TYPE, CHAR_TYPE, VARCHAR_TYPE,
    TEXT_TYPE,

    // 运算符
    EQ, NE, LT, GT, LE, GE,
    AND, OR, IN, LIKE, BETWEEN, IS,

    // 标识符与字面量
    IDENTIFIER,
    INTEGER_LIT,
    FLOAT_LIT,
    STRING_LIT,

    // 符号
    LPAREN, RPAREN,
    COMMA, SEMICOLON,
    DOT, STAR,
    PLUS, MINUS, SLASH,
    ASSIGN, // :=

    // 结束
    END_OF_INPUT,
    UNKNOWN
};

struct SqlToken {
    TokenType type;
    QString    lexeme;      // 原始文本
    int       line;
    int       column;
    SqlToken(TokenType t = TokenType::UNKNOWN,
             const QString& lex = {},
             int l = 0, int c = 0)
        : type(t), lexeme(lex), line(l), column(c) {}
};

/**
 * 词法分析器：输入 SQL 字符串，输出 Token 流。
 * 纯函数式：不持有状态，不碰文件。
 */
class SqlTokenizer {
public:
    /** 对一条 SQL 字符串执行完整词法分析，返回 Token 列表 */
    static QVector<SqlToken> tokenize(const QString& sql);

private:
    static SqlToken scanToken(const QString& sql, int& pos, int& line, int& col);
    static void skipWhitespaceAndComments(const QString& sql, int& pos, int& line, int& col);
    static QString scanString(const QString& sql, int& pos, int& line, int& col);
    static SqlToken scanNumber(const QString& sql, int& pos, int& line, int& col);
    static SqlToken scanWord(const QString& sql, int& pos, int& line, int& col);
    static TokenType keywordLookup(const QString& word);
};

} // namespace sqlparser

#endif // SQL_PARSER_SQL_TOKENIZER_H
