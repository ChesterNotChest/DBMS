# WTY 前端 / Parser / Service 对齐依据

## 1. 文档定位

本文档的目标不是讨论理想设计，而是作为当前代码的实际对齐依据。

对齐范围限定为：

- 前端调用入口
- `sqlparser::*` 解析输出
- `service::SqlDispatcher::*` 分发输入输出
- `database_service / table_service / tuple_service` 的实际函数接口

对齐精度限定为函数级，并精确到输入输出。

本文档以当前仓库代码为准；若旧报告中的现状描述与代码不符，以本文档修正后的描述为准。

## 2. 总体原则

当前这轮收口遵守下面 6 条原则：

1. `payload` 只允许使用 Qt 基础类型，不允许再把自定义 C++ 结构直接塞进 `QVariant`。
2. parser 负责“识别并结构化表达输入”，不负责调用 service。
3. dispatcher 负责“把 parser payload 转成 service 输入”，不直接操作 repo。
4. service 负责真实业务语义与约束，不要求前端理解底层存储。
5. 对于当前 payload 不完整、无法安全落到 service 的语句，必须明确拒绝，不能伪执行。
6. 对于当前未实现的 SQL 语义，拒绝优于静默降级。

## 3. 当前真实边界

### 3.1 当前已经对齐的主链路

- `CREATE DATABASE`
- `DROP DATABASE`
- `USE`
- `SHOW DATABASES`
- `CREATE TABLE`
- `DROP TABLE`
- `SHOW TABLES`
- `DESC` / `DESCRIBE`
- `SHOW CREATE TABLE`
- `INSERT`
- `SELECT ... LIMIT n`
- 无 `WHERE` 的 `UPDATE`
- 无 `WHERE` 的 `DELETE`
- `ALTER TABLE DROP COLUMN`
- `ALTER TABLE DROP CONSTRAINT`

### 3.2 当前明确拒绝的内容

- 任何 `WHERE`
- `SELECT` 中除 `LIMIT` 外的额外子句
- `ALTER TABLE ADD COLUMN`
- `ALTER TABLE MODIFY COLUMN`
- `ALTER TABLE ADD CONSTRAINT`
- `ALTER TABLE MODIFY CONSTRAINT`

这些语句不是“部分支持”，而是当前代码中应当被视为“未收口，不允许执行”。

### 3.3 当前 service 有，但 parser/dispatcher 还没有 SQL 通路的内容

- `table_service::createIndex(...)`
- `table_service::dropIndex(...)`

因此当前前端不能宣称已经支持 `CREATE INDEX` / `DROP INDEX` SQL。

## 4. 前端入口与返回约定

### 4.1 前端统一执行入口

函数：

```cpp
service::SqlExecResult service::SqlDispatcher::execute(const QString &sql);
```

输入：

- `sql`：原始 SQL 字符串

输出：

```cpp
struct SqlExecResult {
    bool success;
    QString errorMessage;
    QString text;
    int affectedRows;
    SelectRowsResult selectResult;
    QString commandType;
    QVariantMap payload;
};
```

前端使用约定：

- 成功时，不应重新解析 SQL 判断业务结果，应只看 `SqlExecResult`。
- `SELECT` 类结果优先读取 `selectResult`，不要解析 `text`。
- `SHOW CREATE TABLE` 当前走 `text`，UI 可按 `commandType == "SHOW_CREATE_TABLE"` 特判展示。
- `USE_DATABASE` 成功后，前端当前是从 `payload["databaseName"]` 同步当前库，而不是从单独字段同步。
- DDL 完成后，前端可按 `commandType` 前缀 `CREATE_ / DROP_ / ALTER_` 刷新结构树。

### 4.2 结构树读取边界

当前合理读取方式是：

- 数据库列表：`database_service::showDatabases()`
- 当前库表列表：`table_service::showTables()`

## 5. Parser 总入口约定

### 5.1 总入口

函数：

```cpp
sqlparser::ParseResult sqlparser::parseSql(const QString &sql);
```

输出：

```cpp
struct ParseResult {
    bool success;
    QString errorMessage;
    QString commandType;
    QVariantMap payload;
};
```

### 5.2 `payload` 允许的值类型

当前代码中的 `payload` 只应包含 Qt 基础类型：

- `QString`
- `QStringList`
- `QVariantMap`
- `QVariantList`
- `int`
- `double`
- `bool`

不允许再出现：

- `QVector<ColumnDef>`
- `QVector<ConstraintDef>`
- 任何自定义 struct

### 5.3 分层入口

当前 parser 分层如下：

```cpp
ParseResult parseDatabaseSql(const QString &sql, const QVector<SqlToken> &tokens);
ParseResult parseTableSql(const QString &sql, const QVector<SqlToken> &tokens);
ParseResult parseTupleSql(const QString &sql, const QVector<SqlToken> &tokens);
```

