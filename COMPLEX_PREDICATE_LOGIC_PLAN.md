# COMPLEX PREDICATE LOGIC PLAN

目标：构建一套可同时服务 `WHERE` 和 `CHECK` 的复杂谓词表达式模块，并为 `EXISTS / IN (SELECT ...) / ANY / ALL` 预留清晰边界。

本计划的核心原则：

- `utils/logic` 只负责表达式词法、语法、AST 和求值。
- `utils/logic` 不直接依赖前端，不直接操作 repo，不直接依赖 `SqlDispatcher`。
- 子查询统一收口到 `controller/nest_query.h/.cpp` 中的 `QueryExecutor`。
- `QueryExecutor` 只服务子查询通道，不接管普通外部 SQL 执行。
- `QueryExecutor` 只接受 `SELECT`，非 `SELECT` 直接拒绝。
- `CHECK` 禁止子查询，保持当前行局部约束。
- `WHERE` 在阶段 C/D 允许子查询。
- NULL 语义采用 SQL 三值逻辑：`TRUE / FALSE / UNKNOWN`。

## 1. 模块位置

目录：

```text
constants/
  set_def.h

utils/logic/
  logic.h
  logic_types.h
  logic_tokenizer.h
  logic_tokenizer.cpp
  logic_ast.h
  logic_parser.h
  logic_parser.cpp
  simple_logic.h
  simple_logic.cpp
  set_logic.h
  set_logic.cpp
  subquery_logic.h
  subquery_logic.cpp
  logic_evaluator.h
  logic_evaluator.cpp
```

### 1.1 logic.h 聚合入口

正式决议：

- `utils/logic/logic.h` 作为逻辑模块的对外聚合入口头文件。
- 细分模块仍然保持独立 `.h/.cpp`，不合并成单一大头文件。
- `logic.h` 只做聚合 `#include`
- 不在 `logic.h` 中重新定义全部类型和函数
- `logic_types.h`
- `logic_ast.h`
- `logic_parser.h`
- `logic_evaluator.h`
- `subquery_logic.h` 仅在外部确实需要直接使用时暴露
- `simple_logic.h`
- `set_logic.h`

## 2. 总体数据流

不含子查询：

```text
predicate string
-> tokenizeLogicExpression()
-> parseLogicTokens()
-> LogicNode AST
-> evaluateLogicExpression()
-> LogicEvalResult(TRUE / FALSE / UNKNOWN)
```

包含子查询：

```text
predicate string
-> tokenizeLogicExpression()
-> parseLogicTokens()
-> LogicNode AST(Subquery / Exists / InSubquery / QuantifiedCompare)
-> evaluateLogicExpression()
-> QueryExecutor::executeSelectSql(...)
-> normalizeSelectResultToSet()
-> LogicEvalResult(TRUE / FALSE / UNKNOWN)
```

## 3. 基础返回类型

### 3.1 LogicTruthValue

```cpp
enum class LogicTruthValue {
    True,
    False,
    Unknown
};
```

规则：

- 普通比较只要任一侧为 `NULL`，结果为 `UNKNOWN`。
- `WHERE` 只保留 `TRUE` 行；`FALSE` 和 `UNKNOWN` 都不保留。
- `CHECK` 采用严格规则：`TRUE` 通过，`FALSE` 和 `UNKNOWN` 都失败。

### 3.2 通用结果结构

```cpp
struct LogicError {
    QString message;
    int position = -1;
};

struct LogicTokenizeResult {
    bool success = false;
    QList<LogicToken> tokens;
    LogicError error;
};

struct LogicParseResult {
    bool success = false;
    LogicNode root;
    LogicError error;
};

struct LogicEvalResult {
    bool success = false;
    LogicTruthValue truth = LogicTruthValue::Unknown;
    LogicError error;
};
```

### 3.3 LogicToken 最终字段收口

`LogicToken` 固定包含四类信息：

```cpp
enum class LogicTokenType {
    Identifier,
    NumberLiteral,
    StringLiteral,
    NullLiteral,
    LeftParen,
    RightParen,
    Comma,
    CompareOperator,
    Keyword,
    EndOfInput
};

enum class LogicKeywordType {
    None,
    And,
    Or,
    Not,
    In,
    Exists,
    Select,
    Any,
    All,
    Is,
    Null
};

struct LogicToken {
    LogicTokenType type = LogicTokenType::EndOfInput;
    QString rawText;
    int position = -1;
    LogicKeywordType keywordType = LogicKeywordType::None;
};
```

字段说明：

- `type`：区分标识符、数字、字符串、括号、运算符、关键字。
- `rawText`：保留原始文本，用于错误提示、子查询字符串捕获、调试输出。
- `position`：用于报错定位。
- `keywordType`：在 `type == Keyword` 时快速判断 `AND / OR / NOT / IN / EXISTS / SELECT / ANY / ALL / IS / NULL`。

正式约束：

- tokenizer 必须始终填充 `rawText` 和 `position`。
- 非关键字 token 的 `keywordType` 固定为 `None`。
- parser 中所有关键字判断优先通过 `keywordType` 完成，不再依赖 `rawText` 大小写比较。
- 第一版不新增 `Dot` token。
- `outer.id`、`outer.name`、`parent.id`、`alias.id` 这类带前缀引用在词法阶段统一按单个 `Identifier` token 处理。
- 相关前缀是否合法由 parser 负责判定，而不是由 tokenizer 拆成 `outer` `.` `id` 三个 token。

