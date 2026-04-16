目标：

阶段一：(文件读写)
1. 理解我们将使用的文件存储设计思路：
   数据库=目录；表=一个文件
2. 将每个表的具体操作（增删改查）抽象到repos里，分别放到database_repo.py，table_repo.py，field_repo.py。
-------
阶段二：(ddl、dml雏形)
1. 增删改查，均抽象成tasks，分别放到database_task.cpp，meta_repo.cpp，table_repo.cpp（以函数的形式实现select、update等ddl和dml方法）
2. 为tasks设计单元测试用例，分别放到test_database_task.cpp，test_meta_repo.cpp，test_table_repo.cpp
（注：database_tasks.py里的变动需要修改meta.db的内容）
-------
阶段三：
1. 理解我们的命令处理框架设计理念：模板匹配（每个task方法匹配一种SQL模板）。
2. 设计utils/sql_handler.util，读取字符串（sql），返回字符串（方法名/方法编号）用于处置各种模式匹配，以决定执行哪个task方法。
3. 设计run.py。启动时检查有没有meta.db，没有就创一个，作为初始化。否则直接打开一个终端，允许输入sql进行操作。
-------
↑当前目标