`parseSql()` 负责统一 `tokenize` 一次，再把同一份 `tokens` 下传；不应重复 tokenize。

## 6. 命令级输入输出对齐表

本节是本文档最核心的收口依据。

### 6.1 数据库级命令

| commandType | SQL 形式 | parser 输出 payload | dispatcher 函数 | service 函数 | dispatcher 成功输出 |
|---|---|---|---|---|---|
| `CREATE_DATABASE` | `CREATE DATABASE db;` | `databaseName: QString` | `execCreateDatabase` | `database_service::createDatabase(const QString &databaseName)` | `success=true`, `text="Database 'db' created"` |
| `DROP_DATABASE` | `DROP DATABASE db;` | `databaseName: QString` | `execDropDatabase` | `database_service::dropDatabase(const QString &databaseName)` | `success=true`, `text="Database 'db' dropped"` |
| `USE_DATABASE` | `USE db;` | `databaseName: QString` | `execUseDatabase` | `database_service::useDatabase(const QString &databaseName)` | `success=true`, `text="Using database 'db'"` |
| `SHOW_DATABASES` | `SHOW DATABASES;` | 空 | `execShowDatabases` | `database_service::showDatabases()` | `success=true`, `selectResult` 有效 |

说明：

- `USE_DATABASE` 的当前前端同步依据是返回 `payload["databaseName"]`。
- `SHOW_DATABASES` 的 UI 应基于 `selectResult.resultTable` 展示，而不是解析 `text`。

### 6.2 表级命令

#### 6.2.1 `CREATE TABLE`

SQL 形式：

```sql
CREATE TABLE t (
  id INT PRIMARY KEY,
  name VARCHAR(20) NOT NULL,
  parent_id INT,
  CONSTRAINT fk_t_parent FOREIGN KEY (parent_id) REFERENCES parent(id) ON DELETE CASCADE
);
```

parser 输出：

```cpp
payload["tableName"] = QString
payload["columns"] = QVariantList<columnMap>
payload["constraints"] = QVariantList<constraintMap>
```

其中 `columnMap` 当前统一键集合为：

```cpp
{
  "name": QString,
  "type": QString,
  "length": int,
  "notNull": bool,
  "primaryKey": bool,
  "autoIncrement": bool,
  "unique": bool,
  "defaultValue": QString,
  "checkClause": QString,
  "referencesTable": QString,
  "referencedColumns": QStringList,
  "onDeleteAction": QString,
  "onUpdateAction": QString
}
```

其中 `constraintMap` 当前统一键集合为：

```cpp
{
  "name": QString,
  "type": QString,               // PRIMARY_KEY / UNIQUE / CHECK / FOREIGN_KEY
  "columns": QStringList,
  "checkClause": QString,
  "referencedTable": QString,
  "referencedColumns": QStringList,
  "onDeleteAction": QString,
  "onUpdateAction": QString
}
```

dispatcher 行为：

- `execCreateTable(const ParseResult &p)`
- 读取 `tableName`
- 读取 `columns: QVariantList`
- 把每个 `columnMap` 转成 `ColumnDefinition`
- 由 `buildGeneratedConstraints(definition)` 自动生成列级约束
- 把 `constraints: QVariantList` 转成 `tabledef::Constraint`
- 组装 `tabledef::TableSchema`
- 调用：

```cpp
table_service::createTable(const QString &tableName,
                           const tabledef::TableSchema &schema)
```

dispatcher 拒绝条件：

- `columns` 为空
- 某列缺少 `name`
- 某表级约束缺少 `type`

当前错误文案：

- `CREATE TABLE: columns payload is empty or incomplete`
- `CREATE TABLE: column payload is incomplete`
- `CREATE TABLE: constraint payload is incomplete`

#### 6.2.2 `DROP TABLE`

| commandType | SQL 形式 | parser 输出 payload | dispatcher 函数 | service 函数 | dispatcher 成功输出 |
|---|---|---|---|---|---|
| `DROP_TABLE` | `DROP TABLE t;` | `tableName: QString` | `execDropTable` | `table_service::dropTable(const QString &tableName)` | `success=true`, `text="Table 't' dropped"` |

#### 6.2.3 `SHOW TABLES`

| commandType | SQL 形式 | parser 输出 payload | dispatcher 函数 | service 函数 | dispatcher 成功输出 |
|---|---|---|---|---|---|
| `SHOW_TABLES` | `SHOW TABLES;` | 空 | `execShowTables` | `table_service::showTables()` | `success=true`, `selectResult` 有效 |

说明：

- `showTables()` 依赖当前会话库 `service::currentDatabase`。
- 前端在调用前必须确保当前库已经通过 `USE` 或其它 UI 行为同步到 service。

