# THREAD_AND_PERFORMANCE_PLAN

目标：规划 DBMS 文件存储层之上的并发控制与运行性能优化实现。

本文只规划两类事情：

1. 让现有 `service -> repo` 这条链在单进程多线程场景下具备可控的并发访问边界。
2. 让现有热路径减少重复读盘、重复装载 schema、重复的“预热式整表索引重建”。

本文不覆盖以下内容：

1. 跨进程文件锁。
2. WAL / journal / crash recovery。
3. 页式 buffer pool。
4. SQL 优化器。
5. 复杂事务隔离级别。

---

## 阶段一：进程内表级并发控制

目标：在不引入跨进程锁的前提下，先完成单进程多线程的表级并发控制。

### 0. 常量增量

新增文件：[thread_perf_def.h](E:/Qt-projects/DBMS/constants/thread_perf_def.h)

新增常量：

1. `kTableLockAcquireTimeoutMs`
   - 默认值：`3000`
   - 含义：读锁/写锁最长等待时间。

2. `kDatabaseLockAcquireTimeoutMs`
   - 默认值：`5000`
   - 含义：数据库级 DDL 锁最长等待时间。

3. `kEnableSharedReadLock`
   - 默认值：`true`
   - 含义：`SELECT / SHOW / DESC / SHOW CREATE TABLE` 是否走共享读锁。

### 1. 边界划定

本阶段允许修改：

1. [service.h](E:/Qt-projects/DBMS/service/service.h)
2. [database_service.cpp](E:/Qt-projects/DBMS/service/database_service.cpp)
3. [table_service.cpp](E:/Qt-projects/DBMS/service/table_service.cpp)
4. [tuple_service.cpp](E:/Qt-projects/DBMS/service/tuple_service.cpp)
5. [table_dml_service.cpp](E:/Qt-projects/DBMS/service/table_dml_service.cpp)
6. [constraint_worker.cpp](E:/Qt-projects/DBMS/utils/service_common/constraint_worker.cpp)
7. 新增 `utils/thread_runtime/lock_manager.h/.cpp`
8. 新增测试：
   - `tests/test_lock_manager.cpp`
   - `tests/test_threaded_service.cpp`
   - `tests/test_entry.h`
   - `main.cpp`
   - `CMakeLists.txt`

本阶段不修改：

1. `repo::*` 的公开签名。
2. SQL parser / dispatcher。
3. `QueryExecutor`。

锁粒度正式定义：

1. `CREATE DATABASE / DROP DATABASE`：数据库级排它锁。
2. `CREATE TABLE / DROP TABLE / ALTER TABLE / CREATE INDEX / DROP INDEX`：目标表排它锁；若涉及数据库清单文件，则同时持有数据库级排它锁。
3. `INSERT / UPDATE / DELETE`：目标表排它锁。
4. `SELECT / SHOW TABLES / DESC / SHOW CREATE TABLE`：目标表共享锁；`SHOW DATABASES`：数据库级共享锁。
5. FK 级联：一次性按固定顺序获取所有涉及表的排它锁，禁止边执行边追加锁。

### 2. 精确到每个函数的具体输入输出的数据流收口计划

#### 2.1 新增数据结构

```cpp
enum class RuntimeLockMode {
    Shared,
    Exclusive
};

struct RuntimeLockKey {
    QString dataRoot;
    QString databaseName;
    QString tableName;   // 数据库级锁时为空
};

class ScopedRuntimeLock {
public:
    bool isValid() const;
};

class RuntimeLockManager {
public:
    ScopedRuntimeLock acquireLock(const RuntimeLockKey &key,
                                  RuntimeLockMode mode,
                                  int timeoutMs,
                                  QString *error);

    QList<ScopedRuntimeLock> acquireOrderedLocks(const QList<RuntimeLockKey> &keys,
                                                 RuntimeLockMode mode,
                                                 int timeoutMs,
                                                 QString *error);
};
```

#### 2.2 `RuntimeLockManager::acquireLock`

输入：

1. `key`
2. `mode`
3. `timeoutMs`
4. `error`

输出：

1. `ScopedRuntimeLock`

数据流：