## 4. 行上下文与求值上下文

### 4.1 LogicRowContext

`evaluator` 持有完整外层行上下文：

```cpp
struct LogicCellValue {
    QString value;
    tabledef::ColumnType type = tabledef::ColumnType::Varchar;
    bool isNull = false;
};

struct LogicRowContext {
    QString tableName;
    QMap<QString, LogicCellValue> cellsByName;
};
```

用途：

- 普通谓词比较。
- `CHECK` 当前行校验。
- 相关子查询中构造外层相关绑定。

第一版正式约束：

- `tableName` 第一版不参与列匹配、不参与列解析、不参与作用域消歧。
- 第一版所有列匹配仅基于 `cellsByName` 和 `LogicReference`。
- `tableName` 第一版只用于调试、错误提示、日志和后续扩展。

### 4.2 LogicEvalContext

```cpp
class ISubqueryExecutor;

struct LogicEvalContext {
    ISubqueryExecutor *subqueryExecutor = nullptr;
    QString currentDatabase;
    QString dataRoot;
    bool allowSubquery = false;
};
```

规则：

- `CHECK` 调用时 `allowSubquery = false`。
- `WHERE` 调用时统一设为 `true`。
- 若当前 `WHERE` AST 不含子查询节点，`allowSubquery = true` 不产生额外行为。

## 5. 递归下降解析器收口

### 5.1 ParserState

```cpp
struct LogicParserState {
    QList<LogicToken> tokens;
    int index = 0;
};
```

### 5.2 基础辅助函数

```cpp
const LogicToken &peekToken(const LogicParserState &state);
bool isAtEnd(const LogicParserState &state);
bool matchToken(LogicParserState &state, LogicTokenType type);
LogicToken consumeToken(LogicParserState &state, LogicTokenType type, const QString &errorMessage);
LogicParseResult makeParseError(const QString &message, int position);
```

输入输出与流程：

- `peekToken`：输入当前状态，输出当前 token，不推进下标。
- `isAtEnd`：判断是否到 `EndOfInput`。
- `matchToken`：若当前 token 类型匹配则推进一位，返回 `true`。
- `consumeToken`：若匹配则返回 token，否则直接构造 parse error。
- `makeParseError`：统一生成解析错误结果。

### 5.3 递归下降层级

优先级：

```text
parseExpression
-> parseOrExpression
-> parseAndExpression
-> parseNotExpression
-> parsePredicateExpression
-> parsePrimaryExpression
```

#### parseExpression

```cpp
LogicParseResult parseExpression(LogicParserState &state);
```

输入：

- `LogicParserState`

输出：

- 根表达式解析结果

内部流程：

```text
直接调用 parseOrExpression()
-> 返回最终 AST
```

#### parseOrExpression

```cpp
LogicParseResult parseOrExpression(LogicParserState &state);
```

内部流程：

```text
lhs = parseAndExpression()
while 当前 token 是 OR
-> 消费 OR
-> rhs = parseAndExpression()
-> 组装 Binary(OR, lhs, rhs)
-> lhs = 新节点
return lhs
```

#### parseAndExpression

```cpp
LogicParseResult parseAndExpression(LogicParserState &state);
```

内部流程：

```text
lhs = parseNotExpression()
while 当前 token 是 AND
-> 消费 AND
-> rhs = parseNotExpression()
-> 组装 Binary(AND, lhs, rhs)
-> lhs = 新节点
return lhs
```

#### parseNotExpression

```cpp
LogicParseResult parseNotExpression(LogicParserState &state);
```

内部流程：

```text
if 当前 token 是 NOT
-> 消费 NOT
-> child = parseNotExpression()
-> 返回 Unary(NOT, child)
else
-> 返回 parsePredicateExpression()
```

#### parsePredicateExpression

```cpp
LogicParseResult parsePredicateExpression(LogicParserState &state);
```

职责：

- 比较谓词：`a = b`、`a >= b`
- `IS NULL / IS NOT NULL`
- `IN (...) / NOT IN (...)`
- `BETWEEN ... AND ... / NOT BETWEEN ... AND ...`
- `EXISTS (...)`
- `a = ANY (...)`
- `a > ALL (...)`

内部流程：

```text
先解析 lhs = parsePrimaryExpression()
-> 根据后续 token 判断是哪种谓词
-> 比较运算则解析 rhs primary
-> IS NULL / IS NOT NULL 构造 NullTest
-> IN / NOT IN 进入集合谓词分支
-> BETWEEN / NOT BETWEEN 进入范围谓词分支
-> 比较符 + ANY/ALL 进入量化比较分支
-> 若没有后续谓词，则直接返回 lhs
```

#### parsePrimaryExpression

```cpp
LogicParseResult parsePrimaryExpression(LogicParserState &state);
```

职责：

- 括号表达式
- 字面量
- 列名引用
- `EXISTS (SELECT ...)`
- `SELECT` 子查询节点

内部流程：

```text
if '('
-> parseExpression()
-> consume ')'
if literal
-> 返回 Literal 节点
if identifier
-> 返回 ColumnRef 节点
if EXISTS
-> 解析子查询并构造 Exists 节点
否则报错
```

### 5.4 子查询 SQL 捕获规则

当解析器在表达式中遇到左括号 `(` 且后续为 `SELECT` 语句时，按如下规则捕获子查询 SQL：

```cpp
QString captureSubquerySql(const QString &expressionText,
                           LogicParserState &state);
```