#### 6.2.4 `DESC` / `DESCRIBE`

| commandType | SQL 形式 | parser 输出 payload | dispatcher 函数 | service 函数 | dispatcher 成功输出 |
|---|---|---|---|---|---|
| `DESC_TABLE` | `DESC t;` / `DESCRIBE t;` | `tableName: QString` | `execDescTable` | `table_service::describeTable(const QString &tableName)` | `success=true`, `text` 有效 |

#### 6.2.5 `SHOW CREATE TABLE`

| commandType | SQL 形式 | parser 输出 payload | dispatcher 函数 | service 函数 | dispatcher 成功输出 |
|---|---|---|---|---|---|
| `SHOW_CREATE_TABLE` | `SHOW CREATE TABLE t;` | `tableName: QString` | `execShowCreateTable` | `table_service::showCreateTable(const QString &tableName)` | `success=true`, `text` 有效 |

说明：

- 当前 UI 会把 `SHOW_CREATE_TABLE` 的 `text` 转成两列表格展示。
- 这条链路已经收口，不应再视为缺失功能。

### 6.3 `ALTER TABLE` 当前收口状态

当前 parser 已能识别 `ALTER_TABLE`，但不同动作的收口程度不同。

| alterAction | parser 当前输出 | dispatcher 当前要求 | 当前状态 |
|---|---|---|---|
| `ADD_COLUMN` | `tableName`, `alterAction`, `columnName` | 需要完整 `payload["column"]: QVariantMap` | 当前拒绝执行 |
| `MODIFY_COLUMN` | `tableName`, `alterAction`, `columnName` | 需要完整 `payload["column"]: QVariantMap` | 当前拒绝执行 |
| `ADD_CONSTRAINT` | `tableName`, `alterAction` | 需要完整 `payload["constraint"]: QVariantMap` | 当前拒绝执行 |
| `MODIFY_CONSTRAINT` | `tableName`, `alterAction` | 需要 `constraintName + constraint` | 当前拒绝执行 |
| `DROP_COLUMN` | `tableName`, `alterAction`, `columnName` | 只需 `columnName` | 当前已对齐 |
| `DROP_CONSTRAINT` | `tableName`, `alterAction`, `constraintName` | 只需 `constraintName` | 当前已对齐 |

dispatcher 当前对应函数：

```cpp
SqlExecResult execAlterTable(const sqlparser::ParseResult &p);
```

当前可真正落到 service 的只有：

```cpp
table_service::deleteColumn(const QString &tableName,
                            const QString &columnName);

table_service::deleteConstraint(const QString &tableName,
                                const QString &constraintName);
```

如果后续要补齐 `ADD/MODIFY`，parser 必须输出：

- `payload["column"]`：格式与 `CREATE TABLE` 的 `columnMap` 完全一致
- `payload["constraint"]`：格式与 `CREATE TABLE` 的 `constraintMap` 完全一致
- `payload["constraintName"]`：仅 `MODIFY_CONSTRAINT / DROP_CONSTRAINT` 需要

### 6.4 元组级命令

#### 6.4.1 `SELECT`

SQL 形式：

```sql
SELECT * FROM t;
SELECT id, name FROM t LIMIT 100;
```

parser 输出：

```cpp
payload["projection"] = QStringList
payload["tableName"] = QString
payload["limit"] = int    // 缺省为 -1
```

dispatcher 行为：

```cpp
tuple_service::selectRows(const QString &tableName,
                          const QStringList &projectionColumns,
                          const QList<SimpleCondition> &conditions,
                          int limit = -1);
```

当前 dispatcher 调用规则：

- `conditions` 固定传空
- `limit` 从 payload 下推

当前已支持：

- `LIMIT` 非负整数
- `LIMIT` 缺省时按 `-1` 传入

当前明确拒绝：

- 任何 `WHERE`
- 任何额外子句，例如 `ORDER BY`、`GROUP BY`、`JOIN`

当前错误文案包括：

- `WHERE is not supported yet`
- `SELECT: duplicate LIMIT clause`
- `SELECT: LIMIT requires a non-negative integer`
- `SELECT: unsupported clause '...'`

#### 6.4.2 `INSERT`

SQL 形式：

```sql
INSERT INTO t (id, name) VALUES (1, 'a'), (2, 'b');
INSERT INTO t VALUES (1, 'a');
```

parser 输出：

```cpp
payload["tableName"] = QString
payload["columnNames"] = QStringList
payload["rowCount"] = int
payload["rows"] = QVariantList<rowList>
```

其中每个 `rowList` 是一个 `QVariantList`，内部元素为基础类型值。

dispatcher 行为：

- 若 `columnNames` 为空，则先加载表 schema，使用真实列序补足映射。
- 再把 `rows` 转成：

