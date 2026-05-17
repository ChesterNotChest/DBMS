# GROUP_BY

目标：在现有 SELECT / WHERE / ORDER BY / LIMIT / 单表与多表 JOIN 查询能力之上，补齐 `GROUP BY`、`HAVING` 与基础聚组函数能力。

本文只规划聚合查询能力，不规划新的表达式系统。也就是说，本计划支持常见聚合 SQL 的名字解析、分组、聚合计算和 HAVING 过滤，但不引入算术表达式投影、函数嵌套、窗口函数、DISTINCT 聚合或 GROUP BY 表达式。

## 总体收口边界

### 支持

1. 基础聚组函数：

```sql
COUNT(*)
COUNT(col)
SUM(col)
AVG(col)
MIN(col)
MAX(col)
```

2. 无 `GROUP BY` 的全表聚合：

```sql
SELECT COUNT(*) FROM student;
SELECT AVG(score) AS avg_score FROM student WHERE class_id = 1;
```

3. 单列 / 多列 `GROUP BY`：

```sql
SELECT class_id, COUNT(*) FROM student GROUP BY class_id;
SELECT class_id, gender, AVG(score) FROM student GROUP BY class_id, gender;
```

4. `HAVING`：

```sql
SELECT class_id, COUNT(*) AS n
FROM student
GROUP BY class_id
HAVING COUNT(*) > 3;
```

5. 投影别名：

```sql
SELECT class_id, COUNT(*) AS n
FROM student
GROUP BY class_id
HAVING n > 3
ORDER BY n DESC;
```

6. 多表来源上的聚合：

```sql
SELECT c.name, COUNT(s.id) AS n
FROM student s JOIN class c ON s.class_id = c.id
GROUP BY c.name;
```

### 不支持

1. `COUNT(DISTINCT col)`。
2. 聚合函数嵌套，例如 `MAX(COUNT(*))`。
3. 表达式聚合参数，例如 `SUM(a + b)`。
4. 表达式投影，例如 `SELECT class_id + 1`。
5. `GROUP BY` 表达式，例如 `GROUP BY YEAR(created_at)`。
6. `HAVING` 中使用非分组、非聚合列。
7. 窗口函数，例如 `COUNT(*) OVER (...)`。
8. `ROLLUP` / `CUBE` / `GROUPING SETS`。
9. `FILTER (WHERE ...)` 聚合子句。
10. `ORDER BY` 多列，本计划保持当前单列 ORDER BY 能力边界。

### SQL 阶段顺序

运行时顺序必须固定为：

```text
FROM / JOIN
-> WHERE
-> GROUP BY
-> aggregate projection
-> HAVING
-> ORDER BY
-> LIMIT
```

### 聚合查询合法性规则

1. 存在聚合函数但无 `GROUP BY`：全体过滤后行组成一个全局 group。
2. 存在 `GROUP BY`：投影中的非聚合列必须来自 `GROUP BY` 列。
3. `HAVING` 只能在聚合查询中出现：
   - 有 `GROUP BY`，或
   - 有聚合函数。
4. `SELECT *` 在聚合查询中不支持。
5. `COUNT(*)` 对 group 内所有行计数。
6. `COUNT(col)` 只统计非 NULL-like 值。
7. `SUM` / `AVG` 只接受 `INT` / `FLOAT` 列；空值跳过。
8. `MIN` / `MAX` 使用列类型比较；空值跳过。
9. 对空 group：
   - 正常 `GROUP BY` 不产生空 group。
   - 全表聚合在 WHERE 过滤后无行时仍产生一行：
     - `COUNT(*) = 0`
     - 其他聚合结果为空字符串 / NULL-like。

---

## 阶段一：SQL tokenizer 与 SELECT payload 扩展

### 0. 常量增量

新增 `TokenType`：

1. `GROUP`
2. `HAVING`
3. `COUNT`
4. `SUM`
5. `AVG`
6. `MIN`
7. `MAX`

新增 parser payload key，建议只在 SELECT payload 中使用：

1. `groupByColumns`
2. `havingText`
3. `havingAst`
4. `havingAggregateItems`
5. `aggregateItems`
6. `hasAggregation`
7. `isAggregateQuery`

新增 aggregate item map 字段：

1. `functionName`
2. `argument`
3. `isStar`
4. `sourceText`
5. `outputColumn`
6. `syntheticName`

### 1. 影响文件范围

允许修改：

