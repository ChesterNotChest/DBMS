# Qt 前端重构计划

## 1. 目标

本计划用于把前端重构为更接近 VS Code 数据库扩展的形态，并严格收口：

- 每个前端模块负责什么
- 每个前端动作实际调取哪一类 DDL / DML / 元数据 SQL
- 哪些能力首轮必须图形化
- 哪些能力必须回退到 SQL 编辑器

本计划不扩张后端能力，只复用当前正式能力：

- `service::SqlDispatcher`
- `database_service`
- `table_service`
- `tuple_service`

前端的统一原则是：

- 能收口成稳定 GUI 动作的，前端直接生成标准 SQL
- 前端无法表单化表达的语义，只保留 SQL 文本输入入口；实际可执行范围以当前后端已支持语法为准

## 2. 统一确认机制

参考 `vscode-database-client` 后，首轮前端确认机制固定为混合方案：

- 数据表格编辑优先使用“草稿态 + `√` 提交 + `×` 取消”
- 结构新增与复杂结构编辑允许使用页签内按钮触发的编辑面板或对话框
- 全局破坏性删除保留额外确认动作

统一交互规则如下：

1. 数据新增
- 点 `Insert Row`
- 在表格中生成一行本地草稿行
- 用户填写后点 `√`
- 前端生成 `INSERT`
- 成功后重新查询当前表

2. 数据修改
- 单元格改动后，该行进入 dirty 状态
- 该行操作列显示 `√ / ×`
- 点 `√` 时生成单行 `UPDATE`
- 点 `×` 时回滚到原始值

3. 数据删除
- 选中一行或多行后，不立即删
- 工具栏进入“待删除”状态
- 再点一次确认按钮或行内 `√` 才真正执行 `DELETE`
- 默认不使用模态弹窗

4. 结构修改
- `ADD / MODIFY COLUMN` 使用列编辑面板或对话框
- `ADD CONSTRAINT` 使用约束编辑面板或对话框
- `CREATE INDEX` 使用索引编辑面板或对话框
- `DROP COLUMN / DROP CONSTRAINT / DROP INDEX` 使用待删除状态 + 行内 `√ / ×`
- `CREATE TABLE` 使用独立设计页顶部 `Save / Cancel`

只有下面两类高风险动作允许保留额外确认：

- `DROP DATABASE`
- `DROP TABLE`

原因：

- 这两类是全局破坏性动作
- 只靠行内 `√` 风险偏高
- 可以保留轻量确认层，但不强制模态弹窗实现

---

## 阶段零：Qt 运行时与界面编排模块

### 0. 常量增量

需要增加：

- `kFrontendPanelObjectTree`
- `kFrontendCardDataGrid`
- `kFrontendCardStructure`
- `kMaxOpenOverlayCount`
- `kDefaultBrowseLimit`

### 1. 边界划定

本阶段负责 Qt 主窗口、卡片布局、面板装配、统一事件分发，不负责具体 DDL / DML 页面逻辑。

涉及模块：

- `mainwindow.cpp`
- `mainwindow.h`
- `mainwindow.ui`
- `display/object_tree_panel.h`
- `display/object_tree_panel.cpp`
- `display/result_panel.h`
- `display/result_panel.cpp`
- `display/structure_panel.h`
- `display/structure_panel.cpp`
- `display/editor_panel.h`
- `display/editor_panel.cpp`
- `display/overlay_host.h`
- `display/overlay_host.cpp`
- `display/frontend_context.h`
- `display/frontend_context.cpp`

本阶段固定技术形态为：

- Qt 主程序继续作为唯一 UI 宿主
- 前端重构仍然使用 Qt 原生界面体系，不引入独立 Web 前端运行时
- 前端不得直接触达 repo
- 所有 SQL 执行都必须经由 `mainwindow` 统一下发到正式执行入口

现有 Qt 面板的主次关系固定为：

- `display/editor_panel.*` 继续作为 SQL 编辑器卡片
- `display/result_panel.*` 演进为数据页主卡片
- `display/structure_panel.*` 演进为结构页主卡片
- 新增 `display/object_tree_panel.*` 作为左侧对象树主面板

本阶段只允许定义三类主卡片：

- 数据页卡片
- 结构页卡片
- SQL 编辑器卡片

对象树不作为主卡片，而是固定左侧导航面板。

模块到文件的正式映射固定为：

- 左侧对象树面板
  - `display/object_tree_panel.h`
  - `display/object_tree_panel.cpp`
- 数据页卡片
  - `display/result_panel.h`
  - `display/result_panel.cpp`
- 结构页卡片
  - `display/structure_panel.h`
  - `display/structure_panel.cpp`
- SQL 编辑器卡片
  - `display/editor_panel.h`
  - `display/editor_panel.cpp`
- 浮层宿主
  - `display/overlay_host.h`
  - `display/overlay_host.cpp`
- 前端运行时上下文
  - `display/frontend_context.h`
  - `display/frontend_context.cpp`

`FrontendContext` 的正式结构定义固定为：

```cpp
enum class FrontendRoute {
    ObjectTree,
    DataGrid,
    Structure,
    SqlEditor
};

enum class RefreshPolicy {
    None,
    RefreshTree,
    RefreshGrid,
    RefreshStructure,
    ByRule
};

enum class RefreshAction {
    RefreshTree,
    RefreshGrid,
    RefreshStructure
};

QString toString(FrontendRoute route);
QString toString(RefreshPolicy policy);
QString toString(RefreshAction action);

bool parseFrontendRoute(const QString &text, FrontendRoute *route);
bool parseRefreshPolicy(const QString &text, RefreshPolicy *policy);
bool parseRefreshAction(const QString &text, RefreshAction *action);

struct FrontendContext {
    FrontendRoute currentCardRoute;
    QString currentDatabase;
    QString currentTable;
    int currentLimit;
};
```

序列化/反序列化规则固定为：

- Qt 运行时内部一律使用 `FrontendRoute / RefreshPolicy / RefreshAction` 强类型枚举
- 只有在日志输出、配置落盘、调试文本展示时，才允许通过 `toString(...)` 转成稳定字符串
- 从外部文本恢复时，必须统一走 `parseFrontendRoute / parseRefreshPolicy / parseRefreshAction`
- 遇到未知字符串时，必须判定为错误，不允许静默回退到默认值

### 1A. 统一职责边界

本计划所有模块的职责固定为：

- `MainWindow`
  - 只负责统一调度与顶层布局
  - 只允许发起 `dispatchSqlAction(...)`
  - 不直接拼接 SQL，不直接操作 repo/service
- `ObjectTreePanel`
  - 只负责左侧对象树浏览与节点动作发起
  - 不直接执行 SQL
  - 节点点击只产生 `FrontendAction`
- `ResultPanel`
  - 只负责数据页展示与 `GridViewState` 状态迁移
  - 不直接读 repo
  - 不直接管理对象树
- `StructurePanel`
  - 只负责结构页展示与结构编辑器调度
  - 不直接读 repo
  - 不直接执行 `CREATE / DROP / ALTER`，只发起 `FrontendAction`
- `EditorPanel`
  - 只负责 SQL 文本输入、历史与执行按钮
  - 不解释复杂语义
