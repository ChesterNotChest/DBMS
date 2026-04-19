阶段三：(ddl、dml 函数级实现)
1. 先完成 /service/table_dml_service.cpp。它本质是对指定二维表执行通用读写的函数级 service。
它不负责语句解析，也不负责自己猜测要操作哪一类表。
database_task / table_task / tuple_task 先判断目标是 root.dbf / [database].tab / [表名]/table.meta / [表名]/table.con / [表名]/table.dat，再把明确参数传给 table_dml_service。

table_dml_service 至少提供四个专用函数：

selectRows(
    targetDatabaseName,
    targetTableName,
    targetTableKind,
    targetSchema,
    projectionColumns,
    simpleConditions
)
用于查询指定二维表。
它负责读取目标二维表，检查 projectionColumns 是否存在，再按 simpleConditions 做筛选。
当前阶段 simpleConditions 只支持无条件或列等值匹配，多个条件默认 AND。
若 projectionColumns 为 *，则返回整张结果表；否则返回投影后的结果表。
它输出：success、errorMessage、resultTable、affectedRowCount。

insertRows(
    targetDatabaseName,
    targetTableName,
    targetTableKind,
    targetSchema,
    rows,
    validationMode
)
用于向指定二维表插入一行或多行记录。
它负责检查列名是否合法、数据类型是否可转换、NOT NULL 是否满足、DEFAULT 是否需要补入、AUTO_INCREMENT 是否需要自动生成。
若 validationMode == ValidationMode::UserData，还要检查 PRIMARY KEY / UNIQUE 是否冲突。
若目标二维表为用户数据表 [表名]/table.dat，且存在 FOREIGN KEY，则还要检查 referenced_table 和 referenced_columns 对应的父表目标键是否存在。当前 FK 按 NO ACTION / RESTRICT。
它输出：success、errorMessage、affectedRowCount。

updateRows(
    targetDatabaseName,
    targetTableName,
    targetTableKind,
    targetSchema,
    assignmentMap,
    simpleConditions,
    validationMode
)
用于更新指定二维表中满足条件的记录。
它负责检查 assignmentMap 中的目标列是否存在，新值是否能转换成目标类型，NOT NULL 是否满足。
当前阶段 simpleConditions 只支持无条件或列等值匹配。
若 validationMode == ValidationMode::UserData，还要检查 PRIMARY KEY / UNIQUE 是否冲突。
若更新的是子表外键列，则要检查父表目标键存在；
若更新的是父表被引用键，而仍有子表记录引用它，则当前按 NO ACTION / RESTRICT 拒绝更新。
它输出：success、errorMessage、affectedRowCount。

deleteRows(
    targetDatabaseName,
    targetTableName,
    targetTableKind,
    targetSchema,
    simpleConditions,
    validationMode
)
用于删除指定二维表中满足条件的记录。
它负责先按 simpleConditions 定位要删除的记录。
当前阶段 simpleConditions 只支持无条件或列等值匹配。
若 validationMode == ValidationMode::UserData，且删除的是普通用户表 [表名]/table.dat 中的记录，
还要检查这些记录是否正被其它表的 FOREIGN KEY 引用；若存在引用，则当前按 NO ACTION / RESTRICT 拒绝删除。
若删除的是系统二维表 root.dbf / .tab / .meta / .con，则由外层 task 先保证业务动作合法，再调用本函数执行具体删除。
它输出：success、errorMessage、affectedRowCount。

其中：(这部分放到最终注释)
- targetTableKind 用于区分 root.dbf / .tab / table.meta / table.con / table.dat
- targetSchema 用于列检查、类型检查、默认值处理、约束检查
- simpleConditions 当前阶段只表示“列等值匹配条件集合”
- rows 表示待插入的多行记录集合，类型理解为 QList<QMap<QString, QString>>；每个 QMap 表示一行“列名 -> 值”的映射。
- validationMode 用于区分“普通用户数据操作(UserData)”与“系统元数据改写(SystemMeta)”
    此外，
    enum class ValidationMode {
        SystemMeta,
        UserData
    };





2. /service/database_task.cpp 完成"数据库"的ddl函数实现。

