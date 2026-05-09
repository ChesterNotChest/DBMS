# service 层单元测试设计

## 通用规则

- 测试数据统一放在系统临时目录下，避免污染真实数据。
- 每个测试用例开始前清理旧目录，结束后再清理一次，保证测试互不影响。
- 数据库名、表名、约束名都采用 `test_{服务名}_{场景名}` 的命名方式，便于定位。
- 依赖已有结构或已有数据的用例，先通过 service 层完成建库、建表和种子数据准备，再执行被测方法。
- 测试函数命名统一为 `test_{被测方法名}`。

## [test_database_service.cpp](test_database_service.cpp)

- `test_createDatabase`：覆盖空白库名拒绝、创建两个数据库、重复创建失败，以及 `showDatabases()` 返回已有数据库列表。
- `test_dropDatabase`：覆盖删除不存在数据库失败、删除当前数据库后清空 `currentDatabase`，以及再次列出数据库时只剩未删除项。
- `test_useDatabase`：覆盖切换到不存在数据库失败、带前后空格的数据库名可正常切换，以及连续切换多个数据库后 `currentDatabase` 正确更新。
- `test_showDatabases`：覆盖空数据根目录和创建两个数据库后的列表返回顺序。

## [test_table_service.cpp](test_table_service.cpp)

### 基础建表与删表

- `test_createTable`：覆盖空白数据库名、空白表名、正常创建、重复创建失败、`showTables()` 只返回当前表，以及引用不存在父表的外键结构失败。
- `test_createTableWithSelfReferenceForeignKey`：覆盖自引用外键建表与其约束信息的正确落盘。
- `test_dropTable`：覆盖删除不存在表失败、删除已存在表成功、`showTables()` 结果清空，以及父子表引用关系下的删除约束。

### 列操作

- `test_addColumn`：覆盖新增列后表结构和已有数据同步变化，以及重复添加同名列失败。
- `test_addColumnRejectsGeneratedConstraintViolation`：覆盖新增带约束的列时，存量数据会违反新约束的失败路径。
- `test_addColumnCreatesBoundIndex`：覆盖新增列后绑定索引的生成和可见性。
- `test_deleteColumn`：覆盖删除普通列后数据列数和 describe 结果同步变化。
- `test_deleteColumnRejectsForeignKeyColumns`：覆盖删除被外键保护的列会失败。
- `test_modifyColumn`：覆盖修改列类型、长度、默认值、非空属性，以及查询回读后的字段定义变化。
- `test_modifyColumnRejectsEmptyDefinitionName`：覆盖修改列时定义名为空的拒绝路径。
- `test_modifyColumnRenamesIndexedColumn`：覆盖被索引列改名后索引元数据同步重建或迁移。
- `test_modifyColumnRejectsTypeConversionFailure`：覆盖修改列类型时现有数据无法转换的失败路径。
- `test_modifyColumnCreatesBoundIndex`：覆盖修改列后新的绑定索引仍然可用。

### 约束操作

- `test_addConstraint`：覆盖新增普通约束、`showCreateTable()` 可见约束信息，以及同名约束重复添加失败。
- `test_addForeignKeyConstraintWithActions`：覆盖带 `ON DELETE / ON UPDATE` 动作的外键约束新增。
- `test_addForeignKeyConstraintRejectsInvalidActionColumns`：覆盖外键动作列定义不合法时的拒绝路径。
- `test_addConstraintRejectsDuplicateConstraintName`：覆盖约束名与现有主键或其他约束冲突时的失败路径。
- `test_addConstraintRejectsExistingDataViolations`：覆盖新增唯一约束时，存量重复数据导致失败的路径。
- `test_addConstraintRejectsBrokenForeignKey`：覆盖引用不存在父表或父列时新增外键失败。
- `test_modifyConstraint`：覆盖约束改名或改定义后的回读变化，以及修改不存在约束失败。
- `test_modifyForeignKeyConstraintWithActions`：覆盖外键约束修改后动作配置正确保留。
- `test_modifyConstraintUpdatesExistingBoundIndexMetadata`：覆盖约束修改时绑定索引元数据同步更新。
- `test_modifyConstraintSurfacesBoundIndexDeletionFailure`：覆盖绑定索引删除失败会向上冒泡。
- `test_modifyConstraintRejectsBrokenForeignKey`：覆盖修改成坏外键时的拒绝路径。
- `test_deleteConstraint`：覆盖删除普通约束后 `showCreateTable()` 不再显示该约束。
- `test_deleteForeignKeyConstraintRemovesProtection`：覆盖删除外键约束后，原来受保护的数据操作重新放行。
- `test_deleteConstraintSurfacesBoundIndexDeletionFailure`：覆盖删除约束时绑定索引删除失败的反馈路径。