- `OverlayHost`
  - 只负责承载对话框/浮层
  - 不参与 SQL 生成与结果刷新决策

### 1B. 统一状态机

- 数据页状态固定为：
  - `Idle`
  - `DraftInsert`
  - `DirtyUpdate`
  - `PendingDelete`
- 结构页状态固定为：
  - `Idle`
  - `EditingColumn`
  - `EditingConstraint`
  - `EditingIndex`
  - `PendingDelete`
- 任一时刻只允许一个主卡片处于激活态。
- 任一时刻最多只允许一个编辑弹窗或编辑面板处于前景态。
- 成功执行后，必须按 `FrontendActionResult.refreshActions` 刷新指定模块并回写 `FrontendContext`。
- 失败时不得清空当前卡片状态，不得把失败结果静默吞掉。

首轮不允许：

- 多窗口前端实例共享同一张状态表
- 多个同时打开的模态对话框
- 旁路调用 repo 或 service 绕过主窗口分发

### 2. 精确到每个函数的输入输出的数据流收口计划

#### 2.1 `bootstrapQtFrontend()`

输入：

- Qt 主窗口初始化时机

输出：

- 主布局已装配
- 左侧对象树与三个主卡片已创建

数据流：

```text
bootstrapQtFrontend()
-> create ObjectTreePanel
-> create ResultPanel
-> create StructurePanel
-> create EditorPanel
-> create OverlayHost
-> composeMainWindowLayout()
-> activate default card
```

#### 2.2 `composeMainWindowLayout()`

输入：

- 已创建的 panels

输出：

- 主窗口布局稳定可用

数据流：

```text
composeMainWindowLayout()
-> mount ObjectTreePanel to left area
-> mount card host to center area
-> register ResultPanel / StructurePanel / EditorPanel into card host
-> mount OverlayHost above center card host
```

#### 2.3 `dispatchSqlAction(action)`

输入：

- `action.route: FrontendRoute::ObjectTree | FrontendRoute::DataGrid | FrontendRoute::Structure | FrontendRoute::SqlEditor`
- `action.sqlText: QString`
- `action.targetDatabase: QString`
- `action.targetTable: QString`
- `action.refreshPolicy: RefreshPolicy::None | RefreshPolicy::RefreshTree | RefreshPolicy::RefreshGrid | RefreshPolicy::RefreshStructure | RefreshPolicy::ByRule`

输出：

- `FrontendActionResult`
  - `success`
  - `message`
  - `selectColumns`
  - `selectRows`
  - `affectedRows`
  - `refreshActions`
  - `currentDatabase`
  - `currentTable`

`FrontendAction` 的字段名固定为：

```text
route
sqlText
targetDatabase
targetTable
refreshPolicy
```

正式结构定义固定为：

```cpp
struct FrontendAction {
    FrontendRoute route;
    QString sqlText;
    QString targetDatabase;
    QString targetTable;
    RefreshPolicy refreshPolicy;
};
```

`FrontendActionResult` 的字段名固定为：

```text
success
message
errorCode
selectColumns
selectRows
affectedRows
refreshActions
currentDatabase
currentTable
```

正式结构定义固定为：

```cpp
struct FrontendActionResult {
    bool success;
    QString message;
    QString errorCode;
    QStringList selectColumns;
    QList<QVariantMap> selectRows;
    int affectedRows;
    QList<RefreshAction> refreshActions;
    QString currentDatabase;
    QString currentTable;
};
```

数据流：

```text
dispatchSqlAction(action)
-> validate action fields
-> executeSql(action.sqlText)
-> normalizeActionResult(...)
-> resolveRefreshActions(action, execResult)
-> return FrontendActionResult
```

#### 2.4 `normalizeActionResult(execResult)`

输入：

- `SqlExecResult`

输出：

- `FrontendActionResult`

数据流：

```text
normalizeActionResult(execResult)
-> map success / error / selectColumns / selectRows / affectedRows
-> map backend error text without rewriting semantics
-> produce panel-safe result payload
```

#### 2.5 `resolveRefreshActions(action, execResult)`

输入：

- `action`
- `execResult`

输出：

- `refreshActions: QList<RefreshAction>`

数据流：

```text
resolveRefreshActions(...)
-> if action.refreshPolicy is explicit, honor explicit route
-> else apply module-wide refresh rules
-> return refreshTree/refreshGrid/refreshStructure action list
```

#### 2.6 `storeActiveCardContext(route, payload)`

输入补充约束：

- `route` 必须是 `FrontendRoute`
- 若后续需要落盘、日志输出或调试展示，统一使用 `toString(route)`
- 若从外部文本恢复当前卡片路由，统一使用 `parseFrontendRoute(...)`，遇到未知值直接报错

输入：

- `route`
- `payload`

输出：

- 当前活跃卡片上下文已更新

数据流：

```text
storeActiveCardContext(route, payload)
-> store currentCardRoute
-> store currentDatabase / currentTable / currentLimit
-> make later refreshCurrent* functions reusable
```

### 3. 每个关键函数内部具体行为的收口计划

#### 3.1 `bootstrapQtFrontend()`

- 首轮固定只装配一套主窗口布局。
- 不允许对象树、数据页、结构页分别自持第二套主窗口级分发器。
- 若任一核心面板创建失败，必须在 Qt 层报错，不进入半初始化状态。

#### 3.2 `composeMainWindowLayout()`

- 中央区域固定只承载一个主卡片宿主。
- 对象树固定在左侧，不进入中央卡片切换序列。
- 浮层宿主固定覆盖在中央卡片区，不允许每个面板各自管理第二套弹层根节点。

#### 3.3 `dispatchSqlAction(action)`

- `action.route` 必须使用 `FrontendRoute`，不允许裸 `QString` 承载路由。
- `action.refreshPolicy` 必须使用 `RefreshPolicy`，不允许裸 `QString` 承载刷新策略。
- `action.refreshPolicy = RefreshPolicy::ByRule` 时，必须统一走模块间刷新规则，不允许各卡片私自决定。
- `action.sqlText` 不能为空。
- 若后端执行失败，必须原样保留后端错误文本，不允许前端重写为模糊提示。

#### 3.4 `resolveRefreshActions(...)`

- 首轮只允许返回 `RefreshAction::RefreshTree / RefreshAction::RefreshGrid / RefreshAction::RefreshStructure` 三类显式刷新动作。
- 不允许返回页签级、分页级、子面板级刷新动作。

#### 3.5 `storeActiveCardContext(...)`

- 任一时刻只允许一个主卡片处于激活态。
- 任一时刻最多允许一个编辑弹窗或编辑面板处于前景态。
- 当前卡片上下文必须成为后续 `Refresh` 的唯一来源，不允许刷新按钮再额外读取不一致状态。

### 4. 测试计划

#### 4.1 `bootstrapQtFrontend()`

- `test_bootstrapQtFrontendCreatesAllPrimaryPanels`
- `test_bootstrapQtFrontendActivatesDefaultCard`

#### 4.2 `composeMainWindowLayout()`

- `test_composeMainWindowLayoutMountsObjectTreeOnLeft`
- `test_composeMainWindowLayoutUsesSingleCentralCardHost`

#### 4.3 `dispatchSqlAction(action)`

