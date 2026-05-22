# PARTIAL_ALTER_PATCH_PLAN

目标：在不改变现有 `ALTER TABLE ... MODIFY COLUMN ...` 完整替换语义的前提下，补充面向用户和前端的局部列修改语法。局部修改语法只表达一个明确变更点，由 dispatcher 读取旧列定义并补成完整 `ColumnDefinition`，再复用现有 `table_service::modifyColumn(...)`。

本计划只覆盖列级局部 ALTER；`MODIFY CONSTRAINT` 仍保持完整约束定义提交，约束 patch 语义后续如有需要单独规划。

## 阶段一：局部 ALTER COLUMN 语法与执行补丁

### 1. 影响范围

允许修改：

1. [utils/sql_parser/sql_tokenizer.h](../../utils/sql_parser/sql_tokenizer.h)
   - 如现有 token 集合缺少 `RENAME / TO / SET`，补充关键字 token；若 parser 只按 lexeme 判断，可不新增。
2. [utils/sql_parser/sql_tokenizer.cpp](../../utils/sql_parser/sql_tokenizer.cpp)
   - 补齐新增关键字识别。
3. [utils/sql_parser/table_parser.cpp](../../utils/sql_parser/table_parser.cpp)
   - 在 `ALTER_TABLE` 分支中新增局部 alterAction 解析。
4. [controller/sql_dispatcher.cpp](../../controller/sql_dispatcher.cpp)
   - 新增局部 alterAction 分发。
   - 读取旧 schema，构造完整 `ColumnDefinition` 后调用 service。
5. [service/service.h](../../service/service.h)
   - 首选不修改；若需要暴露 schema 查询辅助函数，再最小补充。
6. [service/table_service.cpp](../../service/table_service.cpp)
   - 首选不修改；复用现有 `modifyColumn(tableName, oldColumnName, definition)`。
7. [tests/test_parser_dispatcher.cpp](../../tests/test_parser_dispatcher.cpp)
   - 覆盖 parser payload 与 dispatcher 落库效果。
8. [tests/TEST_PLAN.md](../../tests/TEST_PLAN.md)
   - 如新增 parser/dispatcher 或 service 测试范围，补充统一测试说明。

不修改：

1. 现有 `ALTER TABLE ... MODIFY COLUMN ...` 的完整替换定义语义。
2. 现有 `ADD COLUMN / DROP COLUMN / ADD CONSTRAINT / DROP CONSTRAINT / MODIFY CONSTRAINT` 语义。
3. CLI/GUI 执行链；新语法仍走 `SqlClientEngine -> SqlDispatcher -> parser/service`。

### 2. 数据流函数级收口

#### 2.1 `ALTER TABLE t ALTER COLUMN c SET DEFAULT v`

```text
SQL
-> sqlparser::parseSql(...)
-> table_parser 生成 ParseResult
   commandType = ALTER_TABLE
   payload.tableName = t
   payload.alterAction = ALTER_COLUMN_SET_DEFAULT
   payload.columnName = c
   payload.defaultValue = v
-> SqlDispatcher::execAlterTable(...)
-> load current table schema
-> find old column c
-> build ColumnDefinition from old column and old generated constraints
-> set definition.column.defaultValue = v
-> table_service::modifyColumn(t, c, definition)
-> SqlExecResult
```

#### 2.2 `ALTER TABLE t ALTER COLUMN c DROP DEFAULT`

```text
SQL
-> parser payload alterAction = ALTER_COLUMN_DROP_DEFAULT
-> dispatcher reads old column
-> copy old definition
-> definition.column.defaultValue = ""
-> table_service::modifyColumn(t, c, definition)
```

#### 2.3 `ALTER TABLE t ALTER COLUMN c SET NOT NULL`

```text
SQL
-> parser payload alterAction = ALTER_COLUMN_SET_NOT_NULL
-> dispatcher reads old column
-> copy old definition
-> definition.column.notNull = true
-> table_service::modifyColumn(t, c, definition)
```

#### 2.4 `ALTER TABLE t ALTER COLUMN c DROP NOT NULL`

```text
SQL
-> parser payload alterAction = ALTER_COLUMN_DROP_NOT_NULL
-> dispatcher reads old column
-> copy old definition
-> definition.column.notNull = false
-> table_service::modifyColumn(t, c, definition)
```

#### 2.5 `ALTER TABLE t RENAME COLUMN old_name TO new_name`

```text
SQL
-> parser payload alterAction = RENAME_COLUMN
   payload.columnName = old_name
   payload.newColumnName = new_name
-> dispatcher reads old column
-> copy old definition
-> definition.column.name = new_name
-> table_service::modifyColumn(t, old_name, definition)
```

