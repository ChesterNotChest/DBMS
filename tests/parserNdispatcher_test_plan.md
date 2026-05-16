# Parser / Dispatcher 测试计划

## 目标

本文件记录当前已经落地的 parser / dispatcher 回归范围，直接对应 [test_parser_dispatcher.cpp](E:/Qt-projects/DBMS/tests/test_parser_dispatcher.cpp)。

当前覆盖目标分成两层：

- parser 是否按协议输出正确的 `commandType + payload`
- dispatcher 是否把 payload 正确下推到现有 service，并拒绝不完整输入

## 当前覆盖

### parser 成功路径

- `test_parseCreateTableUsesQtBasePayload`
  验证 `CREATE TABLE` 输出的 `columns`、`constraints` 都是 Qt 基础类型，并保留列级 / 表级 FK 动作。
- `test_parseSelectLimitAndSimpleWhere`
  验证 `SELECT ... WHERE ... AND ... LIMIT ...` 能正确输出 `projection / tableName / limit / conditions`。
- `test_parseUpdateAndDeleteSupportSimpleWhere`
  验证 `UPDATE`、`DELETE` 的简单 `WHERE` 会转成 `conditions`。
- `test_parseInsertWithoutColumnListProducesSingleRowPayload`
  验证无列名 `INSERT` 仍会产出单行 `rows` payload。
- `test_parseAlterAndIndexProduceCompletePayload`
  验证 `ALTER TABLE ADD/MODIFY COLUMN`、`ADD/MODIFY CONSTRAINT`、`CREATE/DROP INDEX` 会输出完整 payload。
- `test_parseAlterForeignKeyAndMultiColumnIndexPayload`
  验证 `ALTER TABLE ... ADD CONSTRAINT FOREIGN KEY ...` 和多列索引的 payload 收口。

### parser 失败路径

- `test_parseSelectLimitAndSimpleWhere`
  验证 `SELECT ... ORDER BY ...` 能输出排序列和升降序 payload。
- `test_parseUpdateAndDeleteSupportSimpleWhere`
  验证 `>` 这类非等值谓词被拒绝。
- `test_parseWhereRejectsUnsupportedForms`
  验证 `OR`、`AND` 结尾、缺失字面量等非法 `WHERE` 形式被拒绝。

### dispatcher 集成路径

- `test_dispatcherInsertWithoutColumnListUsesSchemaOrder`
  验证 dispatcher 会用 schema 列序补齐无列名 `INSERT`。
- `test_dispatcherShowCreateTableReturnsText`
  验证 `SHOW CREATE TABLE` 贯通到 service。
- `test_dispatcherAlterSqlPathsCallService`
  验证 `ALTER TABLE ADD/MODIFY COLUMN/CONSTRAINT` 的 SQL 会真正改动 schema。
- `test_dispatcherWhereAndLimitFlowToService`
  验证 `SELECT / UPDATE / DELETE` 的简单 `WHERE` 和 `LIMIT` 会真正下推到 `tuple_service`。
- `test_dispatcherIndexSqlUsesService`
  验证 `CREATE INDEX / DROP INDEX` 会真正下推到 `table_service`。
- `test_dispatcherUniqueAndMultiColumnIndexSqlUseService`
  验证 `CREATE UNIQUE INDEX` 和多列索引的元数据下推正确。

### dispatcher 拒绝路径

- `test_dispatcherAlterRejectsIncompletePayload`
  验证 `ALTER` 在 payload 不完整时不会伪调用 service。
- `test_dispatcherRejectsMalformedConditionsPayload`
  验证不完整的 `conditions` payload 会被 dispatcher 拒绝。

## 当前结论

当前 parser / dispatcher 测试已经覆盖：

- `CREATE TABLE`
- `SELECT ... WHERE ... AND ... LIMIT ...`
- `UPDATE ... WHERE ...`
- `DELETE ... WHERE ...`
- `INSERT`
- `ALTER TABLE ADD/MODIFY COLUMN`
- `ALTER TABLE ADD/MODIFY CONSTRAINT`
- `CREATE INDEX / DROP INDEX`
- 关键的失败边界与 payload 不完整拒绝策略

仍未追求的是“所有语法变体穷举”，而不是主协议缺失。