```text
调用方构造 RuntimeLockKey
-> LockManager 将 key 归一化成字符串
-> 查找/创建内部 QReadWriteLock
-> 按 mode 尝试在 timeout 内加锁
-> 成功则返回 RAII 锁对象
-> 失败则返回 invalid 锁对象并写 error
```

#### 2.3 `RuntimeLockManager::acquireOrderedLocks`

输入：

1. `keys`
2. `mode`
3. `timeoutMs`
4. `error`

输出：

1. `QList<ScopedRuntimeLock>`

数据流：

```text
调用方提交所有目标表
-> LockManager 去重
-> 按 dataRoot / databaseName / tableName 字典序排序
-> 逐个 acquireLock
-> 任意一步失败则回滚已获得的锁
-> 成功时一次性返回全部锁
```

#### 2.4 `database_service::*`

##### `createDatabase(const QString &databaseName)`

输入：

1. `databaseName`

输出：

1. `TaskResult`

数据流：

```text
normalizeDatabaseName
-> acquire database exclusive lock
-> DatabaseRepo / TabRepo / root.dbf 写入
-> release lock
```

##### `dropDatabase(const QString &databaseName)`

数据流：

```text
normalizeDatabaseName
-> acquire database exclusive lock
-> 删除数据库目录、tab、root 记录
-> release lock
```

##### `showDatabases()`

数据流：

```text
acquire database shared lock
-> DatabaseRepo::listDatabases()
-> release lock
```

#### 2.5 `table_service::*`

##### `createTable / dropTable / addColumn / deleteColumn / modifyColumn / addConstraint / modifyConstraint / deleteConstraint / createIndex / dropIndex`

统一输入：

1. `tableName`
2. 其他 DDL 参数

统一输出：

1. `TaskResult`

统一数据流：

```text
normalizeDatabaseName
-> 构造 database lock key + table lock key
-> acquireOrderedLocks([databaseKey, tableKey], Exclusive)
-> 读取 schema / constraint / index / table 数据
-> 执行校验
-> repo 写入
-> release locks
```

#### 2.6 `tuple_service::*`

##### `selectRows(const QString &tableName, const QStringList &projectionColumns, const QList<SimpleCondition> &conditions, int limit)`

输入：

1. `tableName`
2. `projectionColumns`
3. `conditions`
4. `limit`

输出：

1. `SelectRowsResult`

数据流：

```text
normalizeDatabaseName
-> acquire table shared lock
-> loadUserTableSchema
-> TableDmlService::selectRows(...)
-> release lock
```

##### `insertRows / updateRows / deleteRows`

统一数据流：

```text
normalizeDatabaseName
-> 先计算本次操作涉及的表集合
-> acquireOrderedLocks(allInvolvedTables, Exclusive)
-> loadUserTableSchema
-> TableDmlService::insertRows/updateRows/deleteRows(...)
-> release locks
```

说明：

1. “涉及的表集合”不能在执行过程中动态扩展。
2. 对 FK 级联写，必须在正式修改前先得到完整锁集合。

#### 2.7 `TableDmlService` 与内部 FK 级联函数

本阶段不改公开签名，但增加一个内部约束：

1. `TableDmlService::*` 默认假设“外层 service 入口已经拿到足够的锁”。
2. `validateIncomingForeignKeys / planForeignKeyCascade / applyForeignKeyCascade / commitMutationStates` 禁止内部再次申请新锁。

数据流约束：

```text
tuple_service::updateRows/deleteRows
-> 预计算 involvedTables
-> 外层一次性加锁
-> TableDmlService 内部只做纯业务校验与写入
```

### 3. 每个关键函数内部具体行为的收口计划

#### 3.1 锁键归一化

规则：

1. `dataRoot` 必须做 `QDir::cleanPath`。
2. `databaseName / tableName` 必须使用 normalize 后的值。
3. 数据库级锁强制 `tableName = ""`。

#### 3.2 死锁避免

规则：

1. 多表写只允许 `acquireOrderedLocks(...)`。
2. 禁止先拿 A 再拿 B，同时另一路先拿 B 再拿 A。
3. 级联过程不允许中途追加新锁。

#### 3.3 超时与错误返回

规则：