#### 2.6 可选：`ALTER TABLE t ALTER COLUMN c TYPE VARCHAR(20)`

```text
SQL
-> parser payload alterAction = ALTER_COLUMN_SET_TYPE
   payload.columnName = c
   payload.type = VARCHAR
   payload.length = 20
-> dispatcher reads old column
-> copy old definition
-> definition.column.type / length = new type info
-> table_service::modifyColumn(t, c, definition)
```

本项建议作为同一 patch 的可选能力。如果时间紧，优先实现 default / not null / rename。

### 3. 函数实现的输入输出级收口与关键描述

#### 3.1 `sqlparser::parseSql(sqlText)`

输入：

1. `ALTER TABLE t ALTER COLUMN c SET DEFAULT 18`
2. `ALTER TABLE t ALTER COLUMN c DROP DEFAULT`
3. `ALTER TABLE t ALTER COLUMN c SET NOT NULL`
4. `ALTER TABLE t ALTER COLUMN c DROP NOT NULL`
5. `ALTER TABLE t RENAME COLUMN old_name TO new_name`
6. 可选：`ALTER TABLE t ALTER COLUMN c TYPE VARCHAR(20)`

输出：

```cpp
ParseResult {
    success = true,
    commandType = "ALTER_TABLE",
    payload = {
        "tableName": "...",
        "alterAction": "...",
        "columnName": "...",
        ...
    }
}
```

关键逻辑：

1. `ALTER COLUMN` 只解析局部变更，不调用 `parseColumnSegment()`。
2. `SET DEFAULT` 必须输出 `defaultValue`，允许字符串、数字和普通 identifier literal。
3. `DROP DEFAULT` 不输出空默认值来表达“未提供”，而是用独立 `alterAction` 表达清空。
4. `SET NOT NULL / DROP NOT NULL` 使用独立 `alterAction`，避免把 bool 默认值误解成用户意图。
5. `RENAME COLUMN` 必须同时输出旧列名和新列名。
6. 语法不完整时返回 parser error，不进入 dispatcher。

#### 3.2 `SqlDispatcher::execAlterTable(parseResult)`

输入：

```cpp
ParseResult {
    commandType = "ALTER_TABLE",
    payload["alterAction"] = one of partial alter actions
}
```

输出：

```cpp
SqlExecResult {
    success = true/false,
    errorMessage = "...",
    text = "Column altered" / "Column renamed"
}
```

关键逻辑：

1. 校验 `tableName`、`columnName`、`newColumnName/defaultValue/type` 等必要字段。
2. 读取当前表 schema，定位旧列；列不存在时返回明确错误。
3. 把旧列转换成完整 `ColumnDefinition`。
4. 保留旧列关联的列级生成约束意图：
   - 旧列被主键约束覆盖时，`definition.primaryKey = true`。
   - 旧列被单列 unique 约束覆盖时，`definition.unique = true`。
   - 旧列有 check 字段或列级 check 约束时，保留 check。
   - 外键列级生成约束如无法可靠还原，首轮不主动从表级 FK 反推为列级定义。
5. 只改当前 alterAction 指定的字段。
6. 调用 `table_service::modifyColumn(tableName, oldColumnName, definition)`。
7. 不直接写 repo，不绕过 service 校验与回滚。

#### 3.3 `buildColumnDefinitionFromExistingSchema(schema, columnName)`

建议新增为 `sql_dispatcher.cpp` 内部匿名命名空间 helper。

输入：

```cpp
const tabledef::TableSchema &schema
QString columnName
```

输出：

```cpp
std::optional<ColumnDefinition>
// 或 bool + out param，按项目现有风格选择
```

关键逻辑：

1. 查找 `schema.columns` 中的目标列。
2. 把 `tabledef::Column` 复制到 `ColumnDefinition::column`。
3. 扫描 `schema.constraints`，尽量还原单列生成约束：
   - `PrimaryKey` 且只包含该列：`primaryKey = true`
   - `Unique` 且只包含该列：`unique = true`
   - `Check` 如与列级 check 可明确对应，保留到 `checkClause`
4. 多列约束保持表级约束，不塞回 `ColumnDefinition`。

#### 3.4 `table_service::modifyColumn(tableName, oldColumnName, definition)`

输入：

```cpp
tableName: 当前表名
oldColumnName: 被修改列旧名
definition: dispatcher 补齐后的完整新定义
```

输出：

```cpp
TaskResult {
    success,
    errorMessage,
    affectedRowCount
}
```

关键逻辑：

