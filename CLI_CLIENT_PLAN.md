# CLI_CLIENT_PLAN

目标：构建 CLI 形式的多客户端接入口，并把当前依赖全局 `currentDatabase` 的执行上下文收口为“每个客户端实例独立持有”的会话状态。CLI 是 Qt GUI 软件的附属产物，使用独立可执行文件 `DBMS_CLI`；GUI 可执行文件 `DBMS` 与 CLI 可执行文件复用同一套客户端运行时，而不是各自直接操作 service 全局状态。

本文分为三个阶段：

1. 阶段一：客户端会话运行时与 CLI 基础入口。
2. 阶段二：基础身份验证与权限控制。
3. 阶段三：GUI 接入客户端实例池，并保留 CLI/GUI 共用执行路径。

本文不覆盖以下内容：

1. 跨进程服务端协议、TCP/HTTP server。
2. 密码哈希的工业级安全实现。
3. 细粒度列级权限、角色系统、审计日志。
4. SQL 优化器、事务隔离级别、WAL。

---

## 阶段一：客户端会话运行时与 CLI 基础入口

目标：先完成“一个进程内可以有多个独立客户端实例”的结构。每个客户端拥有自己的 `currentDatabase / dataRoot / userName` 等状态，CLI 输入 SQL 后通过该客户端实例执行，避免多个入口共享一个全局 `currentDatabase`。本阶段同时拆分构建产物：`DBMS` 继续作为 Qt GUI 可执行文件，新增 `DBMS_CLI` 作为独立命令行可执行文件。

### 0. 常量增量

新增文件：[cli_client_def.h](constants/cli_client_def.h)

新增常量：

1. `kDefaultCliPrompt`
   - 默认值：`"dbms> "`
   - 含义：CLI 默认提示符。

2. `kCliContinuationPrompt`
   - 默认值：`"   -> "`
   - 含义：多行 SQL 未遇到 `;` 时的续行提示符。

3. `kDefaultAnonymousUser`
   - 默认值：`"anonymous"`
   - 含义：阶段一未启用认证前的默认客户端用户。

4. `kRootUserName`
   - 默认值：`"root"`
   - 含义：后续 GUI 自动登录与阶段二认证系统的内置管理员用户名。

### 1. 影响的文件范围

本阶段允许修改：

1. [service.h](service/service.h)
2. [name_normalize.cpp](utils/service_common/name_normalize.cpp)
3. [sql_dispatcher.h](controller/sql_dispatcher.h)
4. [sql_dispatcher.cpp](controller/sql_dispatcher.cpp)
5. [nest_query.h](controller/nest_query.h)
6. [nest_query.cpp](controller/nest_query.cpp)
7. [main.cpp](main.cpp)
8. [CMakeLists.txt](CMakeLists.txt)
9. 新增 `client/client_session.h/.cpp`
10. 新增 `client/client_session_pool.h/.cpp`
11. 新增 `client/sql_client_engine.h/.cpp`
12. 新增 `cli/cli_app.h/.cpp`
13. 新增 `cli/main_cli.cpp`
14. 新增测试：
    - `tests/test_client_session.cpp`
    - `tests/test_cli_client.cpp`

本阶段不修改：

1. `repo::*` 文件格式。
2. SQL parser 的语法集合。
3. service 层 DDL/DML 公开函数签名。
4. GUI 具体界面交互。

构建 target 收口：

1. `DBMS`
   - GUI 可执行文件。
   - 使用 `main.cpp`、`mainwindow.*`、`display/*`。
   - 链接 Qt Widgets。

2. `DBMS_CLI`
   - CLI 可执行文件。
   - 使用 `cli/main_cli.cpp`、`cli/cli_app.*`。
   - 不依赖 `mainwindow.*`、`display/*`、`QApplication`。
   - 优先使用 `QCoreApplication` 或纯控制台入口。

3. 公共核心
   - service / repo / parser / dispatcher / client runtime 作为两个 target 共用源文件。
   - CMake 中应显式拆出公共源文件列表，避免 GUI 源混入 CLI target。

### 2. 函数级收口的完整数据流

#### 2.1 CLI 单条 SQL 执行数据流