1. [utils/sql_parser/sql_tokenizer.h](utils/sql_parser/sql_tokenizer.h)
2. [utils/sql_parser/sql_tokenizer.cpp](utils/sql_parser/sql_tokenizer.cpp)
3. [utils/sql_parser/tuple_parser.cpp](utils/sql_parser/tuple_parser.cpp)
4. [tests/test_parser_dispatcher.cpp](tests/test_parser_dispatcher.cpp)
5. [tests/parserNdispatcher_test_plan.md](tests/parserNdispatcher_test_plan.md)

首选不修改：

1. `utils/sql_parser/sql_parser.h`
2. `service/service.h`

### 2. 函数级完整数据流

#### 2.1 GROUP BY 解析

```text
SQL:
SELECT class_id, COUNT(*) AS n
FROM student
WHERE score > 60
GROUP BY class_id
HAVING n > 3
ORDER BY n DESC
LIMIT 5

-> SqlTokenizer::keywordLookup(...)
   GROUP / HAVING / COUNT 被识别为 keyword token

-> parseTupleSql(...)
   fromIdx / whereIdx / groupIdx / havingIdx / orderIdx / limitIdx

-> parseProjectionItems(...)
   projectionItems:
     {sourceColumn: class_id, outputColumn: class_id, itemKind: column}
     {sourceColumn: COUNT(*), outputColumn: n, itemKind: aggregate}
   aggregateItems:
     {functionName: COUNT, argument: "", isStar: true, sourceText: COUNT(*), outputColumn: n, syntheticName: __agg_0}

-> parseGroupByClause(...)
   groupByColumns = ["class_id"]

-> parseHavingClause(...)
   havingText = "n > 3"
   havingAst = LogicNode(Comparison n > 3)

-> payload.hasAggregation = true
-> payload.isAggregateQuery = true
```

#### 2.2 全表聚合解析

```text
SQL:
SELECT COUNT(*) FROM student;

-> groupByColumns = []
-> aggregateItems = [COUNT(*)]
-> hasAggregation = true
-> isAggregateQuery = true
```

#### 2.3 非聚合 SELECT 兼容

```text
SQL:
SELECT id, name FROM student WHERE id = 1;

-> 不产生 groupByColumns / aggregateItems / havingAst
-> hasAggregation = false
-> isAggregateQuery = false
-> 继续走现有 SELECT 路径
```

### 3. 函数级输入输出与内部逻辑

#### 3.1 `SqlTokenizer::keywordLookup(const QString &word)`

输入：

1. 原始 word。

输出：

1. `GROUP` -> `TokenType::GROUP`
2. `HAVING` -> `TokenType::HAVING`
3. `COUNT` / `SUM` / `AVG` / `MIN` / `MAX` -> 对应 token。

内部逻辑：

1. 不改变普通 identifier 扫描规则。
2. 聚合函数名作为 keyword token，只在 SELECT projection / HAVING 中被特殊解析。

#### 3.2 `parseTupleSql(const QString &sql, const QVector<SqlToken> &tokens)`

输入：

1. 原始 SELECT SQL。
2. tokenizer 输出 token。

输出：

1. 成功时 payload 新增：
   - `groupByColumns`
   - `aggregateItems`
   - `hasAggregation`
   - `isAggregateQuery`
   - 可选 `havingText`
   - 可选 `havingAst`
2. 失败时返回明确错误：
   - `GROUP BY: expected column name`
   - `HAVING requires GROUP BY or aggregate projection`
   - `SELECT: aggregate function expects '('`
   - `SELECT: unsupported aggregate argument`

内部逻辑：

1. clause 顺序必须校验：

```text
FROM
WHERE?
GROUP BY?
HAVING?
ORDER BY?
LIMIT?
```

2. `WHERE` 终止点从 `ORDER/LIMIT` 扩展为 `GROUP/HAVING/ORDER/LIMIT`。
3. `GROUP BY` 终止点为 `HAVING/ORDER/LIMIT/;/EOF`。
4. `HAVING` 终止点为 `ORDER/LIMIT/;/EOF`。
5. `parseSelectLimit` 必须允许扫描 `GROUP/HAVING/ORDER/LIMIT` 后的 trailing token，不能吞掉非法尾巴。

#### 3.3 `parseProjectionItems(...)`

输入：

1. SELECT 与 FROM 之间的 token range。

输出：

1. 旧字段：
   - `projection`
   - `projectionItems`
