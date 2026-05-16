# FROM_MULTI_PLAN

目标：在现有单表 `SELECT`、表别名、限定列名、投影别名和相关子查询别名绑定能力基础上，补齐多表 `FROM` 查询能力。

本文同时规划两种语法：

1. 逗号多表：

```sql
SELECT a.id, b.name FROM a, b WHERE a.id = b.a_id;
```

2. 显式 JOIN：

```sql
SELECT a.id, b.name FROM a JOIN b ON a.id = b.a_id;
```

本文规划 `INNER`、`LEFT`、`RIGHT`、`FULL` 四类 JOIN 语义。`JOIN` 不带修饰词时视为 `INNER JOIN`；显式 `INNER JOIN` 可作为同义支持。本文不规划 `NATURAL JOIN`、`CROSS JOIN`、`USING`、FROM 子查询、GROUP BY、聚合函数、UNION 或表达式投影。

## 总体收口边界

### 支持

1. 多表逗号 FROM：

```sql
SELECT a.id, b.name FROM a, b WHERE a.id = b.a_id;
```

2. 多表别名：

```sql
SELECT s.id, c.name FROM student s, class c WHERE s.class_id = c.id;
SELECT s.id FROM student AS s, class AS c WHERE s.class_id = c.id;
```

3. 显式 JOIN ... ON：

```sql
SELECT s.id, c.name FROM student s JOIN class c ON s.class_id = c.id;
SELECT s.id FROM student AS s INNER JOIN class AS c ON s.class_id = c.id;
```

4. 显式外连接，并允许别名：

```sql
SELECT s.id, c.name FROM student s LEFT JOIN class c ON s.class_id = c.id;
SELECT s.id, c.name FROM student AS s RIGHT JOIN class AS c ON s.class_id = c.id;
SELECT s.id, c.name FROM student s FULL JOIN class c ON s.class_id = c.id;
```

5. 多个 JOIN 串联：

```sql
SELECT s.id, c.name, d.name
FROM student s
JOIN class c ON s.class_id = c.id
JOIN department d ON c.department_id = d.id;
```

6. JOIN ON 与 WHERE 合并过滤：

```sql
SELECT s.id
FROM student s JOIN class c ON s.class_id = c.id
WHERE c.name = 'cs';
```

7. 投影别名和 ORDER BY 投影别名沿用现有规则：

```sql
SELECT s.id AS sid, c.name AS cname
FROM student s JOIN class c ON s.class_id = c.id
ORDER BY sid DESC;
```

8. 相关子查询可以引用多表外层别名：

```sql
SELECT s.id
FROM student s JOIN class c ON s.class_id = c.id
WHERE EXISTS (
    SELECT x.id FROM score x WHERE x.student_id = s.id
);
```

### 不支持

1. `NATURAL JOIN`。
2. `CROSS JOIN`。
3. `JOIN ... USING (...)`。
4. FROM 子查询。
5. 多表 `UPDATE` / `DELETE`。
6. 多列 `ORDER BY`。
7. `ORDER BY` 表达式。
8. `SELECT a + b` 这类表达式投影。
9. 聚合函数与 `GROUP BY`。
10. `UNION`。
11. 多数据库限定名，例如 `db.table.column`。

### 名字解析规则

1. 多表查询中，限定列名是推荐写法。
2. 裸列名只有在所有可见表中唯一时才允许。
3. 如果裸列名在多个表中出现，必须报错：

```text
ambiguous column 'id'
```

4. 表名和表别名都可作为限定前缀。
5. 同一个查询作用域内，不允许重复表别名。
6. 如果两个表同名且都没有别名，必须报错；重复表必须使用不同别名。
7. 本地作用域优先于外层相关子查询作用域。
8. 投影别名只影响输出列名和 `ORDER BY`，不参与 `WHERE` / `ON` 绑定。
9. 外连接补空值使用当前系统的 NULL-like 表示，即空字符串 `QString()`。

---

## 阶段一：tokenizer 与 SELECT parser 扩展 FROM source payload

### 0. 常量增量

建议新增 token：

1. `TokenType::JOIN`
2. `TokenType::ON`
3. `TokenType::INNER`
4. `TokenType::LEFT`
5. `TokenType::RIGHT`
6. `TokenType::FULL`

建议新增 parser payload key。优先局部字符串常量，不新增公共常量文件：

1. `fromSources`
2. `joins`
3. `joinType`
4. `leftSourceIndex`
5. `rightSourceIndex`
6. `onAst`
7. `isMultiTable`

建议 payload 结构：

```text
fromSources: [
  { tableName: "student", tableAlias: "s" },
  { tableName: "class",   tableAlias: "c" }
]

joins: [
  {
    joinType: "inner",
    leftSourceIndex: 0,
    rightSourceIndex: 1,
    onAst: LogicNode(a.id = b.a_id)
  }
]
```