```text

DBMS_CLI 进程启动
-> cli/main_cli.cpp 创建 QCoreApplication 或控制台入口
-> CliApp::run(argc, argv)
-> CLI 读取 stdin 行
-> CliApp 拼接到 SQL buffer
-> 遇到分号或退出命令
-> SqlClientEngine::executeSql(clientId, sql)
-> ClientSessionPool::session(clientId)
-> ScopedServiceContext 绑定该 session 的 dataRoot/currentDatabase/currentUser
-> SqlDispatcher::execute(sql)
-> service/database/table/tuple/query executor
-> SqlDispatcher 产出 SqlExecResult
-> ScopedServiceContext 把 USE DATABASE 后的新 currentDatabase 写回 ClientSession
-> CliApp 格式化输出结果或错误
```

#### 2.2 多客户端状态隔离数据流

```text

ClientSessionPool::createSession()
-> 分配 clientId
-> 初始化 session.dataRoot / session.currentDatabase / session.userName
-> 客户端 A 执行 USE db_a
-> A.session.currentDatabase = db_a
-> 客户端 B 执行 USE db_b
-> B.session.currentDatabase = db_b
-> 任一客户端后续执行 SELECT/DDL/DML
-> 只读取自己的 session.currentDatabase
```

#### 2.3 兼容现有 service 全局状态的数据流

```text

SqlClientEngine::executeSql(...)
-> ScopedServiceContext 构造
   -> 保存旧 service::currentDatabase / currentDataRoot / currentUser
   -> 写入 session 中的上下文
-> 调用现有 dispatcher/service
-> ScopedServiceContext 析构
   -> 读取 service::currentDatabase 的最新值
   -> 写回 session.currentDatabase
   -> 恢复旧 service 上下文
```

### 3. 精确到输入输出的函数级收口，以及重要函数内部逻辑

#### 3.1 `struct ClientSession`

输入：无。

输出：一个会话状态对象。

字段：

```cpp
struct ClientSession {
    QString clientId;
    QString dataRoot;
    QString currentDatabase;
    QString userName;
    bool authenticated = false;
};
```

内部规则：

1. `currentDatabase` 不再作为客户端状态长期存放在 `service::currentDatabase`。
2. `service::currentDatabase` 只允许作为一次执行期间的 legacy bridge。
3. `dataRoot` 阶段一默认沿用当前全局 data root，后续由 session 显式持有。

#### 3.2 `ClientSessionPool::createSession`

输入：

1. `dataRoot`
2. `userName`

输出：

1. `QString clientId`

内部逻辑：

```text

生成 UUID
-> 构造 ClientSession
-> dataRoot 为空则使用 repo::FlatFileTableStore::defaultDataRoot()
-> userName 为空则使用 kDefaultAnonymousUser
-> 写入 session map
-> 返回 clientId
```

#### 3.3 `ClientSessionPool::session`

输入：

1. `clientId`
2. `error`

输出：

1. `ClientSession*`

内部逻辑：

```text

查找 clientId
-> 不存在则 error = "client session '<id>' does not exist"
-> 存在则返回指针
```

#### 3.4 `ClientSessionPool::closeSession`

输入：

1. `clientId`

输出：

1. `bool`

内部逻辑：

```text

从 session map 删除
-> 成功返回 true
-> 不存在返回 false
```

#### 3.5 `ScopedServiceContext`

输入：

1. `ClientSession* session`

输出：

1. RAII 上下文对象。

内部逻辑：

```text

构造：
-> 保存 service 当前上下文
-> service::setDataRoot(session->dataRoot)
-> service::currentDatabase = session->currentDatabase
-> service::currentUser = session->userName

析构：
-> session->dataRoot = service::getDataRoot()
-> session->currentDatabase = service::currentDatabase
-> session->userName = service::currentUser
-> 恢复构造前的 service 上下文
```

正式规则：

1. 任意 SQL 执行必须经由 `ScopedServiceContext`。
2. 不允许 CLI/GUI 直接写 `service::currentDatabase`。
3. `USE DATABASE` 的结果必须回写到对应 `ClientSession`。

#### 3.6 `SqlClientEngine::executeSql`

输入：

