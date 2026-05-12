# INTEGRATION_AND_STRESS_TEST_PLAN

目标：补充数据库端到端集成测试与压力测试。两类测试入口和汇总分开：`runIntegrationTests()` 只负责完整读写链路，`runStressTests()` 只负责压力场景。两者默认随 `--run-tests` 执行；压力测试默认使用 1500 行规模，只有明确传入禁用参数或环境变量时才跳过。

## 集成测试

覆盖至少 5 条完整读写链路：

1. 基础 CRUD：`CREATE DATABASE -> USE -> CREATE TABLE -> INSERT -> SELECT -> UPDATE -> SELECT -> DELETE -> SELECT`。
2. 多客户端隔离：两个 client 分别 `USE` 不同数据库，并验证各自 `currentDatabase` 与查询结果不互相污染。
3. 权限链路：root 创建用户与授权，普通用户登录后在授权库中建表、插入、查询、更新、删除。
4. DDL + 索引链路：建表、插入、创建索引、更新索引列、查询、删除索引、删除数据。
5. 外键级联链路：父子表插入，父表主键更新触发子表更新，父表删除触发子表删除。

## 压力测试

压力测试集中在 `tests/test_integration_stress.cpp`，通过独立的 `Stress tests` 汇总输出，默认执行：

1. 多端并行：多个 `DBMS_CLI` 进程并行执行各自数据库中的建表、插入、更新、查询、删除。
2. 巨量数据存取：批量插入大量行，验证查询、更新、删除的正确性。
3. 巨量数据索引构建：先插入大量数据，再创建索引并验证索引列查询可用。
4. 索引消融：创建索引后删除索引文件，再触发写路径修复并验证查询结果正确。

每个阶段会输出时间指标：

```text
[integration-stress] START stress.massive_data_crud rows=1500
[integration-stress] END stress.massive_data_crud rows=1500 elapsed_ms=...
```

其中 `stress.massive_data_crud` 会进一步拆分输出：

```text
[integration-stress] END stress.massive_data_crud.insert rows=1500 elapsed_ms=...
[integration-stress] END stress.massive_data_crud.select_all rows=1500 elapsed_ms=...
[integration-stress] END stress.massive_data_crud.update_then_select rows=1 elapsed_ms=...
[integration-stress] END stress.massive_data_crud.delete_then_select rows=1499 elapsed_ms=...
```

## 运行方式

默认运行全部服务测试，包含集成/压力测试：

```powershell
.\build\Desktop_Qt_6_9_2_MSVC2022_64bit-Debug\DBMS.exe --run-tests
```

调小或调大压测行数：

```powershell
$env:DBMS_STRESS_ROW_COUNT='5000'
.\build\Desktop_Qt_6_9_2_MSVC2022_64bit-Debug\DBMS.exe --run-tests
```

临时禁用压力测试，只保留集成链路：

```powershell
.\build\Desktop_Qt_6_9_2_MSVC2022_64bit-Debug\DBMS.exe --run-tests --skip-stress-tests
```

或：

```powershell
$env:DBMS_SKIP_STRESS_TESTS='1'
.\build\Desktop_Qt_6_9_2_MSVC2022_64bit-Debug\DBMS.exe --run-tests
```

需要保留日志时直接重定向当前输出，旧日志不会参与判断：

```powershell
.\build\Desktop_Qt_6_9_2_MSVC2022_64bit-Debug\DBMS.exe --run-tests *> build\integration_stress_run.log
```