1. 超时统一返回：
   - `failed to acquire runtime lock for '<key>'`
2. 任意加锁失败必须释放本轮已持有的全部锁。
3. 业务函数不得吞掉锁错误。

#### 3.4 共享锁与排它锁边界

规则：

1. 纯读取：共享锁。
2. 任意 repo 写：排它锁。
3. 需要“读后写”的函数从一开始就拿排它锁，不允许先共享后升级。

#### 3.5 回滚与 RAII

规则：

1. 锁对象必须 RAII 释放。
2. 任意 `return` 路径不得手工遗漏解锁。
3. 锁管理不得与业务回滚耦合。

### 4. 测试计划

#### 4.1 `RuntimeLockManager`

1. `test_acquireSharedLockSucceeds`
2. `test_acquireExclusiveLockSucceeds`
3. `test_sharedSharedCanCoexist`
4. `test_sharedExclusiveBlocks`
5. `test_exclusiveExclusiveBlocks`
6. `test_timeoutReturnsError`
7. `test_orderedLocksSortAndDeduplicate`
8. `test_failedOrderedAcquireRollsBackPartialLocks`

#### 4.2 `database_service`

1. `test_createDatabaseSerializesOnSameDatabase`
2. `test_dropDatabaseBlocksConcurrentUseDatabase`
3. `test_showDatabasesUsesSharedLock`

#### 4.3 `table_service`

1. `test_createTableBlocksConcurrentInsert`
2. `test_dropTableBlocksConcurrentSelect`
3. `test_alterTableBlocksConcurrentUpdate`
4. `test_createIndexBlocksConcurrentUpdate`
5. `test_dropIndexBlocksConcurrentSelect`

#### 4.4 `tuple_service`

1. `test_selectRowsSharedSharedAllowed`
2. `test_selectRowsBlockedByExclusiveWriter`
3. `test_insertRowsSerializesSameTable`
4. `test_updateRowsSerializesSameTable`
5. `test_deleteRowsSerializesSameTable`
6. `test_fkCascadeLocksAllRelatedTablesInStableOrder`
7. `test_lockFailureReturnsTaskError`

---

## 阶段二：全量元数据预加载缓存（CatalogCache）

目标：减少 `loadUserTableSchema / loadUserTableIndexes / 约束读取 / showTables / showDatabases` 这类重复读盘，并把单表的 `schema / constraint / index` 元数据在第一次 miss 时一次性完整预加载。

### 0. 常量增量

继续使用 [thread_perf_def.h](E:/Qt-projects/DBMS/constants/thread_perf_def.h)

新增常量：

1. `kCatalogCacheMaxTableEntries`
   - 默认值：`256`

2. `kCatalogCacheMaxDatabaseEntries`
   - 默认值：`64`

3. `kEnableCatalogCache`
   - 默认值：`true`

4. `kEnableTableMetadataPreload`
   - 默认值：`true`
   - 含义：`CatalogCache::getTableCatalog(...)` 在 miss 时一次性读取并组装完整表元数据，而不是按 schema / constraint / index 分散读盘。

### 1. 边界划定

本阶段允许修改：

1. [service_common.h](E:/Qt-projects/DBMS/utils/service_common/service_common.h)
2. [constraint_worker.cpp](E:/Qt-projects/DBMS/utils/service_common/constraint_worker.cpp)
3. [database_service.cpp](E:/Qt-projects/DBMS/service/database_service.cpp)
4. [table_service.cpp](E:/Qt-projects/DBMS/service/table_service.cpp)
5. [tuple_service.cpp](E:/Qt-projects/DBMS/service/tuple_service.cpp)
6. 新增 `utils/thread_runtime/catalog_cache.h/.cpp`
7. 新增测试：
   - `tests/test_catalog_cache.cpp`
   - `tests/test_service_common_cache.cpp`

本阶段不修改：

1. `repo::*` 的存储格式。
2. `table_dml_service` 的候选表业务逻辑。
3. `logic / QueryExecutor`。

### 2. 精确到每个函数的具体输入输出的数据流收口计划

#### 2.1 新增数据结构