输入：

- `expressionText`：原始谓词字符串
- 当前 `state.index` 指向子查询外层左括号 `(`

输出：

- 去掉最外层括号后的子查询 SQL 文本

内部流程：

```text
检查当前 token 为 '('
-> 检查下一 token 是否为关键字 SELECT
-> 进入子查询捕获状态
-> 初始化 depth = 1
-> 从当前左括号之后继续扫描 token
-> 遇到 '(' 则 depth += 1
-> 遇到 ')' 则 depth -= 1
-> 当 depth 回到 0 时停止
-> 根据起止 token.position 从 expressionText 中直接切片
-> 返回子查询 SQL 文本
```

正式约束：

- 捕获必须支持子查询内部再次出现括号。
- 捕获结果不包含最外层一对括号。
- 捕获结果必须保留原始空格、引号、大小写和内部文本顺序。
- 捕获结果不通过 token 重新拼接生成，而是通过原始 `expressionText` 的位置切片得到。
- 若扫描结束时 `depth != 0`，直接返回 parse error。

## 6. AST 设计收口

本计划采用符号树。

### 6.1 节点类型

```cpp
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
    QuantifiedSubquery
};
```

### 6.2 运算符类型

```cpp
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
    Gte
};

enum class LogicQuantifier {
    Any,
    All
};
```

### 6.3 LogicNode

```cpp
struct LogicNode {
    LogicNodeType type;

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
```

收口约定：

- `Literal`：使用 `literalValue / literalType / literalIsNull`
- `ColumnRef`：统一使用 `reference`
- `Unary`：`children.size() == 1`
- `Binary`：`children.size() == 2`
- `Comparison`：`children.size() == 2`
- `NullTest`：`children.size() == 1`
- `InList`：`children[0]` 是 lhs，其余 `children[1..n]` 是字面量列表
- `InSubquery`：`children[0]` 是 lhs，`subquerySql` 保存内层 SQL
- `ExistsSubquery`：不要求 lhs，直接使用 `subquerySql`
- `QuantifiedSubquery`：`children[0]` 是 lhs，`compareOperator + quantifier + subquerySql`

## 7. 简单谓词求值

### 7.1 入口

```cpp
LogicEvalResult evaluateLogicExpression(const LogicNode &root,
                                        const LogicRowContext &rowContext,
                                        const LogicEvalContext &evalContext);
```

内部流程：

```text
按 node.type 分派
-> simple_logic / set_logic / subquery_logic
-> 返回 TRUE / FALSE / UNKNOWN
```

### 7.2 simple_logic 函数

```cpp
LogicEvalResult evaluateUnaryNode(const LogicNode &node,
                                  const LogicRowContext &rowContext,
                                  const LogicEvalContext &evalContext);

LogicEvalResult evaluateBinaryNode(const LogicNode &node,
                                   const LogicRowContext &rowContext,
                                   const LogicEvalContext &evalContext);

LogicEvalResult evaluateComparisonNode(const LogicNode &node,
                                       const LogicRowContext &rowContext);

LogicEvalResult evaluateNullTestNode(const LogicNode &node,
                                     const LogicRowContext &rowContext);
```

### 7.3 三值逻辑规则

- `NOT TRUE = FALSE`
- `NOT FALSE = TRUE`
- `NOT UNKNOWN = UNKNOWN`
- `TRUE AND UNKNOWN = UNKNOWN`
- `FALSE AND UNKNOWN = FALSE`
- `TRUE OR UNKNOWN = TRUE`
- `FALSE OR UNKNOWN = UNKNOWN`

## 8. 集合谓词收口

### 8.1 set_def.h

集合基础类型统一放在 [set_def.h](E:/Qt-projects/DBMS/constants/set_def.h)。

### 8.2 字面量集合

```cpp
LogicEvalResult evaluateInListNode(const LogicNode &node,
                                   const LogicRowContext &rowContext);
```

支持：

- `col IN (1, 2, 3)`
- `col NOT IN ('a', 'b')`

内部流程：

```text
求 lhs
-> 逐个求值 literal list
-> 应用 IN / NOT IN 和三值逻辑
```

### 8.3 量化比较

```cpp
LogicEvalResult evaluateQuantifiedSetComparison(const LogicNode &node,
                                                const QList<setdef::SetValue> &values,
                                                const LogicRowContext &rowContext);
```

用途：

- `a = ANY (subquery)`
- `a > ALL (subquery)`

### 8.4 范围谓词

```cpp
LogicEvalResult evaluateBetweenNode(const LogicNode &node,
                                    const LogicRowContext &rowContext);
```

支持：

- `col BETWEEN 1 AND 10`
- `col NOT BETWEEN 1 AND 10`

实现边界：

- `BETWEEN` 节点固定包含 `lhs / lower / upper` 三个子节点。
- `lower` 和 `upper` 第一版沿用当前谓词解析边界，只接收字面量或列引用，不引入算术表达式。
- 求值等价于 `lhs >= lower AND lhs <= upper`，上下界均为闭区间。
- 比较复用 `compareValues(...)`，不新增一套类型比较规则。
- 任一侧为 `NULL` 或比较无法得到确定真值时，整体按三值逻辑返回 `UNKNOWN`。
- `NOT BETWEEN` 等价于对 `BETWEEN` 结果应用现有 `negateTruth(...)`。

### 8.5 空集、空值细则

本计划第一版正式采用如下规则：