`joinType` 允许值：

1. `inner`
2. `left`
3. `right`
4. `full`

兼容策略：

1. 单表 SELECT 继续输出旧字段：
   - `tableName`
   - `tableAlias`
2. 所有 SELECT 都输出 `fromSources`。
3. 单表时 `isMultiTable = false`，多表或 JOIN 时 `isMultiTable = true`。

### 1. 影响文件范围

允许修改：

1. [utils/sql_parser/sql_tokenizer.h](utils/sql_parser/sql_tokenizer.h)
2. [utils/sql_parser/sql_tokenizer.cpp](utils/sql_parser/sql_tokenizer.cpp)
3. [utils/sql_parser/tuple_parser.cpp](utils/sql_parser/tuple_parser.cpp)
4. [utils/logic/logic_parser.cpp](utils/logic/logic_parser.cpp)，仅当需要复用逻辑解析入口或改善 ON 错误信息。
5. [tests/test_parser_dispatcher.cpp](tests/test_parser_dispatcher.cpp)
6. [tests/parserNdispatcher_test_plan.md](tests/parserNdispatcher_test_plan.md)

首选不修改：

1. `service/service.h`
2. `service/tuple_service.cpp`
3. `service/table_dml_service.cpp`

### 2. 函数级完整数据流

#### 2.1 逗号 FROM

```text
SQL:
SELECT a.id, b.name FROM a, b WHERE a.id = b.a_id

-> sqlparser::parseSql(...)
-> parseTupleSql(...)
-> parseProjectionItems(...) 得到 projectionItems
-> parseFromSources(...)
   source[0] = { tableName: a, tableAlias: "" }
   source[1] = { tableName: b, tableAlias: "" }
-> parse WHERE 为 whereAst
-> payload.fromSources = [a, b]
-> payload.isMultiTable = true
-> payload.joins = []
```

#### 2.2 JOIN ... ON

```text
SQL:
SELECT s.id, c.name FROM student s JOIN class c ON s.class_id = c.id WHERE c.name = 'cs'

-> parseTupleSql(...)
-> parseFromSourcesAndJoins(...)
   source[0] = student alias s
   JOIN token
   source[1] = class alias c
   ON expression text = s.class_id = c.id
   onAst = LogicNode(Comparison)
-> parse WHERE expression text = c.name = 'cs'
-> payload.fromSources
-> payload.joins[0].onAst
-> payload.whereAst
```

#### 2.3 多 JOIN 串联

```text
SQL:
SELECT s.id, d.name
FROM student s
JOIN class c ON s.class_id = c.id
JOIN dept d ON c.dept_id = d.id

-> source[0] = student alias s
-> source[1] = class alias c
-> joins[0] = source 0 join source 1 on s.class_id = c.id
-> source[2] = dept alias d
-> joins[1] = current joined rowset join source 2 on c.dept_id = d.id
```

`leftSourceIndex` 对第二个 JOIN 可记录为 `-1` 表示“当前累计 rowset”，或继续记录为前一个右表 index。执行层以 joins 顺序为准，不依赖 left index 做关系优化。

### 3. 函数级输入输出与内部逻辑

#### 3.1 `parseTupleSql(const QString &sql, const QVector<SqlToken> &tokens)`

输入：

1. 原始 SQL。
2. tokenizer token 列表。

输出：

1. 成功时：
   - `ParseResult.success = true`
   - `commandType = "SELECT"`
   - payload 至少包含：
     - `projection`
     - `projectionItems`
     - `fromSources`
     - `tableName`，兼容旧单表入口。
     - `tableAlias`，兼容旧单表入口。
     - `joins`
     - `isMultiTable`
     - `whereAst`
     - `orderByColumn`
     - `orderByDescending`
     - `limit`
2. 失败时：
   - `success = false`
   - `errorMessage` 明确指出 FROM/JOIN/ON 语法错误。

内部逻辑：

1. `SELECT ... FROM` 前的投影解析继续复用 `parseProjectionItems(...)`。
2. `FROM` 后进入新的 `parseFromClause(...)`。
3. `parseFromClause(...)` 读取到第一个 clause terminator：
   - `WHERE`
   - `ORDER`
   - `LIMIT`
   - `;`
   - EOF
4. 表 source 格式只允许：
   - `table`
   - `table alias`
   - `table AS alias`
5. 逗号 source 与 JOIN source 不建议混用。第一版可允许：

```sql
FROM a, b JOIN c ON b.id = c.b_id
```

但执行语义需要清晰。为降低风险，首版建议拒绝逗号和 JOIN 混用：

```text
SELECT: cannot mix comma FROM and JOIN in the same FROM clause
```