```cpp
struct TableCatalogSnapshot {
    QString dataRoot;
    QString databaseName;
    QString tableName;
    tabledef::TableSchema schema;
    bool fullyLoaded = false;
};

struct DatabaseCatalogSnapshot {
    QString dataRoot;
    QString databaseName;
    QStringList tableNames;
};

struct RootCatalogSnapshot {
    QString dataRoot;
    QStringList databaseNames;
};

class CatalogCache {
public:
    TableCatalogSnapshot getTableCatalog(const QString &dataRoot,
                                         const QString &databaseName,
                                         const QString &tableName,
                                         QString *error);

    DatabaseCatalogSnapshot getDatabaseCatalog(const QString &dataRoot,
                                               const QString &databaseName,
                                               QString *error);

    RootCatalogSnapshot getRootCatalog(const QString &dataRoot,
                                       QString *error);

    void invalidateTableCatalog(const QString &dataRoot,
                                const QString &databaseName,
                                const QString &tableName);

    void invalidateDatabaseCatalog(const QString &dataRoot,
                                   const QString &databaseName);

    void invalidateAllForDataRoot(const QString &dataRoot);
};
```

#### 2.2 `CatalogCache::getTableCatalog`

输入：

1. `dataRoot`
2. `databaseName`
3. `tableName`
4. `error`

输出：

1. `TableCatalogSnapshot`

数据流：

```text
构造 cache key
-> 查缓存
-> 命中则返回 snapshot
-> 未命中则通过 MetaRepo / ConstraintRepo / IndexRepo 读取
-> 组装完整 TableSchema（列定义 + constraints + indexes）
-> 标记 snapshot.fullyLoaded = true
-> 写入缓存
-> 返回 snapshot
```

正式规则：

1. `getTableCatalog(...)` miss 时必须一次性完成单表完整元数据预加载。
2. “完整元数据”在第一版明确定义为：
   - 列定义
   - `constraints`
   - `indexes`
3. `loadUserTableSchema(...) / loadUserTableIndexes(...) / 读取约束的 service_common 路径` 不允许各自再次直接读 repo 拼装局部结果，必须统一复用 `TableCatalogSnapshot.schema`。
4. `TableCatalogSnapshot.schema` 是阶段二里单表元数据的唯一正式缓存载体；不再为 `constraint / index` 额外维护第二份平行缓存结构。

#### 2.3 `CatalogCache::getDatabaseCatalog`

输入：

1. `dataRoot`
2. `databaseName`
3. `error`

输出：

1. `DatabaseCatalogSnapshot`

数据流：

```text
查缓存
-> 未命中时通过 TabRepo::listTables() 读取
-> 写缓存
-> 返回
```

#### 2.4 `CatalogCache::getRootCatalog`

输入：

1. `dataRoot`
2. `error`

输出：

1. `RootCatalogSnapshot`

数据流：

```text
构造 root-level cache key
-> 查缓存
-> 未命中时通过 DatabaseRepo::listDatabases() 读取
-> 组装 RootCatalogSnapshot
-> 写缓存
-> 返回
```

正式规则：

1. `showDatabases()` 的缓存正式落点是 `getRootCatalog(...)`。
2. `RootCatalogSnapshot` 只承载“当前 dataRoot 下有哪些数据库”，不混入表列表。
3. `invalidateAllForDataRoot(dataRoot)` 必须同时失效：
   - root-level 数据库列表缓存
   - 该 `dataRoot` 下全部 database/table 级缓存

#### 2.5 `loadUserTableSchema / loadUserTableIndexes / loadUserTableConstraints`

输入：

1. `tableName`
2. `error`

输出：

1. `tabledef::TableSchema` 或从 `TableSchema` 派生出的 `indexes / constraints`

新数据流：

```text
normalizeDatabaseName
-> CatalogCache::getTableCatalog(currentDataRoot, currentDatabase, tableName, error)
-> 只读取一次完整 snapshot
-> schema 调用方：return snapshot.schema
-> index 调用方：return snapshot.schema.indexes
-> constraint 调用方：return snapshot.schema.constraints
```

正式规则：

1. 约束读取的正式共享入口命名固定为：
   - `loadUserTableConstraints(const QString &tableName, QString *error)`
