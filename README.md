# DBMS

[English](README_EN.md) | 简体中文

DBMS 是一个基于 C++17 和 Qt 的教学型关系数据库管理系统。项目实现了从 SQL 输入、词法分析、语法解析、命令分发、服务执行、文件持久化到 GUI/CLI/RPC 客户端访问的一整套本地数据库链路。

项目当前主要面向课程设计、数据库内核实验和本地演示场景，不依赖外部数据库服务。所有数据库、表结构、约束、索引、用户和权限信息都保存在本地文件系统中。

## 核心特性

- Qt Widgets 图形界面：提供 SQL 编辑、执行结果展示、数据库结构浏览和表结构操作入口。
- 命令行客户端 `DBMS_CLI`：支持交互式 REPL 和一次性 SQL 执行模式。
- TCP 服务端 `DBMS_SERVER`：提供基于 JSON frame 的轻量 RPC 访问入口。
- 自研 SQL tokenizer/parser/dispatcher：将 SQL 解析为结构化 payload 后交给服务层执行。
- 文件系统持久化：数据库、表、元数据、约束、索引和用户权限均落盘保存。
- 认证与授权：支持 root 用户、普通用户、数据库级授权和表级授权。
- 表定义能力：支持建表、删表、改表、列定义、默认值、非空、主键、唯一、外键、Check 等约束。
- 元组操作能力：支持 `SELECT`、`INSERT`、`UPDATE`、`DELETE`，并包含复杂条件、子查询、多表来源、聚合等扩展逻辑。
- 索引和约束维护：提供 B+ 树风格索引文件、唯一索引检查、约束绑定索引和运行时修复测试。
- 自动化测试：通过 Qt Test 覆盖 parser/dispatcher、服务层、缓存、锁、客户端、CLI、认证、GUI runtime、集成和压力场景。

## 技术栈

- C++17
- Qt 6 / Qt 5，推荐 Qt 6
- Qt Widgets / Qt Core / Qt Network / Qt Test
- CMake 3.16+
- MSVC 2022，Windows 环境优先验证
- 本地文件系统持久化

## 架构概览

```text
GUI / CLI / Remote Client
        |
Client runtime and session engine
        |
SQL tokenizer / parser
        |
SQL dispatcher
        |
Service layer
        |
Repository layer
        |
Local data directory
```

主要模块职责：

| 目录 | 说明 |
| --- | --- |
| `display/` | Qt Widgets 图形界面组件，包括编辑器、结果面板、结构面板和表结构对话框。 |
| `cli/` | 命令行客户端入口和 REPL 逻辑。 |
| `server/` | TCP RPC 服务端入口。 |
| `client/` | 客户端运行时、会话池、本地 SQL 引擎、远程客户端和结果格式化。 |
| `utils/sql_parser/` | SQL tokenizer、分类器和各类语句解析器。 |
| `controller/` | SQL dispatcher 与复杂查询执行器。 |
| `service/` | 数据库、表、元组、认证、约束、索引和 DML 服务。 |
| `repo/` | 文件系统 repository，实现数据和元数据读写。 |
| `constants/` | 表定义、客户端默认值和线程性能相关常量。 |
| `tests/` | Qt Test 测试用例。 |
| `data/` | 默认本地数据目录。 |

## 构建要求

推荐环境：

- Windows 10/11
- Visual Studio 2022 C++ 工具链
- Qt 6.9.x MSVC 2022 64-bit kit
- CMake

如果使用 MSVC 命令行构建，请先进入 Visual Studio Developer Command Prompt，或先执行：

```bat
call "D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64
```

## 构建方式

使用 Qt Creator 时，直接打开仓库根目录的 `CMakeLists.txt`，选择 Qt Kit 后配置并构建即可。

命令行构建示例：

```powershell
E:/Qt/Tools/CMake_64/bin/cmake.exe `
  -S E:/Qt-projects/DBMS `
  -B E:/Qt-projects/DBMS/build/codex-vs-debug `
  -G "Visual Studio 17 2022" `
  -A x64 `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_PREFIX_PATH=E:/Qt/6.9.2/msvc2022_64

E:/Qt/Tools/CMake_64/bin/cmake.exe `
  --build E:/Qt-projects/DBMS/build/codex-vs-debug `
  --config Debug