1. 复用现有实现。
2. 类型、默认值、not null、重命名对存量数据的影响继续由 service 校验。
3. 索引、约束、表数据回滚继续由 service 负责。
4. 如果现有 service 行为暴露出“无法保留某类表级约束”的问题，应优先在 dispatcher helper 限制输入并返回明确错误，而不是绕过 service。

### 4. 测试用例

#### 4.1 parser 测试

新增或扩展 [tests/test_parser_dispatcher.cpp](../../tests/test_parser_dispatcher.cpp)：

1. `test_parseAlterColumnSetDefault`
   - 输入：`ALTER TABLE student ALTER COLUMN age SET DEFAULT 18`
   - 断言：`alterAction == ALTER_COLUMN_SET_DEFAULT`，`columnName == age`，`defaultValue == 18`
2. `test_parseAlterColumnDropDefault`
   - 输入：`ALTER TABLE student ALTER COLUMN age DROP DEFAULT`
   - 断言：`alterAction == ALTER_COLUMN_DROP_DEFAULT`
3. `test_parseAlterColumnSetNotNull`
   - 输入：`ALTER TABLE student ALTER COLUMN name SET NOT NULL`
   - 断言：`alterAction == ALTER_COLUMN_SET_NOT_NULL`
4. `test_parseAlterColumnDropNotNull`
   - 输入：`ALTER TABLE student ALTER COLUMN name DROP NOT NULL`
   - 断言：`alterAction == ALTER_COLUMN_DROP_NOT_NULL`
5. `test_parseRenameColumn`
   - 输入：`ALTER TABLE student RENAME COLUMN old_name TO new_name`
   - 断言：`columnName == old_name`，`newColumnName == new_name`
6. 可选：`test_parseAlterColumnSetType`
   - 输入：`ALTER TABLE student ALTER COLUMN name TYPE VARCHAR(64)`
   - 断言：`type == VARCHAR`，`length == 64`

#### 4.2 dispatcher 集成测试

1. `test_dispatchAlterColumnSetDefaultPreservesOtherAttributes`
   - 建表：`name VARCHAR(20) NOT NULL`
   - 执行：`ALTER TABLE t ALTER COLUMN name SET DEFAULT 'anonymous'`
   - 断言：`type/length/notNull` 保持不变，`defaultValue` 更新。
2. `test_dispatchAlterColumnDropDefaultPreservesNotNull`
   - 建表：`age INT NOT NULL DEFAULT 18`
   - 执行：`DROP DEFAULT`
   - 断言：`defaultValue` 为空，`notNull == true`。
3. `test_dispatchAlterColumnSetNotNullRejectsExistingEmptyValues`
   - 表中已有空值。
   - 执行：`SET NOT NULL`
   - 断言：失败且错误来自 service 约束校验。
4. `test_dispatchAlterColumnDropNotNullPreservesDefault`
   - 建表：`age INT NOT NULL DEFAULT 18`
   - 执行：`DROP NOT NULL`
   - 断言：`notNull == false`，`defaultValue == 18`。
5. `test_dispatchRenameColumnPreservesDataAndIndexes`
   - 建表并创建索引。
   - 执行：`RENAME COLUMN old_name TO new_name`
   - 断言：数据可查询，新列名可见，旧列名不可用，索引元数据更新。
6. `test_dispatchRenameColumnRejectsDuplicateColumn`
   - 新列名已存在。
   - 断言：失败且 schema 不变。
7. 可选：`test_dispatchAlterColumnSetTypeValidatesExistingRows`
   - 存量数据无法转换时失败，schema 不变。

#### 4.3 CLI / GUI 回归测试

不需要新增专门 CLI/GUI 行为测试。原因：

1. 新语法走同一条 parser/dispatcher/service 执行路径。
2. CLI 只负责传入 SQL 与展示结果。
3. GUI 后续如果使用局部 ALTER，也应通过 `SqlClientEngine` 执行 SQL。

保留全量 `--run-tests` 回归即可：

```powershell
$env:QT_QPA_PLATFORM='offscreen'
$env:PATH='E:\Qt\6.9.2\msvc2022_64\bin;' + $env:PATH
.\build\Desktop_Qt_6_9_2_MSVC2022_64bit-Debug\DBMS.exe --run-tests
```

### 5. 验收边界

1. 现有 `MODIFY COLUMN` 测试继续通过，语义不变化。
2. 局部 ALTER 不依赖 parser 默认值猜测用户意图。
3. 所有局部 ALTER 最终都复用 `table_service::modifyColumn(...)`。
4. 失败时 schema、数据、约束、索引不应出现半更新。
5. 前端以后可以选择：
   - 图形化完整编辑器继续提交完整 `MODIFY COLUMN`。
   - 快捷操作使用局部 `ALTER COLUMN`。
