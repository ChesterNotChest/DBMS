# service 层单元测试设计

## 通用规则

- 测试数据统一放在系统临时目录下，避免污染真实数据。
- 每个测试用例开始前清理旧目录，结束后再清理一次，保证测试互不影响。
- 数据库名、表名、约束名都采用 `test_{服务名}_{场景名}` 的命名方式，便于定位。
- 依赖已有结构或已有数据的用例，先通过 service 层完成建库、建表和种子数据准备，再执行被测方法。
- 测试函数命名统一为 `test_{被测方法名}`。

## [test_database_service.cpp](test_database_service.cpp)

### `test_createDatabase`
用例 1：传入空白数据库名，期望创建失败，错误信息提示数据库名不能为空。

用例 2：创建 `test_database_service_create_alpha`，期望成功。

用例 3：创建 `test_database_service_create_beta`，期望成功。

用例 4：重复创建 `test_database_service_create_alpha`，期望失败，错误信息提示已存在。

用例 5：调用 `showDatabases()`，期望返回 `alpha` 和 `beta` 两条记录。

### `test_dropDatabase`
用例 1：先创建 `test_database_service_drop_alpha` 和 `test_database_service_drop_beta`，再切换当前数据库到 `alpha`。

用例 2：删除不存在的 `test_database_service_drop_missing`，期望失败。

用例 3：删除当前数据库 `alpha`，期望成功，并清空 `currentDatabase`。

用例 4：再次调用 `showDatabases()`，期望只剩 `beta`。

用例 5：再次删除已经不存在的 `alpha`，期望失败。

### `test_useDatabase`
用例 1：先创建 `test_database_service_use_alpha` 和 `test_database_service_use_beta`。

用例 2：切换到不存在的 `test_database_service_use_missing`，期望失败。

用例 3：切换到带前后空格的 `test_database_service_use_alpha`，期望成功，并把 `currentDatabase` 更新为 `alpha`。

用例 4：再切换到 `beta`，期望成功，并把 `currentDatabase` 更新为 `beta`。

### `test_showDatabases`
用例 1：空根目录下调用 `showDatabases()`，期望返回 0 条记录。

用例 2：先创建 `test_database_service_show_alpha` 和 `test_database_service_show_beta`。

用例 3：再次调用 `showDatabases()`，期望结果包含这两个数据库名，顺序与创建顺序一致。

## [test_table_service.cpp](test_table_service.cpp)

### `test_createTable`
用例 1：数据库名为空白时创建表，期望失败。

用例 2：表名为空白时创建表，期望失败。

用例 3：在 `test_table_service_create_db` 中创建 `test_table_service_create_table_main`，期望成功。

用例 4：重复创建同名表，期望失败。

用例 5：调用 `showTables()`，期望只返回这一张表。

用例 6：创建一个引用不存在父表的 FOREIGN KEY 约束表结构，期望失败。

### `test_dropTable`
用例 1：在 `test_table_service_drop_db` 中创建 `test_table_service_drop_table_main`。

用例 2：删除不存在的 `test_table_service_drop_missing`，期望失败。

用例 3：删除已存在的 `test_table_service_drop_table_main`，期望成功。

用例 4：再次调用 `showTables()`，期望结果为空。

用例 5：创建父表和引用它的子表后，删除父表应失败；删除子表后再删父表应成功。

### `test_addColumn`
用例 1：在 `test_table_service_add_column_db/test_table_service_add_column_table` 中先插入一行 `{1, alice}`。

用例 2：新增 `age INT DEFAULT 18`，期望成功。

用例 3：再次查看表结构，期望字段列表变成 `id, name, age`。

用例 4：再次查询表数据，期望原有行自动补入默认值 `18`。

用例 5：重复添加 `age` 列，期望失败。

### `test_addColumnRejectsGeneratedConstraintViolation`
用例 1：在目标表中先放入两行不同主键但相同业务列数据。

用例 2：新增一个带 `UNIQUE` 的列且默认值相同，期望失败，因为存量数据会违反新约束。

### `test_deleteColumn`
用例 1：在 `test_table_service_delete_column_db/test_table_service_delete_column_table` 中先插入一行 `{1, alice, 22}`。

