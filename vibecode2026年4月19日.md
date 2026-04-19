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

阶段四 
实装index。具体来说：

1. 在 table_def.h 里补充 [表名]/table.idx 的文件，用于存放索引表，来指示每个列的索引情况。
对象化内容参看：
```
struct IndexMeta {
    QString indexName;        // 索引名（唯一）
    QStringList columnNames;  // 索引列（支持多列）
    bool isUnique;            // 是否唯一索引
};
.idx 文件内
```



2. 在table_service.cpp里补充构建索引的方法

CREATE INDEX 索引名 (列名1 [, ...])
主要先调用table_dml_service检查有没有

DROP INDEX 索引名

3. 在tuple_service.cpp里补充PK、UNIQUE自动构建索引的逻辑

在构建的时候 CREATE INDEX (自动产生的索引名) PK列

3. 在/util/service_common/constraint_worker.cpp里实装依靠

4. 在/utils/service_common/sort_index.cpp 里 构建 B+ 树 索引构建逻辑。

约束待完成备忘

- PRIMARY KEY 约束的完整索引联动
- UNIQUE 约束的完整索引联动
- FOREIGN KEY 的插入校验
- FOREIGN KEY 的更新校验
- FOREIGN KEY 的删除校验
- 新增约束时对历史数据的完整校验
- 修改约束时对历史数据的完整校验
- 删除约束时对关联索引的清理
- 删除约束时对关联元数据的清理
- CREATE CONSTRAINT 的统一入口
- DROP CONSTRAINT 的统一入口
- MODIFY CONSTRAINT 的统一入口
- CREATE INDEX 与 PRIMARY KEY / UNIQUE 的联动
- DROP INDEX 与 PRIMARY KEY / UNIQUE 的联动

- CHECK 约束的完整执行逻辑