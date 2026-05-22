```
data/
  root.dbf
  [数据库名].tab
  [数据库名]/
    [表名].meta
    [表名].con
    [表名].idx
    [表名].dat
    indexes/
      ...
```

阶段四：(index 函数级实现)

本阶段只收口索引相关内容。CHECK 仍按阶段三说明继续暂缓，不纳入本文件。

1. `constants/table_def.h` 补充索引元数据结构

用于在 `.idx` 文件中记录一个表有哪些索引，以及每个索引对应哪些列。

```cpp
struct IndexMeta {
    QString indexName;       // 索引名，唯一
    QStringList columnNames; // 索引列，支持多列
    bool isUnique;           // 是否唯一索引
};
```

建议配套的基础函数：

- `schemaIndexNames(const TableSchema &schema)`
  - 输入：`schema`
  - 输出：索引名列表
- `findIndexIndex(const TableSchema &schema, const QString &indexName)`
  - 输入：`schema`、`indexName`
  - 输出：索引在 schema 中的位置；找不到返回 `-1`
- `hasIndex(const TableSchema &schema, const QString &indexName)`
  - 输入：`schema`、`indexName`
  - 输出：是否存在同名索引
- `sameIndexSemantics(const IndexMeta &lhs, const IndexMeta &rhs)`
  - 输入：两个索引定义
  - 输出：是否表示同一组列、同一类语义
- `validateIndexDefinition(const TableSchema &schema, const IndexMeta &candidate, const QString &skipIndexName = QString(), QString *error = nullptr)`
  - 输入：现有 schema、新索引定义、可选跳过的索引名
  - 输出：是否合法、错误信息

2. `/service/table_service.cpp` 补充索引 DDL

### `createIndex`

```cpp
TaskResult createIndex(const QString &tableName,
                       const QString &indexName,
                       const QStringList &columnNames,
                       bool isUnique);
```

输入：

- `tableName`：目标表名
- `indexName`：索引名
- `columnNames`：索引列，允许多列
- `isUnique`：是否唯一索引

输出：

- `success`
- `errorMessage`

职责：

- 检查表是否存在
- 检查索引名是否重复
- 检查列是否存在、列顺序是否合法、列集合是否重复
- 检查索引语义是否和已有索引重复
- 如果是唯一索引，先扫描现有数据，确认没有重复键
- 创建 `.idx` 中的索引元数据
- 创建 `indexes/` 下对应的 B+ 树索引文件
- 回填现有数据到索引结构中

### `dropIndex`

```cpp
TaskResult dropIndex(const QString &tableName,
                     const QString &indexName);
```

输入：

- `tableName`：目标表名
- `indexName`：待删除的索引名

输出：

- `success`
- `errorMessage`

职责：

- 检查索引是否存在
- 如果该索引被 PRIMARY KEY / UNIQUE 约束引用，则拒绝单独删除
- 删除 `.idx` 中的索引元数据
- 删除 `indexes/` 下对应的 B+ 树文件

3. `/service/tuple_service.cpp` 补充 PK / UNIQUE 自动索引联动

本阶段 tuple_service 不再只关心 `.dat`，还要同步维护 `.idx` 和 `indexes/` 下的真实索引内容。

建议补充的内部函数：

- `rebuildTableIndexes(const QString &tableName, const tabledef::TableSchema &schema, const repo::TableData &tableData)`
  - 输入：表名、schema、当前表数据
  - 输出：所有索引是否重建成功
- `ensureConstraintBoundIndex(const QString &tableName, const tabledef::Constraint &constraint)`
  - 输入：表名、约束定义
  - 输出：是否创建或找到对应的 PK / UNIQUE 索引
- `removeConstraintBoundIndex(const QString &tableName, const QString &constraintName)`
  - 输入：表名、约束名
  - 输出：是否删除对应索引

联动规则：

- CREATE TABLE 时，如果存在 PRIMARY KEY / UNIQUE，则自动创建对应索引
- ADD CONSTRAINT / MODIFY CONSTRAINT 时，如果新增或修改成 PRIMARY KEY / UNIQUE，则自动创建索引
- DELETE CONSTRAINT 时，如果删掉的是 PRIMARY KEY / UNIQUE，则同步删除对应索引
- ADD COLUMN / DELETE COLUMN / MODIFY COLUMN 若影响到已建索引列，则需要重建或删除相关索引

