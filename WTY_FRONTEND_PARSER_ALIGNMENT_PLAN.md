# wty 前端 / 解析器对齐收口计划

## 目的

这份文档只用于对齐，不用于本轮直接修代码。

当前最合适的集成思路是把双方工作做一次“左右 JOIN”：

- 左侧：`wty` 分支已经完成的前端、SQL 解析器、SQL 分发器。
- 右侧：当前 `chester` 分支已经完成的 service / repo / FK / index 能力。

不要默认任何一侧是另一侧的严格超集。

需要注意：

- `wty` 侧有些 SQL 入口已经写出来了，但当前 service 层并没有对应执行语义。
- 当前 service 侧有些能力已经实现了，但 `wty` 侧 parser / dispatcher 还没有解析或调度入口。
- 第一轮合并只应该接双方交集，也就是能用稳定函数输入输出明确收口的部分。

## 依托的 review comment

第一轮收口建议严格依托下面这组 comment：

```text
animated_button.h 补全 Qt 绘制相关头文件，修复编译错误。
mainwindow.cpp 调整初始化顺序，确保 setDataRoot 在 setupLayout 之前执行。
mainwindow.cpp 修复 USE 语句解析逻辑，仅在语句以分号结尾时才移除末尾分号。
mainwindow.cpp 修正 “关于” 界面文案，仅保留当前已实际支持的 SQL 功能。
table_parser.cpp 修复约束解析逻辑，按括号深度正确跳过完整约束定义。
structure_panel.cpp 修正 service.h 错误的相对路径引用。
sql_dispatcher.cpp 修复 QVariant 获取自定义类型问题，禁止使用未注册元类型。
sql_dispatcher.cpp 彻底回滚 buildConditions 相关伪实现，不支持任何 WHERE 条件。
回滚 listTablesInDatabase、listAllDatabases 的硬编码实现，不属于本次 PR 范围。
table_parser.cpp 修复 QVector<ColumnDef> 存入 QVariant 的问题，改用基础类型传递。
tuple_parser.cpp 避免重复 tokenize，统一使用一次分词结果。
```

这组 comment 适合作为第一轮合并边界，因为它主要解决：

- 编译阻断问题。
- 启动路径状态问题。
- UI 功能宣称越界问题。
- `QVariant` 自定义类型传递风险。
- WHERE 伪支持问题。
- 非本 PR 范围的硬编码 service helper。

它不要求重构整个 DBMS。

## 当前难度判断

继续对齐没有明显的结构性困难。

难度等级：中低。

主要风险在于：

- `wty` UI 宣称了超过双方当前真实能力的 SQL 功能。
- `wty` dispatcher 调了一些当前主线 service 边界里不存在的 helper。
- `wty` parser 把自定义 C++ 类型塞进 `QVariant`，如果不注册元类型，运行期容易取值失败。
- 当前 service 层已经有更丰富的 schema、constraint、index、FK action 能力，但 `wty` parser 还没有完整表达这些输入。
- 不能因为 parser 识别了 `WHERE` 关键字，就默认 service 已经支持 SQL WHERE 语义。

## 左右 JOIN 总览

### 左侧：`wty` 分支已经提供的东西

| 模块 | `wty` 入口 | 状态 |
|---|---|---|
| UI 执行入口 | `MainWindow::onExecuteRequested(sql)` | 可用 |
| UI 结果展示 | `ResultPanel::showTable/showLog/showError` | 可用 |
| UI 编辑器 | `EditorPanel::executeRequested(sql)` | 可用 |
| 结构树 | `StructurePanel::loadStructure()` | 可用，但当前依赖非主线 helper |
| Parser 总入口 | `sqlparser::parseSql(sql)` | 可用 |
| Tokenizer | `SqlTokenizer::tokenize(sql)` | 可用 |
| Dispatcher 总入口 | `SqlDispatcher::execute(sql)` | 可用 |
| DDL 分发 | `execCreateDatabase/execDropDatabase/execUseDatabase/execCreateTable/...` | 部分可用 |
| DML 分发 | `execSelect/execInsert/execUpdate/execDelete` | 部分可用 |

### 右侧：当前 service 已经提供的东西