### 查询与展示

- `test_showTables`：覆盖当前数据库下表列表的读取。
- `test_describeTable`：覆盖表结构描述的文本输出。
- `test_describeTableShowsForeignKeyActions`：覆盖 describe 输出中外键动作信息的展示。
- `test_showCreateTable`：覆盖 `SHOW CREATE TABLE` 文本生成和回读。

### parser / dispatcher 贯通

- `test_parseCreateTableWithCompositeConstraints`：覆盖创建表 SQL 中复合约束的解析结果。
- `test_dispatcherCreateTablePreservesSchemaConstraints`：覆盖 dispatcher 把 parser payload 转成 schema 时，约束信息没有丢失。
- `test_dispatcherSelectLimitAndRejectsWhere`：覆盖 `SELECT LIMIT` 的贯通和 `WHERE` 的拒绝路径。

### 索引与绑定索引

- `test_createIndexAndDropIndex`：覆盖普通索引的创建、查询和删除闭环。
- `test_createIndexCleansUpOnTreeFailure`：覆盖索引树构建失败时的清理回滚。
- `test_sortIndexLeafNextChain`：覆盖 sort index 的叶子链表 next 指针关系。
- `test_primaryKeyBoundIndexIsUnique`：覆盖主键绑定索引必须保持唯一性。
- `test_sortIndexPersistsSourceTable`：覆盖 sort index 元数据中源表名的持久化。
- `test_createUniqueIndexRejectsDuplicateData`：覆盖唯一索引在重复数据下的创建失败。
- `test_createUniqueIndexHandlesSeparatorLikeValues`：覆盖包含分隔符特征值时的复合唯一索引创建和查询。
- `test_createUniqueIndexIgnoresEmptyValues`：覆盖空值在唯一索引构建中的处理。
- `test_boundIndexLifecycle`：覆盖绑定索引从创建、读取、改名到删除的完整生命周期。

## [test_tuple_service.cpp](test_tuple_service.cpp)

- `test_selectRows`：覆盖全列查询、投影查询和带条件查询的结果正确性。
- `test_insertRows`：覆盖普通插入、批量插入和外键校验失败路径。
- `test_insertRowsSelfReferenceBatch`：覆盖自引用表批量插入的约束校验。
- `test_deleteRows`：覆盖普通删除和被外键引用时的删除保护。
- `test_updateRows`：覆盖普通更新、外键相关更新，以及主键被引用时的更新失败。
- `test_deleteRowsCascadeRecursively`：覆盖级联删除的递归传播。
- `test_deleteRowsCascadeMultipleParentRows`：覆盖多个父行同时参与级联删除的情况。
- `test_deleteRowsCascadeDoesNotTouchUnrelatedRowIdSidecar`：覆盖级联删除不会误伤无关 row id sidecar 数据。
- `test_updateRowsCascade`：覆盖级联更新的基本传播。
- `test_updateRowsCascadeRecursively`：覆盖级联更新的递归传播。
- `test_updateRowsGraphPlanMixedBranches`：覆盖图状依赖下混合分支更新计划的执行。
- `test_updateRowsSelfReferenceCascadeTerminates`：覆盖自引用级联更新不会无限循环。
- `test_deleteRowsSelfReferenceCascadeTerminates`：覆盖自引用级联删除不会无限循环。
- `test_deleteRowsSelfReferenceNoActionAllowsDeletingAllRows`：覆盖自引用 No Action 场景下允许清空删除链。
- `test_deleteRowsCascadeBlockedByRecursiveNoActionRollsBack`：覆盖递归 No Action 阻断级联删除并回滚。
- `test_deleteRowsRestrict`：覆盖 RESTRICT 删除保护。
- `test_updateRowsRestrict`：覆盖 RESTRICT 更新保护。
- `test_deleteRowsSetNull`：覆盖删除时把外键列置空。
- `test_updateRowsSetNull`：覆盖更新时把外键列置空。
- `test_updateRowsSetDefault`：覆盖更新时把外键列恢复为默认值。
- `test_deleteRowsSetDefault`：覆盖删除时把外键列恢复为默认值。
- `test_deleteRowsSetDefaultRejectsMissingDefaultParentAndRollsBack`：覆盖删除时缺少默认父键导致失败并回滚。
- `test_updateRowsSetDefaultRejectsMissingDefaultParentAndRollsBack`：覆盖更新时缺少默认父键导致失败并回滚。
- `test_deleteRowsCascadeDiamondTopology`：覆盖菱形拓扑下的级联删除。
- `test_uniqueConstraintRejectsDuplicateDml`：覆盖 DML 写入违反唯一约束时的拒绝。
- `test_uniqueConstraintStillRejectsDuplicatesWithoutIndexMetadata`：覆盖缺少索引元数据时唯一约束仍然生效。
- `test_incrementalIndexMaintenance`：覆盖增量写入下索引维护的一致性。

