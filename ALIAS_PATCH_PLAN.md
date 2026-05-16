# ALIAS_PATCH_PLAN

目标：在不扩展 SELECT 查询能力边界的前提下，为现有单表 SELECT 能力补齐表别名、限定列名、投影别名和相关子查询外层别名引用。

本文只规划“名字解析增强”，不规划新的关系代数能力。换句话说，本计划让现有可执行查询可以用更自然的 SQL 名字写法表达，但不新增 JOIN、GROUP BY、聚合、表达式投影、多表 FROM 或多列 ORDER BY。

## 总体收口边界

### 支持

1. 单表 FROM 别名：

```sql
SELECT id FROM student s;
SELECT id FROM student AS s;
```

2. 限定列引用：

```sql
SELECT s.id FROM student s;
SELECT student.id FROM student;
SELECT * FROM student s WHERE s.id = 1;
SELECT * FROM student s ORDER BY s.id DESC;
```

3. 投影别名：

```sql
SELECT id AS sid FROM student;
SELECT id sid FROM student;
SELECT id AS sid FROM student ORDER BY sid DESC;
```

4. 相关子查询外层别名：

```sql
SELECT * FROM parent p
WHERE EXISTS (
    SELECT * FROM child c WHERE c.parent_id = p.id
);
```

5. 兼容旧写法：

```sql
SELECT * FROM parent
WHERE EXISTS (
    SELECT * FROM child WHERE child.parent_id = outer.id
);
```

### 不支持

1. 多表 FROM。
2. JOIN。
3. FROM 子查询。
4. UNION。
5. GROUP BY / HAVING。
6. COUNT / SUM / AVG / MIN / MAX 等聚合函数。
7. SELECT 表达式投影，例如 `SELECT a + b`。
8. ORDER BY 表达式。
9. 多列 ORDER BY。
10. WHERE 中使用投影别名。
11. `SELECT * AS alias`。
12. 投影别名参与相关子查询绑定。

### 作用域规则

1. 本计划仍只处理单表 SELECT，所以未限定列名按当前表列解析。
2. 本地作用域优先于外层作用域：
   - 子查询内 `c.id` 优先解析为子查询本地别名 `c`。
   - 只有不属于子查询本地表名/别名的限定名前缀，才可作为外层相关引用。
3. 外层别名引用按完整名绑定：
   - `p.id` 必须能在外层 row context 中找到 `p.id`。
4. 旧兼容引用 `outer.id` 继续有效。
5. 投影别名只影响输出列名和 ORDER BY 解析，不影响 WHERE 解析。

---

## 阶段一：SELECT 解析层补齐别名 payload

### 0. 常量增量

本阶段不新增全局常量。

如果实现时需要内部字符串 key，必须只在 parser 层局部使用以下 payload key，不新增公共常量文件：

1. `tableAlias`
2. `projectionItems`
3. `sourceColumn`
4. `outputColumn`
5. `orderByColumn`
6. `orderByOutputAlias`

### 1. 影响文件范围

允许修改：

1. [utils/sql_parser/tuple_parser.cpp](utils/sql_parser/tuple_parser.cpp)
2. [utils/sql_parser/sql_tokenizer.h](utils/sql_parser/sql_tokenizer.h)
3. [utils/sql_parser/sql_tokenizer.cpp](utils/sql_parser/sql_tokenizer.cpp)
4. [tests/test_parser_dispatcher.cpp](tests/test_parser_dispatcher.cpp)
5. [tests/parserNdispatcher_test_plan.md](tests/parserNdispatcher_test_plan.md)

首选不修改：

1. `utils/sql_parser/sql_parser.h`
2. `controller/sql_dispatcher.cpp`
3. `service/service.h`

如果 `AS` 目前已经能作为普通 lexeme 判断，则不新增 token；如果 tokenizer 会把 `AS` 作为普通 identifier 且影响解析可读性，可以新增 `TokenType::AS`，但仅用于 SELECT alias 解析。

### 2. 函数级完整数据流

#### 2.1 FROM 表别名

```text
SQL: SELECT id FROM student s
-> sqlparser::parseSql(...)
-> parseTupleSql(...)
-> 定位 FROM 后第一个 IDENTIFIER 作为 tableName = student
-> 检查 tableName 后、WHERE/ORDER/LIMIT/;/EOF 前的可选别名
-> payload.tableName = student
-> payload.tableAlias = s
```