| 模块 | 当前 service 入口 | 状态 |
|---|---|---|
| 数据根目录 | `service::setDataRoot/getDataRoot` | 稳定 |
| 当前数据库会话 | `service::currentDatabase` | 当前设计下可用 |
| 数据库 DDL | `database_service::createDatabase/dropDatabase/useDatabase/showDatabases` | 稳定 |
| 表 DDL | `table_service::createTable/dropTable/addColumn/deleteColumn/modifyColumn` | 稳定 |
| 约束 DDL | `table_service::addConstraint/modifyConstraint/deleteConstraint` | 稳定 |
| 索引 DDL | `table_service::createIndex/dropIndex` | 稳定 |
| 表元信息展示 | `table_service::showTables/describeTable/showCreateTable` | 稳定 |
| 元组 DML | `tuple_service::selectRows/insertRows/updateRows/deleteRows` | 稳定 |
| 条件输入 | `QList<SimpleCondition>` | 只有简单等值语义，不等于 SQL WHERE |

## 按 `origin/wty` 当时已有 service 口对齐

这一节只对齐 `origin/wty` 当时已经存在的 service 口。

理论排除以下三个结构树临时 helper：

```cpp
QStringList listAllDatabases();
QStringList listTablesInDatabase(const QString &databaseName);
QStringList listTableColumns(const QString &tableName);
```

排除理由：

- 它们是结构树展示便利函数，不是 SQL dispatcher 的核心业务 API。
- 它们在 `wty` 分支里仍然存在，但不应该作为本轮 parser/dispatcher 对齐依据。
- 如果结构树确实需要这类能力，应后续单独设计正式 read-only service，而不是混在本 PR 里。

### 对方已有 service 口对齐表