1. `clientId`
2. `sql`

输出：

1. `SqlExecResult`

内部逻辑：

```text

通过 ClientSessionPool 获取 session
-> 失败则返回 SqlExecResult{success=false, errorMessage}
-> ScopedServiceContext 绑定 session
-> 调用 SqlDispatcher::execute(sql)
-> 返回 dispatcher 结果
```

#### 3.7 `CliApp::run`

输入：

1. `argc`
2. `argv`

输出：

1. 进程退出码。

内部逻辑：

```text

解析 --data-root / --user / --execute 等参数
-> 创建 ClientSession
-> 如果存在 --execute，则执行单条 SQL 后退出
-> 否则进入 REPL
-> 逐行读取 stdin
-> 遇到 quit/exit/.quit/.exit 退出
-> SQL buffer 遇到 ; 后交给 SqlClientEngine
-> 输出表格、文本或错误
```

正式规则：

1. `CliApp` 不负责创建 `QApplication`，CLI target 不启动 GUI 事件循环。
2. `CliApp` 只处理 CLI 元命令：`quit / exit / .quit / .exit / .help`。
3. 普通 SQL，包括后续新增的认证 DDL/DCL，必须进入 `SqlClientEngine -> SqlDispatcher -> parser`。
4. `--execute` 模式执行完成后必须直接返回退出码，不进入 REPL。

#### 3.8 `cli/main_cli.cpp::main`

输入：

1. `argc`
2. `argv`

输出：

1. 进程退出码。

内部逻辑：

```text

创建 QCoreApplication 或直接保留 argc/argv
-> 构造 ClientSessionPool
-> 构造 SqlClientEngine
-> 构造 CliApp
-> return CliApp::run(argc, argv)
```

正式规则：

1. `cli/main_cli.cpp` 不 include `mainwindow.h`。
2. `cli/main_cli.cpp` 不创建 `QApplication`。
3. `DBMS_CLI` 不能因为 GUI 依赖缺失而无法运行。

### 4. 测试用例的构建描述

#### 4.1 `tests/test_client_session.cpp`

1. `test_createSessionHasIndependentCurrentDatabase`
   - 创建两个 session。
   - 分别执行 `USE db_a` / `USE db_b`。
   - 验证两个 session 的 `currentDatabase` 不互相覆盖。

2. `test_executeSqlRestoresPreviousServiceContext`
   - 手动设置旧 `service::currentDatabase`。
   - 通过 `SqlClientEngine` 执行 SQL。
   - 验证执行结束后旧 service 上下文被恢复。

3. `test_closeSessionRemovesClient`
   - 创建 session 后关闭。
   - 再执行 SQL 应返回 session 不存在错误。

#### 4.2 `tests/test_cli_client.cpp`

1. `test_executeOneShotSql`
   - 使用 `CliApp` 的非交互执行入口执行 `CREATE DATABASE`。
   - 验证返回码为 0，结果成功。

2. `test_multilineSqlBufferExecutesOnlyAfterSemicolon`
   - 输入不带分号的 SQL 片段。
   - 验证不会提前执行。
   - 补充分号后验证执行成功。

3. `test_quitCommandExitsWithoutExecutingSql`
   - 输入 `quit`。
   - 验证退出码为 0，SQL dispatcher 未被调用。

4. `test_cliTargetDoesNotLinkGuiEntry`
   - 构建 `DBMS_CLI`。
   - 验证 CLI target 不包含 `mainwindow.cpp` 与 `display/*`。

5. `test_executeModeReturnsAfterOneSql`
   - 使用 `DBMS_CLI --execute "SHOW DATABASES;"`。
   - 验证命令结束后进程退出，不进入 GUI 或 REPL。

---

## 阶段二：基础身份验证与权限控制

目标：为 CLI 多客户端引入最小可用的认证与权限系统。每个客户端先登录，再执行 SQL；DDL/DML 进入 service 前完成权限检查。阶段二只做用户、数据库级权限、基础 DCL，不做角色与列级权限。新增的认证 DDL/DCL 都按普通 SQL 处理：必须经过 tokenizer/parser/dispatcher，不作为 CLI 内置命令绕过 SQL 层。