2. 新字段：
   - `aggregateItems`
   - `hasAggregation`

内部逻辑：

1. 支持普通列：

```text
id
s.id
id AS sid
```

2. 支持聚合项：

```text
COUNT(*)
COUNT(id)
SUM(score)
AVG(s.score)
MIN(name)
MAX(name)
COUNT(*) AS n
COUNT(*) n
```

3. 聚合 item 写入：

```text
projectionItems[i].itemKind = aggregate
projectionItems[i].aggregateIndex = k
aggregateItems[k].syntheticName = __agg_k
```

4. 不支持：

```sql
COUNT(DISTINCT id)
SUM(a + b)
COUNT()
COUNT(*, id)
```

#### 3.4 `parseGroupByClause(...)`

输入：

1. token 列表。
2. `GROUP` token index。
3. group clause end index。

输出：

1. `QStringList groupByColumns`

内部逻辑：

1. 必须是 `GROUP BY`。
2. 只允许逗号分隔限定列名：

```sql
GROUP BY id
GROUP BY s.class_id, c.name
```

3. 不允许空列、尾逗号、表达式。

#### 3.5 `parseHavingClause(...)`

输入：

1. 原始 SQL。
2. tokens。
3. `HAVING` index。
4. having end index。
5. projection aggregate aliases。

输出：

1. `havingText`
2. `havingAst`
3. `havingAggregateItems`

内部逻辑：

1. 第一阶段建议只允许 HAVING 中引用：
   - group column
   - aggregate output alias
   - 聚合函数调用
   - literal
   - AND / OR / NOT / IS NULL / LIKE / 比较运算
2. 聚合函数调用需要被改写为 synthetic identifier：

```text
HAVING COUNT(*) > 3
-> rewrittenHavingText: __agg_0 > 3
```

3. 如果 HAVING 使用 projection alias：

```text
HAVING n > 3
```

执行层解析时通过 aggregate row context 找到 `n`。

4. 不允许 HAVING 子查询，除非后续显式扩展。

### 4. 测试用例构建

Parser 测试：

1. `SELECT COUNT(*) FROM student`
   - `hasAggregation = true`
   - `aggregateItems[0].functionName = COUNT`
   - `aggregateItems[0].isStar = true`
2. `SELECT class_id, COUNT(*) AS n FROM student GROUP BY class_id`
   - `groupByColumns = ["class_id"]`
   - `projectionItems` 包含普通列和 aggregate。
3. `SELECT class_id FROM student GROUP BY class_id HAVING COUNT(*) > 1`
   - `havingAst` 存在。
4. 失败用例：
   - `SELECT COUNT() FROM student`
   - `SELECT SUM(a + b) FROM student`
   - `SELECT id FROM student GROUP`
   - `SELECT id FROM student HAVING COUNT(*) > 1`
   - `SELECT id FROM student GROUP BY id WHERE id = 1`

---

## 阶段二：聚合查询执行模型与公共数据结构

### 0. 常量增量

新增局部枚举和结构体，建议位于 [controller/nest_query.cpp](controller/nest_query.cpp) 匿名 namespace：

```cpp
enum class AggregateFunction {
    Count,
    Sum,
    Avg,
    Min,
    Max,
};

struct AggregateSpec {
    AggregateFunction function;
    QString argumentName;
    QString resolvedArgumentKey;
    bool isStar = false;
    QString sourceText;
    QString syntheticName;
    QString outputName;
    tabledef::ColumnType outputType = tabledef::ColumnType::Varchar;
};

struct AggregateProjectionItem {
    bool isAggregate = false;
    QString sourceName;
    QString resolvedKey;
    int aggregateIndex = -1;
    QString outputName;
    tabledef::ColumnType outputType = tabledef::ColumnType::Varchar;
};

struct GroupedRows {
    QString groupKey;
    logic::LogicRowContext representative;
    QVector<logic::LogicRowContext> rows;
};
```

### 1. 影响文件范围

允许修改：

1. [controller/nest_query.cpp](controller/nest_query.cpp)
2. [controller/sql_dispatcher.cpp](controller/sql_dispatcher.cpp)
3. [tests/test_query_executor.cpp](tests/test_query_executor.cpp)
4. [tests/test_parser_dispatcher.cpp](tests/test_parser_dispatcher.cpp)

首选不修改：

1. `service/tuple_service.cpp`
2. `service/table_dml_service.cpp`
3. `repo/*`