| 对方已有 service 口 | dispatcher 应接收的 parser 输入 | dispatcher 应调用方式 | 返回值收口 | 当前对齐建议 |
|---|---|---|---|---|
| `database_service::createDatabase(const QString &databaseName)` | `payload["databaseName"]` | `createDatabase(databaseName)` | `TaskResult.success/errorMessage` | 可直接对齐 |
| `database_service::dropDatabase(const QString &databaseName)` | `payload["databaseName"]` | `dropDatabase(databaseName)` | `TaskResult.success/errorMessage/affectedRowCount` | 可直接对齐 |
| `database_service::useDatabase(const QString &databaseName)` | `payload["databaseName"]` | `useDatabase(databaseName)` | `TaskResult.success/errorMessage` | 可直接对齐；UI 应通过执行结果同步当前库 |
| `database_service::showDatabases()` | 无 payload | `showDatabases()` | `SelectRowsResult.resultTable` | 可直接对齐；UI 展示表格 |
| `table_service::createTable(const QString &tableName, const tabledef::TableSchema &schema)` | `tableName` + `columns` + 可选 `constraints` | dispatcher 构造 `TableSchema` 后调用 | `TaskResult.success/errorMessage` | 可对齐，但 parser payload 必须改成基础类型 |
| `table_service::dropTable(const QString &tableName)` | `payload["tableName"]` | `dropTable(tableName)` | `TaskResult.success/errorMessage` | 可直接对齐 |
| `table_service::addColumn(const QString &tableName, const ColumnDefinition &definition)` | `tableName` + 完整 column map | dispatcher 构造 `ColumnDefinition` 后调用 | `TaskResult.success/errorMessage` | 只有 parser 能输出完整列定义时才开放 |
| `table_service::deleteColumn(const QString &tableName, const QString &columnName)` | `tableName` + `columnName` | `deleteColumn(tableName, columnName)` | `TaskResult.success/errorMessage` | 可对齐 |
| `table_service::modifyColumn(const QString &tableName, const QString &columnName, const ColumnDefinition &definition)` | `tableName` + `columnName` + 完整 column map | dispatcher 构造 `ColumnDefinition` 后调用 | `TaskResult.success/errorMessage` | 只有 parser 能输出完整列定义时才开放 |
| `table_service::addConstraint(const QString &tableName, const tabledef::Constraint &constraint)` | `tableName` + 完整 constraint map | dispatcher 构造 `Constraint` 后调用 | `TaskResult.success/errorMessage` | 不能传空 `Constraint`；payload 不完整就拒绝 |
| `table_service::modifyConstraint(const QString &tableName, const QString &constraintName, const tabledef::Constraint &constraint)` | `tableName` + `constraintName` + 完整 constraint map | dispatcher 构造 `Constraint` 后调用 | `TaskResult.success/errorMessage` | 不能传空 `Constraint`；payload 不完整就拒绝 |
| `table_service::deleteConstraint(const QString &tableName, const QString &constraintName)` | `tableName` + `constraintName` | `deleteConstraint(tableName, constraintName)` | `TaskResult.success/errorMessage` | 可对齐 |
| `table_service::showTables()` | 无 payload，但依赖 `currentDatabase` | `showTables()` | `SelectRowsResult.resultTable` | 可直接对齐；执行前必须已有当前库 |
| `table_service::describeTable(const QString &tableName)` | `payload["tableName"]` | `describeTable(tableName)` | `TextResult.text` | 可直接对齐 |
| `table_service::showCreateTable(const QString &tableName)` | `payload["tableName"]` | `showCreateTable(tableName)` | `TextResult.text` | 对方 service 有，但 dispatcher 暂无 SQL 通路；可暂缓 |
| `tuple_service::selectRows(const QString &tableName, const QStringList &projectionColumns, const QList<SimpleCondition> &conditions, int limit = -1)` | `tableName` + `projection` + 可选 `limit`，本轮不接 WHERE | `selectRows(tableName, projection, {}, limit)` | `SelectRowsResult.resultTable` | 可对齐；如果 parser 发现 WHERE，应拒绝而不是静默忽略 |
| `tuple_service::insertRows(const QString &tableName, const QList<QMap<QString, QString>> &rows)` | `tableName` + `columnNames` + `rows` | dispatcher 转成 `QList<QMap<QString, QString>>` 后调用 | `TaskResult.success/errorMessage/affectedRowCount` | 可对齐；rows payload 必须改成基础 `QVariantList` |
| `tuple_service::updateRows(const QString &tableName, const QMap<QString, QString> &assignmentMap, const QList<SimpleCondition> &conditions)` | `tableName` + `assignments`，本轮不接 WHERE | `updateRows(tableName, assignmentMap, {})` | `TaskResult.success/errorMessage/affectedRowCount` | 可对齐；如果 parser 发现 WHERE，应拒绝而不是全表更新 |
| `tuple_service::deleteRows(const QString &tableName, const QList<SimpleCondition> &conditions)` | `tableName`，本轮不接 WHERE | `deleteRows(tableName, {})` | `TaskResult.success/errorMessage/affectedRowCount` | 可对齐；如果 parser 发现 WHERE，应拒绝而不是全表删除 |

### 不建议 dispatcher 直接对齐的底层口

`origin/wty` 当时也有 `TableDmlService`：

```cpp
TableDmlService::selectRows(...)
TableDmlService::insertRows(...)
TableDmlService::updateRows(...)
TableDmlService::deleteRows(...)
```

这些是通用二维表 DML 底层 service，不建议 SQL dispatcher 直接调用。

推荐边界：

- SQL dispatcher 调 `database_service` / `table_service` / `tuple_service`。
- DDL/DML service 内部再按需要调用 `TableDmlService`。

这样能避免 dispatcher 绕过 schema 加载、约束检查、当前数据库上下文等上层语义。

### 新版 `origin/wty` 的歧义变化

相比上一版，歧义减少的点：

- `parseSql` 已经改成一次 tokenize 后把 token stream 传给具体 parser，重复 tokenize 问题基本收口。
- dispatcher 里的 `buildConditions()` 函数已经消失，不再主动把 WHERE 包装成 `SimpleCondition`。
- `listAllDatabases()` / `listTablesInDatabase()` 的实现已经改为复用 `showDatabases()` / `showTables()`，比直接读 repo 更接近 service 语义。

仍未完全收口的点：

