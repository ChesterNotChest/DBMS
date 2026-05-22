#ifndef UTILS_LOGIC_LOGIC_TYPES_H
#define UTILS_LOGIC_LOGIC_TYPES_H

#include "../../constants/set_def.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace logic {

enum class LogicTruthValue {
    True,
    False,
    Unknown
};

struct LogicError {
    QString message;
    int position = -1;
};

enum class LogicTokenType {
    Identifier,
    NumberLiteral,
    StringLiteral,
    NullLiteral,
    LeftParen,
    RightParen,
    Comma,
    CompareOperator,
    Asterisk,
    Keyword,
    EndOfInput
};

enum class LogicKeywordType {
    None,
    And,
    Or,
    Not,
    In,
    Between,
    Exists,
    Select,
    Any,
    All,
    Is,
    Like,
    Null
};

struct LogicToken {
    LogicTokenType type = LogicTokenType::EndOfInput;
    QString rawText;
    int position = -1;
    LogicKeywordType keywordType = LogicKeywordType::None;
};

enum class LogicReferenceScope {
    Local,
    Outer
};

struct LogicReference {
    LogicReferenceScope scope = LogicReferenceScope::Local;
    QString name;
};

enum class LogicNodeType {
    Literal,
    ColumnRef,
    Unary,
    Binary,
    Comparison,
    NullTest,
    InList,
    InSubquery,
    ExistsSubquery,
    QuantifiedSubquery,
    ScalarSubquery,
    Between
};

enum class LogicUnaryOperator {
    Not
};

enum class LogicBinaryOperator {
    And,
    Or
};

enum class LogicCompareOperator {
    Eq,
    NotEq,
    Lt,
    Lte,
    Gt,
    Gte,
    Like
};

enum class LogicQuantifier {
    Any,
    All
};

struct LogicNode {
    LogicNodeType type = LogicNodeType::Literal;

    QString rawText;
    QString literalValue;
    tabledef::ColumnType literalType = tabledef::ColumnType::Varchar;
    bool literalIsNull = false;
    LogicReference reference;

    LogicUnaryOperator unaryOperator = LogicUnaryOperator::Not;
    LogicBinaryOperator binaryOperator = LogicBinaryOperator::And;
    LogicCompareOperator compareOperator = LogicCompareOperator::Eq;
    LogicQuantifier quantifier = LogicQuantifier::Any;

    bool negated = false;
    bool isNotNullTest = false;

    QString subquerySql;
    QStringList referencedOuterNames;

    QList<LogicNode> children;
};

struct LogicTokenizeResult {
    bool success = false;
    QList<LogicToken> tokens;
    LogicError error;
};

struct LogicEvalResult {
    bool success = false;
    LogicTruthValue truth = LogicTruthValue::Unknown;
    LogicError error;
};

struct LogicCellValue {
    QString value;
    tabledef::ColumnType type = tabledef::ColumnType::Varchar;
    bool isNull = false;
};

struct LogicRowContext {
    QString tableName;
    QMap<QString, LogicCellValue> cellsByName;
};

class ISubqueryExecutor;

struct LogicEvalContext {
    ISubqueryExecutor *subqueryExecutor = nullptr;
    QString currentDatabase;
    QString dataRoot;
    bool allowSubquery = false;
};


struct LogicParseResult {
    bool success = false;
    LogicNode root;
    LogicError error;
};

inline void ensureLogicMetaTypesRegistered()
{
    qRegisterMetaType<LogicNode>("logic::LogicNode");
}

} // namespace logic

Q_DECLARE_METATYPE(logic::LogicNode)

#endif // UTILS_LOGIC_LOGIC_TYPES_H
