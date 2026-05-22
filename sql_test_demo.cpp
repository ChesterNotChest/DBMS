
#include "client/client_session_pool.h"
#include "client/sql_client_engine.h"
#include "service/service.h"

#include &lt;QCoreApplication&gt;
#include &lt;QDir&gt;
#include &lt;QTextStream&gt;
#include &lt;iostream&gt;

void printResult(const service::SqlExecResult &amp;result)
{
    QTextStream out(stdout);
    
    if (!result.success) {
        out &lt;&lt; "ERROR: " &lt;&lt; result.errorMessage &lt;&lt; Qt::endl;
        return;
    }
    
    if (!result.text.isEmpty()) {
        out &lt;&lt; result.text &lt;&lt; Qt::endl;
    }
    
    if (result.selectResult.success) {
        out &lt;&lt; Qt::endl &lt;&lt; "Query Result:" &lt;&lt; Qt::endl;
        out &lt;&lt; "Columns: " &lt;&lt; result.selectResult.resultTable.columns.join(", ") &lt;&lt; Qt::endl;
        for (const auto &amp;row : result.selectResult.resultTable.rows) {
            out &lt;&lt; "  " &lt;&lt; row.join(" | ") &lt;&lt; Qt::endl;
        }
        out &lt;&lt; result.selectResult.resultTable.rows.size() &lt;&lt; " row(s) in set" &lt;&lt; Qt::endl;
    }
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    // 初始化数据根目录
    QString dataRoot = QDir::temp().absoluteFilePath("DBMS_demo");
    QDir().mkpath(dataRoot);
    
    // 清理旧数据
    QDir dir(dataRoot);
    if (dir.exists()) {
        dir.removeRecursively();
    }
    dir.mkpath(".");
    
    QTextStream out(stdout);
    out &lt;&lt; "========================================" &lt;&lt; Qt::endl;
    out &lt;&lt; "   DBMS SQL 测试程序" &lt;&lt; Qt::endl;
    out &lt;&lt; "========================================" &lt;&lt; Qt::endl;
    out &lt;&lt; Qt::endl;
    
    // 创建客户端会话
    client::ClientSessionPool pool;
    client::SqlClientEngine engine(&amp;pool);
    QString clientId = pool.createSession(dataRoot);
    
    // 登录root用户
    out &lt;&lt; "--- 登录 root 用户 ---" &lt;&lt; Qt::endl;
    auto loginResult = engine.login(clientId, "root", "");
    printResult(loginResult);
    if (!loginResult.success) {
        return 1;
    }
    out &lt;&lt; Qt::endl;
    
    // 1. 创建数据库
    out &lt;&lt; "--- 1. 创建数据库: test_db ---" &lt;&lt; Qt::endl;
    auto result1 = engine.executeSql(clientId, "CREATE DATABASE test_db;");
    printResult(result1);
    out &lt;&lt; Qt::endl;
    
    // 2. 查看数据库
    out &lt;&lt; "--- 2. 查看所有数据库 ---" &lt;&lt; Qt::endl;
    auto result2 = engine.executeSql(clientId, "SHOW DATABASES;");
    printResult(result2);
    out &lt;&lt; Qt::endl;
    
    // 3. 使用数据库
    out &lt;&lt; "--- 3. 使用 test_db ---" &lt;&lt; Qt::endl;
    auto result3 = engine.executeSql(clientId, "USE test_db;");
    printResult(result3);
    out &lt;&lt; Qt::endl;
    
    // 4. 创建表
    out &lt;&lt; "--- 4. 创建表: students ---" &lt;&lt; Qt::endl;
    QString createTableSql = 
        "CREATE TABLE students ("
        "    id INT PRIMARY KEY AUTO_INCREMENT,"
        "    name VARCHAR(50) NOT NULL,"
        "    age INT,"
        "    grade VARCHAR(20),"
        "    email VARCHAR(100)"
        ");";
    auto result4 = engine.executeSql(clientId, createTableSql);
    printResult(result4);
    out &lt;&lt; Qt::endl;
    
    // 5. 查看表
    out &lt;&lt; "--- 5. 查看所有表 ---" &lt;&lt; Qt::endl;
    auto result5 = engine.executeSql(clientId, "SHOW TABLES;");
    printResult(result5);
    out &lt;&lt; Qt::endl;
    
    // 6. 查看表结构
    out &lt;&lt; "--- 6. 查看 students 表结构 ---" &lt;&lt; Qt::endl;
    auto result6 = engine.executeSql(clientId, "DESC students;");
    printResult(result6);
    out &lt;&lt; Qt::endl;
    
    // 7. 插入数据
    out &lt;&lt; "--- 7. 插入数据 ---" &lt;&lt; Qt::endl;
    auto result7a = engine.executeSql(clientId, "INSERT INTO students (name, age, grade, email) VALUES ('张三', 20, '大二', 'zhangsan@example.com');");
    printResult(result7a);
    
    auto result7b = engine.executeSql(clientId, "INSERT INTO students (name, age, grade, email) VALUES ('李四', 21, '大三', 'lisi@example.com');");
    printResult(result7b);
    
    auto result7c = engine.executeSql(clientId, "INSERT INTO students (name, age, grade, email) VALUES ('王五', 19, '大一', 'wangwu@example.com');");
    printResult(result7c);
    out &lt;&lt; Qt::endl;
    
    // 8. 查询数据
    out &lt;&lt; "--- 8. 查询所有学生 ---" &lt;&lt; Qt::endl;
    auto result8 = engine.executeSql(clientId, "SELECT * FROM students;");
    printResult(result8);
    out &lt;&lt; Qt::endl;
    
    // 9. 条件查询
    out &lt;&lt; "--- 9. 查询大二学生 ---" &lt;&lt; Qt::endl;
    auto result9 = engine.executeSql(clientId, "SELECT name, email FROM students WHERE grade = '大二';");
    printResult(result9);
    out &lt;&lt; Qt::endl;
    
    // 10. 更新数据
    out &lt;&lt; "--- 10. 更新张三的年龄为 21 ---" &lt;&lt; Qt::endl;
    auto result10 = engine.executeSql(clientId, "UPDATE students SET age = 21 WHERE name = '张三';");
    printResult(result10);
    out &lt;&lt; Qt::endl;
    
    // 11. 查看更新后的数据
    out &lt;&lt; "--- 11. 查看更新后的数据 ---" &lt;&lt; Qt::endl;
    auto result11 = engine.executeSql(clientId, "SELECT * FROM students;");
    printResult(result11);
    out &lt;&lt; Qt::endl;
    
    // 12. 删除数据
    out &lt;&lt; "--- 12. 删除王五 ---" &lt;&lt; Qt::endl;
    auto result12 = engine.executeSql(clientId, "DELETE FROM students WHERE name = '王五';");
    printResult(result12);
    out &lt;&lt; Qt::endl;
    
    // 13. 查看最终数据
    out &lt;&lt; "--- 13. 查看最终数据 ---" &lt;&lt; Qt::endl;
    auto result13 = engine.executeSql(clientId, "SELECT * FROM students;");
    printResult(result13);
    out &lt;&lt; Qt::endl;
    
    out &lt;&lt; "========================================" &lt;&lt; Qt::endl;
    out &lt;&lt; "   测试完成！" &lt;&lt; Qt::endl;
    out &lt;&lt; "========================================" &lt;&lt; Qt::endl;
    
    return 0;
}