```

仓库中也保留了一份更具体的 Windows Debug 构建说明：[BUILD_WINDOWS_QT_MSVC2022_DEBUG.md](BUILD_WINDOWS_QT_MSVC2022_DEBUG.md)。

## 构建目标

| 目标 | 说明 |
| --- | --- |
| `DBMS` | Qt Widgets GUI 程序，同时包含测试入口。 |
| `DBMS_CLI` | 命令行客户端，通过 RPC 连接 `DBMS_SERVER`。 |
| `DBMS_SERVER` | 本地 TCP SQL 服务端，内部复用会话池和 SQL 执行引擎。 |

## 运行 GUI

构建完成后运行 `DBMS`。Debug 构建下默认会先运行测试；如果只想启动 GUI，可以使用：

```powershell
.\DBMS.exe --skip-tests
```

常用参数：

| 参数 | 说明 |
| --- | --- |
| `--run-tests` | 只运行测试并退出。 |
| `--skip-tests` / `--no-tests` | 跳过 Debug 构建默认测试，直接启动 GUI。 |
| `--skip-stress-tests` / `--no-stress-tests` | 运行测试时跳过压力测试。 |

## 运行服务端和 CLI

先启动服务端：

```powershell
.\DBMS_SERVER.exe --host 127.0.0.1 --port 54545
```

再启动 CLI：

```powershell
.\DBMS_CLI.exe --host 127.0.0.1 --port 54545 -u root -p
```

一次性执行 SQL：

```powershell
.\DBMS_CLI.exe --host 127.0.0.1 --port 54545 -u root -p --execute "show databases;"
```

CLI 常用参数：

| 参数 | 说明 |
| --- | --- |
| `--host HOST` | 指定服务端地址，默认 `127.0.0.1`。 |
| `--port PORT` | 指定服务端端口，默认 `54545`。 |
| `--data-root PATH` | 创建会话时指定数据目录。 |
| `-u NAME` | 登录用户名。 |
| `-p [PASSWORD]` | 指定密码；不带值时交互输入密码。 |
| `--execute "SQL;"` / `-e "SQL;"` | 登录后执行 SQL 并退出。 |
| `--help` / `-h` | 输出帮助。 |

CLI REPL 支持 `.help`、`quit`、`exit`、`.quit` 和 `.exit`。

## 默认账号和权限

首次初始化认证存储时会创建 root 用户：

| 用户 | 初始密码 | 说明 |
| --- | --- | --- |
| `root` | 空字符串 | 超级用户，拥有全部权限。 |

普通用户需要 root 创建和授权：

```sql
CREATE USER alice IDENTIFIED BY secret;
GRANT ALL ON app_db.* TO alice;
GRANT ALL ON app_db.students TO alice;
REVOKE ALL ON app_db.students FROM alice;
DROP USER alice;
```

权限模型要点：

- `root` 可以创建/删除数据库、管理用户和授权。
- 普通用户不能访问系统认证库 `__dbms_auth`。
- 数据库级授权使用 `database.*`。
- 表级授权使用 `database.table`。
- 普通用户只有在具备数据库级权限或对应表权限时才能执行相关 SQL。

## SQL 能力概览

当前 parser 和 dispatcher 覆盖的主要语句包括：

```sql
CREATE DATABASE demo;
DROP DATABASE demo;
SHOW DATABASES;
USE demo;

CREATE TABLE users (
  id INT PRIMARY KEY,
  name VARCHAR(32) NOT NULL,
  age INT DEFAULT 0
);
DROP TABLE users;
ALTER TABLE users ADD COLUMN email VARCHAR(64);
ALTER TABLE users DROP COLUMN email;
DESC users;
SHOW CREATE TABLE users;
SHOW TABLES;

CREATE INDEX idx_users_name ON users(name);
DROP INDEX idx_users_name ON users;

INSERT INTO users VALUES (1, 'Alice', 20);
SELECT * FROM users;
UPDATE users SET age = 21 WHERE id = 1;
DELETE FROM users WHERE id = 1;

LOGIN root IDENTIFIED BY '';
CREATE USER alice IDENTIFIED BY secret;
GRANT ALL ON demo.* TO alice;
REVOKE ALL ON demo.* FROM alice;
```

复杂查询相关能力仍在持续演进，代码中已经包含多表来源、别名解析、复杂 `WHERE`、子查询、集合逻辑和聚合查询的实现与测试。具体支持范围以当前 parser 测试和 dispatcher 行为为准。

## 数据目录

默认数据目录为仓库根目录下的 `data/`。可以通过以下方式指定独立数据目录：

- GUI：设置 `DBMS_GUI_DATA_ROOT` 或 `DBMS_DATA_ROOT` 环境变量。
- CLI：使用 `--data-root PATH`。
- 服务端/客户端会话：由客户端创建 session 时传入。

示例：

```powershell
$env:DBMS_DATA_ROOT="E:\Qt-projects\DBMS\data"
.\DBMS_CLI.exe --data-root "E:\Qt-projects\DBMS\data" -u root -p
```

本项目使用本地文件存储，建议在测试、演示和真实数据之间使用不同的数据目录。升级结构或切换分支前，建议备份整个数据目录。

## 测试

GUI 目标 `DBMS` 内置测试入口：

```powershell
.\DBMS.exe --run-tests
```

跳过压力测试：

```powershell
.\DBMS.exe --run-tests --skip-stress-tests
```

测试覆盖范围包括：

- SQL parser 和 dispatcher
- database/table/tuple service
- 逻辑表达式和复杂查询执行
- 锁管理和 catalog cache
- 索引运行时修复
- 客户端会话、CLI、认证授权
- GUI client runtime
- 集成和压力测试

## 开发提示

- 修改 SQL 语法时，优先同步更新 `utils/sql_parser/`、`controller/sql_dispatcher.cpp` 和相关测试。
- 修改持久化格式时，需要考虑 `repo/`、`service/` 和旧数据兼容。
- 修改权限模型时，需要同时检查 `service/auth_service.*`、`client/sql_client_engine.cpp`、`utils/sql_parser/auth_parser.cpp` 和 `tests/test_auth_client.cpp`。
- GUI 和 CLI 应尽量复用 `client/` 层能力，避免绕过统一的会话、认证和分发流程。

## 相关文档

- [Windows Qt MSVC2022 Debug Build Guide](BUILD_WINDOWS_QT_MSVC2022_DEBUG.md)
- [CLI Client Plan](CLI_CLIENT_PLAN.md)
- [Thread and Performance Plan](THREAD_AND_PERFORMANCE_PLAN.md)
- [Integration and Stress Test Plan](INTEGRATION_AND_STRESS_TEST_PLAN.md)
- [Partial Alter Patch Plan](PARTIAL_ALTER_PATCH_PLAN.md)
- [Test Plan](tests/TEST_PLAN.md)

## 许可证

本项目采用 [MIT License](LICENSE)。

## 项目状态

这是一个教学和实验用途的本地 DBMS 项目。功能覆盖面较广，但并不等价于生产级数据库系统。当前实现重点是 SQL 执行链路、文件持久化、约束/索引维护、客户端会话和认证授权，而完整事务、崩溃恢复、跨进程强一致锁和生产级安全能力仍属于后续扩展方向。