2. `loadUserTableConstraints(...)` 必须直接复用 `CatalogCache::getTableCatalog(...)` 的结果：
   - `return snapshot.schema.constraints`
3. 不允许在业务点继续直接写：
   - `ConstraintRepo(...).listConstraints(...)`
   作为常规约束读取路径。

#### 2.6 `table_service` 的 DDL 写成功后失效路径

##### `createTable / dropTable`

```text
repo 写入成功
-> invalidateTableCatalog(dataRoot, db, table)
-> invalidateDatabaseCatalog(dataRoot, db)
```

##### `addColumn / deleteColumn / modifyColumn / addConstraint / modifyConstraint / deleteConstraint / createIndex / dropIndex`

```text
repo 写入成功
-> invalidateTableCatalog(dataRoot, db, table)
```

##### `createDatabase / dropDatabase`

```text
repo 写入成功
-> invalidateAllForDataRoot(dataRoot)
```

### 3. 每个关键函数内部具体行为的收口计划

#### 3.1 缓存 key 规则

1. `dataRoot + databaseName + tableName` 是表级唯一键。
2. `dataRoot + databaseName` 是数据库级唯一键。
3. 切换 `currentDataRoot` 后，旧数据根缓存不得复用。

#### 3.2 缓存写入时机

1. 只有 repo 读取完整成功后才写缓存。
2. 任意一步 repo 读取失败，不得缓存半成品。
3. DDL 失败不得失效缓存。
4. DDL 成功后必须失效缓存，不允许尝试“局部手工修补”旧缓存。
5. `TableCatalogSnapshot` 若 `fullyLoaded == false`，不得写入缓存。

#### 3.3 线程安全

1. `CatalogCache` 自身必须内部加锁。
2. 读写缓存时不得长时间持有 service 业务锁。
3. 缓存锁与表运行时锁分离，避免互相嵌套导致死锁。

#### 3.4 与阶段一的配合

1. 读缓存前外层已拿共享/排它锁。
2. 缓存只负责减读盘频，不改变并发控制边界。
3. “全量元数据预加载”只改变读盘次数，不改变 `service -> repo` 的对外语义。

### 4. 测试计划

#### 4.1 `CatalogCache`

1. `test_getTableCatalogMissThenHit`
2. `test_getDatabaseCatalogMissThenHit`
3. `test_getRootCatalogMissThenHit`
4. `test_failedReadDoesNotPopulateCache`
5. `test_invalidateTableCatalogOnlyRemovesOneTable`
6. `test_invalidateDatabaseCatalogRemovesTableListOnly`
7. `test_invalidateAllForDataRootClearsCurrentRoot`
8. `test_differentDataRootsDoNotShareCache`
9. `test_tableCatalogMissPreloadsSchemaConstraintAndIndexTogether`

#### 4.2 `service_common`

1. `test_loadUserTableSchemaUsesCatalogCache`
2. `test_loadUserTableIndexesUsesCatalogCache`
3. `test_loadUserTableConstraintsUsesCatalogCache`
4. `test_showTablesUsesDatabaseCatalog`
5. `test_showDatabasesUsesRootCatalog`

#### 4.3 `table_service / database_service`

1. `test_createTableInvalidatesTableAndDatabaseCatalog`
2. `test_dropTableInvalidatesTableAndDatabaseCatalog`
3. `test_modifyColumnInvalidatesOnlyTargetTableCatalog`
4. `test_createIndexInvalidatesOnlyTargetTableCatalog`
5. `test_dropDatabaseInvalidatesAllForDataRoot`

---

## 阶段三：DML 热路径与索引维护降盘频

目标：在不引入 buffer pool 的前提下，减少 DML 主链里的重复装载、重复校验，以及入口处多余的整表索引重建。

当前代码前提：

1. 当前代码已经具备增量索引维护接口：
   - `insertTableIndexes(...)`
   - `updateTableIndexes(...)`
   - `deleteTableIndexes(...)`
2. 因此本阶段不是“补做增量索引维护”，而是“删掉错误位置的整表 rebuild，只保留正确位置的 rebuild”。

### 1. 边界划定

本阶段允许修改：