```text
SQL: SELECT id FROM student AS s
-> parseTupleSql(...)
-> tableName = student
-> AS 后 IDENTIFIER 作为 tableAlias = s
```

#### 2.2 投影列与投影别名

```text
SQL: SELECT s.id AS sid FROM student s
-> parseTupleSql(...)
-> projectionItems[0].sourceColumn = s.id
-> projectionItems[0].outputColumn = sid
-> projection = ["s.id"]                // 兼容旧调用
-> projectionOutputColumns = ["sid"]    // 如需要可新增 payload key
```

```text
SQL: SELECT id sid FROM student
-> parseTupleSql(...)
-> projectionItems[0].sourceColumn = id
-> projectionItems[0].outputColumn = sid
```

```text
SQL: SELECT * FROM student s
-> selectAll = true
-> projection 仍按旧逻辑清空
-> 不允许 SELECT * AS x
```

#### 2.3 ORDER BY 与投影别名

```text
SQL: SELECT id AS sid FROM student ORDER BY sid DESC
-> parseTupleSql(...)
-> projectionItems: id -> sid
-> orderByColumn = sid
-> orderByDescending = true
-> 不在 parser 层判断 sid 是真实列还是投影别名
```

### 3. 函数级输入输出与内部逻辑

#### 3.1 `parseTupleSql(const QString &sql, const QVector<SqlToken> &tokens)`

输入：

1. 原始 SELECT SQL。
2. tokenizer 输出 token 列表。

输出：

1. 成功时：
   - `ParseResult.success = true`
   - `commandType = "SELECT"`
   - payload 至少包含：
     - `tableName`
     - `tableAlias`，无别名时为空字符串或不存在
     - `projection`
     - `projectionItems`
     - `selectAll`
     - `whereAst`
     - `conditions`
     - `orderByColumn`
     - `orderByDescending`
     - `limit`
2. 失败时：
   - `success = false`
   - `errorMessage` 明确指出 SELECT alias / projection alias / trailing token 错误。

内部逻辑收口：

1. FROM 后只允许一个表名和最多一个别名。
2. 别名不得为保留 clause keyword：
   - `WHERE`
   - `ORDER`
   - `LIMIT`
3. `AS` 后必须有 identifier。
4. 投影 item 只允许：
   - `*`
   - `col`
   - `table.col`
   - `alias.col`
   - 上述列引用加 `AS alias`
   - 上述列引用后直接跟 alias
5. 多个投影 item 仍按逗号分隔。
6. 不解析表达式投影。
7. 不在 parser 层解析 projection alias 与真实列冲突；冲突由执行层名字解析统一处理。

#### 3.2 `parseOrderByClause(...)`

输入：

1. token 列表。
2. `ORDER` token index。

输出：

1. `payload.orderByColumn`
   - 可以是 `id`
   - 可以是 `s.id`
   - 可以是投影别名 `sid`
2. `payload.orderByDescending`

内部逻辑收口：

1. 仍只允许单个 ORDER BY item。
2. item 后只允许可选 `ASC` / `DESC`。
3. 方向 token 后只允许：
   - `LIMIT`
   - `;`
   - EOF
4. `ORDER BY a, b` 必须失败。

### 4. 测试用例构建

新增 parser 测试：

1. `SELECT id FROM student s`
   - `tableName = student`
   - `tableAlias = s`
2. `SELECT id FROM student AS s`
   - `tableAlias = s`
3. `SELECT s.id FROM student s`
   - `projectionItems[0].sourceColumn = s.id`
   - `outputColumn` 默认可为 `id` 或 `s.id`，执行阶段最终定义。
4. `SELECT s.id AS sid FROM student s`
   - `sourceColumn = s.id`
   - `outputColumn = sid`
5. `SELECT id sid FROM student`
   - `outputColumn = sid`
6. `SELECT id AS sid FROM student ORDER BY sid DESC`
   - `orderByColumn = sid`
   - `orderByDescending = true`
7. 失败用例：
   - `SELECT * AS x FROM student`
   - `SELECT id FROM student AS`
   - `SELECT id FROM student s extra`
   - `SELECT id FROM student ORDER BY a, b`