- `service.h` 里仍声明了 `listAllDatabases()`、`listTablesInDatabase()`、`listTableColumns()`，本轮理论上仍应排除。
- `sql_dispatcher.cpp` 仍在使用 `payload["columns"].value<QVector<sqlparser::ColumnDef>>()`。
- `sql_dispatcher.cpp` 仍在使用 `payload["rows"].value<QVector<QVector<QVariant>>>()`。
- `table_parser.cpp` 仍以 `QVector<ColumnDef>` 作为中间 payload 形态，仍需改成基础 `QVariantList/QVariantMap`。
- `tuple_parser.cpp` 仍会解析并写入 `whereColumn/whereOp/whereValue`，但 dispatcher 现在只是忽略 WHERE。更安全的行为应该是：只要 payload 出现 WHERE，直接返回 unsupported，避免 `UPDATE/DELETE WHERE ...` 被误执行成全表操作。

## 双方交集：第一轮可以安全合并的范围

这些路径双方都有明确函数级入口，可以作为第一轮合并范围。

| SQL / 功能 | Parser 输出 | Dispatcher 调用 | Service 调用 | 输出收口 |
|---|---|---|---|---|
| `CREATE DATABASE name` | `databaseName` | `execCreateDatabase` | `database_service::createDatabase` | `SqlExecResult{success,text,errorMessage}` |
| `DROP DATABASE name` | `databaseName` | `execDropDatabase` | `database_service::dropDatabase` | `SqlExecResult{success,text,errorMessage}` |
| `USE name` | `databaseName` | `execUseDatabase` | `database_service::useDatabase` | `SqlExecResult{success,activeDatabase}` |
| `SHOW DATABASES` | 无 | `execShowDatabases` | `database_service::showDatabases` | `SqlExecResult{selectResult}` |
| `CREATE TABLE` 基础列定义 | `tableName`, `columns` | `execCreateTable` | `table_service::createTable` | `SqlExecResult{success,text,errorMessage}` |
| `DROP TABLE name` | `tableName` | `execDropTable` | `table_service::dropTable` | `SqlExecResult{success,text,errorMessage}` |
| `SHOW TABLES` | 无 | `execShowTables` | `table_service::showTables` | `SqlExecResult{selectResult}` |
| `DESC table` | `tableName` | `execDescTable` | `table_service::describeTable` | `SqlExecResult{text}` |
| `INSERT INTO table ...` | `tableName`, `columnNames`, `rows` | `execInsert` | `tuple_service::insertRows` | `SqlExecResult{affectedRows}` |
| `SELECT cols FROM table` 无 WHERE | `tableName`, `projection` | `execSelect` | `tuple_service::selectRows` | `SqlExecResult{selectResult}` |
| `UPDATE table SET ...` 无 WHERE | `tableName`, `assignments` | `execUpdate` | `tuple_service::updateRows` | `SqlExecResult{affectedRows}` |
| `DELETE FROM table` 无 WHERE | `tableName` | `execDelete` | `tuple_service::deleteRows` | `SqlExecResult{affectedRows}` |

第一轮规则：

- SQL 中出现 `WHERE` 时，dispatcher 先拒绝。
- SQL 中出现 `LIMIT n` 时，可以在 parser/dispatcher 显式解析为非负整数并下推到 service；如果没有完成这条下推，就必须拒绝，不能静默执行成无上限 SELECT。
- SQL 中出现 `ORDER BY`、`GROUP BY`、`HAVING`、`JOIN`、聚合函数、子查询时，parser 或 dispatcher 先拒绝。
- 拒绝比伪执行更安全。

## 左侧独有：`wty` 有，但当前不能照收的内容

| `wty` 侧内容 | 问题 | 第一轮决策 |
|---|---|---|
| `database_service::listAllDatabases()` | 硬编码只读 helper，不属于当前正式 service API | 本 PR 回滚 |
| `database_service::listTablesInDatabase(databaseName)` | 硬编码只读 helper，不属于当前正式 service API | 本 PR 回滚 |
| `table_service::listTableColumns(tableName)` | 硬编码只读 helper，不属于当前正式 service API | 本 PR 回滚 |
| `buildConditions()` | 假装 WHERE 可以映射进 service | 彻底回滚 |
| About 文案宣称 ORDER/GROUP/JOIN 等复杂查询 | 功能宣称不真实 | 移除宣称；`LIMIT` 只有完成 parser/dispatcher 下推后才能宣称 |
| tokenizer 识别大量高级 SQL 关键字 | token 支持不等于执行支持 | tokenizer 可保留，但 dispatcher 必须拒绝未支持语义 |
| `QVariant::fromValue(QVector<ColumnDef>)` | 需要注册元类型 | 改成 `QVariantList/QVariantMap` |
| `QVariant::fromValue(QVector<QVector<QVariant>>)` | 需要注册元类型 | 改成基础 `QVariantList` 行结构 |