- `test_dispatchSqlActionRoutesThroughUnifiedExecuteSql`
- `test_dispatchSqlActionNormalizesBackendError`
- `test_dispatchSqlActionReturnsRefreshActions`
- `test_dispatchSqlActionRejectsUnknownRoute`
- `test_dispatchSqlActionRejectsUnknownRefreshPolicy`

#### 4.4 `resolveRefreshActions(...)`

- `test_resolveRefreshActionsRespectsExplicitPolicy`
- `test_resolveRefreshActionsFallsBackToRuleBasedPolicy`
- `test_resolveRefreshActionsDoesNotReturnPerTabRefresh`

#### 4.5 `storeActiveCardContext(...)`

- `test_storeActiveCardContextStoresCurrentDatabaseAndTable`
- `test_storeActiveCardContextSupportsSingleActiveCardOnly`

#### 4.6 模块职责边界

- `test_objectTreePanelOnlyEmitsActions`
- `test_resultPanelOwnsGridStateAndDoesNotTouchRepository`
- `test_structurePanelUsesEditorFormsAndDoesNotExecuteSqlDirectly`
- `test_editorPanelSubmitsSqlOnlyThroughMainWindow`
- `test_overlayHostOnlyHostsDialogsAndDoesNotTriggerExecution`

---

## 阶段一：对象树模块

### 1. 边界划定

本阶段只负责左侧对象树，不负责数据表格编辑。

涉及模块：

- `display/object_tree_panel.h`
- `display/object_tree_panel.cpp`
- `display/frontend_context.h`
- `display/frontend_context.cpp`
- `mainwindow.cpp`
- `mainwindow.h`

本阶段只允许调取下面这些 SQL / 动作：

- `SHOW DATABASES;`
- `USE db_name;`
- `SHOW TABLES;`
- `DESC table_name;`
- `SHOW CREATE TABLE table_name;`
- `SELECT * FROM table_name LIMIT n;`

本阶段对象名规则固定为：

- 自动生成 SQL 时，`databaseName / tableName / columnName` 只允许使用当前解析器稳定支持的未加引号标识符
- 第一版合法形式固定为正则：`[A-Za-z_][A-Za-z0-9_]*`
- 第一版不承诺 quoted identifiers，不使用反引号、双引号或其他转义形式自动生成 SQL
- 若现有对象名不满足该规则，前端必须拒绝拼接自动 SQL，并提示用户改走手写 SQL 或先扩展后端解析器

本阶段不负责：

- `INSERT`
- `UPDATE`
- `DELETE`
- `ALTER TABLE`
- `CREATE INDEX`
- `DROP INDEX`

### 2. 精确到每个函数的输入输出的数据流收口计划

#### 2.0 `validateSqlObjectName(name, kind)`

输入：
- `name: QString`
- `kind: database | table | column`

输出：
- `bool`
- 失败时返回稳定错误文案

数据流：

```text
validateSqlObjectName(name, kind)
-> trim name
-> reject empty name
-> match [A-Za-z_][A-Za-z0-9_]*
-> if not match, return invalid identifier error
-> return true
```

#### 2.1 `refreshDatabaseTree()`

输入：

- 无

输出：

- 对象树根节点数据库列表

数据流：

```text
refreshDatabaseTree()
-> executeSql("SHOW DATABASES;")
-> SqlExecResult.selectResult
-> 解析数据库名列表
-> 重建数据库根节点
```

该函数同时作为对象树工具栏 `Refresh` 的唯一入口。

#### 2.2 `openDatabaseNode(databaseName)`

输入：

- `databaseName: QString`

输出：

- 当前数据库切换成功
- 该数据库下表列表节点

数据流：

```text
openDatabaseNode(databaseName)
-> validateSqlObjectName(databaseName, database)
-> executeSql("USE databaseName;")
-> 若成功
-> executeSql("SHOW TABLES;")
-> SqlExecResult.selectResult
-> 解析表名列表
-> 展开该数据库节点
```

#### 2.3 `refreshTableList(databaseName)`

输入：

- `databaseName: QString`

输出：

- 指定数据库的表列表

数据流：

```text
refreshTableList(databaseName)
-> validateSqlObjectName(databaseName, database)
-> 若当前数据库不是 databaseName，先 executeSql("USE databaseName;")
-> executeSql("SHOW TABLES;")
-> SqlExecResult.selectResult
-> 更新对象树中的表节点列表
```

#### 2.4 `openTableData(tableName, limit)`

输入：

- `tableName: QString`
- `limit: int`

输出：

- 数据表格页的查询结果

数据流：

```text
openTableData(tableName, limit)
-> validateSqlObjectName(tableName, table)
-> executeSql("SELECT * FROM tableName LIMIT limit;")
-> SqlExecResult.selectResult
-> 打开或刷新 Data Grid 页面
```

#### 2.4A `refreshCurrentTableData(tableName, limit)`

输入：

- `tableName: QString`
- `limit: int`

输出：

- 当前数据页重新加载后的查询结果

数据流：

```text
refreshCurrentTableData(tableName, limit)
-> validateSqlObjectName(tableName, table)
-> executeSql("SELECT * FROM tableName LIMIT limit;")
-> SqlExecResult.selectResult
-> 刷新当前 Data Grid 卡片
```

#### 2.5 `openTableStructure(tableName)`

输入：

- `tableName: QString`

输出：

- 结构页所需的列定义和建表 SQL

数据流：

```text
openTableStructure(tableName)
-> validateSqlObjectName(tableName, table)
-> executeSql("DESC tableName;")
-> executeSql("SHOW CREATE TABLE tableName;")
-> 合并结果
-> 打开或刷新结构页
```

#### 2.5A `refreshCurrentTableStructure(tableName)`

输入：

- `tableName: QString`

输出：

- 当前结构页重新加载后的列定义与建表 SQL

数据流：

```text
refreshCurrentTableStructure(tableName)
-> validateSqlObjectName(tableName, table)
-> executeSql("DESC tableName;")
-> executeSql("SHOW CREATE TABLE tableName;")
-> 合并结果
-> 刷新当前结构页卡片
```

### 3. 每个关键函数内部具体行为的收口计划

#### 3.1 `refreshDatabaseTree()`

- 若 `SHOW DATABASES` 失败，不得保留半更新树状态。
- 应保留旧树，弹出错误提示。
- 不得在失败时清空全部节点。
- 对象树首轮只保留一个显式 `Refresh` 按钮，不再为数据库节点单独增加第二套刷新按钮。
- 对象树展示到的数据库名若不满足 `[A-Za-z_][A-Za-z0-9_]*`，允许显示，但必须标记为“不可自动操作”状态。

#### 3.2 `openDatabaseNode(databaseName)`

- `USE` 失败时不得继续执行 `SHOW TABLES`。
- `SHOW TABLES` 失败时，当前数据库切换结果保留，但表节点不更新。
- 若重复点击当前已激活数据库，可直接刷新表列表，不重复弹提示。
- 若 `validateSqlObjectName(databaseName, database)` 失败，必须在执行前直接拒绝，不得向后端发送拼接 SQL。

#### 3.3 `openTableData(tableName, limit)`