---

## 阶段二：名字解析与执行层投影/排序归一化

### 0. 常量增量

本阶段不新增全局常量。

允许新增局部结构体，建议位于 `controller/sql_dispatcher.cpp` 或匿名 namespace：

```cpp
struct SelectProjectionItem {
    QString sourceName;
    QString resolvedColumnName;
    QString outputName;
};

struct SelectNameResolution {
    QString tableName;
    QString tableAlias;
    QMap<QString, QString> visibleColumnToRealColumn;
    QMap<QString, QString> outputAliasToRealColumn;
};
```

如果发现 `TableDmlService::selectRows(...)` 必须接收输出列名，则优先新增一个局部 wrapper，避免扩大 service 公共签名。只有在无法复用时，才考虑给 `SelectRowsResult` 后处理输出列名。

### 1. 影响文件范围

允许修改：

1. [controller/sql_dispatcher.cpp](controller/sql_dispatcher.cpp)
2. [controller/nest_query.cpp](controller/nest_query.cpp)
3. [service/table_dml_service.cpp](service/table_dml_service.cpp)
4. [service/tuple_service.cpp](service/tuple_service.cpp)
5. [service/service.h](service/service.h)，仅在必须扩展 `OrderByClause` 或 SELECT 输出别名时修改。
6. [tests/test_parser_dispatcher.cpp](tests/test_parser_dispatcher.cpp)
7. [tests/test_logic.cpp](tests/test_logic.cpp)

首选不修改：

1. `repo::*`
2. `table_service::*`
3. `database_service::*`

### 2. 函数级完整数据流

#### 2.1 普通 SELECT 投影别名

```text
SQL: SELECT s.id AS sid FROM student s
-> parser payload:
   tableName = student
   tableAlias = s
   projectionItems = [{ sourceColumn: s.id, outputColumn: sid }]
-> SqlDispatcher::execSelect(...)
-> load schema / build SelectNameResolution
-> resolve s.id -> id
-> tuple_service::selectRows(tableName, ["id"], ..., orderBy)
-> result.resultTable.columns 从 ["id"] 改写为 ["sid"]
-> SqlExecResult
```

#### 2.2 WHERE 限定列名

```text
SQL: SELECT * FROM student s WHERE s.id = 1
-> parser whereAst 中 ColumnRef name = s.id
-> dispatcher / executor 构建 rowContext:
   id
   student.id
   s.id
-> logic evaluator 直接找到 s.id
-> 命中行
```

对于 simple conditions：

```text
SQL: SELECT * FROM student s WHERE s.id = 1
-> tuple_parser::isSimpleWhereNode 当前会拒绝带点列名
-> 允许保持 complex whereAst 路径
-> 不强求索引优化命中
```

如后续要让索引优化命中，可单独把 `s.id` 归一化为 `id` 后进入 `conditions`，本计划不作为必须项。

#### 2.3 ORDER BY 投影别名

```text
SQL: SELECT id AS sid FROM student ORDER BY sid DESC
-> parser orderByColumn = sid
-> dispatcher 识别 sid 是 outputAlias
-> resolved orderBy.columnName = id
-> tuple_service::selectRows(... orderBy id DESC)
-> result columns 改写为 sid
```

#### 2.4 ORDER BY 限定列名

```text
SQL: SELECT * FROM student s ORDER BY s.id DESC
-> parser orderByColumn = s.id
-> dispatcher resolve s.id -> id
-> service ORDER BY id DESC
```

### 3. 函数级输入输出与内部逻辑

#### 3.1 `SqlDispatcher::execSelect(const sqlparser::ParseResult &p)`

输入：

1. SELECT ParseResult。
2. payload 中的 `tableName`、`tableAlias`、`projectionItems`、`orderByColumn`。

输出：

1. 旧输出格式保持不变。
2. `SelectRowsResult.resultTable.columns` 使用投影输出名：
   - `SELECT id AS sid` 输出 `sid`
   - `SELECT s.id` 输出默认列名建议为 `id`
   - `SELECT *` 输出真实列名

内部逻辑：

1. 加载 schema。
2. 构建可见名映射：