## Client runtime / CLI / GUI tests

### [test_client_session.cpp](test_client_session.cpp)

- `test_createSessionHasIndependentCurrentDatabase`: verifies two client sessions can switch to different databases without overwriting each other.
- `test_executeSqlRestoresPreviousServiceContext`: verifies `SqlClientEngine` restores legacy service globals after each execution.
- `test_closeSessionRemovesClient`: verifies closed sessions are rejected with an explicit error.

### [test_cli_client.cpp](test_cli_client.cpp)

- `test_executeOneShotSql`: covers `DBMS_CLI --execute` one-shot execution.
- `test_multilineSqlBufferExecutesOnlyAfterSemicolon`: covers REPL SQL buffering and continuation prompt behavior.
- `test_quitCommandExitsWithoutExecutingSql`: covers quit handling without dispatching SQL.
- `test_startupPromptsForCredentials`: covers startup `Username:` / `Password:` prompting.
- `test_passwordValueDoesNotPrompt`: covers `-u user -p value` login without password prompt.
- `test_displayStatementsUseCliTableFormatting`: covers boxed output for `SHOW DATABASES`, `SHOW TABLES`, `SELECT`, `DESC`, and `SHOW CREATE TABLE`.

### [test_auth_client.cpp](test_auth_client.cpp)

- `test_parseAuthSql`: covers parser output for `LOGIN` and `GRANT ALL`.
- `test_unauthenticatedSqlIsRejected`: verifies unauthenticated sessions cannot execute normal SQL.
- `test_rootCanCreateUserAndGrantDatabasePrivilege`: covers root user management and granted database access.
- `test_revokeRemovesDatabasePrivilege`: verifies revoked users can no longer `USE` the database.
- `test_dropUserRemovesLogin`: verifies dropped users cannot authenticate.
- `test_nonRootCannotCreateUsers`: verifies non-root users cannot execute user-management DDL.

### [test_gui_client_runtime.cpp](test_gui_client_runtime.cpp)

- `test_mainWindowCreatesGuiClient`: verifies GUI startup creates an authenticated root client session.
- `test_guiAutoRootCanRunDdl`: verifies GUI auto-root can execute DDL through `SqlClientEngine`.
- `test_guiExecuteSqlUsesOwnSession`: verifies GUI and CLI-style sessions keep independent `currentDatabase`.
- `test_structureRefreshUsesGuiClientContext`: verifies structure refresh queries use GUI client context.
- `test_guiExecuteSqlRestoresPreviousServiceContext`: verifies GUI execution does not leak service globals.
- `test_guiExecuteSqlReportsPermissionFailure`: verifies permission failures still surface through the shared client runtime.

Run the full in-process regression suite with:

```powershell
$env:QT_QPA_PLATFORM='offscreen'
$env:PATH='E:\Qt\6.9.2\msvc2022_64\bin;' + $env:PATH
.\build\Desktop_Qt_6_9_2_MSVC2022_64bit-Debug\DBMS.exe --run-tests
```
