# 集成、压力与性能数据测试计划

本计划对应 `tests/test_integration_stress.cpp`。集成测试由 `runIntegrationTests()` 汇总，压力与性能采样由 `runStressTests()` 汇总，默认随 `--run-tests` 执行。压力测试可用 `--skip-stress-tests`、`--no-stress-tests` 或 `DBMS_SKIP_STRESS_TESTS=1` 临时跳过。

## 集成测试

| 测试用例 | 测试目标 | 测试输入 | 预期输出 | 通过真值 |
| --- | --- | --- | --- | --- |
| `test_crudFullChain` | 验证单客户端基础 CRUD 完整链路。 | root 登录后执行 `CREATE DATABASE`、`USE`、`CREATE TABLE`、三行 `INSERT`、按 id `SELECT`、`UPDATE`、`DELETE`、全表 `SELECT`。 | 插入后能查到 `Bob,20`；更新后 id=2 的 score 为 `25`；删除 id=1 后剩余 2 行。 | 所有 SQL 成功；查询结果行数和值与断言一致。 |
| `test_multiClientDatabaseIsolation` | 验证两个客户端的当前数据库与数据互不污染。 | 两个 root session 分别创建并使用 `it_client_a`、`it_client_b`，各自建同名表并插入不同 marker。 | 第一个客户端查到 `alpha`，第二个客户端查到 `beta`；两个 session 的 `currentDatabase` 分别保持各自库名。 | 两边查询值正确，session 状态没有互相覆盖。 |
| `test_authorizedUserCrudChain` | 验证授权用户可以在授权库执行 CRUD。 | root 创建 `it_auth_db`、用户 `alice`，执行 `GRANT ALL`；alice 登录后建表、插入、更新、删除、查询。 | alice 能在授权库完成写入和修改，最终查询 id=1 的 body 为 `published`。 | 登录、授权、CRUD 全部成功，最终查询结果为 1 行且值正确。 |
| `test_ddlIndexLifecycleChain` | 验证 DDL、索引创建、索引列更新、索引删除后的读写闭环。 | 建表 `items`，插入 3 行，创建 `idx_items_code`，更新 code，按 code 查询，再删除索引和一行数据。 | 更新后的 code `c2b` 可查到 id=2、qty=20；删除索引和 id=3 后剩余 2 行。 | 创建/删除索引成功；索引列更新后查询结果正确；最终行数正确。 |
| `test_foreignKeyCascadeChain` | 验证外键 `ON UPDATE CASCADE` 与 `ON DELETE CASCADE` 的端到端效果。 | 建 parent/child 表，child 外键引用 parent，插入父子行；更新父表主键，再删除父行。 | 父表 id 从 1 更新到 2 后 child.parent_id 同步为 2；删除父行后 child 清空。 | 级联更新和级联删除 SQL 成功，子表查询结果符合预期。 |

## 压力测试

| 测试用例 | 测试目标 | 测试输入 | 预期输出 | 通过真值 |
| --- | --- | --- | --- | --- |
| `test_stressMultiClientParallel` | 验证多个 CLI 进程并行执行互不干扰。 | 预建 4 个数据库，启动 4 个 `DBMS_CLI` 进程，各自建表、批量插入 40 行、更新、删除、查询。 | 每个 CLI 正常退出，输出包含更新后的值 `999`。 | 4 个进程均正常退出且 exit code 为 0，输出检查通过。 |
| `test_stressMassiveDataCrud` | 验证大批量数据插入、全表查询、单行更新、删除后的正确性。 | 默认 `DBMS_STRESS_ROW_COUNT` 未设置时使用 500 行；也可由环境变量调整。 | 插入后全表查询行数等于 row_count；更新目标行后值为 `4242`；删除后剩余 row_count - 1 行。 | 每个阶段 SQL 成功，行数和值均与断言一致。 |
| `test_stressMassiveIndexBuild` | 验证大量数据后创建索引并按索引列查询。 | 插入 row_count 行到 `indexed_rows`，创建 `idx_indexed_rows_code`，按目标 code 查询。 | 查询返回 1 行，id 等于目标 id。 | 索引创建成功，按索引列查询结果唯一且正确。 |
| `test_stressIndexAblation` | 验证索引文件丢失后写路径能触发修复。 | 创建索引后手动删除首个索引文件，再执行目标行更新并按 code 查询。 | 更新成功，查询返回修复后的值 `31337`，索引文件重新存在。 | 写路径没有失败，查询值正确，索引文件恢复。 |
| `test_stressIndexLookupBenefit` | 采集建索引前后索引列和非索引列查询耗时，用于观察索引收益。 | 插入 row_count 行；分别记录建索引前按 code 查询、按 value 查询、创建索引、建索引后按 code 查询、按 value 查询。 | 每个查询都返回有效结果，CSV/日志记录各阶段耗时。 | 查询均成功，索引列查询返回唯一目标行，非索引列查询非空。 |