```cpp
QList<QMap<QString, QString>>
```

- 最终调用：

```cpp
tuple_service::insertRows(const QString &tableName,
                          const QList<QMap<QString, QString>> &rows);
```

#### 6.4.3 `UPDATE`

SQL 形式：

```sql
UPDATE t SET name = 'x';
```

parser 输出：

```cpp
payload["tableName"] = QString
payload["assignments"] = QVariantMap
```

dispatcher 行为：

- 把 `assignments` 转成：

```cpp
QMap<QString, QString>
```

- `conditions` 固定为空
- 调用：

```cpp
tuple_service::updateRows(const QString &tableName,
                          const QMap<QString, QString> &assignmentMap,
                          const QList<SimpleCondition> &conditions);
```

当前明确拒绝：

- 任何 `WHERE`

#### 6.4.4 `DELETE`

SQL 形式：

```sql
DELETE FROM t;
```

parser 输出：

```cpp
payload["tableName"] = QString
```

dispatcher 行为：

- `conditions` 固定为空
- 调用：

```cpp
tuple_service::deleteRows(const QString &tableName,
                          const QList<SimpleCondition> &conditions);
```

当前明确拒绝：

- 任何 `WHERE`

## 7. Service 侧函数收口

本节只记录当前 parser/dispatcher 需要依赖的 service 口。

### 7.1 数据库级

```cpp
TaskResult createDatabase(const QString &databaseName);
TaskResult dropDatabase(const QString &databaseName);
TaskResult useDatabase(const QString &databaseName);
SelectRowsResult showDatabases();
```

### 7.2 表级

```cpp
TaskResult createTable(const QString &tableName,
                       const tabledef::TableSchema &schema);

TaskResult dropTable(const QString &tableName);

TaskResult addColumn(const QString &tableName,
                     const ColumnDefinition &definition);

TaskResult deleteColumn(const QString &tableName,
                        const QString &columnName);

TaskResult modifyColumn(const QString &tableName,
                        const QString &columnName,
                        const ColumnDefinition &definition);

TaskResult addConstraint(const QString &tableName,
                         const tabledef::Constraint &constraint);

TaskResult modifyConstraint(const QString &tableName,
                            const QString &constraintName,
                            const tabledef::Constraint &constraint);

TaskResult deleteConstraint(const QString &tableName,
                            const QString &constraintName);

TaskResult createIndex(const QString &tableName,
                       const QString &indexName,
                       const QStringList &columnNames,
                       bool isUnique);

TaskResult dropIndex(const QString &tableName,
                     const QString &indexName);

SelectRowsResult showTables();

TextResult describeTable(const QString &tableName);

TextResult showCreateTable(const QString &tableName);
```

### 7.3 元组级

```cpp
SelectRowsResult selectRows(const QString &tableName,
                            const QStringList &projectionColumns,
                            const QList<SimpleCondition> &conditions,
                            int limit = -1);

TaskResult insertRows(const QString &tableName,
                      const QList<QMap<QString, QString>> &rows);

TaskResult deleteRows(const QString &tableName,
                      const QList<SimpleCondition> &conditions);

TaskResult updateRows(const QString &tableName,
                      const QMap<QString, QString> &assignmentMap,
                      const QList<SimpleCondition> &conditions);
```

说明：

- `SimpleCondition` 目前不是 SQL `WHERE` 的正式外部协议。
- 当前 SQL 层没有把 `WHERE` 收口到 `SimpleCondition`，因此不能对外宣称已支持 `WHERE`。

## 8. 当前 parser -> service 的关键转换规则

### 8.1 列定义转换

dispatcher 当前将 `columnMap` 转成：

```cpp
service::ColumnDefinition
```

其核心来源字段如下：

| `columnMap` 键 | 目标字段 |
|---|---|
| `name` | `definition.column.name` |
| `type` | `definition.column.type` |
| `length` | `definition.column.length` |
| `notNull` | `definition.column.notNull` |
| `defaultValue` | `definition.column.defaultValue` |
| `autoIncrement` | `definition.column.autoIncrement` |
| `checkClause` | `definition.column.check` 与 `definition.checkClause` |
| `primaryKey` | `definition.primaryKey` |
| `unique` | `definition.unique` |
| `referencesTable` | `definition.referencedTable` |
| `referencedColumns` | `definition.referencedColumns` |
| `onDeleteAction` | `definition.onDeleteAction` |
| `onUpdateAction` | `definition.onUpdateAction` |

### 8.2 表级约束转换

dispatcher 当前将 `constraintMap` 转成：

```cpp
tabledef::Constraint
```

其核心来源字段如下：

