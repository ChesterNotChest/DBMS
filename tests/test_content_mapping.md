# 测试内容与测试方法映射表

| 测试内容 | 测试类型 | 对应方法 |
| --- | --- | --- |
| 数据库创建、删除、切换与展示 | 单元测试 | `runDatabaseServiceTests` |
| SQL 解析与命令分发 | 单元测试 | `runParserDispatcherTests` |
| DDL 建表、删表与表结构维护 | 单元测试 | `runTableServiceTests` |
| ALTER TABLE 解析与执行链路 | 单元测试 | `runParserDispatcherTests` / `runTableServiceTests` |
| 约束新增、修改、删除 | 单元测试 | `runTableServiceTests` |
| 外键动作与级联约束 | 单元测试 | `runTableServiceTests` / `runTupleServiceTests` |
| DML 插入、查询、更新、删除 | 单元测试 | `runTupleServiceTests` |
| 唯一约束与 DML 冲突检测 | 单元测试 | `runTupleServiceTests` |
| 复杂 WHERE 条件解析与执行 | 单元测试 | `runLogicTests` / `runQueryExecutorTests` |
| 嵌套查询与相关子查询 | 单元测试 | `runLogicTests` / `runQueryExecutorTests` |
| 索引创建、删除与查询维护 | 单元测试 | `runTableServiceTests` / `runTupleServiceTests` |
| 索引运行时修复 | 单元测试 | `runIndexRuntimeRepairTests` |
| 目录缓存与元数据缓存 | 单元测试 | `runCatalogCacheTests` / `runServiceCommonCacheTests` |
| 锁管理与并发互斥 | 单元测试 | `runLockManagerTests` |
| 多线程读写服务 | 单元测试 | `runThreadedServiceTests` |
| 客户端会话隔离 | 单元测试 | `runClientSessionTests` |
| CLI 命令行客户端 | 单元测试 | `runCliClientTests` |
| 用户认证与权限控制 | 单元测试 | `runAuthClientTests` |
| GUI 客户端运行时 | 单元测试 | `runGuiClientRuntimeTests` |
| 基础 CRUD 端到端链路 | 集成测试 | `runIntegrationTests` |
| 多客户端数据库隔离链路 | 集成测试 | `runIntegrationTests` |
| 授权用户 CRUD 链路 | 集成测试 | `runIntegrationTests` |
| DDL 与索引生命周期链路 | 集成测试 | `runIntegrationTests` |
| 外键级联端到端链路 | 集成测试 | `runIntegrationTests` |
| 多客户端并行压力测试 | 压力测试 | `runStressTests` |
| 大批量数据 CRUD 压力测试 | 压力测试 | `runStressTests` |
| 大批量索引构建压力测试 | 压力测试 | `runStressTests` |
| 索引消融与修复压力测试 | 压力测试 | `runStressTests` |
| 多规模性能数据采样 | 性能测试 | `runStressTests` |
| SQL 解析模块 | 功能测试 | `utils/sql_parser` / `controller/sql_dispatcher` |
| DDL 结构管理模块 | 功能测试 | `service/table_service` |
| DML 数据操作模块 | 功能测试 | `service/tuple_service` / `service/table_dml_service` |
| 查询执行模块 | 功能测试 | `controller/nest_query` / `utils/logic` |
| 约束与索引模块 | 功能测试 | `service/table_service` / `repo/sort_index_repo` |
| 用户认证与权限模块 | 功能测试 | `service/auth_service` / `client/sql_client_engine` |
| GUI 与 CLI 交互模块 | 功能测试 | `mainwindow` / `cli/cli_app` |
| 数据持久化模块 | 功能测试 | `repo/*` |