- `x IN (empty set)` -> `FALSE`
- `x NOT IN (empty set)` -> `TRUE`
- `x = ANY (empty set)` -> `FALSE`
- `x > ALL (empty set)` -> `TRUE`

集合中含 `NULL` 时，第一版统一按三值逻辑处理：

- 若当前谓词不能在不依赖 `NULL` 的情况下直接确定为 `TRUE` 或 `FALSE`，则结果为 `UNKNOWN`

实现级规则表：

- `x IN (v1, v2, ..., NULL)`
  - 若命中任一非 `NULL` 值 -> `TRUE`
  - 若未命中任何非 `NULL` 值且集合含 `NULL` -> `UNKNOWN`
  - 若未命中任何非 `NULL` 值且集合不含 `NULL` -> `FALSE`
- `x NOT IN (set)`
  - 等价于 `NOT (x IN (set))`
- `x = ANY (v1, v2, ..., NULL)`
  - 若任一非 `NULL` 比较为 `TRUE` -> `TRUE`
  - 若所有非 `NULL` 比较为 `FALSE` 且集合含 `NULL` -> `UNKNOWN`
  - 若所有非 `NULL` 比较为 `FALSE` 且集合不含 `NULL` -> `FALSE`
- `x > ALL (v1, v2, ..., NULL)`
  - 若任一非 `NULL` 比较为 `FALSE` -> `FALSE`
  - 若所有非 `NULL` 比较为 `TRUE` 且集合含 `NULL` -> `UNKNOWN`
  - 若所有非 `NULL` 比较为 `TRUE` 且集合不含 `NULL` -> `TRUE`

对子查询结果的正式约束：

- `IN / NOT IN / ANY / ALL` 只允许单列结果
- `EXISTS` 只看是否有行，不限制列数

`EXISTS` 规则：

- 有行 -> `TRUE`
- 无行 -> `FALSE`
- `IN / NOT IN / ANY / ALL` 若子查询返回多列，直接返回求值错误
- `EXISTS` 不关心列值是否为 `NULL`，也不关心列数，只关心结果集是否为空

## 9. QueryExecutor 真实接口收口

位置：

- [nest_query.h](E:/Qt-projects/DBMS/controller/nest_query.h)
- [nest_query.cpp](E:/Qt-projects/DBMS/controller/nest_query.cpp)

正式决议：

- `QueryExecutor` 只服务相关子查询、非相关子查询。
- `QueryExecutor` 不接管普通外部查询。
- `SHOW / DESC / SHOW CREATE TABLE / DDL / DML` 继续留在 `SqlDispatcher`。
- `QueryExecutor` 只接受 `SELECT`，非 `SELECT` 直接拒绝。

### 9.1 QueryExecuteContext

```cpp
struct QueryExecuteContext {
    QString currentDatabase;
    QString dataRoot;
};
```

### 9.2 QueryExecuteResult

```cpp
struct QueryExecuteResult {
    bool success = false;
    QString errorMessage;
    QString text;
    int affectedRows = 0;
    SelectRowsResult selectResult;
};
```

### 9.3 QueryExecutor

```cpp
class QueryExecutor {
public:
    QueryExecuteResult executeSql(const QString &sql,
                                  const QueryExecuteContext &context = {}) const;

    QueryExecuteResult executeParsed(const sqlparser::ParseResult &parsed,
                                     const QueryExecuteContext &context = {}) const;

    QueryExecuteResult executeSelectSql(const QString &sql,
                                        const QueryExecuteContext &context = {}) const;

    QueryExecuteResult executeCorrelatedSelect(const QString &sql,
                                               const CorrelationBindings &bindings,
                                               const QueryExecuteContext &context = {}) const;
};
```

函数边界：

- `executeSql`：输入 SQL 字符串；内部 parse；若不是 `SELECT` 则拒绝。
- `executeParsed`：输入已解析结果；仅接受 `commandType == SELECT`。
- `executeSelectSql`：非相关子查询正式入口。
- `executeCorrelatedSelect`：相关子查询正式入口；按绑定查值，不做 SQL 字符串替换。

### 9.4 ParseResult.payload 标准键

为避免新增 `ParseResult` 公共字段，复杂谓词相关信息统一放入现有 `payload`：

```text
payload                 : QVariantMap
payload["hasComplexWhere"] : bool
payload["whereAst"]        : LogicNode
payload["selectAll"]       : bool
payload["projection"]      : QStringList
```

Qt 级承载方式：

```cpp
Q_DECLARE_METATYPE(logic::LogicNode)
```

正式约束：

- `ParseResult.payload` 第一版按 `QVariantMap` 使用。
- `LogicNode` 必须可放入 `QVariantMap`，因此必须声明 Qt metatype。
- parser、`QueryExecutor`、测试代码从 `payload["whereAst"]` 取值时，统一使用 `value<logic::LogicNode>()`。
- `SELECT` 若存在复杂 `WHERE`，parser 必须填充 `hasComplexWhere = true`
- `SELECT` 若不存在复杂 `WHERE`，parser 必须填充 `hasComplexWhere = false`
- `SELECT` 若存在复杂 `WHERE`，parser 必须填充 `whereAst`
- `SELECT *` 时，parser 必须填充 `selectAll = true`
- 非 `SELECT *` 时，parser 必须填充 `selectAll = false`
- `QueryExecutor::executeSelectSql(...)` 读取复杂谓词时，唯一来源是 `ParseResult.payload["whereAst"]`
- 第一版不新增 `ParseResult` 的正式字段