6. `JOIN` 后必须是 table source。
7. `JOIN` 后必须有 `ON`。
8. `ON` 表达式结束位置是下一个 `JOIN`、`WHERE`、`ORDER`、`LIMIT`、`;` 或 EOF。
9. `ON` 表达式用现有 logic parser 解析成 `LogicNode`。
10. `JOIN` 不带 `ON` 必须失败。
11. `INNER JOIN` 与 `JOIN` 生成相同 payload，`joinType = inner`。
12. `LEFT JOIN`、`RIGHT JOIN`、`FULL JOIN` 必须生成对应 `joinType`，并允许左右表都使用别名。
13. `NATURAL/CROSS/USING` 必须失败，并给出不支持信息。

#### 3.2 `parseFromClause(...)`

建议签名：

```cpp
static bool parseFromClause(const QVector<SqlToken> &tokens,
                            int fromIndex,
                            int clauseEndIndex,
                            QVariantList *fromSources,
                            QVariantList *joins,
                            QString *singleTableName,
                            QString *singleTableAlias,
                            bool *isMultiTable,
                            QString *error);
```

输入：

1. token 列表。
2. `FROM` token index。
3. FROM clause 结束 index。

输出：

1. `fromSources`：按 SQL 出现顺序排列。
2. `joins`：按 JOIN 执行顺序排列。
3. `singleTableName/tableAlias`：兼容旧字段。
4. `isMultiTable`。

内部逻辑：

1. 调用 `parseTableSource(...)` 解析首表。
2. 如果遇到 `,`，进入 comma mode。
3. 如果遇到 `JOIN`、`INNER JOIN`、`LEFT JOIN`、`RIGHT JOIN` 或 `FULL JOIN`，进入 join mode。
4. mode 一旦确定，不允许混用。
5. 每次新增 source 时执行别名冲突检查。
6. 表名和别名都写入 visible prefix 集合；如果重复表名无别名，报错。

#### 3.3 `parseTableSource(...)`

建议签名：

```cpp
static bool parseTableSource(const QVector<SqlToken> &tokens,
                             int startIndex,
                             int endIndex,
                             QVariantMap *source,
                             int *nextIndex,
                             QString *error);
```

输入：

1. token 列表。
2. table source 起始位置。
3. table source 最大结束位置。

输出：

```text
source.tableName
source.tableAlias
nextIndex
```

内部逻辑：

1. 起始 token 必须是 identifier-like table name。
2. 支持可选 `AS alias`。
3. 支持无 `AS` alias。
4. alias 不能是 clause keyword 或 join keyword。
5. 不支持 `schema.table` 或 `db.table`；如果出现点号，报错。

#### 3.4 `parseJoinOnAst(...)`

输入：

1. `ON` 之后的 token range。

输出：

1. `LogicNode onAst`。

内部逻辑：

1. 将 token range 对应 SQL 文本片段交给 logic parser。
2. 支持现有 WHERE 支持的条件能力。
3. 不引入新的表达式能力。
4. 空 ON 表达式失败。
5. `ON a.id = b.id AND b.x = 1` 合法。

### 4. 测试用例构建

新增 parser 测试：

1. 逗号多表：

```sql
SELECT a.id, b.name FROM a, b WHERE a.id = b.a_id;
```

期望：

```text
fromSources.size = 2
isMultiTable = true
joins.size = 0
whereAst 存在
```

2. 多表 alias：

```sql
SELECT s.id, c.name FROM student s, class AS c WHERE s.class_id = c.id;
```

期望：

```text
source[0] tableName=student tableAlias=s
source[1] tableName=class tableAlias=c
```

3. JOIN ON：

```sql
SELECT s.id FROM student s JOIN class c ON s.class_id = c.id;
```

期望：

```text
fromSources.size = 2
joins.size = 1
joins[0].joinType = inner
joins[0].onAst 存在
```

4. INNER JOIN：

```sql
SELECT s.id FROM student s INNER JOIN class c ON s.class_id = c.id;
```

期望同 JOIN。

5. JOIN + WHERE：

```sql
SELECT s.id FROM student s JOIN class c ON s.class_id = c.id WHERE c.name = 'cs';
```

期望 `onAst` 与 `whereAst` 都存在。

6. LEFT / RIGHT / FULL JOIN，并允许别名：

```sql
SELECT s.id FROM student s LEFT JOIN class c ON s.class_id = c.id;
SELECT s.id FROM student s RIGHT JOIN class c ON s.class_id = c.id;
SELECT s.id FROM student s FULL JOIN class c ON s.class_id = c.id;
```

期望：

```text
joinType 分别为 left / right / full
source[0] tableAlias=s
source[1] tableAlias=c
onAst 存在
```

失败用例：

1. `SELECT * FROM a JOIN b;`
2. `SELECT * FROM a JOIN b ON;`
3. `SELECT * FROM a NATURAL JOIN b;`
4. `SELECT * FROM a CROSS JOIN b;`
5. `SELECT * FROM a JOIN b USING (id);`
6. `SELECT * FROM a, b JOIN c ON b.id = c.id;`
7. `SELECT * FROM a x, b x;`
8. `SELECT * FROM a, a;`