### 2. 函数级完整数据流

#### 2.1 单表聚合

```text
SqlDispatcher::execSelect(p)
-> payload.isAggregateQuery = true
-> QueryExecutor::executeParsed(p)
-> QueryExecutor::execSelect(...)
-> load schema/table
-> build rowContext for each row
-> WHERE filter
-> groupRows(filteredRows, groupByColumns)
-> computeAggregateRows(groups, aggregateSpecs)
-> HAVING filter on aggregate rowContext
-> ORDER BY aggregate alias/group column
-> LIMIT
-> SelectRowsResult
```

#### 2.2 多表聚合

```text
QueryExecutor::execMultiTableSelect(...)
-> load sources
-> JOIN / comma-product rows
-> WHERE filter
-> groupRows(filteredRows, groupByColumns)
-> computeAggregateRows(groups, aggregateSpecs)
-> HAVING filter
-> ORDER BY
-> projection emit
```

### 3. 函数级输入输出与内部逻辑

#### 3.1 `SqlDispatcher::execSelect(const ParseResult &p)`

输入：

1. SELECT ParseResult。

输出：

1. 聚合查询交给 `QueryExecutor`。
2. 非聚合简单查询保持现有 tuple_service 快路径。

内部逻辑：

```cpp
if (p.payload.value("isAggregateQuery").toBool()
    || p.payload.value("hasComplexWhere").toBool()
    || p.payload.value("isMultiTable").toBool()) {
    QueryExecutor executor;
    return executor.executeParsed(...);
}
```

#### 3.2 `collectInputRowsForSelect(...)`

输入：

1. ParseResult。
2. 可选 correlated bindings。

输出：

1. `QVector<logic::LogicRowContext> filteredRows`
2. `NameResolution` / `MultiNameResolution`

内部逻辑：

1. 单表：
   - load schema/data
   - build rowContext
   - merge correlated bindings
   - WHERE filter
2. 多表：
   - 复用 `execMultiTableSelect` 当前 FROM/JOIN/WHERE 阶段。
   - 建议抽出 `buildJoinedFilteredRows(...)`，避免聚合和非聚合多表 SELECT 复制逻辑。

#### 3.3 `resolveAggregateSpecs(...)`

输入：

1. payload.aggregateItems。
2. 当前 name resolution。

输出：

1. `QList<AggregateSpec>`

内部逻辑：

1. `COUNT(*)` 不解析列。
2. `COUNT(col)` 解析 `col` / `table.col` / `alias.col`。
3. `SUM/AVG` 解析列后校验类型为 `INT` 或 `FLOAT`。
4. `MIN/MAX` 允许所有基础类型。
5. 输出类型：
   - `COUNT` -> `Int`
   - `SUM(INT)` -> `Int`
   - `SUM(FLOAT)` -> `Float`
   - `AVG(...)` -> `Float`
   - `MIN/MAX` -> 参数列类型

#### 3.4 `groupRows(...)`

输入：

1. filtered row contexts。
2. resolved group keys。

输出：

1. `QVector<GroupedRows>`

内部逻辑：

1. 无 `GROUP BY` 且有聚合：
   - 返回一个 group。
   - 如果 filtered rows 为空，group.rows 为空，representative 为空 row context。
2. 有 `GROUP BY`：
   - 对每行按 group key 值拼接 groupKey。
   - NULL-like 值作为独立可比较 key。
   - representative 使用该 group 第一行。
3. group key 序列化必须避免简单字符串碰撞，建议使用长度前缀：

```text
len:value|len:value
```

#### 3.5 `computeAggregateValue(...)`

输入：

1. `AggregateSpec`
2. `GroupedRows`

输出：

1. `logic::LogicCellValue`

内部逻辑：

1. `COUNT(*)`：
   - 返回 group.rows.size()
2. `COUNT(col)`：
   - 统计 `!cell.isNull && !cell.value.isEmpty()`
3. `SUM`：
   - 跳过 NULL-like。
   - 无非空值返回 NULL-like。
4. `AVG`：
   - `SUM / countNonNull`
   - 无非空值返回 NULL-like。
5. `MIN/MAX`：
   - 跳过 NULL-like。
   - 使用当前 `compareCellValues(...)`。

#### 3.6 `buildAggregateRowContext(...)`

输入：

1. `GroupedRows`
2. group projection / group keys。
3. aggregate specs。

输出：

1. `logic::LogicRowContext aggregateRow`