projection 编码规则：

- `SELECT *` -> `payload["selectAll"] = true`，`payload["projection"] = QStringList{}`
- `SELECT a, b` -> `payload["selectAll"] = false`，`payload["projection"] = {"a", "b"}`
- `projectCandidateRows(...)` 在生成最终结果时先判断 `selectAll`
- 若 `selectAll == true`，直接按完整候选行投影输出，不依赖 `"*"` 特殊字符串

## 10. 相关子查询收口

实现目标：

```sql
EXISTS (SELECT ... WHERE inner.col = outer.id)
```

本阶段的正式决议：

- 外层引用必须使用明确前缀，不允许裸列名猜测。
- evaluator 自己持有完整 `LogicRowContext`。
- 传给子查询执行器的不是完整外层上下文，而是精确的 `CorrelationBindings`。
- `CorrelationBindings` 传完整绑定单元：`name + value + type + isNull`。
- 子查询按绑定查值，不做 SQL 字符串替换。
- `outer.xxx` 不在 parser 阶段替换。
- 第一版禁用相关子查询缓存，但保留后续扩展余地。

### 10.1 相关引用语法

第一版强制外层引用写成显式限定名，例如：

```sql
outer.id
outer.name
```

不允许：

```sql
id
name
parent.id
t.id
alias.id
```

正式约束：

- 第一版只允许 `outer.xxx`
- 不允许 `parent.xxx`
- 不允许 `表名.xxx`
- 不允许 `别名.xxx`
- 若解析到上述形式，直接返回 parse error

### 10.2 CorrelationBindings

```cpp
struct CorrelatedBinding {
    QString name;
    QString value;
    tabledef::ColumnType type = tabledef::ColumnType::Varchar;
    bool isNull = false;
};

struct CorrelationBindings {
    QList<CorrelatedBinding> items;
};
```
- `name` 是外层显式绑定名，例如 `outer.id`。
- `value` 是当前外层行该绑定的字符串值。
- `type` 用于比较和类型转换。
- `isNull` 用于三值逻辑。

### 10.3 构造相关绑定

```cpp
CorrelationBindings buildCorrelationBindings(const LogicRowContext &outerRowContext,
                                             const QStringList &referencedOuterNames);
```

输入：

- `outerRowContext`：evaluator 持有的完整外层行上下文。
- `referencedOuterNames`：当前子查询实际引用到的外层名列表。

输出：

- `CorrelationBindings`

内部流程：

```text
遍历 referencedOuterNames
-> 从 outerRowContext 精确提取对应值
-> 组装 CorrelatedBinding(name, value, type, isNull)
-> 返回 CorrelationBindings
```

### 10.4 相关子查询 evaluator 入口

```cpp
LogicEvalResult evaluateCorrelatedSubquery(const LogicNode &subqueryNode,
                                           const LogicRowContext &outerRowContext,
                                           const LogicEvalContext &evalContext);
```

输入：

- 子查询节点
- 完整外层行上下文
- 求值上下文

输出：

- 三值逻辑结果

内部流程：

```text
分析 subqueryNode 中引用到的 outer.xxx
-> buildCorrelationBindings(outerRowContext, referencedOuterNames)
-> QueryExecutor::executeCorrelatedSelect(subquerySql, bindings, context)
-> normalizeSelectResultToSet / EXISTS 判断
-> 返回 TRUE / FALSE / UNKNOWN
```

### 10.4.1 executeCorrelatedSelect 内部执行步骤

`QueryExecutor::executeCorrelatedSelect(...)` 第一版按如下步骤执行：

```text
输入 subquerySql + bindings + context
-> parseSql(subquerySql)
-> 检查 commandType == SELECT，否则直接返回错误
-> 直接复用现有 ParseResult，不新增 QueryPlan / FilterPlan
-> 读取 WHERE 子树中记录的 outer.xxx 引用点
-> 对每个 outer.xxx 引用点，通过 bindings 查到绑定值
-> 在比较求值阶段把该引用点解释为“外层绑定值节点”
-> 调用 tuple_service::selectRows(...) 读取候选行
-> 对候选行执行内层 WHERE 过滤
-> 返回 SelectRowsResult
```

第一版正式约束：

- `executeCorrelatedSelect(...)` 不做 SQL 文本替换
- `executeCorrelatedSelect(...)` 不重写 `subquerySql`
- `executeCorrelatedSelect(...)` 直接复用现有 `ParseResult`
- 第一版不新增 `QueryPlan / FilterPlan`
- 相关子查询中的复杂谓词 AST 同样从 `ParseResult.payload` 读取
- `executeCorrelatedSelect(...)` 通过“绑定值节点解释”参与比较
- 绑定缺失时直接返回执行错误，不允许静默降级

### 10.5 绑定查值执行落点

正式决议：

- `outer.xxx` 只在执行阶段通过 `CorrelationBindings` 查值。
- parser 只负责把 `outer.xxx` 识别为“外层引用”并记录到 `referencedOuterNames`。
- parser 不做字符串替换，不把外层值拼进 `subquerySql`。

对应分层：

- parser：识别并收集 `outer.xxx`
- evaluator：根据当前外层行构造 `CorrelationBindings`
- `QueryExecutor::executeCorrelatedSelect(...)`：消费绑定并完成相关子查询执行

### 10.5.1 outer.xxx 的可执行结构

为了让相关子查询达到可编码级，第一版补充如下结构：