```text
id          -> id
student.id  -> id
s.id        -> id
outer.id    -> id   // 仅在外层 row context 使用；dispatcher 投影解析可不接受 outer.id
```

3. 解析 projection：
   - `*` 直接保留。
   - `s.id` / `student.id` / `id` 解析成真实列 `id`。
   - output alias 单独记录。
4. 解析 ORDER BY：
   - 优先匹配 projection output alias。
   - 再匹配真实列/限定列。
   - 未匹配时报 `ORDER BY column 'x' does not exist`。
5. 调用 `tuple_service::selectRows(...)` 时传真实列名。
6. 返回前改写输出列名。

#### 3.2 `QueryExecutor::execSelect(...)`

输入：

1. 子查询 SELECT ParseResult。
2. 可选 `CorrelationBindings`。

输出：

1. 支持本地别名列引用。
2. 支持外层别名列引用。
3. 旧 `outer.id` 继续支持。

内部逻辑：

1. 从 payload 读取 `tableAlias`。
2. 构建本地 row context 时同时写入：

```text
id
tableName.id
tableAlias.id
```

3. 如果存在外层 bindings，merge 后包含：

```text
p.id
outer.id
```

4. projection 解析时使用和 dispatcher 一致的名字解析规则。

#### 3.3 `buildRowContext(...)`

输入：

1. `TableSchema schema`
2. `TableData table`
3. `rowIndex`
4. 新增可选 `tableAlias`

输出：

1. `LogicRowContext.cellsByName` 包含：
   - 裸列名
   - 表名限定列
   - 别名限定列

内部逻辑：

1. 对每一列写入 `column.name`。
2. 写入 `schema.tableName + "." + column.name`。
3. 如果 `tableAlias` 非空，写入 `tableAlias + "." + column.name`。
4. 空字符串仍按 NULL-like 处理。

#### 3.4 `mergeBindings(...)`

输入：

1. 当前子查询 rowContext。
2. 外层 correlation bindings。

输出：

1. 子查询 rowContext 增加所有外层绑定。

内部逻辑：

1. 保持当前按 binding.name 插入。
2. 如果 binding 是 `p.id`，不得自动覆盖本地 `id`。
3. 如果 binding 是 `outer.id`，只作为兼容入口。
4. 内层本地 alias 与外层 alias 同名时，本地构建在前，merge 外层时不得覆盖已有本地限定名；如现有实现会覆盖，应改为 `if (!contains) insert`。

### 4. 测试用例构建

新增 dispatcher / executor 集成测试：

1. 表别名投影：

```sql
SELECT s.id FROM student s;
```

期望：

1. 查询成功。
2. 返回行数正确。
3. 输出列名为 `id`。

2. 投影别名：

```sql
SELECT id AS sid FROM student;
```

期望：

1. 查询成功。
2. 输出列名为 `sid`。

3. WHERE 限定列名：

```sql
SELECT * FROM student s WHERE s.id = 1;
```

期望：

1. 查询成功。
2. 只返回 id = 1 的行。

4. ORDER BY 限定列名：

```sql
SELECT * FROM student s ORDER BY s.id DESC;
```

期望：

1. 查询成功。
2. id 降序。

5. ORDER BY 投影别名：

```sql
SELECT id AS sid FROM student ORDER BY sid DESC;
```

期望：

1. 查询成功。
2. 输出列名为 `sid`。
3. 值按真实列 id 降序。

---

## 阶段三：相关子查询外层别名支持

### 0. 常量增量

本阶段不新增全局常量。

保留字符串约定：

1. `outer.`：旧兼容外层引用前缀。
2. `tableAlias.column`：新外层别名引用格式。

### 1. 影响文件范围

允许修改：

1. [utils/logic/logic_parser.cpp](utils/logic/logic_parser.cpp)
2. [utils/logic/subquery_logic.cpp](utils/logic/subquery_logic.cpp)
3. [controller/nest_query.cpp](controller/nest_query.cpp)
4. [tests/test_logic.cpp](tests/test_logic.cpp)
5. [tests/test_parser_dispatcher.cpp](tests/test_parser_dispatcher.cpp)

首选不修改：

1. `utils/logic/logic_types.h`
2. `utils/logic/logic_tokenizer.cpp`
3. `utils/logic/logic_evaluator.cpp`