1. [table_dml_service.cpp](E:/Qt-projects/DBMS/service/table_dml_service.cpp)
2. [tuple_service.cpp](E:/Qt-projects/DBMS/service/tuple_service.cpp)
3. [constraint_worker.cpp](E:/Qt-projects/DBMS/utils/service_common/constraint_worker.cpp)
4. [service_common.h](E:/Qt-projects/DBMS/utils/service_common/service_common.h)
5. [sort_index_repo.cpp](E:/Qt-projects/DBMS/repo/sort_index_repo.cpp)
6. 新增测试：
   - `tests/test_table_runtime_pipeline.cpp`
   - `tests/test_index_runtime_repair.cpp`

本阶段不修改：

1. `repo::FlatFileTableStore` 文件格式。
2. parser / dispatcher。
3. `logic / QueryExecutor` 的表达式语义。

### 2. 精确到每个函数的具体输入输出的数据流收口计划

本阶段复用现有 `TableMutationState`，不再新增第二套“运行时快照”结构。

#### 2.1 扩展 `TableMutationState`

扩展为：

```cpp
struct TableMutationState
{
    QString databaseName;
    QString tableName;
    tabledef::TableSchema schema;
    repo::TableData originalTable;
    repo::TableData candidateTable;
    QStringList originalRowIds;
    QStringList candidateRowIds;
    bool rowIdsInitialized = false;
    bool runtimeArtifactsChecked = false;
    bool indexesHealthy = false;
    bool dirty = false;
};
```

#### 2.2 `ensureTableMutationState(...)`

输入：

1. `databaseName`
2. `tableName`
3. `states`
4. `error`

输出：

1. `TableMutationState*`

新数据流：

```text
查 states
-> 已存在则直接返回
-> 不存在则：
   -> 通过 CatalogCache 读 schema
   -> 读 table.dat
   -> 读 row-id sidecar
   -> 填充 original/candidate
   -> 不在这里主动 rebuild index
-> 写入 states
-> 返回
```

#### 2.3 `ensureMutationStateRuntimeArtifacts(...)`

新增内部函数：

```cpp
bool ensureMutationStateRuntimeArtifacts(TableMutationState *state,
                                         QString *error);
```

输入：

1. `state`
2. `error`

输出：

1. `bool`

数据流：

```text
若 state->runtimeArtifactsChecked == true
-> 直接返回

否则：
-> 校验 row-id sidecar 行数是否匹配
-> 校验每个已声明索引文件是否存在且结构可读
-> 若发现缺失/不一致：
   -> 只修当前表
   -> saveUserTableRowIds(...)
   -> 仅在工件缺失/损坏时 rebuildTableIndexes(...)
-> 标记 runtimeArtifactsChecked=true
-> 标记 indexesHealthy=true
```

正式修复触发条件：

1. `row-id` 文件不存在。
2. `row-id` 行数与 `table.dat` 行数不一致。
3. schema 中声明的某个索引文件不存在。
4. 索引文件可读但结构不合法。
5. 唯一索引校验失败，说明索引内容与主表状态已失配。

不属于修复触发条件的情况：

1. 普通 `insert / update / delete` 入口。
2. 普通 `createIndex` 前置读取。
3. 普通 `ensureConstraintBoundIndex(...)` 前置路径。
4. 单纯因为 `rowIdsInitialized == true`。

#### 2.4 `insertRows / updateRows / deleteRows`

新的统一主链：

```text
外层 tuple_service 取得锁
-> ensureTableMutationState(targetTable)
-> ensureMutationStateRuntimeArtifacts(targetTable)
-> 构造 matchedRowIndexes / candidateTable / candidateRowIds
-> FK 检查与 cascade 复用 states 中的快照
-> validateAllMutationStates(...)
-> commitMutationStates(...)
```

#### 2.5 `commitMutationStates(...)`

输入：

1. `states`
2. `error`

输出：

1. `bool`

新数据流：

```text
遍历 dirty states
-> 先写 table.dat
-> 再写 row-id sidecar
-> 最后对每张 dirty 表执行一次增量 index 更新
-> 不在 commit 正常路径调用 rebuildTableIndexes(...)
-> 任意失败则回滚已提交表
```

收口要求：

