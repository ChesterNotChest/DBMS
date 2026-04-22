# Parser / Dispatcher 测试计划

## 目标

本计划用于为 `WTY_FRONTEND_PARSER_ALIGNMENT_PLAN.md` 提供可执行回归点，重点验证：

- parser 是否按当前协议输出 `commandType + payload`
- dispatcher 是否按当前协议把 payload 落到现有 service
- 当前明确拒绝的边界是否被稳定拒绝

本计划分为两层：

1. 当前已实现协议的回归测试
2. 面向 `SERVICE FULL COVERAGE GAP` 的后续扩展测试

## 当前已实现协议测试

### parser

1. `CREATE TABLE` 输出基础 Qt payload
- 输入：包含列级定义、表级 UNIQUE / FOREIGN KEY 的 SQL
- 断言：
  - `commandType == "CREATE_TABLE"`
  - `payload["columns"]` 为 `QVariantList`
  - `payload["constraints"]` 为 `QVariantList`
  - 列项与约束项均为 `QVariantMap`
  - 外键动作值正确保留

2. `SELECT ... LIMIT n` 输出 limit
- 输入：`SELECT id FROM t LIMIT 3`
- 断言：
  - `commandType == "SELECT"`
  - `payload["limit"] == 3`
  - `payload["projection"]`、`payload["tableName"]` 正确

3. 当前不支持的 `WHERE`
- 输入：
  - `UPDATE t SET name = 'x' WHERE id = 1`
  - `DELETE FROM t WHERE id = 1`
- 断言：
  - parser 直接失败
  - 错误信息包含 `WHERE is not supported`

4. 当前不支持的额外 `SELECT` 子句
- 输入：`SELECT * FROM t ORDER BY id`
- 断言：
  - parser 直接失败
  - 错误信息包含 `unsupported clause`

### dispatcher

1. `INSERT INTO table VALUES (...)` 自动按 schema 列序写入
- 前置：建库、建表
- 输入：`INSERT INTO table VALUES (1, 'alice')`
- 断言：
  - dispatcher 返回成功
  - service 中真实写入的行值与 schema 列序一致

2. `SHOW CREATE TABLE` 贯通 parser -> dispatcher -> service
- 前置：建库、建表
- 输入：`SHOW CREATE TABLE table_name`
- 断言：
  - dispatcher 返回成功
  - 返回 `text` 以 `CREATE TABLE` 开头
  - 文本包含目标表名

3. `ALTER TABLE ADD/MODIFY COLUMN/CONSTRAINT` 当前拒绝不完整 payload
- 前置：建库、建表
- 输入：
  - `ALTER TABLE t ADD COLUMN age INT`
  - `ALTER TABLE t MODIFY COLUMN age INT`
  - `ALTER TABLE t ADD CONSTRAINT uq_t UNIQUE (name)`
  - `ALTER TABLE t MODIFY CONSTRAINT uq_t CONSTRAINT uq_t UNIQUE (name)`
- 断言：
  - dispatcher 返回失败
  - 错误信息包含 `complete column payload` 或 `complete constraint payload`

4. 当前未接入的索引 SQL
- 输入：
  - `CREATE INDEX idx_t_name ON t(name)`
  - `DROP INDEX idx_t_name ON t`
- 断言：
  - 当前 parser/dispatcher 返回 unsupported

## 面向 full coverage 的后续测试

这些测试在 SQL 通路真正补齐后再启用。

1. `ALTER TABLE ADD COLUMN` 完整协议测试
- parser 输出完整 `payload["column"]`
- dispatcher 成功调用 `table_service::addColumn(...)`

2. `ALTER TABLE MODIFY COLUMN` 完整协议测试
- parser 输出完整 `payload["column"]`
- dispatcher 成功调用 `table_service::modifyColumn(...)`

3. `ALTER TABLE ADD CONSTRAINT` 完整协议测试
- parser 输出完整 `payload["constraint"]`
- dispatcher 成功调用 `table_service::addConstraint(...)`

4. `ALTER TABLE MODIFY CONSTRAINT` 完整协议测试
- parser 输出 `constraintName + constraint`
- dispatcher 成功调用 `table_service::modifyConstraint(...)`

5. `CREATE INDEX / DROP INDEX` 完整协议测试
- parser 输出 `indexName / tableName / columnNames / isUnique`
- dispatcher 成功调用 `table_service::createIndex(...) / dropIndex(...)`

6. `WHERE` 简单等值协议测试
- 支持：`col = literal [AND ...]`
- 不支持：`OR`、比较运算、括号、`LIKE/IN/BETWEEN/IS NULL`
- dispatcher 将条件正确映射到 `QList<SimpleCondition>`

## 注册方式

- 新增独立测试文件：`tests/test_parser_dispatcher.cpp`
- 在 `tests/test_entry.h` 中登记：
  - `int runParserDispatcherTests();`
- 在 `main.cpp` 中和其他测试组一起直接执行，不加开关
