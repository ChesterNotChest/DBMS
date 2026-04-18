阶段三：(ddl、dml 函数级实现)
1. 先完成 table_dml_service.cpp。它本质是对任意二维表执行通用行列操作的 service。

SELECT 列名/* FROM 表名;
主要根据参数传入的

INSERT INTO 表名 VALUES (...);


DELETE FROM 表名;


UPDATE 表名 SET 列名=值;






2. database_task.cpp 完成"数据库"的ddl函数实现。

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


3. table_task.cpp 完成"表"的ddl函数实现。

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




4. tuple_task.cpp 完成"表"的dml函数实现。