内部逻辑：

1. 写入 group columns：

```text
class_id
student.class_id
s.class_id
```

2. 写入 aggregate synthetic name：

```text
__agg_0
```

3. 写入 aggregate source text：

```text
COUNT(*)
SUM(score)
```

4. 写入 projection alias：

```text
n
avg_score
```

5. HAVING 和 ORDER BY 都在这个 aggregate rowContext 上评估。

#### 3.7 `resolveAggregateProjection(...)`

输入：

1. projectionItems payload。
2. groupByColumns。
3. aggregate specs。
4. name resolution。

输出：

1. `QList<AggregateProjectionItem>`
2. output columns。
3. output types。

内部逻辑：

1. 普通列必须是 group key。
2. 聚合项通过 aggregateIndex 绑定 spec。
3. 输出列名使用 projection alias。
4. `SELECT *` 直接拒绝：

```text
SELECT * is not supported in aggregate queries
```

#### 3.8 `evaluateHaving(...)`

输入：

1. 可选 `havingAst`。
2. aggregate rowContext。

输出：

1. true/false。

内部逻辑：

1. 无 HAVING -> true。
2. `LogicTruthValue::True` -> true。
3. `False/Unknown` -> false。
4. missing column -> 错误返回。

### 4. 测试用例构建

QueryExecutor 测试：

1. 全表聚合：

```sql
SELECT COUNT(*) AS n FROM child;
```

期望：

```text
columns = ["n"]
rows = [["2"]]
type = Int
```

2. WHERE 后聚合：

```sql
SELECT COUNT(*) FROM child WHERE parent_id = 10;
```

期望只统计过滤后行。

3. GROUP BY 单列：

```sql
SELECT parent_id, COUNT(*) AS n FROM child GROUP BY parent_id;
```

期望每个 parent_id 一行。

4. GROUP BY + HAVING：

```sql
SELECT parent_id, COUNT(*) AS n
FROM child
GROUP BY parent_id
HAVING n > 1;
```

期望只返回 n > 1 的 group。

5. AVG / SUM / MIN / MAX：

```sql
SELECT SUM(score), AVG(score), MIN(name), MAX(name) FROM student;
```

期望类型和值正确。

6. 多表聚合：

```sql
SELECT c.name, COUNT(s.id) AS n
FROM student s JOIN class c ON s.class_id = c.id
GROUP BY c.name;
```

期望 JOIN 后分组正确。

失败用例：

1. `SELECT id, COUNT(*) FROM child`
   - 非聚合列不在 GROUP BY。
2. `SELECT * FROM child GROUP BY parent_id`
   - aggregate query 不支持 `*`。
3. `SELECT SUM(name) FROM child`
   - SUM 参数不是数值列。
4. `SELECT parent_id FROM child HAVING COUNT(*) > 1`
   - HAVING 没有 GROUP BY / aggregate projection。

---

## 阶段三：HAVING 中聚合函数与别名解析

### 0. 常量增量

本阶段不新增全局常量。

建议新增局部 helper 约定：

```text
__agg_0
__agg_1
...
```

### 1. 影响文件范围

允许修改：

1. [utils/sql_parser/tuple_parser.cpp](utils/sql_parser/tuple_parser.cpp)
2. [controller/nest_query.cpp](controller/nest_query.cpp)
3. [tests/test_logic.cpp](tests/test_logic.cpp)
4. [tests/test_query_executor.cpp](tests/test_query_executor.cpp)

首选不修改：

1. `utils/logic/logic_types.h`
2. `utils/logic/logic_parser.cpp`

如果 HAVING 聚合函数改写无法局部完成，才考虑给 logic parser 增加函数调用节点；首选不扩大 logic AST。

### 2. 函数级完整数据流

```text
HAVING COUNT(*) > 1 AND AVG(score) >= 60
-> parseHavingClause(...)
-> find aggregate calls
-> aggregateItems append missing specs:
   COUNT(*) -> __agg_0
   AVG(score) -> __agg_1
-> rewrittenHavingText:
   __agg_0 > 1 AND __agg_1 >= 60
-> logic::parseLogicTokens(...)
-> havingAst
-> buildAggregateRowContext(...)
   __agg_0 = computed count
   __agg_1 = computed avg
-> evaluateLogicExpression(havingAst, aggregateRowContext)
```

### 3. 函数级输入输出与内部逻辑

#### 3.1 `rewriteHavingAggregates(...)`

输入：