如果必须在 `LogicNode` 中记录本地 table alias 集合，才修改 `logic_types.h`；首选从 subquery SQL 的 payload 中读取 `tableName/tableAlias` 并在收集 outer names 时判断。

### 2. 函数级完整数据流

```text
SQL:
SELECT * FROM parent p
WHERE EXISTS (
    SELECT * FROM child c WHERE c.parent_id = p.id
)

-> 外层 parseTupleSql:
   tableName = parent
   tableAlias = p
   whereAst = EXISTS subquery node

-> logic_parser capture subquery SQL:
   SELECT * FROM child c WHERE c.parent_id = p.id

-> collectOuterNamesFromText(subquerySql):
   parse subquery SELECT
   local tableName = child
   local tableAlias = c
   whereAst contains c.parent_id and p.id
   c.parent_id 是本地限定列
   p.id 不是本地 tableName/tableAlias，也不是 outer. 旧前缀
   收集 referencedOuterNames = ["p.id"]

-> 外层逐行执行:
   buildRowContext(parent, alias p)
   rowContext contains:
     id
     parent.id
     p.id
     outer.id

-> buildCorrelationBindings(rowContext, ["p.id"])
   binding p.id = 当前外层行 id

-> executeCorrelatedSelect(subquerySql, binding p.id)
   子查询逐行构建 local rowContext:
     parent_id
     child.parent_id
     c.parent_id
   merge binding:
     p.id
   evaluate c.parent_id = p.id
```

### 3. 函数级输入输出与内部逻辑

#### 3.1 `collectOuterNamesFromText(const QString &text, QStringList *names, LogicError *error)`

输入：

1. 子查询 SQL 原文。

输出：

1. `names` 包含子查询中引用的外层限定名。
2. 本地 tableName/tableAlias 限定名不进入 `names`。

内部逻辑：

1. 调用 `sqlparser::parseSql(text)`。
2. 读取：
   - `payload.tableName`
   - `payload.tableAlias`
3. 本地允许前缀集合：

```text
tableName + "."
tableAlias + "."
```

4. 遍历 whereAst：
   - `outer.xxx`：加入外层引用。
   - `localTable.xxx`：本地引用，忽略。
   - `localAlias.xxx`：本地引用，忽略。
   - `other.xxx`：加入外层引用。
5. 如果 `other.xxx` 既不是本地也无法在外层绑定，执行阶段报 missing correlated binding。

#### 3.2 `buildCorrelationBindings(const LogicRowContext &outerRowContext, const QStringList &referencedOuterNames)`

输入：

1. 外层 row context。
2. 相关引用名列表，例如：
   - `p.id`
   - `outer.id`

输出：

1. `CorrelationBindings.items`。

内部逻辑：

1. 先按完整名查找：
   - `p.id`
   - `outer.id`
2. 如果查不到，保留当前 fallback：取点号后的 local name 再查找。
3. fallback 只为旧兼容和缺省别名服务，不得覆盖完整名优先规则。

#### 3.3 `QueryExecutor::execSelect(...)`

输入：

1. SELECT ParseResult。
2. 可选外层 bindings。

输出：

1. 子查询 SELECT 正常执行。
2. 缺少相关绑定时报：

```text
missing correlated binding 'p.id'
```

内部逻辑：

1. 进入每行 evaluation 前，构造 local row context。
2. local row context 包含本地裸列、表名限定列、别名限定列。
3. merge 外层 bindings。
4. 评估 whereAst。

### 4. 测试用例构建

新增 logic parser 测试：

1. 新外层别名引用：

```text
EXISTS (SELECT id FROM child c WHERE c.parent_id = p.id)
```

期望：

```text
referencedOuterNames = ["p.id"]
```

2. 本地别名不被误判：

```text
EXISTS (SELECT id FROM child c WHERE c.parent_id = 1)
```

期望：

```text
referencedOuterNames = []
```

3. 旧写法兼容：

```text
EXISTS (SELECT id FROM child WHERE child.parent_id = outer.id)
```

期望：

```text
referencedOuterNames = ["outer.id"]
```

新增 dispatcher / integration 测试：

1. `EXISTS` 相关子查询：