CREATE DATABASE 数据库名;
主要用table_dml_service检查root.dbf是否存在同名数据库。
调用repo方法构建对应的数据库文件夹。
构建数据库的[数据库名].tab文件。
用table_dml_service写入记录到root.dbf。


DROP DATABASE 数据库名;
主要用table_dml_service检查数据库是否存在。
调用repo方法删除数据库文件夹。
用table_dml_service删除root.dbf的记录。

USE 数据库名;
将currentDatabase这样的会话信息存到service.h里。

SHOW DATABASES;
使用table_dml_service直接查root.dbf。


3. /service/table_task.cpp 完成"表"的ddl函数实现。

首要事项：
[CHECK(条件)] 属于扩展功能，暂不实现。下文出现只是备忘。[CHECK(条件)]需要等到逻辑谓词的单独UTIL完成后再引入。
        

具体工作是：
CREATE TABLE 表名 (
    列名1 数据类型 
    [NOT NULL] 
    [DEFAULT 默认值] 
    [UNIQUE]  
    [AUTO_INCREMENT]
    [PRIMARY KEY]
    [REFERENCES 表名(列名)]
    [CHECK(条件)]
);
主要是用table_dml_service写入.tab，创建并写入[表名]/table.con和[表名]/table.meta。
创建[表名]/table.dat准备存入数据。
这些存.tab： 表名
这些和列名一起存[表名]/table.meta： [NOT NULL] [DEFAULT 默认值] [AUTO_INCREMENT]
这些存[表名]/table.con： [PRIMARY KEY] [UNIQUE] [CHECK(条件)] [FOREIGN KEY、referenced_table、referenced_columns]

DROP TABLE; 
主要用table_dml_service查一下.tab有没有这个表。然后删掉对应表的[表名]目录，然后用table_dml_service删除对应的.tab的记录。

ALTER TABLE
    ADD COLUMN 列名 数据类型 
        [PRIMARY KEY]
        [NOT NULL] 
        [DEFAULT 默认值] 
        [UNIQUE]
        [AUTO_INCREMENT]
        [REFERENCES 表名(列名)]
        [CHECK(条件)];
主要是用table_dml_service查看[表名]/table.meta有没有对应列记录。
插入或更新子表外键列时，要检查父表目标键存在。
写入[表名]/table.con和[表名]/table.meta。
处理 table.dat，考察是否有DEFAULT后加入新一列。
这些和对应列名一起存[表名]/table.meta： [NOT NULL] [DEFAULT 默认值] [AUTO_INCREMENT]
这些存[表名]/table.con：[PRIMARY KEY] [UNIQUE] [CHECK(条件)] [FOREIGN KEY 列名 REFERENCES 表名(列名)]

ALTER TABLE
    DELETE COLUMN 列名;
主要是用table_dml_service查看[表名]/table.meta有没有对应列记录。
然后用table_dml_service删掉[表名]/table.dat文件的对应的整列内容记录。
用table_dml_service 删除 [表名]/table.con 中该列的所有约束。
最后用table_dml_service删掉 [表名]/table.meta里的对应列记录。

ALTER TABLE
    MODIFY COLUMN 列名 新数据类型
        [PRIMARY KEY]
        [NOT NULL] 
        [DEFAULT 默认值] 
        [UNIQUE] 
        [AUTO_INCREMENT]
        [REFERENCES 表名(列名)]
        [CHECK(条件)];
主要是用table_dml_service查看[表名]/table.meta有没有对应列记录。
插入或更新子表外键列时，要检查父表目标键存在。
如果新类型或约束变化影响已有数据，需要先尝试转换，如果转不了则失败。
然后用table_dml_service写入[表名]/table.con和[表名]/table.meta。
这些和对应列存一起[表名]/table.meta [NOT NULL] [DEFAULT 默认值] [AUTO_INCREMENT]
这些存[表名]/table.con [PRIMARY KEY] [UNIQUE] [CHECK(条件)] [FOREIGN KEY 列名 REFERENCES 表名(列名)]


(一个constraint只能有一个约束类型)
(PK全局只能有一个)
ALTER TABLE
    ADD CONSTRAINT 约束名 
        [PRIMARY KEY (列名 [, ...])]
        [FOREIGN KEY 列名 REFERENCES 表名(列名)]
        [UNIQUE(列名 [,...])]
        [CHECK(条件)];