1. 同一张表在一次业务操作里最多只允许一次最终提交。
2. 禁止在入口、校验中途、提交前多次重复 rebuild。

#### 2.6 `tuple_service::selectRows(...)`

新数据流：

```text
共享锁
-> 通过 CatalogCache 取 schema
-> TableDmlService::selectRows(...)
-> 不做 row-id / index 健康修复
```

说明：

1. 纯查询不触发索引自修复。
2. 索引/row-id 修复只在写路径或显式管理操作中触发。

### 3. 每个关键函数内部具体行为的收口计划

#### 3.1 取消入口处的无条件重建

现状中 `insertRows / updateRows / deleteRows` 在入口会根据 `rowIdsInitialized` 执行 `rebuildTableIndexes(...)`。

本阶段正式改为：

1. 入口不再无条件 rebuild。
2. 正常提交路径继续复用现有增量接口：
   - `insertTableIndexes(...)`
   - `updateTableIndexes(...)`
   - `deleteTableIndexes(...)`
3. 只有 `ensureMutationStateRuntimeArtifacts(...)` 检测出不一致时才修复。

这部分是本阶段最核心的正式改动边界：

1. 必须删除的“正常路径预热式 rebuild”
   - [table_dml_service.cpp](E:/Qt-projects/DBMS/service/table_dml_service.cpp#L1879)
   - [table_dml_service.cpp](E:/Qt-projects/DBMS/service/table_dml_service.cpp#L2022)
   - [table_dml_service.cpp](E:/Qt-projects/DBMS/service/table_dml_service.cpp#L2266)
   - [constraint_worker.cpp](E:/Qt-projects/DBMS/utils/service_common/constraint_worker.cpp#L855)
   - [table_service.cpp](E:/Qt-projects/DBMS/service/table_service.cpp#L1092)

必须删除的 rebuild 行为：

1. `TableDmlService::insertRows(...)` 入口：
   - 删除 `rowIdsInitialized -> rebuildTableIndexes(...)`
2. `TableDmlService::updateRows(...)` 入口：
   - 删除 `rowIdsInitialized -> rebuildTableIndexes(...)`
3. `TableDmlService::deleteRows(...)` 入口：
   - 删除 `rowIdsInitialized -> rebuildTableIndexes(...)`
4. `ensureConstraintBoundIndex(...)` 前置路径：
   - 删除“先 `loadUserTableRowIds` 再 `rebuildTableIndexes(...)`”这段预热式行为
   - 该函数只负责确保目标约束绑定索引存在/重建目标索引本身，不负责顺手修全表索引
5. `table_service::createIndex(...)` 前置路径：
   - 删除“若 `rowIdsInitialized == true` 则先 `rebuildIndexesForTable(...)`”
   - `createIndex(...)` 只构建当前新索引，不顺手重建其他旧索引

允许保留的 rebuild 行为：

1. `restoreTableArtifacts(...)`
   - 这是失败回滚后的显式修复路径，允许整表 rebuild
2. `rebuildIndexesForTable(...)`
   - 这是显式修复 helper，允许保留
3. `ensureMutationStateRuntimeArtifacts(...)`
   - 在确认工件损坏后，允许对当前表执行整表 rebuild

当前已经正确走增量的正常提交路径：

1. [table_dml_service.cpp](E:/Qt-projects/DBMS/service/table_dml_service.cpp#L1955) `insertTableIndexes(...)`
2. [table_dml_service.cpp](E:/Qt-projects/DBMS/service/table_dml_service.cpp#L2199) `updateTableIndexes(...)`
3. [table_dml_service.cpp](E:/Qt-projects/DBMS/service/table_dml_service.cpp#L2418) `deleteTableIndexes(...)`

#### 3.2 row-id sidecar 策略

规则：

1. row-id 文件缺失：
   - 写路径允许自动重建。
   - 读路径不自动落盘。

2. row-id 行数与表行数不一致：
   - 写路径修复。
   - 读路径报错或返回瞬态值，但不写盘。

#### 3.3 索引健康检查策略

规则：

1. 只检查声明在 schema 里的索引。
2. 缺索引文件、结构不可读、唯一索引校验失败时，触发“当前表单表修复”。
3. 不做全库扫描式修复。

#### 3.4 提交顺序

规则：

1. 先表数据。
2. 再 row-id。
3. 再索引。

索引层正式规则：

1. 正常 DML 提交优先走现有增量接口。
2. 不允许把正常 DML 提交重新退化成整表 `rebuildTableIndexes(...)`。
3. 只有索引文件缺失、结构不可读、row-id 与索引定位已失配时，才允许修复性整表 rebuild。
4. `createIndex(...)` 只创建目标索引，不负责顺手修复其他索引。
5. `ensureConstraintBoundIndex(...)` 只确保目标约束绑定索引存在，不负责顺手修复其他索引。

理由：

1. 索引和 row-id 都是从表数据派生出来的。
2. 派生层永远追随主表状态。

#### 3.5 回滚策略

规则：

1. `commitMutationStates(...)` 失败时必须按已成功提交表逆序恢复：
   - table.dat
   - row-id
   - index
2. 回滚错误需要拼接进最终 error。

### 4. 测试计划

#### 4.1 `ensureTableMutationState`

1. `test_ensureTableMutationStateLoadsSchemaOnlyOncePerTable`
2. `test_ensureTableMutationStateReusesExistingState`
3. `test_ensureTableMutationStateDoesNotRebuildIndexesOnLoad`

#### 4.2 `ensureMutationStateRuntimeArtifacts`

1. `test_missingRowIdSidecarRepairsOnWritePath`
2. `test_missingIndexFileRepairsCurrentTableOnly`
3. `test_runtimeArtifactsCheckedPreventsDuplicateRepair`
4. `test_readPathDoesNotPersistRepair`

#### 4.3 `commitMutationStates`

1. `test_commitMutationStatesWritesEachDirtyTableOnce`
2. `test_commitMutationStatesSkipsCleanTables`
3. `test_commitMutationStatesRollbackOnRowIdWriteFailure`
4. `test_commitMutationStatesRollbackOnIndexWriteFailure`
5. `test_commitMutationStatesReverseRollbackOrder`

#### 4.4 `insertRows / updateRows / deleteRows`

1. `test_insertRowsNoRedundantSchemaReload`
2. `test_updateRowsNoEntryRebuildWhenArtifactsHealthy`
3. `test_deleteRowsNoEntryRebuildWhenArtifactsHealthy`
4. `test_fkCascadeSharesLoadedMutationStates`
5. `test_multiTableCascadeCommitsEachTableOnce`
6. `test_failedCascadeDoesNotPartiallyPersistArtifacts`

---

## 阶段间总数据流

### 写路径

```text
tuple_service::{insertRows|updateRows|deleteRows}
-> 阶段一：外层一次性获取表级排它锁
-> 阶段二：CatalogCache 提供完整表元数据（schema / constraint / index）
-> 阶段三：ensureTableMutationState(...)
-> 阶段三：ensureMutationStateRuntimeArtifacts(...)
-> 业务校验 / FK / cascade
-> commitMutationStates(...)
-> 释放锁
```

### 读路径

```text
tuple_service::selectRows
-> 阶段一：共享锁
-> 阶段二：CatalogCache 提供完整表元数据
-> TableDmlService::selectRows(...)
-> 释放锁
```

### DDL 路径

```text
table_service::*
-> 阶段一：数据库级/表级排它锁
-> repo 写入
-> 阶段二：失效 CatalogCache
-> 释放锁
```

---

## 最终验收口径

完成全部阶段后，应满足以下口径：

1. 单进程多线程下，同一张表的写写冲突必须被串行化。
2. 同一张表的读写冲突必须被共享/排它锁正确阻塞。
3. FK 级联必须一次性按稳定顺序获取全部相关表锁。
4. `loadUserTableSchema / loadUserTableIndexes / loadUserTableConstraints / showTables / showDatabases` 必须具备缓存。
5. `insert/update/delete` 入口不得再无条件重建索引。
6. 单次 DML 业务链中，同一张表只允许一次最终 artifact 提交。
7. 任何失败路径都必须返回明确错误，不允许静默跳过锁、缓存失效、row-id 修复、索引修复。