- `limit` 必须由前端配置给出，首轮不允许空值。
- 首轮默认值固定为 `100`。
- 表节点点击行为固定为“打开数据页”，不是打开结构页。
- 若 `validateSqlObjectName(tableName, table)` 失败，必须直接拒绝自动浏览，并提示用户改走 SQL 编辑器。

#### 3.3A `refreshCurrentTableData(tableName, limit)`

- 若当前表名不满足合法标识符规则，`Refresh` 必须进入拒绝执行路径。

- 数据页卡片必须提供显式 `Refresh` 按钮。
- 该按钮只重跑当前表的浏览 SQL，不改变当前卡片所指向的表。
- 首轮刷新固定重用当前 `limit`，不额外引入分页级刷新按钮。
- 若刷新失败，保留旧表格内容并展示错误，不清空当前卡片。

#### 3.4 `openTableStructure(tableName)`

- 若 `validateSqlObjectName(tableName, table)` 失败，必须拒绝自动打开结构页。

- `DESC` 成功但 `SHOW CREATE TABLE` 失败时，仍允许展示部分结构。
- `SHOW CREATE TABLE` 成功但 `DESC` 失败时，也允许展示源码视图。
- 不允许因为一个接口失败而全部空白。

#### 3.4A `refreshCurrentTableStructure(tableName)`

- 若当前表名不满足合法标识符规则，结构页 `Refresh` 必须拒绝执行。

- 结构页卡片必须提供显式 `Refresh` 按钮。
- 该按钮统一刷新当前结构卡片，不再为 `Column / Foreign Key / Index / Check` 分别增加独立刷新按钮。
- 若 `DESC` 与 `SHOW CREATE TABLE` 其中之一失败，仍按部分成功策略保留另一部分结果。
- 若两者都失败，保留旧结构卡片内容并展示错误。

#### 3.5 对象树右键危险动作确认

- `DROP DATABASE`
  - 不直接执行
  - 先进入待确认态
  - 二次点击确认后执行：

```sql
DROP DATABASE db_name;
```

- `DROP TABLE`
  - 不直接执行
  - 先进入待确认态
  - 二次点击确认后执行：

```sql
DROP TABLE table_name;
```

- 这里允许保留轻量确认 UI
- 但不要求必须使用模态弹窗

### 4. 测试计划

#### 4.1 `refreshDatabaseTree()`

- `test_refreshDatabaseTreeLoadsDatabases`
- `test_refreshDatabaseTreePreservesOldTreeOnFailure`
- `test_refreshDatabaseTreeIsBoundToTreeRefreshButton`
- `test_refreshDatabaseTreeMarksInvalidDatabaseNamesAsNonExecutable`

#### 4.1A `validateSqlObjectName(name, kind)`

- `test_validateSqlObjectNameAcceptsAsciiIdentifier`
- `test_validateSqlObjectNameRejectsEmptyName`
- `test_validateSqlObjectNameRejectsDashSpaceAndQuotedName`
- `test_validateSqlObjectNameRejectsLeadingDigit`

#### 4.2 `openDatabaseNode(databaseName)`

- `test_openDatabaseNodeExecutesUseThenShowTables`
- `test_openDatabaseNodeStopsWhenUseFails`
- `test_openDatabaseNodeRefreshesOnlySelectedDatabase`
- `test_openDatabaseNodeRejectsInvalidDatabaseNameBeforeSql`

#### 4.3 `refreshTableList(databaseName)`

- `test_refreshTableListUsesShowTables`
- `test_refreshTableListSwitchesDatabaseWhenNeeded`

#### 4.4 `openTableData(tableName, limit)`

- `test_openTableDataBuildsSelectStarLimitSql`
- `test_openTableDataPassesSelectResultToGrid`
- `test_openTableDataRejectsInvalidTableNameBeforeSql`

#### 4.4A `refreshCurrentTableData(tableName, limit)`

- `test_refreshCurrentTableDataReusesCurrentLimit`
- `test_refreshCurrentTableDataKeepsCurrentTableContext`
- `test_refreshCurrentTableDataKeepsOldGridOnFailure`
- `test_refreshCurrentTableDataRejectsInvalidTableNameBeforeSql`

#### 4.5 `openTableStructure(tableName)`

- `test_openTableStructureCallsDescAndShowCreateTable`
- `test_openTableStructureAllowsPartialSuccess`
- `test_openTableStructureRejectsInvalidTableNameBeforeSql`

#### 4.5A `refreshCurrentTableStructure(tableName)`

- `test_refreshCurrentTableStructureCallsDescAndShowCreateTable`
- `test_refreshCurrentTableStructureKeepsOldCardOnFailure`
- `test_refreshCurrentTableStructureDoesNotIntroducePerTabRefresh`
- `test_refreshCurrentTableStructureRejectsInvalidTableNameBeforeSql`

---

## 阶段二：SQL 编辑器模块

### 0. 常量增量

需要增加：

- `kDefaultBrowseLimit`
- `kAllowMultiStatementExecution`

### 1. 边界划定

本阶段负责 SQL 编辑器，不负责图形化表单。

涉及模块：

- `display/editor_panel.h`
- `display/editor_panel.cpp`
- `display/frontend_context.h`
- `display/frontend_context.cpp`
- SQL 执行日志区域

本阶段只作为 SQL 文本输入入口，不承诺扩张当前后端能力。

本阶段不额外承诺：

- 子查询外部执行
- 复杂谓词图形化编辑
- 超出当前 parser / `service::SqlDispatcher` 已支持范围的 SQL 语法
- `MODIFY CONSTRAINT`
- 批量初始化脚本

### 2. 精确到每个函数的输入输出的数据流收口计划

#### 2.1 `executeEditorSql(sqlText)`

输入：

- `sqlText: QString`

输出：

- 执行结果
- 日志文本
- 若最后一条是 `SELECT`，则更新结果表格

数据流：

```text
executeEditorSql(sqlText)
-> executeSql(sqlText)
-> SqlExecResult
-> 若是 SELECT，更新 Data Grid
-> 若是 DDL/DML，更新日志与必要的对象树
```

#### 2.2 `executeTemplateSql(templateType, targetName)`

输入：

- `templateType: enum`
- `targetName: QString`

输出：

- SQL 编辑器中插入模板 SQL

模板 SQL 必须收口为：

- 浏览表数据：
  - `SELECT * FROM table_name LIMIT 100;`
- 删除模板：
  - `DELETE FROM table_name WHERE key = '<value>';`
- 更新模板：
  - `UPDATE table_name SET col = '<new_value>' WHERE key = '<key_value>';`
- 插入模板：
  - `INSERT INTO table_name (col1, col2) VALUES ('<value1>', '<value2>');`

### 3. 每个关键函数内部具体行为的收口计划

#### 3.1 `executeEditorSql(sqlText)`

- 多语句执行必须顺序执行。
- 首错停止，不做自动回滚承诺。
- 若最后一条语句不是 `SELECT`，Data Grid 不自动切空，只保留上次查询结果。
- 若执行的是 DDL，成功后允许通知对象树刷新。

#### 3.2 `executeTemplateSql(templateType, targetName)`