---

## 阶段二：多表名字解析模型与虚拟结果表

### 0. 常量增量

本阶段不新增全局常量。

建议新增局部结构体，优先放在 `controller/nest_query.cpp` 和 `controller/sql_dispatcher.cpp` 的匿名 namespace 中；如果两处重复明显，再移动到一个小型共享 helper。

```cpp
struct SelectTableSource {
    QString tableName;
    QString tableAlias;
    tabledef::TableSchema schema;
    repo::TableData data;
};

struct MultiTableCell {
    QString sourceTableName;
    QString sourceAlias;
    QString columnName;
    QString value;
};

struct JoinedRow {
    QMap<QString, QString> cellsByName;
    QVector<QString> outputValuesByQualifiedColumn;
};

struct MultiTableNameResolution {
    QMap<QString, QString> visibleNameToQualifiedName;
    QSet<QString> ambiguousBareColumns;
    QStringList outputColumns;
};
```

建议内部标准列名格式：

```text
sourcePrefix.columnName
```

其中 `sourcePrefix` 优先使用表别名；无别名时使用表名。

### 1. 影响文件范围

允许修改：

1. [controller/sql_dispatcher.cpp](controller/sql_dispatcher.cpp)
2. [controller/nest_query.cpp](controller/nest_query.cpp)
3. [utils/logic/subquery_logic.cpp](utils/logic/subquery_logic.cpp)，仅当 outer binding 构造需要多表 context 适配。
4. [utils/logic/subquery_logic.h](utils/logic/subquery_logic.h)，仅当签名必须扩展。
5. [tests/test_parser_dispatcher.cpp](tests/test_parser_dispatcher.cpp)
6. [tests/test_query_executor.cpp](tests/test_query_executor.cpp)

首选不修改：

1. `service/tuple_service.cpp`
2. `service/table_dml_service.cpp`
3. `repo/*`

### 2. 函数级完整数据流

#### 2.1 dispatcher 多表 SELECT

```text
SQL:
SELECT s.id, c.name
FROM student s, class c
WHERE s.class_id = c.id

-> SqlDispatcher::execSelect(parsed)
-> payload.isMultiTable = true
-> QueryExecutor::execSelect(parsed, nullptr)
-> load all schemas/data
-> buildMultiTableNameResolution(...)
-> build cartesian product rows
-> evaluate whereAst on joined row context
-> project s.id, c.name
-> SqlExecResult.resultTable
```

#### 2.2 JOIN SELECT

```text
SQL:
SELECT s.id, c.name
FROM student s JOIN class c ON s.class_id = c.id
WHERE c.name = 'cs'

-> QueryExecutor::execSelect(...)
-> load source[0] student rows as initial rowset
-> for join[0]:
   combine current rowset with class rows
   evaluate joins[0].onAst
   keep matching rows
-> evaluate whereAst
-> project
```

#### 2.3 LEFT / RIGHT / FULL JOIN SELECT

```text
SQL:
SELECT s.id, c.name FROM student s LEFT JOIN class c ON s.class_id = c.id

-> QueryExecutor::execSelect(...)
-> load student/class
-> left source rows as initial rowset
-> joinType = left
-> for each left row:
   combine with every right row
   evaluate ON
   if at least one match: keep matched combined rows
   if no match: keep left row + right-side NULL-like cells
-> WHERE / ORDER BY / LIMIT / projection
```

`RIGHT JOIN` 按同样规则保留右侧未匹配行；`FULL JOIN` 同时保留左右两侧未匹配行。

### 3. 函数级输入输出与内部逻辑

#### 3.1 `SqlDispatcher::execSelect(const sqlparser::ParseResult &p)`

输入：

1. SELECT ParseResult。

输出：

1. `SqlExecResult`。

内部逻辑：

1. 如果 `payload.isMultiTable != true`，保持现有单表快路径。
2. 如果是多表 SELECT，统一交给 `QueryExecutor::execSelect(...)`。
3. 不通过 `tuple_service::selectRows(...)`，因为 tuple service 当前以单表为基本单位。
4. dispatcher 层仍负责把 `QueryExecuteResult` 转成 `SqlExecResult`。

#### 3.2 `QueryExecutor::execSelect(...)`

输入：

1. SELECT ParseResult。
2. 可选 `CorrelationBindings`。

输出：

1. `QueryExecuteResult.success = true` 和 `SelectRowsResult`。
2. 失败时返回明确错误。

内部逻辑：

1. 读取 `fromSources`。
2. 如果只有一个 source，走现有单表执行逻辑。
3. 如果多个 source：
   - 加载每个 source 的 schema。
   - 加载每个 source 的 table data。
   - 构建多表名字解析。
   - 构建 joined rowset。
   - 执行 WHERE。
   - 执行 ORDER BY。
   - 执行 LIMIT。
   - 执行投影。