```sql
SELECT * FROM parent p
WHERE EXISTS (
    SELECT * FROM child c WHERE c.parent_id = p.id
);
```

数据：

```text
parent: (1), (2), (3)
child:  (10, 1), (11, 1), (12, 3)
```

期望：

```text
返回 parent id = 1, 3
```

2. `IN` 相关子查询：

```sql
SELECT * FROM parent p
WHERE p.id IN (
    SELECT c.parent_id FROM child c WHERE c.parent_id = p.id
);
```

期望：

```text
返回 parent id = 1, 3
```

3. `ANY/ALL` 相关子查询如果现有测试已覆盖 `outer.id`，新增同构 alias 版本；如果执行层目前不支持该 SQL 形态，则只补 logic 层测试，不扩大执行范围。

---

## 阶段四：文档与回归收口

### 0. 常量增量

本阶段不新增常量。

### 1. 影响文件范围

允许修改：

1. [tests/parserNdispatcher_test_plan.md](tests/parserNdispatcher_test_plan.md)
2. [tests/TEST_PLAN.md](tests/TEST_PLAN.md)
3. [INTEGRATION_AND_STRESS_TEST_PLAN.md](INTEGRATION_AND_STRESS_TEST_PLAN.md)，仅当集成压测文档需要说明相关子查询 alias 时修改。

不修改：

1. 性能图表脚本。
2. 压测规模。
3. 已有索引排序图表命名。

### 2. 函数级完整数据流

本阶段不新增运行时数据流，只对前述阶段的数据流做文档化：

```text
SQL alias syntax
-> parser payload
-> dispatcher name resolution
-> service execution
-> rowContext alias binding
-> subquery correlated binding
-> tests
```

### 3. 函数级输入输出与内部逻辑

本阶段不新增函数。

文档必须明确：

1. 别名增强只覆盖单表 SELECT。
2. 投影别名只影响输出和 ORDER BY。
3. WHERE 不支持投影别名。
4. 相关子查询支持外层表别名。
5. 旧 `outer.xxx` 写法保留。

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

如果实施影响索引排序相关 SELECT 路径，再额外刷新性能 CSV 和图表：

```powershell
$env:DBMS_PERF_CSV_PATH='E:\Qt-projects\DBMS\build\performance_samples.csv'
$env:DBMS_STRESS_ROW_COUNTS='50,100,200,500'
& 'E:\Qt-projects\DBMS\build\codex-vs-debug\Debug\DBMS.exe' --run-tests
```

```powershell
wsl bash -lc "source ~/miniconda3/etc/profile.d/conda.sh && conda activate base && cd /mnt/e/Qt-projects/DBMS && python tests/tools/plot_performance_charts.py"
```

---

## 实施顺序建议

1. 阶段一先单独完成 parser payload 和 parser 测试。
2. 阶段二完成普通 SELECT 的别名投影、WHERE 限定列、ORDER BY 限定列和投影别名。
3. 阶段三再接相关子查询外层别名。
4. 阶段四补文档并跑全量回归。

阶段二完成前，不进入阶段三。原因：相关子查询 alias 依赖本地 row context 和限定列解析，直接做阶段三会把 parser 问题和执行问题混在一起。

---

## 验收口径

本计划完成时，以下 SQL 必须通过：

```sql
SELECT s.id FROM student s;
SELECT id AS sid FROM student ORDER BY sid DESC;
SELECT * FROM student s WHERE s.id = 1;
SELECT * FROM student s ORDER BY s.id DESC;
SELECT * FROM parent p
WHERE EXISTS (
    SELECT * FROM child c WHERE c.parent_id = p.id
);
```

以下 SQL 必须继续失败：

```sql
SELECT * AS x FROM student;
SELECT id FROM student s extra;
SELECT id FROM student ORDER BY a, b;
SELECT a + b FROM student;
SELECT COUNT(*) FROM student;
SELECT * FROM a JOIN b ON a.id = b.id;
```

完成后不得破坏：

1. 旧 `SELECT * FROM t WHERE id = 1`。
2. 旧 `SELECT id FROM t LIMIT 1`。
3. 旧 `SELECT id FROM t ORDER BY id DESC`。
4. 旧 `outer.id` 相关子查询。
5. 索引排序专项压测。