主要是用table_dml_service查看[表名]/table.con有没有对应约束的记录。
考察[PRIMARY KEY]是否已有其它约束条件定义过。
新定义的约束条件如果是UNIQUE或FOREIGN KEY，要考察涉及到的 列名/列名集合 有没有被定义过对应约束条件。
当前FK按 NO ACTION / RESTRICT
然后用table_dml_service写入[表名]/table.con。

ALTER TABLE
    MODIFY CONSTRAINT 约束名 
        [FOREIGN KEY 列名 REFERENCES 表名(列名)]
        [CHECK(条件)];
主要是用table_dml_service查看[表名]/table.con有没有对应约束的记录。
新定义的约束条件如果是UNIQUE或FOREIGN KEY，要考察涉及到的 列名/列名集合 有没有被定义过对应约束条件。
插入或更新子表外键列时，要检查父表目标键存在。
当前FK按 NO ACTION / RESTRICT
然后用table_dml_service更新[表名]/table.con对应名称的约束。

ALTER TABLE
    DELETE CONSTRAINT 约束名 
主要是用table_dml_service查看[表名]/table.con有没有对应约束的记录。
然后用table_dml_service删除[表名]/table.con对应名称的约束。


SHOW TABLES
主要是用table_dml_service做一次对.tab的展示。

DESC 表名;
主要是用table_dml_service查看[表名]/table.meta。

SHOW CREATE TABLE 表名;
主要是用table_dml_service查看[表名]/table.meta和[表名]/table.con，输出合并后的内容。




4. /service/tuple_task.cpp 完成"表中元组"的 dml 函数实现。

SELECT 列名/* FROM 表名;
tuple_task.cpp 接收已经解析好的参数，先根据当前会话信息确定目标数据库，再定位到 [表名]/table.dat。
同时读取 [表名]/table.meta 和 [表名]/table.con，作为本次查询的列定义与约束信息。
然后调用 table_dml_service 完成查询。
当前阶段若带条件，则先只支持无条件或简单列等值匹配。
若传入 *，则返回整张结果表；若传入列名集合，则先检查列是否存在，再返回投影后的结果。

INSERT INTO 表名 VALUES (...);
tuple_task.cpp 接收已经解析好的参数，先根据当前会话信息确定目标数据库，再定位到 [表名]/table.dat。
同时读取 [表名]/table.meta 和 [表名]/table.con，作为本次插入的列定义与约束信息。
然后调用 table_dml_service 完成插入。
插入时要检查：列数是否匹配、数据类型是否可转换、NOT NULL 是否满足、DEFAULT 是否需要补入、AUTO_INCREMENT 是否需要自动生成、PRIMARY KEY / UNIQUE 是否冲突。
若涉及 FOREIGN KEY，则要检查 referenced_table 和 referenced_columns 对应的父表目标键是否存在。当前 FK 按 NO ACTION / RESTRICT。

DELETE FROM 表名;
tuple_task.cpp 接收已经解析好的参数，先根据当前会话信息确定目标数据库，再定位到 [表名]/table.dat。
同时读取 [表名]/table.meta 和 [表名]/table.con，作为本次删除的列定义与约束信息。
然后调用 table_dml_service 完成删除。
当前阶段若带条件，则先只支持无条件或简单列等值匹配。
若当前表中的记录正被其它表的 FOREIGN KEY 引用，则当前按 NO ACTION / RESTRICT 拒绝删除。

UPDATE 表名 SET 列名=值;
tuple_task.cpp 接收已经解析好的参数，先根据当前会话信息确定目标数据库，再定位到 [表名]/table.dat。
同时读取 [表名]/table.meta 和 [表名]/table.con，作为本次更新的列定义与约束信息。
然后调用 table_dml_service 完成更新。
更新时要检查：目标列是否存在、新值是否能转换成目标类型、NOT NULL 是否满足、PRIMARY KEY / UNIQUE 是否冲突。
当前阶段若带条件，则先只支持无条件或简单列等值匹配。
若更新的是子表外键列，则要检查父表目标键存在；若更新的是父表被引用键，而仍有子表记录引用它，则当前按 NO ACTION / RESTRICT 拒绝更新。