重要区分：

- 回滚硬编码 list helper，不代表结构树不能做。
- 它只表示这次 PR 不应该偷偷新增非正式 service API。
- 如果结构树长期需要这些能力，应该后续单独设计正式 read-only service。

## 右侧独有：当前 service 有，但 `wty` 暂时不必接的内容

| 当前 service 能力 | `wty` 支持情况 | 第一轮决策 |
|---|---|---|
| FK 的 `onDeleteAction/onUpdateAction` | parser 暂未完整表达 | 除非 CREATE TABLE FK 语法已明确解析，否则暂缓 |
| 多列 FK / PK / UNIQUE | parser 当前 table constraint 解析不稳 | 至少不能解析坏；完整支持可分阶段 |
| `createIndex/dropIndex` | `wty` parser 暂无 CREATE/DROP INDEX 通路 | 暂缓 |
| `showCreateTable` | `wty` 暂无直接 SQL 通路 | 暂缓 |
| `modifyConstraint` | `wty` ALTER payload 不完整 | 暂缓 |
| FK DML 级联行为 | service 内部已支持 | 不需要 UI/parser 额外处理 |

结论：

当前 service 可以比第一轮前端/parser 合并范围更丰富。

## 函数级输入输出收口

### 1. UI 到 Dispatcher

函数：

```cpp
SqlExecResult SqlDispatcher::execute(const QString &sql);
```

输入：

- 编辑器传入的原始 SQL 字符串。

输出：

- `success`
- `errorMessage`
- `text`
- `affectedRows`
- `selectResult`
- 可选：`activeDatabase`
- 可选：`structureChanged`

收口规则：

- UI 不应该通过解析 SQL 字符串来推断业务状态。
- UI 不应该手动解析 `USE`。
- UI 应该通过 dispatcher 的返回结果更新当前数据库和结构树。

### 2. Parser 到 Dispatcher

函数：

```cpp
sqlparser::ParseResult parseSql(const QString &sql);
```

输入：

- 原始 SQL 字符串。

输出：

- `success`
- `errorMessage`
- `commandType`
- `payload`

收口规则：

- `payload` 只使用 Qt 基础类型。
- 不把自定义 `QVector<CustomType>` 塞进 `QVariant`。
- parser 不调用 service。
- parser 不访问 repo。

推荐 payload 基础类型：

- `QString`
- `QStringList`
- `QVariantMap`
- `QVariantList`
- `int`
- `bool`

### 3. Dispatcher 到 database service

接受的映射：

```cpp
database_service::createDatabase(QString) -> TaskResult
database_service::dropDatabase(QString) -> TaskResult
database_service::useDatabase(QString) -> TaskResult
database_service::showDatabases() -> SelectRowsResult
```

收口规则：

- 本 PR 不新增 `listAllDatabases()`。
- 结构树如果需要数据库列表，可以临时消费 `showDatabases()`，或者等待后续正式 read-only service。

### 4. Dispatcher 到 table service

接受的映射：

```cpp
table_service::createTable(QString, TableSchema) -> TaskResult
table_service::dropTable(QString) -> TaskResult
table_service::showTables() -> SelectRowsResult
table_service::describeTable(QString) -> TextResult
```

有条件接受：

```cpp
table_service::addColumn(...)
table_service::deleteColumn(...)
table_service::modifyColumn(...)
table_service::addConstraint(...)
table_service::modifyConstraint(...)
table_service::deleteConstraint(...)
```

只有 parser payload 完整时才允许接。

第一轮不要求接：

```cpp
table_service::createIndex(...)
table_service::dropIndex(...)
table_service::showCreateTable(...)
```

### 5. Dispatcher 到 tuple service

接受的映射：

