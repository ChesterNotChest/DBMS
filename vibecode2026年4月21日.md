FK 具体实现

字段名	数据类型	说明
name	CHAR[128]	外键约束名称
field	CHAR[128]	外键字段名
type	INTEGER	约束类型标识（外键对应固定类型值）
param	CHAR[256]	外键参数：主表名、主表主键字段名

## 函数图

```text
parent row / parent table
  |
  v
validateIncomingForeignKeys() / validateOutgoingForeignKeys()
  |
  v
collectForeignKeyDependents()
  |
  v
planForeignKeyCascade()
  |
  v
applyForeignKeyCascade()
  |
  v
insertRows() / updateRows() / deleteRows()
  |
  v
table_service::*DDL入口
  |
  v
schema定义 / 持久化 / show create / describe
```

## 收口原则

这套 FK 设计按“函数级 I/O 收口”来拆，不再把动作语义散落到零碎分支里。

单跳函数负责局部正确性，递归闭包负责全局传播，`visited` 负责终止条件，拓扑顺序负责执行顺序，回滚边界负责失败一致性。

## 0. 方向辨析

FK 边本身只有一个定义方向：`child -> parent`。但执行时会站在两端看同一条边，所以要先区分“校验方向”和“影响方向”。

| 场景 | 走向 | 目的 | 典型函数 |
|---|---|---|---|
| 插入 / 更新子表 | `ref`，即 `child -> parent` | 证明引用存在 | `validateOutgoingForeignKeys()` |
| 删除 / 更新父表 | `refed by`，即 `parent -> children` | 找出受影响的下游表和行 | `validateIncomingForeignKeys()`、`collectForeignKeyDependents()` |

判定规则很简单：

- 当前操作如果是在确认“我能不能指向它”，就走 `ref`。
- 当前操作如果是在确认“我动了它会影响谁”，就走 `refed by`。
- 同一条 FK 在模型里不变，变的是当前操作站在边的哪一端看它。
- CASCADE、SET NULL、SET DEFAULT、NO ACTION 都只是在 `refed by` 这条影响路径上展开不同动作。

## 1. Schema 与持久化定义

| 函数 | 输入 | 输出 | 前置条件 | 失败回滚 |
|---|---|---|---|---|
| `tabledef::validateConstraintDefinitions()` | `Constraint` 列表、`TableSchema` | `bool + error` | 约束名称、引用列、动作字段必须完整 | 失败时不落盘，保持原 schema |
| `tabledef::validateConstraintRows()` | `Constraint`、`TableSchema`、待校验列和值 | `bool + error` | 约束模型已经包含 FK 动作语义 | 失败时不修改任何表数据 |
| `tabledef::isForeignKeyReferenceComplete()` | 单个 `Constraint` | `bool` | 外键引用目标、引用列、动作字段已填 | 无状态回滚，纯校验 |

目标：把 `ON DELETE` / `ON UPDATE` 的动作写进 `Constraint`，并进入序列化、反序列化、`show create`、`describe`。

## 2. DDL 入口

| 函数 | 输入 | 输出 | 前置条件 | 失败回滚 |
|---|---|---|---|---|
| `table_service::addConstraint()` | 表名、约束定义 | `TaskResult` | 目标表已存在，约束定义完整 | 失败时删除已写入的约束和绑定索引 |
| `table_service::modifyConstraint()` | 表名、旧约束名、新约束定义 | `TaskResult` | 旧约束存在，新约束可通过 schema 校验 | 失败时恢复旧约束和旧绑定索引 |
| `table_service::deleteConstraint()` | 表名、约束名 | `TaskResult` | 目标约束存在 | 失败时保持原约束和原索引状态 |
| `table_service::createTable()` | 表名、目标 schema | `TaskResult` | schema 完整，初始约束可落盘 | 失败时不创建表目录和表文件 |
| `table_service::dropTable()` | 表名 | `TaskResult` | 没有 incoming FK 引用，或已被显式处理 | 失败时保留所有表文件和元数据 |
| `table_service::modifyColumn()` | 表名、旧列名、新列定义 | `TaskResult` | 列定义完整，目标列存在 | 失败时恢复原表、原列、原约束、原索引 |
| `table_service::deleteColumn()` | 表名、列名 | `TaskResult` | 该列不参与 PK/UNIQUE/FK/索引，且无 incoming FK 依赖 | 失败时恢复原表和原元数据 |