4. 外层 bindings merge 到每一条 joined row context。

#### 3.3 `buildMultiTableNameResolution(...)`

建议签名：

```cpp
static bool buildMultiTableNameResolution(const QList<SelectTableSource> &sources,
                                          MultiTableNameResolution *resolution,
                                          QString *error);
```

输入：

1. 已加载 schema 的 source 列表。

输出：

1. `visibleNameToQualifiedName`：
   - `s.id -> s.id`
   - `student.id -> s.id`
   - 裸 `id -> s.id`，仅当唯一。
2. `ambiguousBareColumns`。

内部逻辑：

1. 每个 source 的 canonical prefix：
   - 有 alias 用 alias。
   - 无 alias 用 tableName。
2. 每列写入：
   - `canonicalPrefix.column`
   - `tableName.column`
   - `alias.column`，如果 alias 非空。
3. 裸列名：
   - 第一次出现，暂存。
   - 第二次来自不同 source，移入 ambiguous。
4. 如果限定名前缀不存在，解析时报：

```text
unknown table or alias 'x'
```

5. 如果裸列名 ambiguous，解析时报：

```text
ambiguous column 'id'
```

#### 3.4 `buildInitialJoinedRows(...)`

建议签名：

```cpp
static QVector<logic::LogicRowContext> buildInitialJoinedRows(const SelectTableSource &source);
```

输入：

1. 一个 source。

输出：

1. 每一行一个 `LogicRowContext`。

内部逻辑：

1. 对每行写入：
   - 裸列名，若当前单 source 内唯一。
   - `tableName.column`
   - `alias.column`
   - `canonicalPrefix.column`
2. 多表组合阶段重新处理裸列名歧义；最终多表 context 中 ambiguous 裸列不应出现，避免误绑定。

#### 3.5 `joinRowsets(...)`

建议签名：

```cpp
static QVector<logic::LogicRowContext> joinRowsets(const QVector<logic::LogicRowContext> &leftRows,
                                                   const SelectTableSource &rightSource,
                                                   const QString &joinType,
                                                   const logic::LogicNode *onAst,
                                                   const logic::CorrelationBindings *bindings,
                                                   QString *error);
```

输入：

1. 当前累计 rowset。
2. 右表 source。
3. join type：`inner` / `left` / `right` / `full`。
4. 可选 ON AST。
5. 可选外层 bindings。

输出：

1. 按 join type 生成的 joined rowset。

内部逻辑：

1. 对每条 left row 和 right row 做组合。
2. 合并 cells 时：
   - 限定名全部保留。
   - 裸列名只在不 ambiguous 时保留。
   - 外层 binding 不覆盖本地同名 key。
3. 如果有 `onAst`，用 logic evaluator 评估。
4. `inner`：
   - `True` 保留，`False/Unknown` 丢弃。
5. `left`：
   - 匹配行按 `inner` 保留。
   - 某条 left row 没有任何匹配时，补一条 right source 全列为空字符串的 combined row。
6. `right`：
   - 匹配行按 `inner` 保留。
   - 某条 right row 没有任何匹配时，补一条 left side 全列为空字符串的 combined row。
7. `full`：
   - 同时执行 left-preserve 与 right-preserve。
8. unmatched 补空行中的列 key 必须完整包含表名限定列和别名限定列，值为 `QString()`。

#### 3.6 `buildCommaCartesianRows(...)`

输入：

1. 多个 source。
2. 可选外层 bindings。

输出：

1. 完整笛卡尔积 rowset。

内部逻辑：

1. 从第一个 source rowset 开始。
2. 逐个 source 做无 ON 的 `joinRowsets(...)`。
3. 生成后再由 WHERE 过滤。
4. 第一版不做 join predicate 下推优化。

#### 3.7 `resolveMultiTableProjection(...)`

输入：

1. `projectionItems`。
2. `MultiTableNameResolution`。

输出：

1. resolved projection 列 key 列表。
2. output column 列名列表。

内部逻辑：

1. `SELECT *`：
   - 输出所有 source 的真实列。
   - 如果列名冲突，输出建议为 `prefix.column`。
   - 如果列名不冲突，可输出裸列名；为稳定起见，首版建议多表 `SELECT *` 输出 `prefix.column`。
2. `SELECT s.id`：
   - resolve 为 `s.id`。
   - 默认输出列名为 `id`。
3. `SELECT id`：
   - 如果唯一，resolve。
   - 如果 ambiguous，报错。
4. `SELECT s.id AS sid`：
   - 输出列名 `sid`。

#### 3.8 `resolveMultiTableOrderBy(...)`

输入：

1. `orderByColumn`。
2. 投影别名映射。
3. 多表名字解析。