用例 2：删除 `age` 列，期望成功。

用例 3：再次查询表数据，期望只剩 `id, name` 两列，且行值变为 `{1, alice}`。

用例 4：再次查看描述信息，期望不再包含 `age`。

用例 5：删除不存在的 `missing` 列，期望失败。

### `test_modifyColumn`
用例 1：在 `test_table_service_modify_column_db/test_table_service_modify_column_table` 中创建基础表。

用例 2：把 `name` 列修改成 `VARCHAR(64) NOT NULL DEFAULT guest`，期望成功。

用例 3：再次读取列定义，期望 `name` 的默认值变成 `guest`，并保持 `notNull = true`。

用例 4：修改不存在的 `missing` 列，期望失败。

### `test_modifyColumnRejectsEmptyDefinitionName`
用例 1：在 `test_table_service_modify_column_empty_name_db/test_table_service_modify_column_empty_name_table` 中创建基础表。

用例 2：把 `name` 列修改成一个空列名定义，期望失败，错误信息提示列名不能为空。

### `test_modifyColumnRejectsTypeConversionFailure`
用例 1：在目标表中先插入一条字符串类型数据。

用例 2：把同名列改成 `INT`，期望失败，因为现有数据无法转换。

### `test_addConstraint`
用例 1：在 `test_table_service_add_constraint_db/test_table_service_add_constraint_table` 中创建基础表。

用例 2：添加 `uq_test_table_service_name UNIQUE(name)`，期望成功。

用例 3：查看 `showCreateTable()`，期望文本中包含 `UNIQUE` 和约束名。

用例 4：再次添加同名约束，期望失败。

### `test_addConstraintRejectsDuplicateConstraintName`
用例 1：在 `test_table_service_add_constraint_name_dup_db/test_table_service_add_constraint_name_dup_table` 中创建基础表。

用例 2：添加与现有主键同名的 `UNIQUE(name)` 约束，期望失败，错误信息提示约束名已存在。

### `test_addConstraintRejectsExistingDataViolations`
用例 1：在目标表中先插入两行 `name` 相同的数据。

用例 2：新增 `UNIQUE(name)`，期望失败，因为存量数据已违规。

### `test_addConstraintRejectsBrokenForeignKey`
用例 1：在目标表中准备一个外键列，但不创建父表。

用例 2：添加引用不存在父表的 FOREIGN KEY，期望失败。

用例 3：创建父表后，再添加引用不存在父列的 FOREIGN KEY，期望失败。

### `test_modifyConstraint`
用例 1：先给表添加 `uq_test_table_service_name UNIQUE(name)`。

用例 2：把它改成 `uq_test_table_service_name_mod UNIQUE(name)`，期望成功。

用例 3：再次查看 `showCreateTable()`，期望旧名字消失，新名字出现。

用例 4：修改不存在的 `missing` 约束，期望失败。

### `test_modifyConstraintRejectsBrokenForeignKey`
用例 1：先准备一个可修改的约束。

用例 2：把它改成引用不存在父表的 FOREIGN KEY，期望失败。

### `test_deleteConstraint`
用例 1：先给表添加 `uq_test_table_service_name UNIQUE(name)`。

用例 2：删除该约束，期望成功。

用例 3：查看 `showCreateTable()`，期望不再包含该约束名。

用例 4：删除不存在的 `missing` 约束，期望失败。

### `test_showTables`
用例 1：在同一个数据库中创建 `test_table_service_show_tables_a` 和 `test_table_service_show_tables_b`。

用例 2：调用 `showTables()`，期望返回两张表。

### `test_describeTable`
用例 1：在 `test_table_service_describe_db/test_table_service_describe_table` 上创建基础结构。

用例 2：再加一个 `UNIQUE(name)` 约束。

用例 3：调用 `describeTable()`，期望返回内容同时包含 `id`、`name`、`age` 和 `UNIQUE`。

### `test_showCreateTable`
用例 1：在 `test_table_service_show_create_db/test_table_service_show_create_table` 上创建带 `age` 列和 `UNIQUE(name)` 的表。

