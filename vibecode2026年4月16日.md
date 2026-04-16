阶段二：(ddl、dml 函数级实现)
1. 增删改查，均抽象成tasks，分别放到database_task.cpp，meta_task.cpp，table_task.cpp。基本的二维表增删改查写到table_dml_service.cpp，供DDL修改meta时系统自己取用、供DML修改用户数据时直接取用。可能需要的类定义、常量定义、库引用放到services.h。

2. database_task.cpp 接受 DDL 语句，主要负责接收CREATE DATABASE; DROP DATABASE; USE;
CREATE DATABASE后用table_dml_service检查数据库是否存在同名表。若不存在，则调用repo方法构建数据库文件夹，构建数据库的meta文件。最后用table_dml_service写入记录。
DROP DATABASE后先用table_dml_service检查数据库是否存在。如果存在，则调用repo方法删除数据库文件夹。然后，调用repo方法删除meta文件。最后用table_dml_service删除database_repo管理的二维表的记录。
USE后将currentDatabase这样的会话信息存到service.h里。

3. meta_task.cpp 接受 DDL 语句，主要负责接受





2. 为tasks设计单元测试用例，分别放到test_database_task.cpp，test_meta_repo.cpp，test_table_repo.cpp