输出：

1. joined row context 中可读取的列 key。

内部逻辑：

1. 优先匹配投影输出别名。
2. 再匹配限定列名。
3. 再匹配唯一裸列名。
4. ambiguous 报错。
5. 保持单列 ORDER BY 限制。

### 4. 测试用例构建

新增 dispatcher / executor 测试：

1. 逗号 FROM 等值连接：

```sql
SELECT s.id, c.name FROM student s, class c WHERE s.class_id = c.id;
```

数据：

```text
student: (1, 10), (2, 20), (3, 99)
class:   (10, cs), (20, math)
```

期望：

```text
返回 2 行：1/cs, 2/math
```

2. JOIN ON 等值连接：

```sql
SELECT s.id, c.name FROM student s JOIN class c ON s.class_id = c.id;
```

期望同上。

3. LEFT JOIN 保留左侧未匹配行，并允许别名：

```sql
SELECT s.id, c.name FROM student s LEFT JOIN class c ON s.class_id = c.id ORDER BY s.id ASC;
```

期望：

```text
返回 student 全部 3 行；class 未匹配的第 3 行 c.name 为空字符串。
```

4. RIGHT JOIN 保留右侧未匹配行，并允许别名：

```sql
SELECT s.id, c.name FROM student s RIGHT JOIN class c ON s.class_id = c.id ORDER BY c.id ASC;
```

期望：

```text
返回 class 全部行；没有 student 的 class 行中 s.id 为空字符串。
```

5. FULL JOIN 保留两侧未匹配行，并允许别名：

```sql
SELECT s.id, c.name FROM student s FULL JOIN class c ON s.class_id = c.id;
```

期望：

```text
返回 inner 匹配行 + left-only 行 + right-only 行。
```

6. JOIN ON + WHERE：

```sql
SELECT s.id FROM student s JOIN class c ON s.class_id = c.id WHERE c.name = 'cs';
```

期望只返回 student id = 1。

7. 多 JOIN：

```sql
SELECT s.id, d.name
FROM student s
JOIN class c ON s.class_id = c.id
JOIN dept d ON c.dept_id = d.id;
```

期望按链路返回。

8. 投影别名 + ORDER BY：

```sql
SELECT s.id AS sid FROM student s JOIN class c ON s.class_id = c.id ORDER BY sid DESC;
```

期望输出列名 `sid`，按 id 降序。

9. ambiguous 裸列失败：

```sql
SELECT id FROM student s JOIN class c ON s.class_id = c.id;
```

如果两表都有 `id`，期望失败并包含 `ambiguous column 'id'`。

10. 唯一裸列成功：

```sql
SELECT name FROM student s JOIN class c ON s.class_id = c.id;
```

仅当 `name` 只存在一张表时成功。

---

## 阶段三：ON / WHERE / 相关子查询作用域收口

### 0. 常量增量

本阶段不新增全局常量。

建议继续使用已有：

1. `LogicRowContext`
2. `CorrelationBindings`
3. `referencedOuterNames`

### 1. 影响文件范围

允许修改：

1. [utils/logic/logic_parser.cpp](utils/logic/logic_parser.cpp)
2. [utils/logic/subquery_logic.cpp](utils/logic/subquery_logic.cpp)
3. [controller/nest_query.cpp](controller/nest_query.cpp)
4. [tests/test_logic.cpp](tests/test_logic.cpp)
5. [tests/test_query_executor.cpp](tests/test_query_executor.cpp)

首选不修改：

1. `utils/logic/logic_types.h`
2. `utils/logic/logic_evaluator.cpp`

### 2. 函数级完整数据流

```text
SQL:
SELECT s.id
FROM student s JOIN class c ON s.class_id = c.id
WHERE EXISTS (
    SELECT x.id FROM score x WHERE x.student_id = s.id
)

-> 外层 parser:
   fromSources = student/s, class/c
   joins[0].onAst = s.class_id = c.id
   whereAst = EXISTS subquery
-> QueryExecutor outer:
   joined row context contains:
     s.id
     student.id
     c.id
     class.id
-> logic_parser collectOuterNamesFromText(subquery):
   local source score/x
   x.student_id 是本地
   s.id 是外层
-> buildCorrelationBindings(outer row context, ["s.id"])
-> 子查询执行时 merge s.id binding
```

### 3. 函数级输入输出与内部逻辑

#### 3.1 `collectOuterNamesFromText(...)`

输入：

1. 子查询 SQL 原文。

输出：

1. 外层引用名列表。

内部逻辑：

1. 读取子查询 `fromSources`。
2. 本地前缀集合包含所有本地 tableName 和 tableAlias。
3. 遍历子查询 whereAst 和 join onAst：
   - 本地前缀忽略。
   - `outer.` 前缀收集。
   - 非本地限定名前缀收集为外层引用。