```cpp
tuple_service::selectRows(tableName, projectionColumns, {}, limit) -> SelectRowsResult
tuple_service::insertRows(tableName, rows) -> TaskResult
tuple_service::updateRows(tableName, assignmentMap, {}) -> TaskResult
tuple_service::deleteRows(tableName, {}) -> TaskResult
```

本 PR 拒绝：

```cpp
tuple_service::*Rows(..., conditionsFromWhere)
```

原因：

- 当前 `SimpleCondition` 不是完整 SQL WHERE 模型。
- review comment 已经明确要求回滚 `buildConditions` 伪实现。

如果未来要开放 WHERE，应先定义窄契约：

```text
WHERE column = literal
```

并拒绝其他所有操作符。

## 基于 review comment 的验收清单

### 编译和启动

| Comment | 收口目标 | 验收 |
|---|---|---|
| `animated_button.h` includes | 补 Qt 绘制头文件 | 构建通过 |
| `structure_panel.cpp` include | 修正 include 路径 | 构建通过 |
| `setDataRoot` 早于 `setupLayout` | 在 `StructurePanel` 构造前设置 data root | 首次结构树加载路径正确 |

### UI 状态和文案

| Comment | 收口目标 | 验收 |
|---|---|---|
| `USE` chopped bug | 仅在末尾存在分号时移除，或直接使用 dispatcher 返回结果 | `USE db` 和 `USE db;` 都正确 |
| About 文案越界 | 只保留当前真实支持的 SQL | UI 文案与当前能力一致 |

### Parser

| Comment | 收口目标 | 验收 |
|---|---|---|
| 约束遇逗号跳过错误 | 按括号深度处理完整约束定义 | 多列约束不被错误拆开 |
| `QVector<ColumnDef>` in QVariant | 使用 `QVariantList/QVariantMap` | 不需要元类型注册 |
| 重复 tokenize | 统一只分词一次 | parser 路径只有一份 token stream |

### Dispatcher

| Comment | 收口目标 | 验收 |
|---|---|---|
| QVariant 自定义类型提取 | 改用基础 QVariant 类型 | 运行期取值稳定 |
| `buildConditions` 伪 WHERE | 删除或拒绝 WHERE | WHERE SQL 返回 unsupported |
| 硬编码 list helper | 从本 PR 回滚 | 不新增非正式 service API |

## 第一轮支持的 SQL 集合

支持：

```sql
CREATE DATABASE db;
DROP DATABASE db;
USE db;
SHOW DATABASES;
CREATE TABLE t (...);
DROP TABLE t;
SHOW TABLES;
DESC t;
INSERT INTO t VALUES (...);
INSERT INTO t (a, b) VALUES (...);
SELECT * FROM t;
SELECT a, b FROM t;
UPDATE t SET a = 1;
DELETE FROM t;
SELECT * FROM t LIMIT 100;
```

拒绝：

```sql
SELECT * FROM t WHERE a = 1;
UPDATE t SET a = 1 WHERE id = 1;
DELETE FROM t WHERE id = 1;
SELECT * FROM t ORDER BY id;
SELECT * FROM a JOIN b ON a.id = b.id;
SELECT a, COUNT(*) FROM t GROUP BY a;
```

原因：

- 拒绝比假执行更安全。
- `LIMIT` 只有在 parser 解析出非负整数、dispatcher 下推到 `tuple_service::selectRows(..., limit)` 时才算支持。
- 查询谓词和查询引擎可以后续单独设计。

## 推荐合并策略

1. 先让 `wty` 按 review comment 完成修正。
2. 然后做一次 compile-only merge rehearsal。
3. 再补 dispatcher-service 函数映射测试。
4. 交集稳定后，再决定结构树是否需要正式 read-only service API。
5. 第一轮合并不要顺手扩大 SQL 支持范围。

## 最终判断

这次集成可行。

最安全的心智模型是：

```text
wty frontend/parser LEFT JOIN current service
current service RIGHT JOIN wty frontend/parser
intersection = 第一轮合并范围
left-only = 回滚、拒绝或暂缓
right-only = 暂缓，除非 parser payload 已经完整
```

按这组 review comment 收口，足够完成第一轮对齐。

下一步真正需要单独决策的是：结构树读取能力是否要成为正式 service API。这个不应该藏在前端/parser PR 里硬加。