- 只负责生成模板，不负责自动执行。
- 模板必须和当前后端真实能力一致。
- 模板中的 `<...>` 占位符必须要求用户手工替换；未替换模板不得一键直接执行。
- 不允许插入未实现语法，例如：
  - `JOIN`
  - `GROUP BY`
  - 复杂 `ORDER BY` 设计器模板

### 4. 测试计划

#### 4.1 `executeEditorSql(sqlText)`

- `test_executeEditorSqlRunsSingleStatement`
- `test_executeEditorSqlRunsMultipleStatementsInOrder`
- `test_executeEditorSqlStopsOnFirstError`
- `test_executeEditorSqlOnlyUpdatesGridForSelectTail`

#### 4.2 `executeTemplateSql(templateType, targetName)`

- `test_executeTemplateSqlBuildsSelectTemplate`
- `test_executeTemplateSqlBuildsInsertTemplate`
- `test_executeTemplateSqlBuildsUpdateTemplate`
- `test_executeTemplateSqlBuildsDeleteTemplate`

---

## 阶段三：数据表格模块

### 0. 常量增量

需要增加：

- 复用 `kDefaultBrowseLimit`
- `kAllowInlineUpdate`
- `kAllowInlineDelete`
- `kAllowInlineInsert`
- `kGridNullDisplayText`
- `kGridDirtyBadgeText`

### 1. 边界划定

本阶段负责类 Excel 的表格视图。

涉及模块：

- `display/result_panel.h`
- `display/result_panel.cpp`
- `display/frontend_context.h`
- `display/frontend_context.cpp`
- 行编辑器
- 插入草稿行控制器
- 删除确认动作

本阶段只收口单表数据操作，不负责复杂查询设计器。

本阶段允许调取的 SQL：

- `SELECT * FROM table LIMIT n;`
- `INSERT INTO table (...) VALUES (...);`
- `UPDATE table SET ... WHERE ...;`
- `DELETE FROM table WHERE ...;`

首轮 GUI 动作边界：

- 插入：支持
- 单行修改：支持
- 单行删除：支持
- 多行删除：支持
- 批量修改：不做专门 GUI
- 复杂 `WHERE`：不做专门 GUI

### 2. 精确到每个函数的输入输出的数据流收口计划

#### 2.1 `loadTableRows(tableName, limit)`

输入：

- `tableName: QString`
- `limit: int`

输出：

- 表格展示数据

数据流：

```text
loadTableRows(tableName, limit)
-> executeSql("SELECT * FROM tableName LIMIT limit;")
-> SqlExecResult.selectResult
-> buildGridViewState(tableName, selectResult)
-> 转成 GridModel
```

#### 2.1A `buildGridViewState(tableName, selectResult)`

输入：

- `tableName: QString`
- `selectResult`

输出：

- `GridViewState`
  - `tableName`
  - `columnNames`
  - `rows`
  - `keyColumns`
  - `draftInsertRow`
    空 `QVariantMap` 表示当前不存在插入草稿行
  - `dirtyRowIds`
  - `pendingDeleteRowIds`

正式结构定义固定为：

```cpp
struct GridViewState {
    QString tableName;
    QStringList columnNames;
    QList<QVariantMap> rows;
    QStringList keyColumns;
    QVariantMap draftInsertRow;
    QSet<int> dirtyRowIds;
    QSet<int> pendingDeleteRowIds;
};
```

数据流：

```text
buildGridViewState(...)
-> resolveGridKeyColumns(tableName)
-> normalize selectResult rows
-> initialize draft / dirty / pending delete sets empty
-> return GridViewState
```

#### 2.1B `resolveGridKeyColumns(tableName)`

输入：

- `tableName: QString`

输出：

- `keyColumns: QStringList`

数据流：

```text
resolveGridKeyColumns(tableName)
-> read current structure metadata snapshot
-> prefer primary key columns
-> else prefer unique constraint columns
-> else prefer single-column unique index
-> else return empty list
```

#### 2.2 `insertGridRow(tableName, rowMap)`

输入：

- `tableName: QString`
- `rowMap: QMap<QString, QString>`

输出：

- 插入成功/失败
- 成功后刷新当前表格

数据流：

```text
insertGridRow(tableName, rowMap)
-> buildInsertSql(tableName, rowMap)
-> executeSql(insertSql)
-> 若成功，reload current table
```

#### 2.2A `beginInsertDraftRow(tableName)`

输入：

- `tableName: QString`

输出：

- 表格中新增一行本地草稿行

数据流：

```text
beginInsertDraftRow(tableName)
-> 根据当前列定义生成空草稿行
-> 标记 rowState = DraftInsert
-> 该行显示 `√ / ×`
```

#### 2.2B `normalizeGridCellValue(rawValue)`

输入：

- `rawValue`

输出：

- `GridCellValue`
  - `displayText`
  - `actualValue`
  - `isNull`

正式结构定义固定为：

```cpp
struct GridCellValue {
    QString displayText;
    QVariant actualValue;
    bool isNull;
};
```

数据流：

```text
normalizeGridCellValue(rawValue)
-> if backend value is NULL, map to isNull = true
-> else keep original scalar text
-> build stable display payload
```

#### 2.3 `updateGridRow(tableName, originalRow, editedRow, keyColumns)`

输入：

- `tableName: QString`
- `originalRow`
- `editedRow`
- `keyColumns: QStringList`

输出：

- 更新成功/失败
- 成功后刷新当前表格或局部更新当前行

数据流：

```text
updateGridRow(...)
-> diff editedRow vs originalRow
-> buildUpdateSql(tableName, changedAssignments, whereByKeyColumns)
-> executeSql(updateSql)
-> 若成功，刷新当前表格
```

#### 2.3A `markGridRowDirty(rowId, editedCellValue)`

输入：

- `rowId`
- 编辑后的单元格值

输出：

- 该行进入 dirty 状态

数据流：

```text
markGridRowDirty(...)
-> 记录 originalRow / editedRow 差异
-> 标记 rowState = DirtyUpdate
-> 该行显示 `√ / ×`
```

#### 2.4 `deleteGridRows(tableName, rows, keyColumns)`

输入：

- `tableName: QString`
- `rows: QList<Row>`
- `keyColumns: QStringList`

输出：

- 删除成功/失败
- 成功后刷新当前表格

数据流：

```text
deleteGridRows(...)
-> 对每一行构造带键条件的 DELETE SQL
-> 顺序执行
-> 成功后刷新当前表格
```

#### 2.4A `markGridRowsPendingDelete(tableName, rows, keyColumns)`

输入：

- `tableName`
- `rows`
- `keyColumns`

输出：

- 选中行进入待删除状态

数据流：

```text
markGridRowsPendingDelete(...)
-> 标记 rowState = PendingDelete
-> 工具栏或行操作区显示确认 `√` 与取消 `×`
-> 不立即执行 SQL
```

### 3. 每个关键函数内部具体行为的收口计划

#### 3.1 `loadTableRows(tableName, limit)`

- 首轮固定只做“浏览表数据”，不做表格内自由改写查询。
- 若 `SELECT` 失败，保留旧表格，不清空到空白。
- 表格标题应同步显示当前表名与 limit。
- 数据页内部状态固定统一为 `GridViewState`，不允许草稿行、脏行、待删除行分散存到多个互不一致的局部变量。