4. 如果子查询内部也是多表，所有本地 source 前缀都必须被识别为本地。

#### 3.2 `requiredOuterReferences(const LogicNode &whereAst)`

输入：

1. 外层 WHERE AST。

输出：

1. 子查询节点所需外层引用。

内部逻辑：

1. 继续使用 `LogicNode.referencedOuterNames`。
2. 多表外层 row context 中按完整名绑定。
3. 对旧 `outer.id` 保持 fallback。

#### 3.3 `buildCorrelationBindings(...)`

输入：

1. 多表 joined row context。
2. 外层引用名列表。

输出：

1. correlation bindings。

内部逻辑：

1. 完整名优先：
   - `s.id`
   - `student.id`
2. `outer.id` fallback：
   - 如果外层 context 有唯一裸 `id`，可绑定。
   - 如果裸 `id` ambiguous，不允许 fallback 静默绑定，返回 missing/ambiguous binding 错误。
3. 不自动把所有外层列都注入子查询，只注入 referenced names。

### 4. 测试用例构建

新增 logic / query executor 测试：

1. 子查询收集多表外层别名：

```sql
EXISTS (SELECT x.id FROM score x WHERE x.student_id = s.id)
```

在外层 source 为 `student s, class c` 时，期望收集 `s.id`。

2. 子查询本地多表别名不误判：

```sql
EXISTS (
  SELECT x.id FROM score x JOIN exam e ON x.exam_id = e.id WHERE e.name = 'mid'
)
```

期望外层引用为空。

3. 多表外层相关 EXISTS：

```sql
SELECT s.id
FROM student s JOIN class c ON s.class_id = c.id
WHERE EXISTS (SELECT x.id FROM score x WHERE x.student_id = s.id);
```

期望只返回有 score 的 student。

4. `outer.id` 在多表 ambiguous 时失败：

```sql
SELECT s.id FROM student s JOIN class c ON s.class_id = c.id
WHERE EXISTS (SELECT x.id FROM score x WHERE x.student_id = outer.id);
```

如果外层 `id` 在 student/class 都存在，期望失败，避免错误绑定。

---

## 阶段四：LIMIT / ORDER BY / 输出 schema 与错误路径细化

### 0. 常量增量

本阶段不新增全局常量。

### 1. 影响文件范围

允许修改：

1. [controller/nest_query.cpp](controller/nest_query.cpp)
2. [controller/sql_dispatcher.cpp](controller/sql_dispatcher.cpp)
3. [tests/test_parser_dispatcher.cpp](tests/test_parser_dispatcher.cpp)
4. [tests/test_query_executor.cpp](tests/test_query_executor.cpp)

### 2. 函数级完整数据流

```text
joined rowset
-> WHERE / ON filtered rows
-> ORDER BY sort key resolved from output alias or visible column
-> LIMIT truncate
-> projection output table
```

### 3. 函数级输入输出与内部逻辑

#### 3.1 多表 ORDER BY

输入：

1. filtered joined rows。
2. resolved order key。
3. column type 信息。

输出：

1. 排序后的 rows。

内部逻辑：

1. 尽量复用单表 `applyOrderBy` 的比较规则。
2. column type 从 resolved column 对应 source schema 中取得。
3. NULL-like 空字符串处理沿用当前单表排序语义。
4. 投影别名优先于真实列名。

#### 3.2 多表 LIMIT

输入：

1. sorted rows。
2. limit。

输出：

1. 截断后的 rows。

内部逻辑：

1. `limit < 0` 不截断。
2. `limit = 0` 返回空结果。
3. 行数不足时返回全部。

#### 3.3 输出 schema

输入：

1. resolved projection。
2. output aliases。

输出：

1. `TableData.columns`。
2. `TableData.rows`。

内部逻辑：

1. `SELECT s.id AS sid` 输出 `sid`。
2. `SELECT s.id` 默认输出 `id`。
3. `SELECT *` 多表首版输出 `prefix.column`，避免列名冲突。
4. 如果用户显式投影两个同名输出列，允许重复列名还是拒绝需要收口。建议首版允许，因为 SQL 允许结果集中重复显示名；测试只依赖列位置。

### 4. 测试用例构建

新增测试：

1. `ORDER BY` 限定列：

```sql
SELECT s.id, c.name FROM student s JOIN class c ON s.class_id = c.id ORDER BY c.name DESC;
```

2. `ORDER BY` 投影别名：

```sql
SELECT c.name AS cname FROM student s JOIN class c ON s.class_id = c.id ORDER BY cname ASC;
```

3. `LIMIT`：

```sql
SELECT s.id FROM student s JOIN class c ON s.class_id = c.id ORDER BY s.id DESC LIMIT 1;
```

4. `SELECT *` 输出：

```sql
SELECT * FROM student s JOIN class c ON s.class_id = c.id;
```