```cpp
enum class LogicReferenceScope {
    Local,
    Outer
};

struct LogicReference {
    LogicReferenceScope scope = LogicReferenceScope::Local;
    QString name;
};
```

在 `LogicNode` 中，所有列引用节点统一解释为：

- 局部列：`scope == Local`
- 外层列：`scope == Outer`

第一版正式要求：

- `LogicNode::ColumnRef` 统一使用 `LogicReference(scope, name)`
- 不再使用裸 `identifier`
- 所有列引用统一合并为 `reference` 结构
- parser 识别到 `outer.xxx` 时，不再把它当普通 `identifier`
- parser 必须把该引用编码为 `LogicReference{ scope = Outer, name = "outer.xxx" }`
- 非相关列引用编码为 `LogicReference{ scope = Local, name = "col" }`

比较节点中的执行规则：

```text
若 Comparison 左右子节点出现 Local 引用
-> 从当前内层 rowContext 取值
若出现 Outer 引用
-> 从 CorrelationBindings 取值
-> 两侧值进入同一比较函数
-> 应用三值逻辑得到结果
```

这样阶段 D 的实际执行结构收口为：

```text
parser
-> LogicReference(scope, name)
-> evaluator / QueryExecutor 比较时按 scope 分流取值
```

对应 AST 收口补充：

- `LogicNodeType::ColumnRef` 节点只保留 `reference`
- `identifier` 字段从 `LogicNode` 中移除
- 所有读取列名的逻辑统一走 `node.reference`

### 10.6 缓存策略

第一版正式策略：

- 禁用相关子查询缓存
- 逐行执行相关子查询
- 先保证正确性，不以缓存为前置条件
- 当前接口设计不得封死后续缓存扩展
- 后续可在 `QueryExecutor` 或更高层执行器中加入基于 `subquerySql + CorrelationBindings` 的结果缓存
- 当前文档不要求第一版实现缓存命中、缓存失效、缓存淘汰策略

## 11. subquery_logic 收口

### 11.1 子查询执行接口

```cpp
class ISubqueryExecutor {
public:
    virtual ~ISubqueryExecutor() = default;

    virtual QueryExecuteResult executeSelectSql(const QString &sql,
                                                const QueryExecuteContext &context) = 0;

    virtual QueryExecuteResult executeCorrelatedSelect(const QString &sql,
                                                       const CorrelationBindings &bindings,
                                                       const QueryExecuteContext &context) = 0;
};
```

### 11.2 适配器

```cpp
class LogicSubqueryExecutorAdapter : public ISubqueryExecutor {
public:
    explicit LogicSubqueryExecutorAdapter(QueryExecutor *executor);

    QueryExecuteResult executeSelectSql(const QString &sql,
                                        const QueryExecuteContext &context) override;

    QueryExecuteResult executeCorrelatedSelect(const QString &sql,
                                               const CorrelationBindings &bindings,
                                               const QueryExecuteContext &context) override;
};
```

### 11.3 subquery_logic 函数

```cpp
LogicEvalResult evaluateExistsSubqueryNode(const LogicNode &node,
                                           const LogicRowContext &rowContext,
                                           const LogicEvalContext &evalContext);

LogicEvalResult evaluateInSubqueryNode(const LogicNode &node,
                                       const LogicRowContext &rowContext,
                                       const LogicEvalContext &evalContext);

LogicEvalResult evaluateQuantifiedSubqueryNode(const LogicNode &node,
                                               const LogicRowContext &rowContext,
                                               const LogicEvalContext &evalContext);

QList<setdef::SetValue> normalizeSelectResultToSet(const SelectRowsResult &result);
```

内部流程：

```text
判断是否相关子查询
-> 非相关则 executor.executeSelectSql(sql, context)
-> 相关则 executor.executeCorrelatedSelect(sql, bindings, context)
-> normalizeSelectResultToSet()
-> EXISTS / IN / ANY / ALL 求值
```

## 12. service 接入位置

本节放在靠后位置，原因是 `utils/logic` 和 `QueryExecutor` 的函数级收口应先定清楚，service 只负责接入落点。

### 12.1 WHERE

第一版正式落点：`QueryExecutor::executeSelectSql(...)`

完整候选行读取统一复用现有 service 签名：

```cpp
SelectRowsResult tuple_service::selectRows(const QString &tableName,
                                           const QStringList &projectionColumns,
                                           const QList<SimpleCondition> &conditions,
                                           int limit = -1);
```

第一版正式约定：

- `QueryExecutor` 不新增“读取完整候选行”的 service 接口。
- 当 `QueryExecutor` 需要为复杂 `WHERE` 构造完整候选行时，统一调用：

```cpp
tuple_service::selectRows(tableName, QStringList{}, QList<SimpleCondition>{}, -1)
```

- 其中 `projectionColumns = QStringList{}` 的语义固定为：按表 schema 顺序读取完整行。
- `QueryExecutor` 再基于“完整行结果 + table schema 元数据”构造 `QueryCandidateSet.fullRowContext`。
- 复杂谓词主链中不做 `SimpleCondition` 条件下推。
- `tuple_service::selectRows(...)` 的 `conditions` 参数在复杂谓词主链中固定传空列表。
- 复杂谓词主链中，`tuple_service::selectRows(...)` 的 `limit` 参数固定传 `-1`。
- `LIMIT` 只能在 `applyWhereAstToRows(...)` 和 `projectCandidateRows(...)` 之后生效。