#### 3.2 `insertGridRow(tableName, rowMap)`

- 不允许前端直接跳过 SQL 路径调 repo。
- 插入后必须以重新查询为准，不以前端本地虚拟追加行为作为最终真相。
- 若返回 FK / UNIQUE / CHECK 失败，必须原样展示后端错误。
- 首轮插入确认方式固定为：
  - 点 `Insert Row` 生成草稿行
  - 草稿行点 `√` 时才真正执行：

```sql
INSERT INTO table_name (col1, col2, ...)
VALUES ('<value1>', '<value2>', ...);
```

- 草稿行点 `×` 时直接丢弃，不发 SQL。
- 草稿行必须以当前列顺序渲染，但生成 `INSERT` 时仍按显式列名拼 SQL。
- `NULL` 与空字符串必须区分；前端不得把空输入一律改写为 `NULL`。

#### 3.3 `updateGridRow(...)`

- 首轮只允许生成单行 `UPDATE`。
- `WHERE` 条件必须由主键或唯一可定位列构造。
- 若拿不到可定位键列，必须拒绝“行内直接修改”，改为提示走 SQL 编辑器。
- 行内修改确认方式固定为：
  - 单元格改动后只进入 dirty 状态
  - 行内 `√` 才执行：

```sql
UPDATE table_name
SET col1 = value1, col2 = value2
WHERE key1 = oldValue1 AND key2 = oldValue2;
```

- 行内 `×` 时恢复 `originalRow`，不发 SQL。
- `keyColumns` 解析失败时，整行 `edit` 能力必须禁用，而不是乐观生成无键 `UPDATE`。

#### 3.4 `deleteGridRows(...)`

- 单行删除和多行删除都必须显式确认。
- 多行删除首轮允许逐条执行，不要求批量合并 SQL。
- FK 阻止删除时，必须展示后端真实错误。
- 删除确认方式固定为：
  - 首次点击删除只标记 `PendingDelete`
  - 再点 `√` 时才执行：

```sql
DELETE FROM table_name WHERE key = '<value>';
```

- 多行删除允许逐行执行多条 `DELETE`
- 点 `×` 时取消待删除态
- 首轮尽量不用模态弹窗
- 批量删除必须复用同一套 `keyColumns` 解析结果，不允许每行重新猜测键列。

### 4. 测试计划

#### 4.1 `loadTableRows(tableName, limit)`

- `test_loadTableRowsUsesSelectStarLimit`
- `test_loadTableRowsKeepsOldGridOnFailure`
- `test_buildGridViewStateInitializesEmptyDraftDirtyDeleteSets`
- `test_resolveGridKeyColumnsPrefersPrimaryKeyThenUnique`

#### 4.2 `insertGridRow(tableName, rowMap)`

- `test_insertGridRowBuildsInsertSql`
- `test_insertGridRowReloadsAfterSuccess`
- `test_insertGridRowShowsConstraintError`
- `test_beginInsertDraftRowCreatesDraftState`
- `test_normalizeGridCellValueDistinguishesNullAndEmptyString`
- `test_insertDraftRowConfirmExecutesOnlyOnCheck`
- `test_insertDraftRowCancelDropsWithoutSql`

#### 4.3 `updateGridRow(...)`

- `test_updateGridRowBuildsSingleRowUpdate`
- `test_updateGridRowRejectsWhenNoKeyColumns`
- `test_updateGridRowReloadsAfterSuccess`
- `test_markGridRowDirtyShowsPendingConfirmState`
- `test_updateGridRowConfirmExecutesOnlyOnCheck`
- `test_updateGridRowCancelRestoresOriginalRow`

#### 4.4 `deleteGridRows(...)`

- `test_deleteGridRowsBuildsDeleteSqlByKey`
- `test_deleteGridRowsSupportsMultiSelectionSequentially`
- `test_deleteGridRowsShowsForeignKeyError`
- `test_markGridRowsPendingDeleteDoesNotExecuteImmediately`
- `test_deleteGridRowsConfirmExecutesOnCheck`
- `test_deleteGridRowsCancelClearsPendingState`

---

## 阶段四：结构设计模块

### 0. 常量增量

需要增加：

- `kEnableGuiAlterTable`
- `kEnableGuiIndexManager`
- `kAllowSingleStructureOverlayEditor`
- `kColumnEditorRequiresCompleteDefinition`

### 1. 边界划定

本阶段负责“表结构设计页”，只覆盖当前真实适合图形化的 DDL。

涉及模块：

- `display/structure_panel.h`
- `display/structure_panel.cpp`
- `display/overlay_host.h`
- `display/overlay_host.cpp`
- `display/frontend_context.h`
- `display/frontend_context.cpp`
- 列定义编辑器
- 约束面板
- 索引面板

本阶段允许调取的 SQL：

- `CREATE TABLE`
- `DROP TABLE`
- `ALTER TABLE ... ADD COLUMN`
- `ALTER TABLE ... DROP COLUMN`
- `ALTER TABLE ... MODIFY COLUMN`
- `ALTER TABLE ... ADD CONSTRAINT`
- `ALTER TABLE ... DROP CONSTRAINT`
- `CREATE INDEX`
- `DROP INDEX`
- `DESC`
- `SHOW CREATE TABLE`

本阶段不要求首轮图形化覆盖：

- `MODIFY CONSTRAINT`
- 复杂 `CHECK` 表达式图形化设计器

本阶段的 UI 编辑方式固定为：

- `CREATE TABLE` 使用独立设计页
- `ADD / MODIFY COLUMN` 使用列编辑面板或对话框
- `ADD CONSTRAINT` 使用约束编辑面板或对话框
- `CREATE INDEX` 使用索引编辑面板或对话框
- `DROP COLUMN / DROP CONSTRAINT / DROP INDEX` 使用待删除状态 + 行内 `√ / ×`

### 2. 精确到每个函数的输入输出的数据流收口计划

#### 2.1 `createTableFromDesigner(tableSchemaForm)`

输入：

- 表单化的表定义

输出：

- 创建成功/失败
- 成功后刷新对象树与结构页

数据流：

```text
createTableFromDesigner(...)
-> buildCreateTableSql(...)
-> executeSql(createTableSql)
-> 成功后 refresh table list
```

#### 2.2 `dropTableFromDesigner(tableName)`

输入：

- `tableName: QString`

输出：

- 删除成功/失败

数据流：

```text
dropTableFromDesigner(tableName)
-> executeSql("DROP TABLE tableName;")
-> 成功后 refresh table list
```

#### 2.3 `addColumnFromDesigner(tableName, columnDefinition)`

输入：

- `tableName: QString`
- 完整列定义

输出：

- 新增列成功/失败

数据流：

```text
addColumnFromDesigner(...)
-> buildAlterAddColumnSql(...)
-> executeSql(sql)
-> 成功后 refresh structure
```

#### 2.3A `openAddColumnEditor(tableName)`

输入：

- `tableName: QString`

输出：

- 打开列编辑面板或对话框
- 初始化空列定义表单

数据流：

```text
openAddColumnEditor(tableName)
-> 打开列编辑面板或对话框
-> buildColumnEditorForm(tableName, null)
-> 初始化空列定义表单
-> 用户确认后调用 addColumnFromDesigner(...)
```