目标：FK 约束新增/修改时，一次性完成“约束写入 + 目标引用校验 + 持久化字段落盘”。删除列/删表时，先做 incoming FK 保护校验；未来若支持 CASCADE，则改成先算影响闭包，再执行，再提交。

## 3. DML 入口

| 函数 | 输入 | 输出 | 前置条件 | 失败回滚 |
|---|---|---|---|---|
| `validateOutgoingForeignKeys()` | 候选表数据、数据库名、目标 schema | `bool + error` | 当前行集已完成基础类型和唯一性检查 | 失败时不写表、不写 row id、不写索引 |
| `validateIncomingForeignKeys()` | 候选表数据、数据库名、目标表名 | `bool + error` | 当前表已完成局部约束检查 | 失败时不提交父表变更 |
| `validateChangedRowsAgainstUniqueIndexes()` | 候选表数据、候选 row id、变更行索引 | `bool + error` | 只在 `TableDat` 路径运行 | 失败时不写 row id sidecar 和索引 |
| `insertRows()` | 数据库名、表名、候选 row、目标 schema | `TaskResult` / `TableDmlResult` | 当前表结构与 schema 一致 | 失败时恢复原表、原 row id、原索引 |
| `updateRows()` | 数据库名、表名、候选 row、目标 schema | `TaskResult` / `TableDmlResult` | 更新列存在，候选值可通过类型检查 | 失败时恢复原表、原 row id、原索引 |
| `deleteRows()` | 数据库名、表名、条件集、目标 schema | `TaskResult` / `TableDmlResult` | 删除目标行不存在 incoming FK 阻断 | 失败时恢复原表、原 row id、原索引 |

目标：当前 FK 主要是“阻止式校验”。未来 CASCADE 要把这些函数收成“先算受影响行，再生成子表候选变更，再统一校验，再统一提交”。如果动作是 `SET NULL` / `SET DEFAULT`，要在这些函数里直接生成新的 candidate row，而不是散在外层调用者里。

## 3.1 递归式删除闭包

| 函数 | 输入 | 输出 | 前置条件 | 失败回滚 |
|---|---|---|---|---|
| `collectForeignKeyDependents()` | parent row、parent table、数据库中的 FK 图 | 依赖集合 / 影响集合 | FK 图可遍历，节点可定位 | 失败时不修改任何表数据 |
| `planForeignKeyCascade()` | 依赖集合、动作类型 | 依赖顺序计划 | `visited` 集合可用，能处理环 | 失败时不生成提交计划 |
| `applyForeignKeyCascade()` | 执行计划、候选 row 修改 / 删除结果 | `TaskResult` / `bool + error` | 计划已按拓扑顺序生成 | 失败时回滚到闭包执行前的所有状态 |

遍历方式：有向依赖图，不是无向图。父表指向引用它的子表，单跳函数只处理一条边，闭包函数把多跳边串起来；遇到环时必须靠 `visited` 截断。

规则：
- `CASCADE` 按拓扑顺序向下传播，先直接子表，再孙表，再更深层。
- `NO ACTION` 在闭包计算阶段就要能断言失败，而不是只检查直接引用。
- `SET NULL` / `SET DEFAULT` 也要沿闭包传播，但每层都要重新跑局部校验。

如果不加这一层，当前计划会把 FK 行为收口成“单层阻止式校验”，实现上会遗漏递归删除/递归更新的语义。

## 4. 完成标准

| 标准项 | 说明 |
|---|---|
| 约束模型 | 已有动作字段，且能稳定序列化 / 反序列化 |
| DDL 侧 | 可以创建、修改、删除带动作的 FK，并且失败可回滚 |
| DML 侧 | 可以对 child / parent 的插入、更新、删除按动作返回确定结果 |
| 测试覆盖 | 能覆盖 `CASCADE`、`SET NULL`、`SET DEFAULT`、`NO ACTION` 的主路径和失败回滚路径 |