### 0. 常量增量

继续使用 [cli_client_def.h](constants/cli_client_def.h)

新增常量：

1. `kAuthDatabaseName`
   - 默认值：`"__dbms_auth"`
   - 含义：内置认证元数据数据库名。

2. `kUserTableName`
   - 默认值：`"sys_users"`
   - 含义：用户表名。

3. `kPrivilegeTableName`
   - 默认值：`"sys_database_privileges"`
   - 含义：数据库级权限关系表名。

4. `kRootUserName`
   - 默认值：`"root"`
   - 含义：内置管理员用户名。

5. `kRootInitialPassword`
   - 默认值：空字符串。
   - 含义：第一版 root 暂时允许空密码，后续再迁移为初始化配置。

### 1. 影响的文件范围

本阶段允许修改：

1. [service.h](service/service.h)
2. [database_service.cpp](service/database_service.cpp)
3. [table_service.cpp](service/table_service.cpp)
4. [tuple_service.cpp](service/tuple_service.cpp)
5. [sql_parser.h](utils/sql_parser/sql_parser.h)
6. [database_parser.cpp](utils/sql_parser/database_parser.cpp)
7. [sql_dispatcher.h](controller/sql_dispatcher.h)
8. [sql_dispatcher.cpp](controller/sql_dispatcher.cpp)
9. [client_session.h/.cpp](client/client_session.h)
10. [sql_client_engine.h/.cpp](client/sql_client_engine.h)
11. 新增 `service/auth_service.h/.cpp`
12. 新增 `constants/cli_client_def.h`
13. 新增测试：
    - `tests/test_auth_service.cpp`
    - `tests/test_auth_dispatcher.cpp`
    - `tests/test_cli_auth.cpp`

本阶段不修改：

1. 用户表以外的已有 repo 存储格式。
2. 普通 DDL/DML parser 的既有语义。
3. GUI 登录界面。

新增 SQL 语法收口：

1. `LOGIN user IDENTIFIED BY password`
   - 作为认证 SQL 命令进入 parser。
   - 用于客户端登录。

2. `CREATE USER user IDENTIFIED BY password`
   - 作为 DDL 进入 parser。
   - 创建用户。

3. `DROP USER user`
   - 作为 DDL 进入 parser。
   - 删除用户。

4. `ALTER USER user IDENTIFIED BY password`
   - 作为 DDL 进入 parser。
   - 修改密码。

5. `GRANT ALL ON database.* TO user`
   - 作为 DCL 进入 parser。
   - 授予数据库级权限。

6. `REVOKE ALL ON database.* FROM user`
   - 作为 DCL 进入 parser。
   - 回收数据库级权限。

正式规则：

1. CLI 不用字符串特判 `CREATE USER / GRANT / REVOKE`。
2. dispatcher 根据 parser 结果分派到 `AuthService`。
3. parser 返回的 command kind 必须能被 `AuthService::authorize` 使用。

### 2. 函数级收口的完整数据流

#### 2.1 登录数据流

```text

CLI 输入 LOGIN user IDENTIFIED BY password
-> CliApp 将整条 SQL 交给 SqlClientEngine::executeSql
-> SqlDispatcher 调用 parser
-> parser 识别 AuthCommand::Login
-> dispatcher 调用 SqlClientEngine/AuthService 登录路径
-> AuthService::authenticate(userName, password)
-> 读取 __dbms_auth.sys_users
-> 校验用户存在、密码匹配、enabled=true
-> session.authenticated = true
-> session.userName = userName
-> 返回成功
```

#### 2.2 权限检查数据流

```text

SqlClientEngine::executeSql(clientId, sql)
-> 获取 session
-> SqlDispatcher parse SQL
-> 若 SQL 不是 AuthCommand::Login 且不是 CLI 元命令，要求 authenticated=true
-> AuthService::authorize(session.userName, parsedCommand, targetDatabase)
-> root 直接放行
-> 非 root 查询 sys_database_privileges
-> 权限不足返回 "permission denied"
-> 权限通过后进入原 dispatcher/service
```

#### 2.3 DCL 数据流