对应新增完整候选行结构：

```cpp
struct QueryCandidateRow {
    LogicRowContext fullRowContext;
    QMap<QString, QString> projectedRow;
};

struct QueryCandidateSet {
    QList<QueryCandidateRow> rows;
    QStringList projection;
};
```

对应新增过滤函数：

```cpp
QueryCandidateSet applyWhereAstToRows(const QueryCandidateSet &inputRows,
                                      const LogicNode &whereAst,
                                      const LogicEvalContext &evalContext);

SelectRowsResult projectCandidateRows(const QueryCandidateSet &filteredRows);
```

输入：

- `inputRows`：包含完整 `LogicRowContext` 的候选行集合，而不是仅投影列结果
- `whereAst`：已经解析完成的 `WHERE` 谓词 AST
- `evalContext`：逻辑求值上下文

输出：

- `applyWhereAstToRows(...)` 输出过滤后的 `QueryCandidateSet`
- `projectCandidateRows(...)` 输出最终 `SelectRowsResult`

内部流程：

```text
遍历 inputRows.rows
-> 直接使用 row.fullRowContext
-> evaluateLogicExpression(whereAst, row.fullRowContext, evalContext)
-> 仅保留 truth == TRUE 的行
-> 返回新的 QueryCandidateSet
-> 过滤完成后再调用 projectCandidateRows(...)
```

第一版分层：

- parser：负责把 `WHERE` 表达式解析为 `LogicNode`
- `QueryExecutor::executeSelectSql(...)`：负责先通过 `tuple_service::selectRows(tableName, QStringList{}, QList<SimpleCondition>{}, -1)` 读取完整候选行
- `QueryExecutor::executeSelectSql(...)`：负责结合 table schema 元数据构造 `QueryCandidateSet.fullRowContext`
- `QueryExecutor::executeSelectSql(...)`：负责再根据 `payload["selectAll"] / payload["projection"]` 生成 `projectedRow`
- `applyWhereAstToRows(...)`：负责基于完整候选行逐行逻辑过滤
- `projectCandidateRows(...)`：负责在过滤后再按投影列生成最终结果
- `QueryExecutor::executeSelectSql(...)`：负责在过滤与投影完成后再应用 `LIMIT`
- service：不负责复杂谓词解释，只提供候选行读取

正式约束：

- 第一版 `WHERE` 复杂谓词过滤发生在 `QueryExecutor` 层
- 第一版 `WHERE` 复杂谓词主链不做 `SimpleCondition` 下推
- 第一版不要求 `tuple_service::selectRows(...)` 直接理解复杂谓词 AST
- 第一版 `WHERE` 允许引用未出现在最终 `SELECT projection` 中的列
- 第一版 `LIMIT` 在复杂谓词主链中只能作用于最终过滤结果，不能先于 `WHERE` 生效
- `FALSE / UNKNOWN` 行都被过滤掉

数据流：

```text
SELECT ... WHERE complex_predicate
-> parser 生成 whereAst
-> QueryExecutor::executeSelectSql(...)
-> tuple_service::selectRows(tableName, QStringList{}, QList<SimpleCondition>{}, -1)
-> QueryExecutor 基于完整候选行 + schema 构造 QueryCandidateSet(fullRowContext + projectedRow)
-> applyWhereAstToRows(candidateRows, whereAst, evalContext)
-> projectCandidateRows(filteredCandidateRows)
-> 对最终结果应用 LIMIT
-> 返回最终 SelectRowsResult
```

### 12.2 CHECK

第一版正式落点：

- 插入：`tuple_service::insertRows(...)`
- 更新：`tuple_service::updateRows(...)`

对应新增校验函数：

```cpp
LogicEvalResult evaluateCheckConstraintForRow(const LogicNode &checkAst,
                                              const LogicRowContext &candidateRowContext,
                                              const LogicEvalContext &evalContext);
```

输入：

- `checkAst`：单条 `CHECK` 约束对应的 AST
- `candidateRowContext`：已经补全后的候选整行上下文
- `evalContext`：逻辑求值上下文，且第一版固定 `allowSubquery = false`

输出：

- `LogicEvalResult`

内部流程：

```text
检查 evalContext.allowSubquery == false
-> 若 checkAst 含子查询节点则直接报错
-> evaluateLogicExpression(checkAst, candidateRowContext, evalContext)
-> truth == TRUE 则通过
-> truth == FALSE / UNKNOWN 则失败
```

插入时的数据流：

```text
tuple_service::insertRows(...)
-> 对每个待插入 row 补全默认值 / 缺省列
-> 构造 candidateRowContext
-> 对该表每个 CHECK 约束调用 evaluateCheckConstraintForRow(...)
-> 全部通过后才允许真正写入
```

更新时的数据流：

```text
tuple_service::updateRows(...)
-> 找到命中的旧行
-> 应用 assignmentMap 生成 candidateRow
-> 构造 candidateRowContext
-> 对该表每个 CHECK 约束调用 evaluateCheckConstraintForRow(...)
-> 全部通过后才允许真正落盘
```

明确限制：

- `CHECK` 第一阶段禁止子查询。
- `CHECK` 若检测到子查询节点，应直接返回错误。

## 13. 阶段拆分

### 阶段 A：纯谓词骨架

支持：

- `= != < <= > >=`
- `AND / OR / NOT`
- 括号
- 数字 / 字符串 / NULL 字面量
- 列名引用

### 阶段 B：集合与 NULL

