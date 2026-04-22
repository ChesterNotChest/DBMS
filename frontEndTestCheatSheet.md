# 前端演示速查

用于答辩现场直接在前端输入 SQL。
只收录当前已经实现、适合稳定演示的语句。

## 1. 数据库

创建数据库：

```sql
CREATE DATABASE demo_db;
```

切换数据库：

```sql
USE demo_db;
```

查看数据库列表：

```sql
SHOW DATABASES;
```

查看当前库中的表：

```sql
SHOW TABLES;
```

## 2. 建表

父表：

```sql
CREATE TABLE parent (
  id INT PRIMARY KEY,
  name VARCHAR(64) NOT NULL,
  age INT DEFAULT 18,
  CONSTRAINT uq_parent_name UNIQUE (name)
);
```

普通子表：

```sql
CREATE TABLE child (
  id INT PRIMARY KEY,
  parent_id INT,
  note VARCHAR(64),
  CONSTRAINT fk_child_parent FOREIGN KEY (parent_id) REFERENCES parent(id) ON DELETE NO ACTION ON UPDATE NO ACTION
);
```

用于演示 `DELETE CASCADE / UPDATE CASCADE` 的子表：

```sql
CREATE TABLE child_cascade (
  id INT PRIMARY KEY,
  parent_id INT,
  note VARCHAR(64),
  CONSTRAINT fk_child_cascade_parent FOREIGN KEY (parent_id) REFERENCES parent(id) ON DELETE CASCADE ON UPDATE CASCADE
);
```

用于演示 `DELETE SET NULL` 的子表：

```sql
CREATE TABLE child_null (
  id INT PRIMARY KEY,
  parent_id INT,
  note VARCHAR(64),
  CONSTRAINT fk_child_null_parent FOREIGN KEY (parent_id) REFERENCES parent(id) ON DELETE SET NULL ON UPDATE NO ACTION
);
```

用于演示 `DELETE SET DEFAULT` 的子表：

```sql
CREATE TABLE child_default (
  id INT PRIMARY KEY,
  parent_id INT DEFAULT 0,
  note VARCHAR(64),
  CONSTRAINT fk_child_default_parent FOREIGN KEY (parent_id) REFERENCES parent(id) ON DELETE SET DEFAULT ON UPDATE NO ACTION
);
```

用于演示 `NO ACTION` 的子表：

```sql
CREATE TABLE child_no_action (
  id INT PRIMARY KEY,
  parent_id INT,
  note VARCHAR(64),
  CONSTRAINT fk_child_no_action_parent FOREIGN KEY (parent_id) REFERENCES parent(id) ON DELETE NO ACTION ON UPDATE NO ACTION
);
```

查看表结构：

```sql
DESC parent;
```

查看建表语句：

```sql
SHOW CREATE TABLE parent;
```

## 3. 插入数据

显式列名插入：

```sql
INSERT INTO parent (id, name, age) VALUES (1, 'alice', 20), (2, 'bob', 21);
```

不写列名插入：

```sql
INSERT INTO parent VALUES (3, 'carol', 22);
```

插入普通子表：

```sql
INSERT INTO child (id, parent_id, note) VALUES (1, 1, 'child of alice'), (2, 2, 'child of bob');
```

插入 FK 演示数据。
这里刻意给不同动作分配不同父键，避免相互干扰：

```sql
INSERT INTO parent (id, name, age) VALUES
  (0, 'root', 0),
  (11, 'cascade_parent', 0),
  (21, 'null_parent', 0),
  (31, 'default_parent', 0),
  (41, 'no_action_parent', 0);

INSERT INTO child_cascade (id, parent_id, note) VALUES (10, 11, 'cascade child');
INSERT INTO child_null (id, parent_id, note) VALUES (20, 21, 'set null child');
INSERT INTO child_default (id, parent_id, note) VALUES (30, 31, 'set default child');
INSERT INTO child_no_action (id, parent_id, note) VALUES (40, 41, 'no action child');
```

## 4. 查询

全表查询：