| `constraintMap` 键 | 目标字段 |
|---|---|
| `type` | `constraint.type` |
| `columns` | `constraint.columns` |
| `checkClause` | `constraint.checkClause` |
| `referencedTable` | `constraint.referencedTable` |
| `referencedColumns` | `constraint.referencedColumns` |
| `onDeleteAction` | `constraint.onDeleteAction` |
| `onUpdateAction` | `constraint.onUpdateAction` |
| `name` | `constraint.name`，为空时 dispatcher 自动生成 |

### 8.3 自动生成的列级约束

`CREATE TABLE` 中列级语义：

- `PRIMARY KEY`
- `UNIQUE`
- `CHECK`
- 列级 `REFERENCES ...`

不会直接以表级 `constraints[]` 传入，而是先进入 `ColumnDefinition`，再由：

```cpp
buildGeneratedConstraints(definition)
```

自动生成 `schema.constraints`。

因此：

- parser 必须保留列级语义字段
- dispatcher 不应丢弃这些字段

## 9. SERVICE FULL COVERAGE GAP

本节的目标不是改造 service，而是以当前 `service.h` 为基准，补齐“为了让 SQL 执行软件覆盖全部正式 service 能力，还差哪些 parser / dispatcher / SQL 协议工作”。

### 9.1 统计口径

本节的“service 全覆盖”只统计当前适合由前端 / parser / dispatcher 直接对接的正式 service 入口：

- `database_service::*`
- `table_service::*`
- `tuple_service::*`

不纳入“SQL 直连全覆盖”目标的接口：

- `service::setDataRoot(...)`
- `service::getDataRoot()`
- `service::currentDatabase`
- `service::TableDmlService::*`

原因：

- 前三者属于运行环境 / 会话状态，不是面向 SQL 的业务命令。
- `TableDmlService` 属于更底层的通用二维表引擎，不应成为前端 SQL 的直接落点。

### 9.2 已完全覆盖的正式 service 入口

| service 函数 | 当前 SQL 通路 | 覆盖状态 |
|---|---|---|
| `database_service::createDatabase(...)` | `CREATE DATABASE` | 已覆盖 |
| `database_service::dropDatabase(...)` | `DROP DATABASE` | 已覆盖 |
| `database_service::useDatabase(...)` | `USE` | 已覆盖 |
| `database_service::showDatabases()` | `SHOW DATABASES` | 已覆盖 |
| `table_service::createTable(...)` | `CREATE TABLE` | 已覆盖 |
| `table_service::dropTable(...)` | `DROP TABLE` | 已覆盖 |
| `table_service::deleteColumn(...)` | `ALTER TABLE ... DROP COLUMN ...` | 已覆盖 |
| `table_service::deleteConstraint(...)` | `ALTER TABLE ... DROP CONSTRAINT ...` | 已覆盖 |
| `table_service::showTables()` | `SHOW TABLES` | 已覆盖 |
| `table_service::describeTable(...)` | `DESC` / `DESCRIBE` | 已覆盖 |
| `table_service::showCreateTable(...)` | `SHOW CREATE TABLE` | 已覆盖 |
| `tuple_service::insertRows(...)` | `INSERT` | 已覆盖 |
| `tuple_service::selectRows(..., limit)` | `SELECT` / `SELECT ... LIMIT n` | 已覆盖无条件路径 |
| `tuple_service::updateRows(..., {})` | 无 `WHERE` 的 `UPDATE` | 已覆盖无条件路径 |
| `tuple_service::deleteRows(..., {})` | 无 `WHERE` 的 `DELETE` | 已覆盖无条件路径 |

说明：

- 对 `tuple_service::selectRows / updateRows / deleteRows` 而言，当前只覆盖了“空条件”调用路径。
- 因为这 3 个函数还带有 `QList<SimpleCondition>`，所以它们在“按 service 能力完全覆盖”的意义上仍然存在条件路径缺口。

### 9.3 仍未覆盖到 SQL 的正式 service 能力

本节不再只写“缺什么”，而是把剩余缺口直接写成可开发协议。

### 9.3.1 `ALTER TABLE ADD/MODIFY COLUMN` 的正式协议

目标覆盖的 service 函数：

```cpp
table_service::addColumn(const QString &tableName,
                         const ColumnDefinition &definition);

table_service::modifyColumn(const QString &tableName,
                            const QString &columnName,
                            const ColumnDefinition &definition);
```

分层归属：

- tokenizer：不需要新增 token，复用现有 `ALTER / TABLE / ADD / MODIFY / COLUMN`
- parser：负责把列定义解析成完整 `columnMap`
- dispatcher：负责把 `columnMap` 转成 `ColumnDefinition`
- service：继续只接收 `ColumnDefinition`，不负责重解析 SQL

SQL 协议：

```sql
ALTER TABLE table_name ADD COLUMN column_definition;
ALTER TABLE table_name MODIFY COLUMN column_definition;
```