对 tuple_service 的 DML 操作：

### `insertRows`

```cpp
TableDmlResult insertRows(...);
```

输入：

- 目标数据库名、表名、表类型、schema、待插入的多行记录、校验模式

输出：

- `success`
- `errorMessage`
- `affectedRowCount`

索引职责：

- 插入成功后，把新行写入所有受影响索引
- 如果是 PRIMARY KEY / UNIQUE 相关索引，先通过索引判断是否冲突

### `updateRows`

```cpp
TableDmlResult updateRows(...);
```

输入：

- 目标数据库名、表名、表类型、schema、赋值映射、条件、校验模式

输出：

- `success`
- `errorMessage`
- `affectedRowCount`

索引职责：

- 如果更新列命中索引列，则先删除旧索引项，再写入新索引项
- 如果更新导致 PK / UNIQUE 冲突，则拒绝

### `deleteRows`

```cpp
TableDmlResult deleteRows(...);
```

输入：

- 目标数据库名、表名、表类型、schema、条件、校验模式

输出：

- `success`
- `errorMessage`
- `affectedRowCount`

索引职责：

- 删除成功后，同步删除受影响的索引项
- 如果删除的是被 PRIMARY KEY / UNIQUE 绑定的记录，索引内容必须同步保持一致

4. `/utils/service_common/constraint_worker.cpp` 通过索引实现 PK / UNIQUE 判定

本阶段原来依赖全表扫描的 PK / UNIQUE 校验，改成优先走索引。

建议保留或补充的函数：

- `checkKeyUniqueness(const tabledef::TableSchema &schema, const repo::TableData &table, const QList<IndexMeta> &indexes, QString *error)`
  - 输入：schema、当前表数据、索引元数据
  - 输出：主键/唯一键是否冲突
- `validateConstraintWithIndex(...)`
  - 输入：约束定义、索引元数据、表数据
  - 输出：约束是否成立

职责：

- PRIMARY KEY / UNIQUE 的重复性判断优先使用对应索引
- 若索引缺失或损坏，再回退到全表校验
- CREATE INDEX 与 PRIMARY KEY / UNIQUE 需要互相联动：
  - 新建 PK / UNIQUE 时自动建索引
  - 删除 PK / UNIQUE 时自动删索引
  - 单独 DROP INDEX 若命中 PK / UNIQUE 绑定关系，则拒绝或要求由约束删除流程驱动

5. `/utils/service_common/sort_index.cpp` 实装 B+ 树索引

建议的索引读写函数：

- `createSortIndex(const QString &indexName, const QStringList &columnNames, bool isUnique, const repo::TableData &tableData, QString *error)`
  - 输入：索引名、索引列、是否唯一、表数据
  - 输出：是否创建成功
- `dropSortIndex(const QString &indexName, QString *error)`
  - 输入：索引名
  - 输出：是否删除成功
- `insertIndexEntry(const QString &indexName, const QStringList &keyValues, const QString &rowLocator, QString *error)`
  - 输入：索引名、键值、行定位信息
  - 输出：是否插入成功
- `deleteIndexEntry(const QString &indexName, const QStringList &keyValues, const QString &rowLocator, QString *error)`
  - 输入：索引名、键值、行定位信息
  - 输出：是否删除成功
- `updateIndexEntry(const QString &indexName, const QStringList &oldKeyValues, const QStringList &newKeyValues, const QString &rowLocator, QString *error)`
  - 输入：索引名、旧键值、新键值、行定位信息
  - 输出：是否更新成功
- `searchIndex(const QString &indexName, const QStringList &keyValues, QString *error)`
  - 输入：索引名、查询键值
  - 输出：命中的行定位或结果集

职责：

- 在磁盘上维护 B+ 树节点
- 支持按单列或多列键查找
- 支持唯一索引和非唯一索引
- 为 PK / UNIQUE / 普通索引提供统一存储实现

6. 阶段四收口原则

- 索引只处理 `PK / UNIQUE / 普通 INDEX`
- `CHECK` 仍然暂缓，继续等逻辑谓词工具单独完成后再接入
- `table.con` 只保留约束本身，`indexName` 作为约束与索引的绑定字段
- `table.idx` 负责描述索引元数据，`indexes/` 负责存放真实 B+ 树文件
- DDL 负责建/删索引，DML 负责维护索引一致性，`constraint_worker.cpp` 负责利用索引做约束校验