```sql
SELECT * FROM parent;
```

投影查询：

```sql
SELECT id, name FROM parent;
```

带 `LIMIT`：

```sql
SELECT * FROM parent LIMIT 2;
```

带简单 `WHERE`：

```sql
SELECT * FROM parent WHERE id = 1;
```

带多条件 `AND`：

```sql
SELECT * FROM parent WHERE id = 1 AND name = 'alice';
```

## 5. 更新与删除

带 `WHERE` 更新：

```sql
UPDATE parent SET age = 30 WHERE id = 1;
```

带多条件 `AND` 更新：

```sql
UPDATE parent SET name = 'alice_new' WHERE id = 1 AND age = 30;
```

带 `WHERE` 删除：

```sql
DELETE FROM child WHERE id = 2;
```

## 6. FK 重点演示

### 6.1 UPDATE CASCADE

先看子表：

```sql
SELECT * FROM child_cascade;
```

更新父表主键：

```sql
UPDATE parent SET id = 12 WHERE id = 11;
```

再看子表，`parent_id` 应同步变成 `12`：

```sql
SELECT * FROM child_cascade;
```

### 6.2 DELETE CASCADE

删除父行：

```sql
DELETE FROM parent WHERE id = 12;
```

再看子表，对应子行应被级联删除：

```sql
SELECT * FROM child_cascade;
```

### 6.3 DELETE SET NULL

先看数据：

```sql
SELECT * FROM child_null;
```

删除父行：

```sql
DELETE FROM parent WHERE id = 21;
```

再看子表，`parent_id` 应变为空：

```sql
SELECT * FROM child_null;
```

### 6.4 DELETE SET DEFAULT

先看数据：

```sql
SELECT * FROM child_default;
```

删除父行：

```sql
DELETE FROM parent WHERE id = 31;
```

再看子表，`parent_id` 应变成默认值 `0`：

```sql
SELECT * FROM child_default;
```

### 6.5 DELETE NO ACTION

先看数据：

```sql
SELECT * FROM child_no_action;
```

尝试删除仍被引用的父行：

```sql
DELETE FROM parent WHERE id = 41;
```

这里应当失败，用于演示外键阻止非法删除。

## 7. ALTER TABLE

加列：

```sql
ALTER TABLE parent ADD COLUMN score INT DEFAULT 60;
```

改列：

```sql
ALTER TABLE parent MODIFY COLUMN score INT DEFAULT 90;
```

加唯一约束：

```sql
ALTER TABLE parent ADD CONSTRAINT uq_parent_name_score UNIQUE (name, score);
```

改约束：

```sql
ALTER TABLE parent MODIFY CONSTRAINT uq_parent_name_score CONSTRAINT uq_parent_name_score_v2 UNIQUE (name, score);
```

删列：

```sql
ALTER TABLE parent DROP COLUMN score;
```

删约束：

```sql
ALTER TABLE parent DROP CONSTRAINT uq_parent_name_score_v2;
```

## 8. 索引

普通索引：

```sql
CREATE INDEX idx_parent_name ON parent(name);
```

唯一索引：

```sql
CREATE UNIQUE INDEX uq_parent_name_age_idx ON parent(name, age);
```

删除索引：

```sql
DROP INDEX idx_parent_name ON parent;
```

## 9. 收尾

查看所有表：

```sql
SHOW TABLES;
```

删除子表：

```sql
DROP TABLE child;
DROP TABLE child_cascade;
DROP TABLE child_null;
DROP TABLE child_default;
DROP TABLE child_no_action;
```

删除父表：

```sql
DROP TABLE parent;
```

删除数据库：

```sql
DROP DATABASE demo_db;
```

## 10. 现场注意

当前适合演示的 `WHERE` 只有：

```sql
WHERE 列名 = 值
WHERE 条件1 AND 条件2
```

现场不要演示这些未实现语法：

```sql
OR
!=
<
>
<=
>=
LIKE
IN
BETWEEN
ORDER BY
GROUP BY
JOIN
```
