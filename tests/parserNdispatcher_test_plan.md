# Parser / Dispatcher 测试计划

## 目标

本文件按当前实际存在的 `tests/test_parser_dispatcher.cpp` 整理，记录已经落地的回归点，不再保留未实现的扩展计划。

当前覆盖重点是：

- parser 是否只输出当前协议允许的 `commandType + payload`
- dispatcher 是否能把 payload 正确落到现有 service
- 当前明确拒绝的 SQL 语义是否稳定失败

## 当前覆盖

### parser

- `test_parseCreateTableUsesQtBasePayload`：验证 `CREATE TABLE` 会输出 Qt 基础类型 payload，`columns` 和 `constraints` 都是 `QVariantList`，列级外键动作和表级约束信息都能保留。
- `test_parseSelectLimitAndRejectUnsupportedClauses`：验证 `SELECT ... LIMIT n` 可以正确解析 `tableName`、`projection` 和 `limit`，同时 `ORDER BY` 会被拒绝。
- `test_parseUpdateAndDeleteRejectWhere`：验证 `UPDATE` 和 `DELETE` 只要带 `WHERE` 就会直接失败，并返回明确的拒绝信息。
- `test_parseInsertWithoutColumnListProducesSingleRowPayload`：验证无列名列表的 `INSERT` 会解析成单行 `rows` payload，并正确保留字面值。

### dispatcher

- `test_dispatcherInsertWithoutColumnListUsesSchemaOrder`：验证 dispatcher 在 `INSERT INTO ... VALUES (...)` 没有显式列名时，会按表 schema 的真实列序写入。
- `test_dispatcherShowCreateTableReturnsText`：验证 `SHOW CREATE TABLE` 会贯通到 service，并返回以 `CREATE TABLE` 开头的文本结果。
- `test_dispatcherAlterRejectsIncompletePayload`：验证 `ALTER TABLE ADD / MODIFY COLUMN / CONSTRAINT` 在 payload 不完整时会被拒绝，错误信息会指向缺失的 column 或 constraint payload。
- `test_dispatcherIndexSqlCurrentlyUnsupported`：验证 `CREATE INDEX` 和 `DROP INDEX` 目前仍然属于 parser 层的未支持语句，解析结果会直接失败。

## 当前结论

现有测试已经覆盖了 CREATE TABLE、SELECT LIMIT、INSERT、SHOW CREATE TABLE 以及 ALTER TABLE 的拒绝路径；索引 SQL 仍然停留在未支持状态。后续如果补齐新的 SQL 通路，再把对应测试加到这里，而不是保留在计划草稿里。