#### 2.4 `modifyColumnFromDesigner(tableName, columnDefinition)`

输入：

- `tableName: QString`
- 完整列定义

输出：

- 修改列成功/失败

数据流：

```text
modifyColumnFromDesigner(...)
-> buildAlterModifyColumnSql(...)
-> executeSql(sql)
-> 成功后 refresh structure
```

#### 2.4A `openModifyColumnEditor(tableName, originalDefinition)`

输入：

- `tableName`
- `originalDefinition`

输出：

- 打开列编辑面板或对话框
- 预填当前列的完整定义

数据流：

```text
openModifyColumnEditor(...)
-> 打开列编辑面板或对话框
-> buildColumnEditorForm(tableName, originalDefinition)
-> 预填当前列完整定义
-> 用户确认后调用 modifyColumnFromDesigner(...)
```

#### 2.4B `buildColumnEditorForm(tableName, originalDefinition)`

输入：

- `tableName: QString`
- `originalDefinition optional`

输出：

- `ColumnEditorForm`
  - `name`
  - `type`
  - `length`
  - `comment`
  - `defaultValue`
  - `notNull`
  - `primaryKey`
  - `unique`
  - `autoIncrement`
  - `unsigned`
  - `zerofill`

正式结构定义固定为：

```cpp
struct ColumnEditorForm {
    QString name;
    QString type;
    QString length;
    QString comment;
    QString defaultValue;
    bool notNull;
    bool primaryKey;
    bool unique;
    bool autoIncrement;
    bool unsignedFlag;
    bool zerofill;
};
```

数据流：

```text
buildColumnEditorForm(...)
-> if originalDefinition exists, copy all fields
-> else initialize empty form with safe defaults
-> return full form model
```

#### 2.5 `dropColumnFromDesigner(tableName, columnName)`

输入：

- `tableName: QString`
- `columnName: QString`

输出：

- 删除列成功/失败

数据流：

```text
dropColumnFromDesigner(...)
-> executeSql("ALTER TABLE tableName DROP COLUMN columnName;")
-> 成功后 refresh structure
```

#### 2.6 `addConstraintFromDesigner(tableName, constraintForm)`

输入：

- `tableName: QString`
- PK / UNIQUE / FK / CHECK 表单

输出：

- 新增约束成功/失败

数据流：

```text
addConstraintFromDesigner(...)
-> buildAlterAddConstraintSql(...)
-> executeSql(sql)
-> 成功后 refresh structure
```

#### 2.6A `openAddConstraintEditor(tableName, constraintKind)`

输入：

- `tableName: QString`
- `constraintKind: primary key | unique | foreign key | check`

输出：

- 打开约束编辑面板或对话框
- 初始化对应约束类型表单

数据流：

```text
openAddConstraintEditor(...)
-> 打开约束编辑面板或对话框
-> buildConstraintEditorForm(tableName, constraintKind, null)
-> 初始化约束表单
-> 用户确认后调用 addConstraintFromDesigner(...)
```

#### 2.6B `buildConstraintEditorForm(tableName, constraintKind, originalConstraint)`

输入：

- `tableName: QString`
- `constraintKind: primary key | unique | foreign key | check`
- `originalConstraint optional`

输出：

- `ConstraintEditorForm`
  - `constraintKind`
  - `constraintName`
  - `columnNames`
  - `referencedTable optional`
  - `referencedColumns optional`
  - `onDeleteAction optional`
  - `onUpdateAction optional`
  - `checkExpression optional`

正式结构定义固定为：

```cpp
struct ConstraintEditorForm {
    QString constraintKind;
    QString constraintName;
    QStringList columnNames;
    QString referencedTable;
    QStringList referencedColumns;
    QString onDeleteAction;
    QString onUpdateAction;
    QString checkExpression;
};
```

数据流：

```text
buildConstraintEditorForm(...)
-> initialize fields by constraintKind
-> if originalConstraint exists, prefill editable fields
-> return constraint form model
```

#### 2.7 `dropConstraintFromDesigner(tableName, constraintName)`

输入：

- `tableName: QString`
- `constraintName: QString`

输出：

- 删除约束成功/失败

数据流：

```text
dropConstraintFromDesigner(...)
-> executeSql("ALTER TABLE tableName DROP CONSTRAINT constraintName;")
-> 成功后 refresh structure
```

#### 2.7A `markStructureItemPendingDelete(kind, name)`

输入：

- `kind: column | constraint | index`
- `name`

输出：

- 结构项进入待删除状态

数据流：

```text
markStructureItemPendingDelete(...)
-> 标记 draftState = PendingDelete
-> 行内显示 `√ / ×`
-> 不立即执行 SQL
```

#### 2.8 `createIndexFromDesigner(tableName, indexForm)`

输入：

- `tableName: QString`
- `indexName`
- `columnNames`
- `isUnique`

输出：

- 创建索引成功/失败

数据流：

```text
createIndexFromDesigner(...)
-> buildCreateIndexSql(...)
-> executeSql(sql)
-> 成功后 refresh structure
```

#### 2.8A `openCreateIndexEditor(tableName)`

输入：

- `tableName`

输出：

- 打开索引编辑面板或对话框
- 初始化空索引定义表单

数据流：

```text
openCreateIndexEditor(tableName)
-> 打开索引编辑面板或对话框
-> buildIndexEditorForm(tableName, null)
-> 初始化索引定义表单
-> 用户确认后调用 createIndexFromDesigner(...)
```

#### 2.8B `buildIndexEditorForm(tableName, originalIndex)`

输入：

- `tableName: QString`
- `originalIndex optional`

输出：

- `IndexEditorForm`
  - `indexName`
  - `columnNames`
  - `isUnique`

正式结构定义固定为：

```cpp
struct IndexEditorForm {
    QString indexName;
    QStringList columnNames;
    bool isUnique;
};
```

数据流：

```text
buildIndexEditorForm(...)
-> initialize empty or prefilled index form
-> keep columnNames ordered
-> return index form model
```

#### 2.9 `dropIndexFromDesigner(tableName, indexName)`

输入：

- `tableName: QString`
- `indexName: QString`

输出：

- 删除索引成功/失败

数据流：

```text
dropIndexFromDesigner(...)
-> executeSql("DROP INDEX indexName ON tableName;")
-> 成功后 refresh structure
```

### 3. 每个关键函数内部具体行为的收口计划

#### 3.1 `createTableFromDesigner(...)`

- 必须允许列定义 + 基础约束一起提交。
- 若前端表单无法表达某个复杂建表语义，必须回退到 SQL 编辑器，不得静默丢字段。
- `CREATE TABLE` 允许使用独立设计页的顶部 `Save / Cancel`
- 不强制塞进行内 `√ / ×`
- 因为它本身是整页草稿态，不是单行修改态

#### 3.2 `modifyColumnFromDesigner(...)`

- 必须按当前后端规则提交“完整列定义”。
- 不允许前端假装只改一个局部属性但实际漏掉其他字段。
- 列编辑器的内部表单模型必须始终持有完整字段集，不允许只保留“被用户改过的字段”。
- 确认方式固定为：
  - 点列操作区 `edit`
  - 打开列编辑面板或对话框
  - 用户在编辑器中确认后才执行：

