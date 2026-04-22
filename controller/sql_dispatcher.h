/**
 * sql_dispatcher.h — SQL 命令分发器
 *
 * 职责：接收 parser 的 ParseResult，调用对应的 service 方法。
 * 所有业务操作走 service 层，不直接操作 repo 或文件。
 */
#ifndef SERVICE_SQL_DISPATCHER_H
#define SERVICE_SQL_DISPATCHER_H

#include "../utils/sql_parser/sql_parser.h"
#include "../service/service.h"

namespace service {

/**
 * SQL 执行结果，统一返回 success / errorMessage / text
 */
struct SqlExecResult {
    bool        success       = false;
    QString     errorMessage;
    QString     text;            // 用于 UI 显示的文本
    int         affectedRows = -1;
    SelectRowsResult selectResult;  // SELECT 的结果
    QString     commandType;     // 解析器返回的命令类型
    QVariantMap payload;         // 解析器返回的参数（如 databaseName, tableName）
};

/**
 * SQL 命令分发器
 * 纯桥接层：ParseResult → service 调用 → SqlExecResult
 */
class SqlDispatcher {
public:
    /** 执行一条 SQL，自动识别类型并分发 */
    SqlExecResult execute(const QString& sql);

    /** 执行已解析的 ParseResult */
    SqlExecResult dispatch(const sqlparser::ParseResult& parsed);

private:
    // 数据库级
    SqlExecResult execCreateDatabase(const sqlparser::ParseResult& p);
    SqlExecResult execDropDatabase(const sqlparser::ParseResult& p);
    SqlExecResult execUseDatabase(const sqlparser::ParseResult& p);
    SqlExecResult execShowDatabases(const sqlparser::ParseResult& p);

    // 表级
    SqlExecResult execCreateTable(const sqlparser::ParseResult& p);
    SqlExecResult execDropTable(const sqlparser::ParseResult& p);
    SqlExecResult execAlterTable(const sqlparser::ParseResult& p);
    SqlExecResult execCreateIndex(const sqlparser::ParseResult& p);
    SqlExecResult execDropIndex(const sqlparser::ParseResult& p);
    SqlExecResult execShowTables(const sqlparser::ParseResult& p);
    SqlExecResult execDescTable(const sqlparser::ParseResult& p);
    SqlExecResult execShowCreateTable(const sqlparser::ParseResult& p);

    // 元组级
    SqlExecResult execSelect(const sqlparser::ParseResult& p);
    SqlExecResult execInsert(const sqlparser::ParseResult& p);
    SqlExecResult execUpdate(const sqlparser::ParseResult& p);
    SqlExecResult execDelete(const sqlparser::ParseResult& p);

    // 辅助
    QString formatSelectResult(const SelectRowsResult& r);
};

} // namespace service

#endif // SERVICE_SQL_DISPATCHER_H
