# Integration and Stress Test Plan

This plan corresponds to `tests/test_integration_stress.cpp`. Integration tests verify end-to-end SQL behavior. Stress and performance tests use the same default row-count scale: `50, 100, 200, 500`.

Stress tests can be skipped with `--skip-stress-tests`, `--no-stress-tests`, or `DBMS_SKIP_STRESS_TESTS=1`.

## Stress And Performance Tests

### Index Impact On CRUD

#### Test Goal

Verify how a normal secondary index affects ascending and descending sort queries on a non-primary, non-unique column. This test also adds 1000-row and 5000-row index sorting samples to observe behavior at larger scales.

#### Case Description

| Input | Expected output | Pass criteria |
| --- | --- | --- |
| Insert deterministic shuffled `sort_key` values; run `ORDER BY sort_key ASC` and `ORDER BY sort_key DESC` before and after creating a normal index on `sort_key`. | CSV records `order_by_asc` and `order_by_desc` elapsed time for both variants. | ASC results are increasing, DESC results are decreasing, and row count matches the input scale. |

### Multi-Client Concurrent CRUD

#### Test Goal

Verify database isolation, lock behavior, and write stability under concurrent clients.

#### Case Description

| Input | Expected output | Pass criteria |
| --- | --- | --- |
| Run single-client sequential CRUD and four-client concurrent CRUD for each row count. | CSV records total elapsed time, per-client elapsed time, and success count. | Every client exits normally and prints the updated target value `999`. |

### Massive CRUD Stage Breakdown

#### Test Goal

Verify time distribution across core CRUD stages under batch data.

#### Case Description

| Input | Expected output | Pass criteria |
| --- | --- | --- |
| Run batch insert, full-table select, single-row update, single-row delete, and select after delete for each row count. | CSV records elapsed time for each stage. | Inserted row count equals the scale; remaining row count is scale minus one after delete. |

### Index Build Cost

#### Test Goal

Verify how normal index creation cost changes with data scale.

#### Case Description

| Input | Expected output | Pass criteria |
| --- | --- | --- |
| Insert rows and create a normal index on `code` for each row count. | CSV records `create_index` elapsed time. | Index creation succeeds and lookup by the indexed column returns the target row. |

### Runtime Index Repair

#### Test Goal

Verify repair ability and repair cost after index file loss on the write path.

#### Case Description

| Input | Expected output | Pass criteria |
| --- | --- | --- |
| Compare update with a healthy index file and update after deleting the index file. | CSV records both update costs. | The deleted index file is recreated and indexed lookup returns the updated value. |

### Foreign-Key Cascade Cost

#### Test Goal

Verify cascade update/delete cost at different scales.

#### Case Description

| Input | Expected output | Pass criteria |
| --- | --- | --- |
| Create parent/child tables, insert parent/child rows, update a parent primary key, then delete the target parent row. | CSV records `cascade_update` and `cascade_delete` elapsed time. | Child foreign key follows the parent update, and the child row is removed after parent delete. |

## CSV Schema

When `DBMS_PERF_CSV_PATH` is set, stress tests append rows with this schema:

```text
test_id,stage,row_count,variant,metric,value,unit,started_at_utc,ended_at_utc
```

## Run

```powershell
$env:QT_QPA_PLATFORM='offscreen'
$env:PATH='E:\Qt\6.9.2\msvc2022_64\bin;' + $env:PATH
$env:DBMS_PERF_CSV_PATH='E:\Qt-projects\DBMS\build\performance_samples.csv'
$env:DBMS_STRESS_ROW_COUNTS='50,100,200,500'
.\build\codex-vs-debug\Debug\DBMS.exe --run-tests
```

Generate charts:

```powershell
py .\tests\tools\plot_performance_charts.py
```