1. `havingText`
2. projection aggregate specs。

输出：

1. rewritten text。
2. merged aggregateItems。

内部逻辑：

1. 扫描 `COUNT|SUM|AVG|MIN|MAX` + parenthesized argument。
2. 对相同 `sourceText` 复用已有 spec。
3. 对新 aggregate call 追加 spec。
4. 字符串字面量内部不扫描。
5. 括号必须匹配。

#### 3.2 `bindHavingAliases(...)`

输入：

1. projection aliases。
2. aggregate rowContext。

输出：

1. rowContext 增加 alias -> aggregate value / group value。

内部逻辑：

1. projection alias 优先于 source text。
2. alias 冲突时 parser 或 resolver 应拒绝：

```text
SELECT COUNT(*) AS n, SUM(score) AS n ...
-> SELECT: duplicate output alias 'n'
```

### 4. 测试用例构建

1. HAVING 直接聚合函数：

```sql
SELECT parent_id FROM child GROUP BY parent_id HAVING COUNT(*) > 1;
```

2. HAVING 投影别名：

```sql
SELECT parent_id, COUNT(*) AS n FROM child GROUP BY parent_id HAVING n > 1;
```

3. HAVING 多聚合：

```sql
SELECT parent_id, COUNT(*) AS n
FROM child
GROUP BY parent_id
HAVING COUNT(*) > 1 AND AVG(score) >= 60;
```

4. HAVING 非法列：

```sql
SELECT parent_id, COUNT(*) FROM child GROUP BY parent_id HAVING score > 60;
```

期望：报错。

---

## 阶段四：ORDER BY / LIMIT 与输出格式收口

### 0. 常量增量

本阶段不新增常量。

### 1. 影响文件范围

允许修改：

1. [controller/nest_query.cpp](controller/nest_query.cpp)
2. [controller/sql_dispatcher.cpp](controller/sql_dispatcher.cpp)
3. [tests/test_query_executor.cpp](tests/test_query_executor.cpp)
4. [tests/test_parser_dispatcher.cpp](tests/test_parser_dispatcher.cpp)

### 2. 函数级完整数据流

```text
aggregate row contexts
-> HAVING filter
-> resolve ORDER BY against aggregate output aliases / group columns / aggregate source text
-> sort rows
-> LIMIT
-> emit SelectRowsResult
```

### 3. 函数级输入输出与内部逻辑

#### 3.1 `resolveAggregateOrderBy(...)`

输入：

1. payload.orderByColumn。
2. aggregate projection items。
3. aggregate rowContext key map。

输出：

1. order key。
2. order type。
3. descending。

内部逻辑：

1. 优先匹配 projection output alias。
2. 再匹配 group column。
3. 再匹配 aggregate source text，例如 `COUNT(*)`。
4. 不支持多列 ORDER BY。

#### 3.2 `emitAggregateSelectResult(...)`

输入：

1. aggregate rows。
2. projection items。
3. output columns。
4. column types。
5. limit。

输出：

1. `SelectRowsResult`

内部逻辑：

1. 按 projection item 读取 aggregate rowContext。
2. 空值输出空字符串。
3. `affectedRowCount = resultTable.rows.size()`。

### 4. 测试用例构建

1. ORDER BY aggregate alias：

```sql
SELECT parent_id, COUNT(*) AS n
FROM child
GROUP BY parent_id
ORDER BY n DESC;
```

2. ORDER BY group column：

```sql
SELECT parent_id, COUNT(*) AS n
FROM child
GROUP BY parent_id
ORDER BY parent_id ASC;
```

3. LIMIT after aggregate：

```sql
SELECT parent_id, COUNT(*) AS n
FROM child
GROUP BY parent_id
ORDER BY n DESC
LIMIT 1;
```

---

## 阶段五：文档、回归与错误口径

### 0. 常量增量

本阶段不新增常量。

### 1. 影响文件范围

允许修改：

1. [tests/TEST_PLAN.md](tests/TEST_PLAN.md)
2. [tests/parserNdispatcher_test_plan.md](tests/parserNdispatcher_test_plan.md)
3. [tests/test_content_mapping.md](tests/test_content_mapping.md)

不修改：

1. 性能压测规模。
2. 索引排序图表。

### 2. 函数级完整数据流

本阶段不新增运行时数据流，只文档化：

```text
SELECT parser payload
-> name resolution
-> WHERE filtered input rows
-> group rows
-> aggregate row context
-> HAVING
-> ORDER/LIMIT
-> SelectRowsResult
```