其中 `column_definition` 的语法与 `CREATE TABLE` 中单列定义完全一致，例如：

```sql
id INT PRIMARY KEY
name VARCHAR(20) NOT NULL DEFAULT 'a'
parent_id INT REFERENCES parent(id) ON DELETE CASCADE
```

parser 正式输出：

```cpp
payload["tableName"] = QString
payload["alterAction"] = "ADD_COLUMN" | "MODIFY_COLUMN"
payload["column"] = QVariantMap
```

`payload["column"]` 的键集合必须与 `CREATE TABLE` 的单列 `columnMap` 完全一致：

```cpp
{
  "name": QString,
  "type": QString,
  "length": int,
  "notNull": bool,
  "primaryKey": bool,
  "autoIncrement": bool,
  "unique": bool,
  "defaultValue": QString,
  "checkClause": QString,
  "referencesTable": QString,
  "referencedColumns": QStringList,
  "onDeleteAction": QString,
  "onUpdateAction": QString
}
```

dispatcher 正式行为：

- `ADD_COLUMN`
  - 校验 `payload["column"]` 必须存在且为 `QVariantMap`
  - 校验 `column["name"]` 非空
  - 将其转为 `ColumnDefinition`
  - 调用：

```cpp
table_service::addColumn(tableName, definition);
```

- `MODIFY_COLUMN`
  - 校验 `payload["column"]` 必须存在且为 `QVariantMap`
  - 校验 `column["name"]` 非空
  - 将其转为 `ColumnDefinition`
  - 调用：

```cpp
table_service::modifyColumn(tableName, definition.column.name, definition);
```

约束说明：

- 本协议下，`MODIFY COLUMN` 不额外支持“旧列名 -> 新列名”的改名语义。
- 因为当前 service 接口没有单独的 rename 协议，所以 `definition.column.name` 就是要修改的目标列名。

dispatcher 错误边界：

- 缺少 `column`：`ALTER TABLE ADD/MODIFY COLUMN requires a complete column payload`
- `column["name"]` 为空：视为不完整 payload，拒绝调用 service

### 9.3.2 `ALTER TABLE ADD/MODIFY CONSTRAINT` 的正式协议

目标覆盖的 service 函数：

```cpp
table_service::addConstraint(const QString &tableName,
                             const tabledef::Constraint &constraint);

table_service::modifyConstraint(const QString &tableName,
                                const QString &constraintName,
                                const tabledef::Constraint &constraint);
```

分层归属：

- tokenizer：不需要新增 token，复用现有 `ALTER / TABLE / ADD / MODIFY / CONSTRAINT`
- parser：负责把表级约束解析成完整 `constraintMap`
- dispatcher：负责把 `constraintMap` 转成 `tabledef::Constraint`
- service：继续只接收 `tabledef::Constraint`

SQL 协议：

```sql
ALTER TABLE table_name ADD constraint_definition;
ALTER TABLE table_name MODIFY CONSTRAINT old_constraint_name constraint_definition;
```

其中 `constraint_definition` 的语法与 `CREATE TABLE` 中表级约束定义完全一致，例如：

```sql
CONSTRAINT pk_t PRIMARY KEY (id)
CONSTRAINT uq_t UNIQUE (a, b)
CONSTRAINT ck_t CHECK (id > 0)
CONSTRAINT fk_t FOREIGN KEY (parent_id) REFERENCES parent(id) ON DELETE NO ACTION
```

parser 正式输出：

- `ADD_CONSTRAINT`

```cpp
payload["tableName"] = QString
payload["alterAction"] = "ADD_CONSTRAINT"
payload["constraint"] = QVariantMap
```

- `MODIFY_CONSTRAINT`

```cpp
payload["tableName"] = QString
payload["alterAction"] = "MODIFY_CONSTRAINT"
payload["constraintName"] = QString
payload["constraint"] = QVariantMap
```

`payload["constraint"]` 的键集合必须与 `CREATE TABLE` 的单个 `constraintMap` 完全一致：

```cpp
{
  "name": QString,
  "type": QString,
  "columns": QStringList,
  "checkClause": QString,
  "referencedTable": QString,
  "referencedColumns": QStringList,
  "onDeleteAction": QString,
  "onUpdateAction": QString
}
```

dispatcher 正式行为：

- `ADD_CONSTRAINT`
  - 校验 `payload["constraint"]` 必须存在且为 `QVariantMap`
  - 校验 `constraint["type"]` 非空
  - 转成 `tabledef::Constraint`
  - 调用：

```cpp
table_service::addConstraint(tableName, constraint);
```

- `MODIFY_CONSTRAINT`
  - 校验 `constraintName` 非空
  - 校验 `payload["constraint"]` 必须存在且为 `QVariantMap`
  - 若 `constraint["name"]` 为空，则 dispatcher 应先把它补成 `constraintName`
  - 转成 `tabledef::Constraint`
  - 调用：

