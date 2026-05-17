/**
 * sql_tokenizer.cpp — SQL 词法分析器实现
 *
 * 纯解析逻辑，不涉及数据库文件操作。
 */
#include "sql_tokenizer.h"

namespace sqlparser {

// ============================================================
//  关键字查找
// ============================================================
TokenType SqlTokenizer::keywordLookup(const QString& word) {
    QString w = word.toUpper();
    if (w == "CREATE")         return TokenType::CREATE;
    if (w == "DROP")           return TokenType::DROP;
    if (w == "TABLE")          return TokenType::TABLE;
    if (w == "DATABASE")        return TokenType::DATABASE;
    if (w == "DATABASES")       return TokenType::DATABASES;
    if (w == "USE")            return TokenType::USE;
    if (w == "SHOW")           return TokenType::SHOW;
    if (w == "DESC" || w == "DESCRIBE")
                                return TokenType::DESC;
    if (w == "SELECT")         return TokenType::SELECT;
    if (w == "FROM")           return TokenType::FROM;
    if (w == "WHERE")          return TokenType::WHERE;
    if (w == "ORDER")          return TokenType::ORDER;
    if (w == "LIMIT")          return TokenType::LIMIT;
    if (w == "INSERT")         return TokenType::INSERT;
    if (w == "INTO")           return TokenType::INTO;
    if (w == "VALUES")         return TokenType::VALUES;
    if (w == "UPDATE")         return TokenType::UPDATE;
    if (w == "SET")            return TokenType::SET;
    if (w == "DELETE")         return TokenType::DELETE;
    if (w == "ALTER")          return TokenType::ALTER;
    if (w == "ADD")            return TokenType::ADD;
    if (w == "COLUMN")         return TokenType::COLUMN;
    if (w == "MODIFY")         return TokenType::MODIFY;
    if (w == "CONSTRAINT")     return TokenType::CONSTRAINT;
    if (w == "INDEX")          return TokenType::INDEX;
    if (w == "ON")             return TokenType::ON;
    if (w == "JOIN")           return TokenType::JOIN;
    if (w == "INNER")          return TokenType::INNER;
    if (w == "LEFT")           return TokenType::LEFT;
    if (w == "RIGHT")          return TokenType::RIGHT;
    if (w == "FULL")           return TokenType::FULL;
    if (w == "NATURAL")        return TokenType::NATURAL;
    if (w == "CROSS")          return TokenType::CROSS;
    if (w == "USING")          return TokenType::USING;
    if (w == "USER")           return TokenType::USER;
    if (w == "IDENTIFIED")     return TokenType::IDENTIFIED;
    if (w == "BY")             return TokenType::BY;
    if (w == "ASC")            return TokenType::ASC;
    if (w == "GRANT")          return TokenType::GRANT;
    if (w == "REVOKE")         return TokenType::REVOKE;
    if (w == "ALL")            return TokenType::ALL;
    if (w == "TO")             return TokenType::TO;
    if (w == "LOGIN")          return TokenType::LOGIN;
    if (w == "PRIMARY")        return TokenType::PRIMARY_KEY;
    if (w == "FOREIGN")        return TokenType::FOREIGN_KEY;
    if (w == "REFERENCES")     return TokenType::REFERENCES;
    if (w == "KEY")            return TokenType::KEY;
    if (w == "UNIQUE")         return TokenType::UNIQUE;
    if (w == "CHECK")          return TokenType::CHECK;
    if (w == "DEFAULT")        return TokenType::DEFAULT;
    if (w == "NULL")           return TokenType::NULL_VAL;
    if (w == "NOT")            return TokenType::NOT;
    if (w == "AUTO_INCREMENT") return TokenType::AUTO_INCREMENT;
    if (w == "AND")            return TokenType::AND;
    if (w == "OR")             return TokenType::OR;
    if (w == "IN")             return TokenType::IN;
    if (w == "LIKE")           return TokenType::LIKE;
    if (w == "BETWEEN")        return TokenType::BETWEEN;
    if (w == "IS")             return TokenType::IS;
    // 类型关键字
    if (w == "INT" || w == "INTEGER")       return TokenType::INT_TYPE;
    if (w == "FLOAT" || w == "DOUBLE" || w == "REAL")
                                          return TokenType::FLOAT_TYPE;
    if (w == "CHAR")                        return TokenType::CHAR_TYPE;
    if (w == "VARCHAR" || w == "VARCHAR2") return TokenType::VARCHAR_TYPE;
    if (w == "TEXT")                        return TokenType::TEXT_TYPE;
    return TokenType::IDENTIFIER;
}

// ============================================================
//  主入口
// ============================================================
QVector<SqlToken> SqlTokenizer::tokenize(const QString& sql) {
    QVector<SqlToken> tokens;
    int pos = 0, line = 1, col = 1;
    const int len = sql.length();

    while (pos < len) {
        skipWhitespaceAndComments(sql, pos, line, col);
        if (pos >= len) break;

        SqlToken tok = scanToken(sql, pos, line, col);
        if (tok.type != TokenType::END_OF_INPUT) {
            tokens.append(tok);
        }
    }

    tokens.append(SqlToken{TokenType::END_OF_INPUT, "", pos, 0, line, col});
    return tokens;
}

// ============================================================
//  跳过空白与注释
// ============================================================
void SqlTokenizer::skipWhitespaceAndComments(const QString& sql, int& pos, int& line, int& col) {
    while (pos < sql.length()) {
        QChar ch = sql[pos];

        // 单行注释 --
        if (ch == '-' && pos + 1 < sql.length() && sql[pos + 1] == '-') {
            pos += 2;
            while (pos < sql.length() && sql[pos] != '\n') { ++pos; }
            continue;
        }
        // 多行注释 /* */
        if (ch == '/' && pos + 1 < sql.length() && sql[pos + 1] == '*') {
            pos += 2;
            while (pos + 1 < sql.length()) {
                if (sql[pos] == '*' && sql[pos + 1] == '/') { pos += 2; break; }
                if (sql[pos] == '\n') { ++line; col = 1; }
                ++pos;
            }
            continue;
        }
        // 空白
        if (ch.isSpace()) {
            if (ch == '\n') { ++line; col = 1; }
            else { ++col; }
            ++pos;
        } else {
            break;
        }
    }
}

// ============================================================
//  单个 Token 扫描
// ============================================================
SqlToken SqlTokenizer::scanToken(const QString& sql, int& pos, int& line, int& col) {
    QChar ch = sql[pos];
    int startLine = line, startCol = col;
    const int startPos = pos;

    // ——— 字符串字面量 ———
    if (ch == '\'' || ch == '"') {
        QString val = scanString(sql, pos, line, col);
        return SqlToken{TokenType::STRING_LIT, val, startPos, pos - startPos, startLine, startCol};
    }

    // ——— 数字 ———
    if (ch.isDigit()) {
        return scanNumber(sql, pos, line, col);
    }

    // ——— 标识符 / 关键字 ———
    if (ch.isLetter() || ch == '_') {
        return scanWord(sql, pos, line, col);
    }

    // ——— 单字符 / 双字符运算符 ———
    ++pos; ++col;

    switch (ch.unicode()) {
    case '(':  return {TokenType::LPAREN, "(", startPos, 1, startLine, startCol};
    case ')':  return {TokenType::RPAREN, ")", startPos, 1, startLine, startCol};
    case ',':  return {TokenType::COMMA,  ",", startPos, 1, startLine, startCol};
    case ';':  return {TokenType::SEMICOLON, ";", startPos, 1, startLine, startCol};
    case '.':  return {TokenType::DOT,   ".", startPos, 1, startLine, startCol};
    case '*':  return {TokenType::STAR,   "*", startPos, 1, startLine, startCol};
    case '+':  return {TokenType::PLUS,  "+", startPos, 1, startLine, startCol};
    case '-':  return {TokenType::MINUS, "-", startPos, 1, startLine, startCol};
    case '/':  return {TokenType::SLASH,  "/", startPos, 1, startLine, startCol};
    case '=':  return {TokenType::EQ,    "=", startPos, 1, startLine, startCol};

    case '<':
        if (pos < sql.length()) {
            QChar next = sql[pos];
            if (next == '=') { ++pos; ++col; return {TokenType::LE, "<=", startPos, 2, startLine, startCol}; }
            if (next == '>') { ++pos; ++col; return {TokenType::NE, "<>", startPos, 2, startLine, startCol}; }
        }
        return {TokenType::LT, "<", startPos, 1, startLine, startCol};

    case '>':
        if (pos < sql.length() && sql[pos] == '=') { ++pos; ++col; return {TokenType::GE, ">=", startPos, 2, startLine, startCol}; }
        return {TokenType::GT, ">", startPos, 1, startLine, startCol};

    case '!':
        if (pos < sql.length() && sql[pos] == '=') { ++pos; ++col; return {TokenType::NE, "!=", startPos, 2, startLine, startCol}; }
        break;

    case ':':
        if (pos < sql.length() && sql[pos] == '=') { ++pos; ++col; return {TokenType::ASSIGN, ":=", startPos, 2, startLine, startCol}; }
        break;

    default:
        break;
    }

    return SqlToken{TokenType::UNKNOWN, QString(1, ch), startPos, 1, startLine, startCol};
}

// ============================================================
//  字符串扫描
// ============================================================
QString SqlTokenizer::scanString(const QString& sql, int& pos, int& line, int& col) {
    QChar quote = sql[pos];
    ++pos; ++col; // 跳过开头引号

    QString val;
    while (pos < sql.length()) {
        QChar ch = sql[pos];
        if (ch == quote) { ++pos; ++col; break; }   // 匹配结尾引号
        if (ch == '\\') {                               // 转义
            ++pos; ++col;
            if (pos < sql.length()) {
                QChar esc = sql[pos];
                switch (esc.unicode()) {
                case 'n':  val += '\n'; break;
                case 't':  val += '\t'; break;
                case 'r':  val += '\r'; break;
                case '\\': val += '\\'; break;
                case '0':  val += '\0'; break;
                case '\'': val += '\''; break;
                default:  val += esc;   break;
                }
                ++pos; ++col;
            }
        } else {
            if (ch == '\n') { ++line; col = 1; }
            else { ++col; }
            val += ch;
            ++pos;
        }
    }
    return val;
}

// ============================================================
//  数字扫描
// ============================================================
SqlToken SqlTokenizer::scanNumber(const QString& sql, int& pos, int& line, int& col) {
    int startLine = line, startCol = col;
    const int startPos = pos;
    QString num;

    while (pos < sql.length() && (sql[pos].isDigit() || sql[pos] == '.')) {
        num += sql[pos];
        if (sql[pos] == '\n') { ++line; col = 1; } else { ++col; }
        ++pos;
    }

    // 科学计数法 e/E
    if (pos < sql.length() && (sql[pos] == 'e' || sql[pos] == 'E')) {
        num += sql[pos]; ++pos; ++col;
        if (pos < sql.length() && (sql[pos] == '+' || sql[pos] == '-')) {
            num += sql[pos]; ++pos; ++col;
        }
        while (pos < sql.length() && sql[pos].isDigit()) {
            num += sql[pos]; ++pos; ++col;
        }
    }

    bool isFloat = num.contains('.') || num.contains('e') || num.contains('E');
    return SqlToken{isFloat ? TokenType::FLOAT_LIT : TokenType::INTEGER_LIT,
                     num, startPos, pos - startPos, startLine, startCol};
}

// ============================================================
//  标识符/关键字扫描
// ============================================================
SqlToken SqlTokenizer::scanWord(const QString& sql, int& pos, int& line, int& col) {
    int startLine = line, startCol = col;
    const int startPos = pos;
    QString word;

    while (pos < sql.length() && (sql[pos].isLetterOrNumber() || sql[pos] == '_')) {
        word += sql[pos];
        ++pos; ++col;
    }

    return SqlToken{keywordLookup(word), word, startPos, pos - startPos, startLine, startCol};
}

} // namespace sqlparser