```text

CREATE USER alice IDENTIFIED BY pwd
-> tokenizer/parser 解析为 AuthCommand::CreateUser
-> dispatcher 调用 AuthService::createUser
-> AuthService 要求当前用户 root
-> 写入 sys_users

GRANT ALL ON db.* TO alice
-> tokenizer/parser 解析为 AuthCommand::GrantAll
-> dispatcher 调用 AuthService::grantDatabaseAll
-> AuthService 要求当前用户 root
-> 写入 sys_database_privileges

REVOKE ALL ON db.* FROM alice
-> tokenizer/parser 解析为 AuthCommand::RevokeAll
-> dispatcher 调用 AuthService::revokeDatabaseAll
-> AuthService 要求当前用户 root
-> 删除权限记录
```

### 3. 精确到输入输出的函数级收口，以及重要函数内部逻辑

#### 3.1 `AuthService::initializeAuthStore`

输入：

1. `dataRoot`

输出：

1. `TaskResult`

内部逻辑：

```text

确保 __dbms_auth 数据库目录存在
-> 确保 sys_users 表存在
-> 确保 sys_database_privileges 表存在
-> 若 root 不存在，创建 root
-> root 初始密码使用 kRootInitialPassword
```

#### 3.2 `AuthService::authenticate`

输入：

1. `userName`
2. `password`
3. `dataRoot`

输出：

1. `AuthResult { success, errorMessage, userName }`

内部逻辑：

```text

normalize userName
-> 读取 sys_users
-> 查找 userName
-> 不存在返回 "user '<name>' does not exist"
-> enabled=false 返回 "user '<name>' is disabled"
-> password 不匹配返回 "invalid password"
-> success=true
```

#### 3.3 `AuthService::createUser`

输入：

1. `requestUser`
2. `newUserName`
3. `password`
4. `dataRoot`

输出：

1. `TaskResult`

内部逻辑：

```text

requireRoot(requestUser)
-> 校验 newUserName 非空且不存在
-> 写 sys_users
-> success=true
```

#### 3.4 `AuthService::dropUser`

输入：

1. `requestUser`
2. `targetUserName`
3. `dataRoot`

输出：

1. `TaskResult`

内部逻辑：

```text

requireRoot(requestUser)
-> 禁止删除 root
-> 删除 sys_users 记录
-> 删除该用户所有 sys_database_privileges 记录
```

#### 3.5 `AuthService::grantDatabaseAll`

输入：

1. `requestUser`
2. `targetUserName`
3. `databaseName`
4. `dataRoot`

输出：

1. `TaskResult`

内部逻辑：

```text

requireRoot(requestUser)
-> 校验用户存在
-> 校验 databaseName 存在
-> 若权限记录不存在则插入
-> 已存在则返回 success=true
```

#### 3.6 `AuthService::authorize`

输入：

1. `userName`
2. `SqlCommandKind commandKind`
3. `targetDatabase`
4. `dataRoot`

输出：

1. `TaskResult`

内部逻辑：

```text

userName == root -> success
SHOW DATABASES -> success
USE database -> 要求该 database 有权限
SELECT/INSERT/UPDATE/DELETE/DDL -> 要求当前或目标 database 有权限
CREATE/DROP USER/GRANT/REVOKE -> 只允许 root
权限不足 -> "permission denied for user '<user>' on database '<db>'"
```

#### 3.7 `sqlparser::parseAuthCommand`

输入：

1. tokenized SQL

输出：

1. `ParseResult`

内部逻辑：

```text

识别 LOGIN / CREATE USER / DROP USER / ALTER USER / GRANT / REVOKE
-> 校验关键字顺序
-> 提取 userName / password / databaseName
-> 写入 ParseResult 的 auth command 字段
-> 语法错误返回明确 parser error
```

正式规则：

1. 认证 DDL/DCL 不允许在 CLI 层用字符串拆分实现。
2. 语法错误必须由 parser 返回，而不是 AuthService 返回。
3. dispatcher 只消费结构化 ParseResult。

### 4. 测试用例的构建描述

#### 4.1 `tests/test_auth_service.cpp`

1. `test_initializeAuthStoreCreatesRoot`
   - 初始化认证存储。
   - 验证 root 用户存在。