期望 columns 包含 `s.id` / `s.class_id` / `c.id` / `c.name` 或按最终 canonical prefix 规则稳定输出。

---

## 阶段五：文档、回归与性能风险收口

### 0. 常量增量

本阶段不新增常量。

### 1. 影响文件范围

允许修改：

1. [tests/parserNdispatcher_test_plan.md](tests/parserNdispatcher_test_plan.md)
2. [tests/TEST_PLAN.md](tests/TEST_PLAN.md)
3. [INTEGRATION_AND_STRESS_TEST_PLAN.md](INTEGRATION_AND_STRESS_TEST_PLAN.md)，仅当新增多表性能采样时修改。

### 2. 函数级完整数据流

本阶段不新增运行时数据流，只文档化：

```text
SQL multi-table SELECT
-> parser fromSources / joins
-> name resolution
-> joined rowset construction
-> ON / WHERE evaluation
-> ORDER BY / LIMIT
-> projection output
```

### 3. 函数级输入输出与内部逻辑

本阶段不新增函数。

文档必须明确：

1. 多表 FROM 第一版是嵌套循环执行，不做 join reorder。
2. `JOIN` 默认是 `INNER JOIN`。
3. `LEFT JOIN` / `RIGHT JOIN` / `FULL JOIN` 支持 `ON` 和表别名，未匹配侧用空字符串表示 NULL-like 值。
4. 逗号 FROM 与 JOIN 首版不混用。
5. 裸列名 ambiguous 必须失败。
6. 多表 `SELECT *` 输出列名采用稳定限定名。
7. 不新增聚合、UNION、`NATURAL JOIN`、`CROSS JOIN` 或 `USING`。

### 4. 测试用例构建

最终回归命令：

```powershell
& 'E:\Qt\Tools\CMake_64\bin\cmake.exe' --build build/codex-vs-debug --config Debug --parallel 1
```

```powershell
$env:QT_QPA_PLATFORM='offscreen'
$env:PATH='E:\Qt\6.9.2\msvc2022_64\bin;' + $env:PATH
& 'E:\Qt-projects\DBMS\build\codex-vs-debug\Debug\DBMS.exe' --run-tests
```

建议新增小规模多表性能烟测，但不并入现有压力测试总原则，除非后续明确要把多表查询纳入性能报告：

```text
student rows = 50, class rows = 10
JOIN ON select elapsed_ms
comma FROM + WHERE elapsed_ms
```

---

## 实施顺序建议

1. 阶段一只做 parser payload 和 parser 测试。
2. 阶段二实现多表执行器，先支持逗号 FROM，再支持 JOIN ON。
3. 阶段三补相关子查询在多表外层作用域下的绑定。
4. 阶段四收紧 ORDER BY / LIMIT / 输出 schema / 错误信息。
5. 阶段五补文档和全量回归。

不要先做优化。第一版目标是语义正确、错误明确、作用域不乱。性能优化可以在 JOIN 基本语义稳定后再单独规划，例如等值连接索引命中、ON predicate 下推、join reorder。

---

## 验收口径

本计划完成时，以下 SQL 必须通过：

```sql
SELECT s.id, c.name FROM student s, class c WHERE s.class_id = c.id;
SELECT s.id, c.name FROM student s JOIN class c ON s.class_id = c.id;
SELECT s.id, c.name FROM student s LEFT JOIN class c ON s.class_id = c.id;
SELECT s.id, c.name FROM student s RIGHT JOIN class c ON s.class_id = c.id;
SELECT s.id, c.name FROM student s FULL JOIN class c ON s.class_id = c.id;
SELECT s.id FROM student s JOIN class c ON s.class_id = c.id WHERE c.name = 'cs';
SELECT s.id AS sid FROM student s JOIN class c ON s.class_id = c.id ORDER BY sid DESC LIMIT 1;
SELECT s.id FROM student s JOIN class c ON s.class_id = c.id
WHERE EXISTS (SELECT x.id FROM score x WHERE x.student_id = s.id);
```

以下 SQL 必须失败：

```sql
SELECT * FROM a JOIN b;
SELECT * FROM a JOIN b ON;
SELECT * FROM a NATURAL JOIN b;
SELECT * FROM a CROSS JOIN b;
SELECT * FROM a JOIN b USING (id);
SELECT * FROM a, b JOIN c ON b.id = c.id;
SELECT id FROM student s JOIN class c ON s.class_id = c.id; -- 两表都有 id 时 ambiguous
SELECT * FROM a x, b x;
SELECT * FROM a, a;
```

完成后不得破坏：

1. 单表 `SELECT * FROM t WHERE id = 1`。
2. 单表表别名 `SELECT s.id FROM student s`。
3. 单表投影别名 `SELECT id AS sid FROM student ORDER BY sid DESC`。
4. 旧 `outer.id` 相关子查询。
5. 现有索引排序专项压测。