```sql
ALTER TABLE table_name MODIFY COLUMN ...
```

- 编辑器 `Cancel` 时恢复原列定义，不执行 SQL

#### 3.3 `addConstraintFromDesigner(...)`

- 首轮只做：
  - PK
  - UNIQUE
  - FK
  - CHECK
- `MODIFY CONSTRAINT` 不在本阶段图形化范围内。
- 约束编辑器首轮必须按约束类型动态显隐字段，但最终提交仍统一产出标准 `ALTER TABLE ... ADD CONSTRAINT ...` SQL。
- 新增约束固定为：
  - 点页签工具栏 `+`
  - 打开约束编辑面板或对话框
  - 用户在编辑器中确认后才执行：

```sql
ALTER TABLE table_name ADD CONSTRAINT ...
```

#### 3.4 `createIndexFromDesigner(...)`

- 首轮只支持普通索引和唯一索引。
- 多列表达式索引、函数索引不在当前范围。
- 索引编辑器必须保持列顺序稳定，前端不得在提交前重排序引列列表。
- 索引确认方式固定为：
  - 点页签工具栏 `+`
  - 打开索引编辑面板或对话框
  - 用户在编辑器中确认后才执行：

```sql
CREATE [UNIQUE] INDEX index_name ON table_name(col1, col2);
```

- 删除索引时：
  - 先标记待删除
  - 行内 `√` 才执行：

```sql
DROP INDEX index_name ON table_name;
```

### 4. 测试计划

#### 4.1 `createTableFromDesigner(...)`

- `test_createTableFromDesignerBuildsCreateTableSql`
- `test_createTableFromDesignerRefreshesTreeOnSuccess`

#### 4.2 `dropTableFromDesigner(tableName)`

- `test_dropTableFromDesignerBuildsDropTableSql`
- `test_dropTableFromDesignerRefreshesTreeOnSuccess`

#### 4.3 `addColumnFromDesigner(...)`

- `test_addColumnFromDesignerBuildsAlterAddColumnSql`
- `test_addColumnFromDesignerRequiresCompleteDefinition`
- `test_openAddColumnEditorInitializesEmptyDefinition`
- `test_addColumnEditorConfirmExecutesOnlyOnConfirm`
- `test_addColumnEditorCancelClosesWithoutSql`
- `test_buildColumnEditorFormContainsFullFieldSet`

#### 4.4 `modifyColumnFromDesigner(...)`

- `test_modifyColumnFromDesignerBuildsAlterModifyColumnSql`
- `test_modifyColumnFromDesignerUsesCompleteDefinition`
- `test_openModifyColumnEditorPrefillsOriginalDefinition`
- `test_modifyColumnEditorConfirmExecutesOnlyOnConfirm`
- `test_modifyColumnEditorCancelRestoresOriginalDefinition`
- `test_buildColumnEditorFormPrefillsCompleteOriginalDefinition`

#### 4.5 `dropColumnFromDesigner(...)`

- `test_dropColumnFromDesignerBuildsAlterDropColumnSql`

#### 4.6 `addConstraintFromDesigner(...)`

- `test_addConstraintFromDesignerBuildsPkSql`
- `test_addConstraintFromDesignerBuildsUniqueSql`
- `test_addConstraintFromDesignerBuildsForeignKeySql`
- `test_addConstraintFromDesignerBuildsCheckSql`
- `test_openAddConstraintEditorInitializesByConstraintKind`
- `test_addConstraintEditorConfirmExecutesOnlyOnConfirm`
- `test_addConstraintEditorCancelClosesWithoutSql`
- `test_buildConstraintEditorFormShowsFieldsByConstraintKind`

#### 4.7 `dropConstraintFromDesigner(...)`

- `test_dropConstraintFromDesignerBuildsDropConstraintSql`
- `test_dropConstraintPendingDeleteDoesNotExecuteImmediately`
- `test_dropConstraintConfirmExecutesOnCheck`

#### 4.8 `createIndexFromDesigner(...)`

- `test_createIndexFromDesignerBuildsCreateIndexSql`
- `test_createIndexFromDesignerSupportsUniqueFlag`
- `test_openCreateIndexEditorInitializesEmptyDefinition`
- `test_createIndexEditorConfirmExecutesOnlyOnConfirm`
- `test_createIndexEditorCancelClosesWithoutSql`
- `test_buildIndexEditorFormKeepsColumnOrderStable`

#### 4.9 `dropIndexFromDesigner(...)`

- `test_dropIndexFromDesignerBuildsDropIndexSql`
- `test_dropIndexPendingDeleteDoesNotExecuteImmediately`
- `test_dropIndexConfirmExecutesOnCheck`

---

## 阶段五：模块间统一规则

### 1. 边界划定

本阶段不新增独立 UI，而是定义所有模块共享的调用规则。

### 2. 精确到每个函数的输入输出的数据流收口计划

#### 2.1 `executeSql(sql)`

统一输入：

- `sql: QString`

统一输出：

- `SqlExecResult`

统一要求：

- 所有对象树、数据表格、结构页、编辑器动作，最终都必须走这一入口

### 3. 每个关键函数内部具体行为的收口计划

#### 3.1 对象树刷新规则

- `CREATE/DROP DATABASE` 成功后：刷新数据库列表
- `USE DATABASE` 成功后：刷新当前数据库表列表
- `CREATE/DROP TABLE` 成功后：刷新当前数据库表列表
- `ALTER TABLE` 成功后：刷新当前表结构页与对象树该表节点
- `CREATE/DROP INDEX` 成功后：刷新当前表结构页
- `INSERT/UPDATE/DELETE` 成功后：默认只刷新当前数据页，不刷新整棵对象树
- 首轮显式刷新按钮只保留三类：
  - 对象树 `Refresh`
  - 数据页 `Refresh`
  - 结构页 `Refresh`
- 不再扩展页签级、分页级、子面板级刷新按钮

#### 3.2 哪些能力必须走 SQL 编辑器

下面这些首轮不得承诺图形化覆盖：

- 复杂 `WHERE`
- 子查询图形化编辑器
- `MODIFY CONSTRAINT`
- 复杂批量 `UPDATE`
- 复杂批量 `DELETE`
- 多语句初始化脚本之外的可视化编排器

### 4. 测试计划

#### 4.1 `executeSql(sql)`

- `test_allUiActionsRouteThroughUnifiedExecuteSql`

#### 4.2 刷新规则

- `test_createTableRefreshesTableListOnly`
- `test_insertRefreshesCurrentGridOnly`
- `test_dropDatabaseRefreshesDatabaseList`
- `test_frontendExposesOnlyTreeGridStructureRefreshButtons`

## 结论

这份前端计划的重点不是“把所有 SQL 做成按钮”，而是把当前真实 DDL / DML 能力分到 5 个模块里，并把每个 GUI 动作最终调取哪条 SQL 收死。

按这份计划落地后：

- 高频数据库浏览、表浏览、单表数据编辑、基础结构修改，都可以被 GUI 覆盖
- 复杂语义不做图形化承诺；待后端能力落地后，仅保留直接输入 SQL 或表达式文本的入口
- 前端不会越过当前后端真实能力边界
