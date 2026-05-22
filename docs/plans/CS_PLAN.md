# CS 收口计划

目标：保留 `DBMS_CLI` 的全部入口能力与交互体验，但把它改成纯 C 端，所有 SQL 执行都走 `DBMS_SERVER`。CLI 不保留离线模式，也不再直接碰 service 层。

范围说明：本计划只收口 CLI -> Server 的 C/S 架构。GUI 先保持现状，不把它混进这次收口，避免把赶工面拉爆。

## 阶段 1：协议与客户端抽象收口

0. 新增常量定义
   - `kDefaultServerHost`
   - `kDefaultServerPort`
   - `kRpcProtocolVersion`
   - `kRpcMaxFrameBytes`
   - `kRpcDefaultTimeoutMs`

1. 影响的文件范围
   - `constants/cli_client_def.h`
   - `client/sql_client_engine.h`
   - `client/sql_client_engine.cpp`
   - 新增 `client/sql_client_runtime.h`
   - 新增 `client/sql_rpc_protocol.h`
   - 新增 `client/sql_rpc_protocol.cpp`
   - `client/sql_result_formatter.h/.cpp`
   - `cli/cli_app.h/.cpp`
   - `cli/main_cli.cpp`

2. 函数级收口的完整数据流
   - `main()` 只负责创建 CLI 运行环境和远端 client runtime。
   - `CliApp::run()` 负责解析参数，创建远端 session，登录并进入执行循环。
   - `SqlClientRuntime::createSession()` 返回 server session id。
   - `SqlClientRuntime::login()/executeSql()/executeSqlPreservingDatabase()` 负责把请求序列化为 RPC frame。
   - `client::formatSqlExecResultForText()` 只消费返回结果，不再关心本地/远端来源。

3. 精确到输入输出的函数级收口，以及重要函数内部逻辑的描述
   - `CliApp::run(const QStringList &arguments) -> int`
     - 输入：CLI 参数列表。
     - 输出：退出码。
     - 逻辑：解析 `--data-root`、`-u`、`-p`、`--execute`，调用 runtime 创建 session，执行登录，按一次性模式或 REPL 模式发 SQL。
   - `SqlClientRuntime::createSession(dataRoot, userName) -> QString`
     - 输入：数据根、默认用户。
     - 输出：远端会话 id。
     - 逻辑：只负责 RPC 建 session，不做本地持久状态。
   - `SqlClientRuntime::login(clientId, userName, password) -> SqlExecResult`
     - 输入：会话 id、用户名、密码。
     - 输出：统一执行结果。
     - 逻辑：请求发往 server，由 server 完成 auth store 初始化和认证。
   - `SqlClientRuntime::executeSql(clientId, sql) -> SqlExecResult`
     - 输入：会话 id、SQL 文本。
     - 输出：执行结果。
     - 逻辑：只做协议收发，不解析 SQL。
   - `SqlClientRuntime::executeSqlPreservingDatabase(clientId, sql) -> SqlExecResult`
     - 输入：会话 id、SQL 文本。
     - 输出：执行结果。
     - 逻辑：走专门的 preserve RPC，保证 GUI/结构类查询不会污染会话当前库。

4. 测试用例的构建描述
   - 协议 round-trip：请求/响应 JSON 序列化和反序列化。
   - CLI 一次性执行：`--execute` 路径只连 server，不碰本地 service。
   - REPL 路径：`USE` 后再 `SHOW TABLES`，确认会话状态留在 server。
   - 异常路径：server 不可达、非法响应、frame 超限、登录失败。

## 阶段 2：DBMS_SERVER 服务端收口

0. 新增常量定义
   - 复用阶段 1 的 RPC 常量。

1. 影响的文件范围
   - 新增 `server/sql_server.h`
   - 新增 `server/sql_server.cpp`
   - 新增 `server/main_server.cpp`
   - `CMakeLists.txt`
   - `client/sql_client_engine.h/.cpp`
   - `client/client_session_pool.h/.cpp`

