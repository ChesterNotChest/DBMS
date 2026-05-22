# DBMS

English | [简体中文](README.md)

DBMS is an educational relational database management system built with C++17 and Qt. It implements a complete local database execution path: SQL input, tokenization, parsing, dispatching, service execution, file persistence, and GUI/CLI/RPC access.

The project is designed for course work, database kernel experiments, and local demonstrations. It does not depend on an external database server. Databases, schemas, constraints, indexes, users, and privileges are persisted in the local file system.

## Highlights

- Qt Widgets GUI for SQL editing, result display, database browsing, and table-structure operations.
- `DBMS_CLI` command-line client with interactive REPL and one-shot SQL execution.
- `DBMS_SERVER` TCP server with a lightweight JSON-frame RPC protocol.
- Custom SQL tokenizer, parser, and dispatcher.
- File-system persistence for databases, tables, metadata, constraints, indexes, users, and privileges.
- Authentication and authorization with root, normal users, database-level grants, and table-level grants.
- Table DDL support for columns, defaults, NOT NULL, primary keys, unique constraints, foreign keys, and CHECK constraints.
- Tuple operations for `SELECT`, `INSERT`, `UPDATE`, and `DELETE`, with additional logic for complex predicates, subqueries, multi-table sources, and aggregation.
- Index and constraint maintenance, including unique-index validation and runtime repair tests.
- Qt Test coverage for parser/dispatcher, services, cache, locks, clients, CLI, auth, GUI runtime, integration, and stress scenarios.

## Tech Stack

- C++17
- Qt 6 / Qt 5, Qt 6 recommended
- Qt Widgets / Qt Core / Qt Network / Qt Test
- CMake 3.16+
- MSVC 2022, primarily verified on Windows
- Local file-system persistence

## Architecture

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

Main directories:

| Directory | Purpose |
| --- | --- |
| `display/` | Qt Widgets UI components, including the editor, result panel, structure panel, and table dialogs. |
| `cli/` | Command-line client entry point and REPL logic. |
| `server/` | TCP RPC server entry point. |
| `client/` | Client runtime, session pool, local SQL engine, remote client, and result formatting. |
| `utils/sql_parser/` | SQL tokenizer, classifier, and statement parsers. |
| `controller/` | SQL dispatcher and complex-query executor. |
| `service/` | Database, table, tuple, auth, constraint, index, and DML services. |
| `repo/` | File-system repository implementations. |
| `constants/` | Table definitions, client defaults, and threading constants. |
| `tests/` | Qt Test suites. |
| `data/` | Default local data directory. |

## Requirements

Recommended environment:

- Windows 10/11
- Visual Studio 2022 C++ toolchain
- Qt 6.9.x MSVC 2022 64-bit kit
- CMake

When building from an MSVC shell, use the Visual Studio Developer Command Prompt or initialize it first:

```bat
call "D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64
```

## Build

With Qt Creator, open the root `CMakeLists.txt`, select a Qt Kit, configure, and build.

Command-line example:

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

See [BUILD_WINDOWS_QT_MSVC2022_DEBUG.md](BUILD_WINDOWS_QT_MSVC2022_DEBUG.md) for a more specific Windows Debug build guide.

## Targets

| Target | Description |
| --- | --- |
| `DBMS` | Qt Widgets GUI application. It also contains the test entry point. |
| `DBMS_CLI` | Command-line client that connects to `DBMS_SERVER` through RPC. |
| `DBMS_SERVER` | Local TCP SQL server that reuses the session pool and SQL execution engine. |

## Run The GUI

Run `DBMS` after building. In Debug builds, tests run by default before the window opens. To skip that and start the GUI directly:

```powershell
.\DBMS.exe --skip-tests
```

Common arguments:

| Argument | Description |
| --- | --- |
| `--run-tests` | Run tests only and exit. |
| `--skip-tests` / `--no-tests` | Skip Debug-build startup tests and open the GUI. |
| `--skip-stress-tests` / `--no-stress-tests` | Skip stress tests when running the test entry point. |

## Run Server And CLI

Start the server first:

```powershell
.\DBMS_SERVER.exe --host 127.0.0.1 --port 54545
```

Then start the CLI:

```powershell
.\DBMS_CLI.exe --host 127.0.0.1 --port 54545 -u root -p
```

Execute SQL once and exit:

```powershell
.\DBMS_CLI.exe --host 127.0.0.1 --port 54545 -u root -p --execute "show databases;"
```

CLI arguments:

| Argument | Description |
| --- | --- |
| `--host HOST` | Server host, default `127.0.0.1`. |
| `--port PORT` | Server port, default `54545`. |
| `--data-root PATH` | Data root used when creating the session. |
| `-u NAME` | Login user name. |
| `-p [PASSWORD]` | Password; omit the value to enter it interactively. |
| `--execute "SQL;"` / `-e "SQL;"` | Execute SQL after login and exit. |
| `--help` / `-h` | Print help. |

The REPL supports `.help`, `quit`, `exit`, `.quit`, and `.exit`.

## Default Account And Privileges

The auth store creates a root user during initialization:

| User | Initial Password | Description |
| --- | --- | --- |
| `root` | Empty string | Superuser with all privileges. |

Root can create users and grant privileges:

```sql
CREATE USER alice IDENTIFIED BY secret;
GRANT ALL ON app_db.* TO alice;
GRANT ALL ON app_db.students TO alice;
REVOKE ALL ON app_db.students FROM alice;
DROP USER alice;
```

Privilege rules:

- `root` can create/drop databases, manage users, and grant or revoke privileges.
- Normal users cannot access the system auth database `__dbms_auth`.
- Database-level grants use `database.*`.
- Table-level grants use `database.table`.
- Normal users need either database-level privilege or matching table privilege for protected SQL operations.

## SQL Overview

The current parser and dispatcher cover the following major statements:

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

Complex query support is still evolving. The codebase already includes implementation and tests for multi-table sources, alias resolution, complex `WHERE`, subqueries, set logic, and aggregate queries. The exact supported behavior should be checked against the current parser tests and dispatcher behavior.

## Data Directory

The default data directory is `data/` under the repository root. You can override it in several ways:

- GUI: set `DBMS_GUI_DATA_ROOT` or `DBMS_DATA_ROOT`.
- CLI: pass `--data-root PATH`.
- Server/client session: pass the data root when creating the session.

Example:

```powershell
$env:DBMS_DATA_ROOT="E:\Qt-projects\DBMS\data"
.\DBMS_CLI.exe --data-root "E:\Qt-projects\DBMS\data" -u root -p
```

Because this project stores data as local files, use separate data directories for tests, demos, and real work. Back up the whole data directory before changing branches or modifying persistent formats.

## Tests

The `DBMS` GUI target contains the test entry point:

```powershell
.\DBMS.exe --run-tests
```

Skip stress tests:

```powershell
.\DBMS.exe --run-tests --skip-stress-tests
```

Test coverage includes:

- SQL parser and dispatcher
- Database, table, and tuple services
- Logic expressions and complex-query execution
- Lock manager and catalog cache
- Index runtime repair
- Client sessions, CLI, authentication, and authorization
- GUI client runtime
- Integration and stress tests

## Development Notes

- When changing SQL syntax, update `utils/sql_parser/`, `controller/sql_dispatcher.cpp`, and the related tests together.
- When changing persistent formats, check `repo/`, `service/`, and compatibility with existing data.
- When changing authorization behavior, check `service/auth_service.*`, `client/sql_client_engine.cpp`, `utils/sql_parser/auth_parser.cpp`, and `tests/test_auth_client.cpp`.
- GUI and CLI code should reuse the `client/` layer instead of bypassing session, auth, and dispatch logic.

## Related Documents

- [Windows Qt MSVC2022 Debug Build Guide](BUILD_WINDOWS_QT_MSVC2022_DEBUG.md)
- [CLI Client Plan](CLI_CLIENT_PLAN.md)
- [Thread and Performance Plan](THREAD_AND_PERFORMANCE_PLAN.md)
- [Integration and Stress Test Plan](INTEGRATION_AND_STRESS_TEST_PLAN.md)
- [Partial Alter Patch Plan](PARTIAL_ALTER_PATCH_PLAN.md)
- [Test Plan](tests/TEST_PLAN.md)

## License

This project is licensed under the [MIT License](LICENSE).

## Project Status

This is a local DBMS for educational and experimental use. It has broad feature coverage, but it is not a production database. The current focus is SQL execution, file persistence, constraint/index maintenance, client sessions, and authentication/authorization. Full transactions, crash recovery, cross-process strict locking, and production-grade security remain future work.