2. `test_authenticateRejectsWrongPassword`
   - 创建用户。
   - 使用错误密码登录失败。

3. `test_createDropUser`
   - root 创建用户。
   - root 删除用户。
   - 验证用户不存在。

4. `test_grantRevokeDatabaseAll`
   - root 授权用户访问数据库。
   - 查询权限存在。
   - revoke 后权限不存在。

#### 4.2 `tests/test_auth_dispatcher.cpp`

1. `test_createUserSqlRequiresRoot`
   - 非 root 执行 `CREATE USER`。
   - 返回 permission denied。

2. `test_grantAllowsUseDatabase`
   - root 创建数据库和用户。
   - 授权后用户可 `USE database`。

3. `test_unprivilegedDmlRejected`
   - 用户未获授权时执行 `SELECT/INSERT`。
   - 返回 permission denied。

4. `test_authDdlGoesThroughParser`
   - 执行 `CREATE USER alice IDENTIFIED BY pwd`。
   - 验证 parser 返回 AuthCommand::CreateUser。
   - 验证 dispatcher 调用 AuthService。

5. `test_invalidGrantSyntaxRejectedByParser`
   - 执行非法 `GRANT` 语句。
   - 验证失败来自 parser error，而不是 CLI 元命令或 AuthService。

#### 4.3 `tests/test_cli_auth.cpp`

1. `test_loginSetsSessionUser`
   - CLI 登录成功后，session.userName 更新。

2. `test_executeBeforeLoginRejected`
   - 未登录执行普通 SQL。
   - 返回 authentication required。

3. `test_rootCanManageUsersFromCli`
   - root 登录。
   - 执行 `CREATE USER / GRANT / REVOKE / DROP USER`。
   - 全部成功。

---

## 阶段三：GUI 接入客户端实例池

目标：前端不再直接依赖全局 `currentDatabase`，而是持有一个 GUI 专属 `clientId`。GUI 启动时自动创建客户端实例并以 root 登录，所有编辑器执行、结构树刷新、结果面板查询都走 `SqlClientEngine`。GUI target 与 CLI target 共享 client runtime，但入口、UI 依赖和启动生命周期保持分离。

### 0. 常量增量

继续使用 [cli_client_def.h](constants/cli_client_def.h)

新增常量：

1. `kGuiDefaultClientName`
   - 默认值：`"gui-root-client"`
   - 含义：GUI 默认客户端实例名称。

2. `kEnableGuiAutoRootLogin`
   - 默认值：`true`
   - 含义：阶段三允许 GUI 启动时自动登录 root，暂时免密码。

### 1. 影响的文件范围

本阶段允许修改：

1. [mainwindow.h](mainwindow.h)
2. [mainwindow.cpp](mainwindow.cpp)
3. [display/editor_panel.h/.cpp](display/editor_panel.h)
4. [display/result_panel.h/.cpp](display/result_panel.h)
5. [display/structure_panel.h/.cpp](display/structure_panel.h)
6. [controller/sql_dispatcher.h/.cpp](controller/sql_dispatcher.h)
7. [client/sql_client_engine.h/.cpp](client/sql_client_engine.h)
8. [main.cpp](main.cpp)
9. [CMakeLists.txt](CMakeLists.txt)
10. 新增测试：
    - `tests/test_gui_client_runtime.cpp`

本阶段不修改：

1. GUI 的大规模布局重构。
2. SQL parser 语法。
3. repo 存储格式。
4. `DBMS_CLI` 的入口与 REPL 行为。

### 2. 函数级收口的完整数据流

#### 2.1 GUI 启动数据流

```text

DBMS 进程启动
-> main.cpp 创建 QApplication
-> MainWindow 构造
-> ClientSessionPool::createSession(defaultDataRoot, root)
-> SqlClientEngine::loginRootForGui(clientId)
-> MainWindow 保存 clientId
-> 子面板通过 signal/slot 获取 clientId 或执行接口
```

#### 2.2 GUI 执行 SQL 数据流

```text

EditorPanel 点击运行
-> 发出 executeSqlRequested(sql)
-> MainWindow::executeSqlFromEditor(sql)
-> SqlClientEngine::executeSql(guiClientId, sql)
-> 返回 SqlExecResult
-> ResultPanel 显示表格/文本/错误
-> StructurePanel 在 DDL 成功后刷新
```