```cpp
table_service::modifyConstraint(tableName, constraintName, constraint);
```

dispatcher 错误边界：

- 缺少 `constraint`：`ALTER TABLE ADD/MODIFY CONSTRAINT requires a complete constraint payload`
- 缺少 `constraintName`：`ALTER TABLE MODIFY CONSTRAINT requires constraintName`
- 缺少 `constraint["type"]`：视为不完整 payload，拒绝调用 service

### 9.3.3 `CREATE/DROP INDEX` 的正式协议

目标覆盖的 service 函数：

```cpp
table_service::createIndex(const QString &tableName,
                           const QString &indexName,
                           const QStringList &columnNames,
                           bool isUnique);

table_service::dropIndex(const QString &tableName,
                         const QString &indexName);
```

分层归属：

- tokenizer：需要新增 `INDEX`、`ON`
- parser：负责识别索引 SQL 并输出 index payload
- dispatcher：负责校验 payload 并调用现有 service
- service：继续只接收 `tableName / indexName / columnNames / isUnique`

SQL 协议：

```sql
CREATE INDEX index_name ON table_name (col1, col2, ...);
CREATE UNIQUE INDEX index_name ON table_name (col1, col2, ...);
DROP INDEX index_name ON table_name;
```

建议 commandType：

```cpp
"CREATE_INDEX"
"DROP_INDEX"
```

parser 正式输出：

- `CREATE_INDEX`

```cpp
payload["tableName"] = QString
payload["indexName"] = QString
payload["columnNames"] = QStringList
payload["isUnique"] = bool
```

- `DROP_INDEX`

```cpp
payload["tableName"] = QString
payload["indexName"] = QString
```

dispatcher 正式行为：

- 新增：

```cpp
SqlExecResult execCreateIndex(const sqlparser::ParseResult &p);
SqlExecResult execDropIndex(const sqlparser::ParseResult &p);
```

- `CREATE_INDEX`
  - 校验 `tableName`、`indexName` 非空
  - 校验 `columnNames` 非空
  - 调用：

```cpp
table_service::createIndex(tableName, indexName, columnNames, isUnique);
```

- `DROP_INDEX`
  - 校验 `tableName`、`indexName` 非空
  - 调用：

```cpp
table_service::dropIndex(tableName, indexName);
```

dispatcher 错误边界：

- `CREATE INDEX: expected index name`
- `CREATE INDEX: expected table name`
- `CREATE INDEX: expected column list`
- `DROP INDEX: expected index name`
- `DROP INDEX: expected table name`

### 9.3.4 `WHERE` 条件路径的正式协议

目标覆盖的 service 函数：

```cpp
tuple_service::selectRows(const QString &tableName,
                          const QStringList &projectionColumns,
                          const QList<SimpleCondition> &conditions,
                          int limit);

tuple_service::updateRows(const QString &tableName,
                          const QMap<QString, QString> &assignmentMap,
                          const QList<SimpleCondition> &conditions);

tuple_service::deleteRows(const QString &tableName,
                          const QList<SimpleCondition> &conditions);
```

分层归属：

- tokenizer：复用现有 `WHERE / AND / EQ`
- parser：只负责把支持范围内的 `WHERE` 解析成条件列表
- dispatcher：只负责把条件 payload 转成 `QList<SimpleCondition>`
- service：继续只接收 `QList<SimpleCondition>`，不负责解释 SQL 语法

当前 `SimpleCondition` 的真实语义边界：

```cpp
struct SimpleCondition
{
    QString columnName;
    QString value;
};
```

结合 `table_dml_service.cpp` 的匹配逻辑，当前能正式暴露给 SQL 的最小条件协议必须限定为：

```sql
WHERE col1 = literal [AND col2 = literal ...]
```

明确不纳入本阶段协议的内容：

- `OR`
- `!=`, `<`, `>`, `<=`, `>=`
- `LIKE`, `IN`, `BETWEEN`, `IS NULL`
- 括号

parser 正式输出：

```cpp
payload["conditions"] = QVariantList<conditionMap>
```

其中每个 `conditionMap` 为：

```cpp
{
  "columnName": QString,
  "value": QString
}
```

命令级适用方式：

- `SELECT`

```cpp
payload["tableName"] = QString
payload["projection"] = QStringList
payload["limit"] = int
payload["conditions"] = QVariantList    // 无 WHERE 时可省略
```

- `UPDATE`

```cpp
payload["tableName"] = QString
payload["assignments"] = QVariantMap
payload["conditions"] = QVariantList    // 无 WHERE 时可省略
```

- `DELETE`

```cpp
payload["tableName"] = QString
payload["conditions"] = QVariantList    // 无 WHERE 时可省略
```