## 性能 CSV 采样测试

| 测试用例 | 测试目标 | 测试输入 | 预期输出 | 通过真值 |
| --- | --- | --- | --- | --- |
| `test_stressPerformanceCsvSamples` | 产出可直接画图的多规模性能数据。 | 默认采样规模为 `100,500,1000` 行；可通过 `DBMS_PERF_ROW_COUNTS` 设置，如 `100,500,2000`。每个规模依次执行建库建表、批量插入、全表查询、单行更新、创建索引、索引列查询。 | 当设置 `DBMS_PERF_CSV_PATH` 时，写出 CSV。CSV 包含 `stage,row_count,elapsed_ms,started_at_utc,ended_at_utc`，同一 stage 可按 row_count 画折线图。 | 所有规模的 SQL 断言通过；CSV 文件存在且包含表头和每个阶段的耗时行。 |

CSV 阶段名说明：

| stage | 含义 | 适合图表 |
| --- | --- | --- |
| `perf.sample.prepare` | 建库、切库、建表耗时。 | 不同规模的初始化成本对比。 |
| `perf.sample.insert` | 批量插入耗时。 | row_count 与插入耗时折线图。 |
| `perf.sample.select_all` | 全表扫描查询耗时。 | row_count 与全表查询耗时折线图。 |
| `perf.sample.update_one` | 单行更新耗时。 | 不同数据规模下单行更新成本。 |
| `perf.sample.create_index` | 创建索引耗时。 | row_count 与索引构建耗时折线图。 |
| `perf.sample.indexed_select` | 建索引后按索引列查询耗时。 | row_count 与索引查询耗时折线图。 |

## 运行方式

默认运行全部服务测试，包含集成、压力和性能采样：

```powershell
$env:QT_QPA_PLATFORM='offscreen'
$env:PATH='E:\Qt\6.9.2\msvc2022_64\bin;' + $env:PATH
.\build\Desktop_Qt_6_9_2_MSVC2022_64bit-Debug\DBMS.exe --run-tests
```

产出 CSV 数据：

```powershell
$env:QT_QPA_PLATFORM='offscreen'
$env:PATH='E:\Qt\6.9.2\msvc2022_64\bin;' + $env:PATH
$env:DBMS_PERF_CSV_PATH='E:\Qt-projects\DBMS\build\performance_samples.csv'
$env:DBMS_PERF_ROW_COUNTS='100,500,1000'
.\build\Desktop_Qt_6_9_2_MSVC2022_64bit-Debug\DBMS.exe --run-tests
```

调小或调大原有压力测试规模：

```powershell
$env:DBMS_STRESS_ROW_COUNT='5000'
.\build\Desktop_Qt_6_9_2_MSVC2022_64bit-Debug\DBMS.exe --run-tests
```

临时禁用压力测试，只保留其他测试：

```powershell
.\build\Desktop_Qt_6_9_2_MSVC2022_64bit-Debug\DBMS.exe --run-tests --skip-stress-tests
```