#### 2.3 GUI 刷新结构树数据流

```text

StructurePanel::refresh()
-> MainWindow 请求 SqlClientEngine 执行 SHOW DATABASES / SHOW TABLES
-> 使用 guiClientId 的当前数据库上下文
-> 结果转换为树节点
```

### 3. 精确到输入输出的函数级收口，以及重要函数内部逻辑

#### 3.1 `MainWindow::initializeClientSession`

输入：无。

输出：

1. `bool`

内部逻辑：

```text

ClientSessionPool::createSession(getDataRoot(), root)
-> 若 kEnableGuiAutoRootLogin=true，则 AuthService root 登录
-> 保存 m_clientId
-> 初始化失败时在 ResultPanel 显示错误
```

#### 3.2 `MainWindow::executeSqlFromEditor`

输入：

1. `sql`

输出：无，结果通过 UI 展示。

内部逻辑：

```text

校验 m_clientId 非空
-> SqlClientEngine::executeSql(m_clientId, sql)
-> success=false 显示错误
-> resultTable 非空则 ResultPanel 显示表格
-> text 非空则 ResultPanel 显示文本
-> DDL/DCL 成功则 StructurePanel refresh
```

#### 3.3 `StructurePanel::setClientId`

输入：

1. `clientId`

输出：无。

内部逻辑：

```text

保存 clientId
-> 后续 refresh 全部通过该 clientId 查询
```

#### 3.4 `ResultPanel::showSqlResult`

输入：

1. `SqlExecResult`

输出：无。

内部逻辑：

```text

success=false -> 显示错误
resultTable 非空 -> 显示表格
text 非空 -> 显示文本
affectedRowCount 可用 -> 显示影响行数
```

### 4. 测试用例的构建描述

#### 4.1 `tests/test_gui_client_runtime.cpp`

1. `test_mainWindowCreatesGuiClient`
   - 构造 MainWindow。
   - 验证存在非空 `clientId`。

2. `test_guiExecuteSqlUsesOwnSession`
   - GUI client 执行 `USE db_gui`。
   - 另建 CLI client 执行 `USE db_cli`。
   - 验证两个 session 的 currentDatabase 不互相覆盖。

3. `test_guiAutoRootCanRunDdl`
   - GUI client 自动 root 登录。
   - 执行 `CREATE DATABASE`。
   - 验证成功。

4. `test_structureRefreshUsesGuiClientContext`
   - GUI client 切换数据库。
   - refresh structure。
   - 验证显示的是 GUI client 当前数据库的表。

5. `test_guiAndCliTargetsUseSharedClientRuntime`
   - GUI client 与 CLI client 分别创建 session。
   - 验证二者调用同一套 `ClientSessionPool / SqlClientEngine` 行为。
   - 验证 GUI target 不依赖 `cli/main_cli.cpp`，CLI target 不依赖 `mainwindow.cpp`。

---

## 最终验收口径

完成三个阶段后，应满足以下要求：

1. `DBMS` 是 GUI 可执行文件，`DBMS_CLI` 是独立 CLI 可执行文件。
2. CLI 可以启动交互式 REPL，并支持单条 SQL 执行模式。
3. 每个客户端实例都有独立 `currentDatabase`，互不污染。
4. `service::currentDatabase` 不再作为长期业务状态，只作为一次 SQL 执行的兼容桥。
5. 未登录客户端不能执行普通 SQL。
6. `LOGIN / CREATE USER / DROP USER / ALTER USER / GRANT / REVOKE` 都经过 SQL parser 与 dispatcher。
7. root 可以创建/删除用户、授权/回收数据库权限。
8. 普通用户只能访问被授权数据库。
9. GUI 与 CLI 复用同一套 `ClientSessionPool + SqlClientEngine`。
10. GUI 自动 root 登录后，原有编辑器执行、结构树刷新、结果展示功能保持可用。
11. 所有失败路径返回明确错误，不允许静默吞掉认证失败、权限失败、session 不存在、SQL 执行失败。