### 3. 函数级输入输出与内部逻辑

统一错误信息建议：

1. `GROUP BY: expected column name`
2. `GROUP BY column 'x' does not exist`
3. `SELECT: non-aggregate column 'x' must appear in GROUP BY`
4. `SELECT: '*' is not supported in aggregate queries`
5. `aggregate function 'SUM' requires a numeric column`
6. `HAVING requires GROUP BY or aggregate projection`
7. `HAVING column 'x' does not exist in aggregate result`
8. `ORDER BY column 'x' does not exist in aggregate result`

### 4. 测试用例构建

最终回归命令：

```powershell
& 'E:\Qt\Tools\CMake_64\bin\cmake.exe' --build build/codex-vs-debug --config Debug --parallel 1
```

```powershell
$env:QT_QPA_PLATFORM='offscreen'
$env:PATH='E:\Qt\6.9.2\msvc2022_64\bin;' + $env:PATH
& 'E:\Qt-projects\DBMS\build\codex-vs-debug\Debug\DBMS.exe' --run-tests --skip-stress-tests -o -,txt
```

如聚合实现影响多表 JOIN 或 WHERE 过滤性能，再额外跑完整 stress：

```powershell
$env:QT_QPA_PLATFORM='offscreen'
$env:PATH='E:\Qt\6.9.2\msvc2022_64\bin;' + $env:PATH
& 'E:\Qt-projects\DBMS\build\codex-vs-debug\Debug\DBMS.exe' --run-tests -o -,txt
```

---

## 实施顺序建议

1. 阶段一先只完成 parser payload，不接执行。
2. 阶段二完成无 HAVING 的聚合执行：
   - `COUNT(*)`
   - `COUNT(col)`
   - `GROUP BY`
3. 阶段三完成 HAVING 与聚合函数重写。
4. 阶段四完成 ORDER BY / LIMIT / 输出类型。
5. 阶段五补文档与全量回归。

不要先做 HAVING。HAVING 依赖 aggregate rowContext，如果先做 HAVING，会把 parser、聚合计算和名字解析三个问题缠在一起。

---

## 严格测试先行矩阵

实现前建议先把本节测试写入 [tests/test_parser_dispatcher.cpp](tests/test_parser_dispatcher.cpp) 与 [tests/test_query_executor.cpp](tests/test_query_executor.cpp)，允许红灯，然后按阶段实现到全绿。

这组用例用于保障第一版就能覆盖：

1. 全局聚合。
2. WHERE 后聚合。
3. 单列 / 多列 GROUP BY。
4. HAVING 聚合函数与投影别名。
5. 多表 JOIN 后聚合。
6. ORDER BY aggregate alias。
7. LIMIT 在聚合结果之后生效。
8. 非法聚合 SQL 明确失败。

### 必须通过的查询

#### 1. 单表全局聚合

```sql
SELECT COUNT(*) AS n FROM student;
```

期望：

```text
columns = ["n"]
rows = [["总行数"]]
```

```sql
SELECT COUNT(score) AS n, SUM(score) AS s, AVG(score) AS a FROM student;
```

期望：

```text
COUNT(score) 跳过 NULL-like score
SUM(score) / AVG(score) 只基于非空数值 score
```

#### 2. WHERE 后聚合

```sql
SELECT COUNT(*) AS n FROM student WHERE score >= 60;
```

期望：

```text
只统计 WHERE 过滤后的行
```

#### 3. 单列 GROUP BY

```sql
SELECT class_id, COUNT(*) AS n
FROM student
GROUP BY class_id
ORDER BY class_id ASC;
```

期望：

```text
每个 class_id 一行
class_id 升序
n 为对应分组行数
```

#### 4. 多列 GROUP BY

```sql
SELECT class_id, gender, AVG(score) AS avg_score
FROM student
GROUP BY class_id, gender
ORDER BY avg_score DESC;
```

期望：

```text
按 class_id + gender 组合分组
avg_score 为每组非空 score 均值
ORDER BY 使用聚合别名
```

#### 5. HAVING 使用聚合函数

```sql
SELECT class_id
FROM student
GROUP BY class_id
HAVING COUNT(*) >= 2;
```

期望：

```text
只返回分组行数 >= 2 的 class_id
```

#### 6. HAVING 使用投影别名