用例 2：调用 `showCreateTable()`，期望结果以 `CREATE TABLE` 开头。

用例 3：期望结果中同时包含 `age` 列和约束定义。

## [test_tuple_service.cpp](test_tuple_service.cpp)

### `test_selectRows`
用例 1：在 `test_tuple_service_select_db/test_tuple_service_select_table` 中插入 `{1, alice}` 和 `{2, bob}`。

用例 2：用 `*` 查询，期望返回全部列和两行数据。

用例 3：用投影列 `name` 加条件 `id = 2` 查询，期望只返回一行 `bob`。

### `test_insertRows`
用例 1：创建父表 `test_tuple_service_insert_parent` 和子表 `test_tuple_service_insert_child`。

用例 2：向父表批量插入 `{1, alice}` 和 `{2, bob}`，期望成功。

用例 3：向子表插入 `{10, 1, ok}`，期望成功。

用例 4：向子表插入 `{11, 999, broken}`，期望失败，错误信息提示父键缺失。

### `test_deleteRows`
用例 1：创建父表和子表，并插入父行 `{1, alice}`、`{2, bob}`，再插入子行 `{10, 1, child}`。

用例 2：删除父表中 `id = 1` 的行，期望失败，因为子表仍引用它。

用例 3：先删除子表中 `id = 10` 的行，期望成功。

用例 4：再删除父表中 `id = 1` 的行，期望成功。

### `test_updateRows`
用例 1：创建父表和子表，并插入父行 `{1, alice}`、`{2, bob}`，再插入子行 `{10, 1, child}`。

用例 2：更新父表中 `id = 1` 的 `name` 为 `alice_updated`，期望成功。

用例 3：更新子表中 `id = 10` 的 `parent_id` 为 `2`，期望成功。

用例 4：把父表中 `id = 2` 的主键改成 `3`，期望失败，因为子表已经引用该键。

## 索引与 row id 新增测试

### `test_createIndexAndDropIndex`
用例 1：创建一张基础表，并先插入两行 `name` 不同的数据。

用例 2：调用 `table_service::createIndex()` 创建普通索引，期望成功，并能通过 `IndexRepo` 读到该索引元数据。

用例 3：通过 `SortIndexRepo::search()` 按索引列查找，期望命中对应行定位。

用例 4：调用 `table_service::dropIndex()` 删除普通索引，期望成功，并且 `IndexRepo` 中不再存在该索引。

用例 5：对同名索引重复创建，期望失败；对空白索引名创建，期望失败。

### `test_createUniqueIndexRejectsDuplicateData`
用例 1：创建一张基础表，并插入两行 `name` 相同的数据。

用例 2：调用 `table_service::createIndex()` 创建 `UNIQUE(name)`，期望失败，因为存量数据存在重复键。

### `test_createUniqueIndexHandlesSeparatorLikeValues`
用例 1：创建一张带 `name`、`age` 列的基础表，并插入两行包含旧分隔符特征值的数据。

用例 2：调用 `table_service::createIndex()` 创建 `UNIQUE(name, age)`，期望成功。

用例 3：分别按两组复合键值调用 `SortIndexRepo::search()`，期望都能命中各自对应的行定位。

### `test_boundIndexLifecycle`
用例 1：先给表添加 `UNIQUE(name)` 约束，期望自动生成绑定索引。

用例 2：通过 `IndexRepo::listIndexes()` 读取索引元数据，期望能看到绑定索引名。

用例 3：修改该约束名称，期望绑定索引随之重建或改名，旧索引名不再存在。

用例 4：删除该约束，期望绑定索引和 `table.idx` 记录同步删除。

### `test_incrementalIndexMaintenance`
用例 1：创建一张带 `UNIQUE(name)` 约束的表，插入两行数据。

用例 2：通过 `repo::FlatFileTableStore` 读取 row id 侧车文件，期望行数与表记录数一致。

用例 3：更新被索引列 `name`，期望 `SortIndexRepo::search()` 能查到新键、查不到旧键。

用例 4：删除其中一行，期望 row id 侧车文件同步缩减，且索引搜索结果同步消失。
