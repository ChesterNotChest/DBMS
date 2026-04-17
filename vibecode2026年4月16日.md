TODO

接下来，我将要实现通用表的对象级结构。
大概有如下要求：

1. 补一份对象化的表结构描述，定义到 /constants/table_def.h。

参考如下构建：
```

// 字段类型
enum class ColumnType {
    INT,
    VARCHAR,
    FLOAT
};

// 约束类型（表约束）
enum class ConstraintType {
    PRIMARY_KEY,
    UNIQUE,
    CHECK
};

// --------------------------
// 1. 一列的结构（含列级约束）
// --------------------------
struct Column {
    QString name;             // 列名
    ColumnType type;          // 类型 INT/VARCHAR
    int length = 0;           // VARCHAR(100) 的长度
    bool notNull = false;     // NOT NULL
    QString defaultValue;     // DEFAULT
    QString check;            // 列级 CHECK(...)
};

// --------------------------
// 2. 表约束
// --------------------------
struct Constraint {
    QString name;             // 约束名
    ConstraintType type;      // PK/UNIQUE/CHECK
    QList<QString> columns;   // 作用在哪些列
    QString checkClause;      // CHECK 内容
};

// --------------------------
// 3. 一张表的完整结构（这就是你要的：对象级表结构）
// --------------------------
struct TableSchema {
    QString tableName;                // 表名
    QList<Column> columns;            // 所有列
    QList<Constraint> constraints;    // 所有表约束
};
```

2. 补一个 .con 的构建逻辑，用来存储约束条件。需要像 meta_repo.cpp那样构建。

3. 对于已经写好的meta和database，也通过实例化刚刚写好的对象化的表描述来补上必要的结构定义，以统一二维表的操作。











--- 如下暂缓实现 ---
--- 如下暂缓实现 ---
--- 如下暂缓实现 ---
阶段三：(ddl、dml 函数级实现)
1. 增删改查，均抽象成tasks，分别放到database_task.cpp，meta_task.cpp，table_task.cpp。基本的二维表增删改查写到table_dml_service.cpp，供DDL修改meta时系统自己取用、供DML修改用户数据时直接取用。可能需要的类定义、常量定义、库引用放到services.h。

2. 先完成 table_dml_service.cpp。它本质是用table_def来存入单个二维表的工具。

3. database_task.cpp 完成"数据库"的ddl函数实现。

主要完成CREATE DATABASE; DROP DATABASE; USE; SHOW DATABASES; 的函数实现。

    (1)CREATE DATABASE后用table_dml_service检查数据库是否存在同名表。若不存在，则调用repo方法构建数据库文件夹，构建数据库的meta文件。最后用table_dml_service写入记录。
    (2)DROP DATABASE后先用table_dml_service检查数据库是否存在。如果存在，则调用repo方法删除数据库文件夹。然后，调用repo方法删除meta文件。最后用table_dml_service删除database_repo管理的二维表的记录。
    (3)USE后将currentDatabase这样的会话信息存到service.h里。
    SHOW DATABASES使用currentDatabase的select方法。




4. meta_task.cpp 完成"表"的ddl函数实现。

首要事项：
这两个属于扩展功能，暂不实现。下文出现只是备忘。
        [FOREIGN KEY 表名(列名)]
        [CHECK(条件)]

具体工作是：
CREATE TABLE 表名 (
    列名1 数据类型 
    [NOT NULL] 
    [DEFAULT 默认值] 
    [UNIQUE] 
    [PRIMARY KEY] 
    [AUTO_INCREMENT]
    [CHECK(条件)],
)
DROP TABLE; 

ALTER TABLE
    ADD COLUMN 列名 数据类型 
        [NOT NULL] 
        [DEFAULT 默认值] 
        [UNIQUE(列名[,列名...])] 
        [AUTO_INCREMENT]
        [CHECK(条件)];
    DELETE COLUMN 列名;
    MODIFY COLUMN 列名 新数据类型
        [NOT NULL] 
        [DEFAULT 默认值] 
        [UNIQUE] 
        [AUTO_INCREMENT]
        [CHECK(条件)];

    ADD CONSTRAINT 约束名 
        [PRIMARY KEY ()]
        [FOREIGN KEY 表名(列名)]
        [CHECK(条件)]
    MODIFY CONSTAINT 约束名 
        [FOREIGN KEY 表名(列名)]
        [CHECK(条件)]
        [PRIMARY KEY ()]

    ADD PRIMARY KEY (列1 [, 列2...]);
    DROP PRIMARY KEY;




CREATE CONSTRAINT; DROP CONSTRAINT; 







5. 为tasks设计单元测试用例，分别放到test_database_task.cpp，test_meta_repo.cpp，test_table_repo.cpp