```sql
SELECT class_id, COUNT(*) AS n
FROM student
GROUP BY class_id
HAVING n >= 2
ORDER BY n DESC;
```

期望：

```text
HAVING n 使用 COUNT(*) 的投影别名
ORDER BY n 使用同一投影别名
```

#### 7. 多表 JOIN 后 GROUP BY

```sql
SELECT c.name, COUNT(s.id) AS n
FROM student s JOIN class c ON s.class_id = c.id
GROUP BY c.name
ORDER BY n DESC;
```

期望：

```text
先 JOIN，再按 c.name 分组
COUNT(s.id) 统计匹配学生数
```

#### 8. 多表 + WHERE + GROUP BY + HAVING + ORDER BY alias + LIMIT

```sql
SELECT c.name AS class_name, COUNT(s.id) AS n, AVG(s.score) AS avg_score
FROM student s JOIN class c ON s.class_id = c.id
WHERE s.score >= 60
GROUP BY c.name
HAVING n >= 2
ORDER BY avg_score DESC
LIMIT 1;
```

期望：

```text
执行顺序为 JOIN -> WHERE -> GROUP BY -> HAVING -> ORDER BY -> LIMIT
只返回 avg_score 最高的一组
输出列名为 class_name / n / avg_score
```

### 必须失败的查询

#### 1. 聚合查询中 SELECT *

```sql
SELECT * FROM student GROUP BY class_id;
```

期望：

```text
SELECT: '*' is not supported in aggregate queries
```

#### 2. 非聚合列不在 GROUP BY

```sql
SELECT id, COUNT(*) FROM student;
```

期望：

```text
SELECT: non-aggregate column 'id' must appear in GROUP BY
```

```sql
SELECT class_id, score FROM student GROUP BY class_id;
```

期望：

```text
SELECT: non-aggregate column 'score' must appear in GROUP BY
```

#### 3. SUM 非数值列

```sql
SELECT SUM(name) FROM student;
```

期望：

```text
aggregate function 'SUM' requires a numeric column
```

#### 4. DISTINCT 聚合不支持

```sql
SELECT COUNT(DISTINCT id) FROM student;
```

期望：

```text
SELECT: unsupported aggregate argument
```

#### 5. HAVING 缺少聚合上下文

```sql
SELECT class_id FROM student HAVING COUNT(*) > 1;
```

期望：

```text
HAVING requires GROUP BY or aggregate projection
```

#### 6. HAVING 引用非分组、非聚合列

```sql
SELECT class_id, COUNT(*) AS n
FROM student
GROUP BY class_id
HAVING score > 60;
```

期望：

```text
HAVING column 'score' does not exist in aggregate result
```

#### 7. ORDER BY 未知别名

```sql
SELECT class_id, COUNT(*) AS n
FROM student
GROUP BY class_id
ORDER BY unknown_alias;
```

期望：

```text
ORDER BY column 'unknown_alias' does not exist in aggregate result
```

---

## 验收口径

完成后以下 SQL 必须通过：

```sql
SELECT COUNT(*) AS n FROM student;
SELECT class_id, COUNT(*) AS n FROM student GROUP BY class_id;
SELECT class_id, AVG(score) AS avg_score FROM student GROUP BY class_id HAVING avg_score >= 60;
SELECT c.name, COUNT(s.id) AS n
FROM student s JOIN class c ON s.class_id = c.id
GROUP BY c.name
ORDER BY n DESC
LIMIT 1;
```

以下 SQL 必须继续失败：

```sql
SELECT * FROM student GROUP BY class_id;
SELECT id, COUNT(*) FROM student;
SELECT SUM(name) FROM student;
SELECT COUNT(DISTINCT id) FROM student;
SELECT COUNT(*) FROM student HAVING unknown_col > 1;
SELECT class_id FROM student HAVING COUNT(*) > 1;
```

完成后不得破坏：

1. 普通 `SELECT * FROM t WHERE id = 1`。
2. 别名 SELECT：

```sql
SELECT s.id AS sid FROM student s ORDER BY sid DESC;
```

3. 多表 JOIN：

```sql
SELECT s.id, c.name FROM student s JOIN class c ON s.class_id = c.id;
```

4. 相关子查询：

```sql
SELECT * FROM parent p
WHERE EXISTS (
    SELECT * FROM child c WHERE c.parent_id = p.id
);
```

5. `LIKE` WHERE：

```sql
SELECT * FROM student WHERE name LIKE 'A%';
```