dispatcher 正式行为：

- 新增条件转换辅助函数，例如：

```cpp
QList<SimpleCondition> simpleConditionsFromPayload(const QVariantList &conditionsPayload);
```

- `SELECT`

```cpp
tuple_service::selectRows(tableName, projectionColumns, conditions, limit);
```

- `UPDATE`

```cpp
tuple_service::updateRows(tableName, assignmentMap, conditions);
```

- `DELETE`

```cpp
tuple_service::deleteRows(tableName, conditions);
```

dispatcher 错误边界：

- condition 缺少 `columnName`：拒绝
- `WHERE` 出现了非 `=` 运算符：parser 直接拒绝
- 出现 `AND` 之外的组合方式：parser 直接拒绝

### 9.4 达成 service 全覆盖前的分层实施清单

若目标是“SQL 层覆盖全部正式 service 能力”，则应按分层补齐，而不是把职责滑到错误层级。

tokenizer 必补：

1. 新增 `INDEX`
2. 新增 `ON`

parser 必补：

1. `ALTER TABLE ADD COLUMN` 输出完整 `payload["column"]`
2. `ALTER TABLE MODIFY COLUMN` 输出完整 `payload["column"]`
3. `ALTER TABLE ADD CONSTRAINT` 输出完整 `payload["constraint"]`
4. `ALTER TABLE MODIFY CONSTRAINT` 输出 `constraintName + constraint`
5. `CREATE INDEX / CREATE UNIQUE INDEX` 输出 index payload
6. `DROP INDEX` 输出 index payload
7. `SELECT / UPDATE / DELETE` 在简单 `WHERE` 下输出 `payload["conditions"]`

dispatcher 必补：

1. 新增 index 的 `execCreateIndex / execDropIndex`
2. 为 `ALTER` 完整 payload 调用现有 `addColumn / modifyColumn / addConstraint / modifyConstraint`
3. 新增 `conditions payload -> QList<SimpleCondition>` 的转换
4. 对不完整 payload 继续保持“拒绝调用 service”的策略

service 必补：

- 无新增签名要求
- 现有正式函数直接作为最终落点使用

### 9.5 达成 service 全覆盖前的结论

在完成 9.3 和 9.4 中定义的协议之前，当前系统只能称为“覆盖已收口交集”，不能称为“覆盖全部正式 service 能力”。

## 10. 当前不应再出现的旧描述

下面这些旧说法已经不符合当前代码，应视为废弃：

- “payload 可以直接传 `QVector<ColumnDef>` / `QVector<ConstraintDef>`”
- “dispatcher 可以临时 buildConditions 支持简单 WHERE”
- “前端当前支持 `WHERE` 简单条件”
- “`SHOW CREATE TABLE` 还没有 service 通路”
- “`LIMIT` 还没有下推到 service”

## 11. 下一步若继续补齐的最低要求

若下一步继续扩 parser / dispatcher，而不改 service 边界，最低要求如下：

### 11.1 若要补 `ALTER TABLE ADD/MODIFY COLUMN`

parser 必须输出完整 `payload["column"]`，格式必须与 `CREATE TABLE` 的单列 `columnMap` 完全一致。

### 11.2 若要补 `ALTER TABLE ADD/MODIFY CONSTRAINT`

parser 必须输出完整 `payload["constraint"]`，格式必须与 `CREATE TABLE` 的单个 `constraintMap` 完全一致。

### 11.3 若要补 `WHERE`

不能再走“parser 解析一点，dispatcher 忽略一点”的伪支持路线。

至少需要：

- 明确 SQL `WHERE` 到 `QList<SimpleCondition>` 的映射规则
- 明确运算符集合
- 明确多条件组合规则
- 明确 parser/dispatcher/service 三层都同时收口

### 11.4 若要补索引 SQL

必须新增 parser / dispatcher 通路，并最终落到：

```cpp
table_service::createIndex(...)
table_service::dropIndex(...)
```

不能只在 UI 文案中宣称支持。

## 12. 结论

当前可作为主要对齐依据的，不再是“wty 旧实现拥有什么”，而是：

- parser 当前真实能输出什么 `payload`
- dispatcher 当前真实接受什么 `payload`
- service 当前真实提供什么函数接口

基于当前代码，最稳妥的对齐结论是：

1. `CREATE/DROP/USE/SHOW DATABASES`
2. `CREATE/DROP TABLE`、`SHOW TABLES`、`DESC`、`SHOW CREATE TABLE`
3. `INSERT`
4. `SELECT ... LIMIT n`
5. 无 `WHERE` 的 `UPDATE/DELETE`
6. `ALTER TABLE DROP COLUMN / DROP CONSTRAINT`

以上可以作为本轮前端 -> parser -> dispatcher -> service 的正式交集。