2. 函数级收口的完整数据流
   - socket accept -> frame decode -> RPC request -> local `SqlClientEngine` -> RPC response encode -> socket reply
   - server 端所有 session 状态都由 `ClientSessionPool` 持有。
   - server 端执行仍然走现有 `controller/sql_dispatcher` 与 service/repo 栈。

3. 精确到输入输出的函数级收口，以及重要函数内部逻辑的描述
   - `DbmsServer::start(host, port) -> bool`
     - 输入：监听地址。
     - 输出：是否监听成功。
   - `DbmsServer::handleRequest(request) -> response`
     - 输入：RPC 请求。
     - 输出：RPC 响应。
     - 逻辑：按 `op` 分派到 `createSession/login/execute/executePreserve/closeSession`。
   - `DbmsServer::executeSql(...)`
     - 输入：server session id 与 SQL。
     - 输出：`SqlExecResult`。
     - 逻辑：直接复用本地 `SqlClientEngine`，不再走客户端本地 fallback。

4. 测试用例的构建描述
   - server 启动烟雾测试。
   - 双客户端并发测试：两个 CLI/remote engine 同时连接，current database 不串。
   - 登录与权限测试：root、普通用户、auth database 保护。

## 阶段 3：CLI 入口切换为纯 C 端

0. 新增常量定义
   - 复用阶段 1 的 server 默认 host/port。

1. 影响的文件范围
   - `cli/main_cli.cpp`
   - `cli/cli_app.h/.cpp`
   - `tests/test_cli_client.cpp`
   - `tests/test_integration_stress.cpp`
   - `tests/test_auth_client.cpp`
   - `tests/test_client_session.cpp`

2. 函数级收口的完整数据流
   - CLI 参数 -> 远端 runtime -> server session -> server service pipeline -> 结果格式化输出。
   - CLI 只保留入口和输出，不再持有独立执行引擎。

3. 精确到输入输出的函数级收口，以及重要函数内部逻辑的描述
   - `CliApp::ensureAuthenticated()`
     - 输入：session id 与用户名/密码。
     - 输出：登录结果。
     - 逻辑：在执行任何 SQL 前先登录 server。
   - `CliApp::runExecuteMode()`
     - 输入：session id 与 SQL。
     - 输出：退出码。
     - 逻辑：只打一条 RPC，不做本地 SQL 执行。
   - `CliApp::runRepl()`
     - 输入：会话 id。
     - 输出：退出码。
     - 逻辑：缓冲多行 SQL，遇到分号再发 server。

4. 测试用例的构建描述
   - CLI help/参数回归。
   - REPL 输入输出回归。
   - one-shot `--execute` 回归。
   - server 不在线时的失败路径。

## 阶段 4：回归测试与收口验证

0. 新增常量定义
   - 如需要可补 `kServerStartTimeoutMs` 之类的测试常量。

1. 影响的文件范围
   - `tests/test_cli_client.cpp`
   - `tests/test_integration_stress.cpp`
   - `tests/test_gui_client_runtime.cpp`
   - `tests/test_entry.h`
   - `main.cpp`
   - `CMakeLists.txt`

2. 函数级收口的完整数据流
   - 测试层先起 server，再跑 CLI/远端 client。
   - 全量回归仍由 `DBMS` 主程序控制，stress 测试继续可跳过。

3. 精确到输入输出的函数级收口，以及重要函数内部逻辑的描述
   - `runCliClientTests()`
     - 输入：测试 fixture。
     - 输出：QTest 结果码。
     - 逻辑：改为依赖 server 进程或测试 server fixture。
   - `runIntegrationTests()/runStressTests()`
     - 输入：测试数据根与 server 地址。
     - 输出：测试结果码。
     - 逻辑：多 client 并发时只验证 server 会话隔离。

4. 测试用例的构建描述
   - 全量回归必须跑。
   - CLI 相关测试必须覆盖 server 启停、登录、执行、退出。
   - 崩溃优先排查，不做“先跳过再说”。