支持：

- `IS NULL`
- `IS NOT NULL`
- `IN (literal list)`
- `NOT IN (literal list)`
- `BETWEEN literal AND literal`
- `NOT BETWEEN literal AND literal`

### 阶段 C：非相关子查询

支持：

- `EXISTS (SELECT ...)`
- `IN (SELECT ...)`
- `= ANY (SELECT ...)`
- `> ALL (SELECT ...)`

限制：

- 子查询不得引用外层列
- 子查询只允许 `SELECT`

### 阶段 D：相关子查询

支持：

- `EXISTS (SELECT ... WHERE inner.col = outer.id)`
- `col IN (SELECT inner.col FROM ... WHERE inner.fk = outer.id)`
- `col = ANY (SELECT ... WHERE inner.col = outer.id)`

限制：

- 外层引用必须显式前缀
- 通过 `CorrelationBindings` 传值
- 不做 SQL 字符串替换

## 14. 测试计划

### 14.1 tokenizer / parser

- `a = 1 AND b = 2`
- `NOT (a = 1 OR b = 2)`
- `a IN (1, 2, 3)`
- `a BETWEEN 1 AND 10`
- `a = ANY (SELECT id FROM t)`
- `EXISTS (SELECT id FROM t WHERE t.id = outer.id)`

### 14.2 evaluator

- 普通比较真值
- `NULL` 比较得到 `UNKNOWN`
- `AND / OR / NOT` 三值逻辑传播
- `IN` 和 `NOT IN`
- `BETWEEN` 和 `NOT BETWEEN`
- `ANY / ALL`
- `EXISTS`

### 14.3 接入测试

- `CHECK` 遇子查询直接拒绝
- `WHERE` 过滤保留 `TRUE`
- `WHERE` 中 `BETWEEN` 只保留闭区间命中行
- 非相关子查询通过 `QueryExecutor::executeSelectSql`
- 相关子查询通过 `QueryExecutor::executeCorrelatedSelect`

### 14.4 阶段 D 函数级测试表

#### parser / AST

测试文件落点：

- `test_logic.cpp`

```cpp
void test_parseCorrelatedExistsCollectsOuterReferences();
```

输入：

```sql
EXISTS (SELECT id FROM child WHERE child.parent_id = outer.id)
```

断言：

- parse 成功
- 根节点类型为 `ExistsSubquery`
- `subquerySql` 保留 `SELECT id FROM child WHERE child.parent_id = outer.id`
- `referencedOuterNames == {"outer.id"}`
- 外层引用节点被编码为 `LogicReferenceScope::Outer`

```cpp
void test_parseCorrelatedReferenceRejectsTableNameOrAliasPrefix();
```

输入：

```sql
EXISTS (SELECT id FROM child WHERE child.parent_id = parent.id)
```

断言：

- parse 失败
- 错误信息指出只允许 `outer.xxx`

#### bindings

测试文件落点：

- `test_logic.cpp`

```cpp
void test_buildCorrelationBindingsExtractsTypedOuterValues();
```

输入：

- `outerRowContext` 含 `outer.id = 10(INT, non-null)`
- `referencedOuterNames == {"outer.id"}`

断言：

- 返回一个 `CorrelationBindings`
- `items.size() == 1`
- `items[0].name == "outer.id"`
- `items[0].value == "10"`
- `items[0].type == INT`
- `items[0].isNull == false`

#### QueryExecutor

测试文件落点：

- `test_query_executor.cpp`

```cpp
void test_executeCorrelatedSelectRejectsMissingBinding();
```

输入：

- `subquerySql = "SELECT id FROM child WHERE child.parent_id = outer.id"`
- `bindings` 为空

断言：

- 执行失败
- 错误信息包含缺失绑定

```cpp
void test_executeCorrelatedSelectFiltersRowsUsingOuterBinding();
```

输入：

- 父行外层绑定 `outer.id = 1`
- 子表中包含 `parent_id = 1` 和 `parent_id = 2`

断言：

- 执行成功
- 只返回 `parent_id = 1` 对应的行

#### subquery_logic

测试文件落点：

- `test_logic.cpp`

```cpp
void test_evaluateCorrelatedExistsReturnsTrueWhenRowsExist();
void test_evaluateCorrelatedExistsReturnsFalseWhenRowsDoNotExist();
```

断言：

- 有匹配行时返回 `TRUE`
- 无匹配行时返回 `FALSE`

```cpp
void test_evaluateCorrelatedInSubqueryUsesOuterBinding();
```

输入：

- 表达式 `id IN (SELECT child.parent_id FROM child WHERE child.parent_id = outer.id)`

断言：

- 外层 `outer.id` 命中时返回 `TRUE`
- 不命中时返回 `FALSE`

#### no-cache behavior

测试文件落点：

- `test_query_executor.cpp`

```cpp
void test_correlatedSubqueryExecutesPerRowWithoutCache();
```

断言：

- 第一版不依赖缓存命中
- 多个外层行分别触发独立相关子查询执行

### 14.5 第一版正式测试要求

- 阶段 D 至少要覆盖 `EXISTS`、`IN (SELECT ...)` 两类相关子查询
- 至少覆盖 1 个绑定缺失错误分支
- 至少覆盖 1 个非法前缀错误分支
- 至少覆盖 1 个逐行执行、无缓存假设的行为断言
- 除 `test_query_executor.cpp` 外，其余逻辑相关测试统一进入 `test_logic.cpp`
