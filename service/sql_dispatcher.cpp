/**
 * sql_dispatcher.cpp — SQL 命令分发器实现
 *
 * 所有业务操作都走 service 层，不直接操作 repo 或文件。
 */
#include "sql_dispatcher.h"
#include "../utils/service_common/service_common.h"
#include <QDebug>

namespace service {

// ============================================================
//  统一入口
// ============================================================
SqlExecResult SqlDispatcher::execute(const QString& sql) {
    auto parsed = sqlparser::parseSql(sql);
    return dispatch(parsed);
}

SqlExecResult SqlDispatcher::dispatch(const sqlparser::ParseResult& p) {
    if (!p.success)
        return {false, p.errorMessage, p.errorMessage, -1, {}, p.commandType, p.payload};

    const QString& cmd = p.commandType;

    auto fillMeta = [&](SqlExecResult &&r) -> SqlExecResult {
        r.commandType = cmd;
        r.payload = p.payload;
        return std::move(r);
    };

    if (cmd == "CREATE_DATABASE") return fillMeta(execCreateDatabase(p));
    if (cmd == "DROP_DATABASE")   return fillMeta(execDropDatabase(p));
    if (cmd == "USE_DATABASE")    return fillMeta(execUseDatabase(p));
    if (cmd == "SHOW_DATABASES")  return fillMeta(execShowDatabases(p));

    if (cmd == "CREATE_TABLE")    return fillMeta(execCreateTable(p));
    if (cmd == "DROP_TABLE")      return fillMeta(execDropTable(p));
    if (cmd == "ALTER_TABLE")     return fillMeta(execAlterTable(p));
    if (cmd == "SHOW_TABLES")        return fillMeta(execShowTables(p));
    if (cmd == "DESC_TABLE")        return fillMeta(execDescTable(p));
    if (cmd == "SHOW_CREATE_TABLE") return fillMeta(execShowCreateTable(p));

    if (cmd == "SELECT") return fillMeta(execSelect(p));
    if (cmd == "INSERT") return fillMeta(execInsert(p));
    if (cmd == "UPDATE") return fillMeta(execUpdate(p));
    if (cmd == "DELETE") return fillMeta(execDelete(p));

    return {false, "Unknown command: " + cmd, "", -1, {}, cmd, p.payload};
}

// ============================================================
//  数据库级
// ============================================================
SqlExecResult SqlDispatcher::execCreateDatabase(const sqlparser::ParseResult& p) {
    QString name = p.payload["databaseName"].toString();
    auto r = database_service::createDatabase(name);
    if (r.success)
        return {true, {}, QString("Database '%1' created").arg(name)};
    return {false, r.errorMessage};
}

SqlExecResult SqlDispatcher::execDropDatabase(const sqlparser::ParseResult& p) {
    QString name = p.payload["databaseName"].toString();
    auto r = database_service::dropDatabase(name);
    if (r.success)
        return {true, {}, QString("Database '%1' dropped").arg(name)};
    return {false, r.errorMessage};
}

SqlExecResult SqlDispatcher::execUseDatabase(const sqlparser::ParseResult& p) {
    QString name = p.payload["databaseName"].toString();
    auto r = database_service::useDatabase(name);
    if (r.success)
        return {true, {}, QString("Using database '%1'").arg(name)};
    return {false, r.errorMessage};
}

SqlExecResult SqlDispatcher::execShowDatabases(const sqlparser::ParseResult&) {
    auto r = database_service::showDatabases();
    if (r.success)
        return {true, {}, formatSelectResult(r), 0, r};
    return {false, r.errorMessage};
}

// ============================================================
//  表级
// ============================================================
SqlExecResult SqlDispatcher::execCreateTable(const sqlparser::ParseResult& p) {
    QString tableName = p.payload["tableName"].toString();

    // 构建 TableSchema
    tabledef::TableSchema schema;
    schema.tableName = tableName;

    auto cols = p.payload["columns"].value<QVector<sqlparser::ColumnDef>>();
    for (const auto& c : cols) {
        ColumnDefinition definition;
        tabledef::Column col;
        col.name = c.name;
        col.notNull = c.notNull;
        col.length = c.length > 0 ? c.length : 255;
        col.defaultValue = c.defaultValue;
        col.autoIncrement = c.autoIncrement;
        col.check = c.checkClause;

        QString t = c.type.toUpper();
        if (t == "INT" || t == "INTEGER")       col.type = tabledef::ColumnType::Int;
        else if (t == "FLOAT" || t == "DOUBLE")  col.type = tabledef::ColumnType::Float;
        else col.type = tabledef::ColumnType::Varchar;

        definition.column = col;
        definition.primaryKey = c.primaryKey;
        definition.unique = c.unique;
        definition.referencedTable = c.referencesTable;
        if (!c.referencesColumn.isEmpty()) {
            definition.referencedColumns = {c.referencesColumn};
        }
        definition.checkClause = c.checkClause;

        schema.columns.append(col);
        const QList<tabledef::Constraint> generatedConstraints = buildGeneratedConstraints(definition);
        for (const tabledef::Constraint &constraint : generatedConstraints) {
            schema.constraints.append(constraint);
        }
    }

    auto r = table_service::createTable(tableName, schema);
    if (r.success)
        return {true, {}, QString("Table '%1' created (%2 columns)").arg(tableName).arg(schema.columns.size())};
    return {false, r.errorMessage};
}

SqlExecResult SqlDispatcher::execDropTable(const sqlparser::ParseResult& p) {
    QString tableName = p.payload["tableName"].toString();
    auto r = table_service::dropTable(tableName);
    if (r.success)
        return {true, {}, QString("Table '%1' dropped").arg(tableName)};
    return {false, r.errorMessage};
}

SqlExecResult SqlDispatcher::execAlterTable(const sqlparser::ParseResult& p) {
    QString tableName = p.payload["tableName"].toString();
    QString action = p.payload["alterAction"].toString();

    if (action == "ADD_COLUMN") {
        ColumnDefinition colDef;
        colDef.column.name = p.payload["columnName"].toString();
        auto r = table_service::addColumn(tableName, colDef);
        if (r.success) return {true, {}, "Column added"};
        return {false, r.errorMessage};
    }
    if (action == "DROP_COLUMN") {
        QString colName = p.payload["columnName"].toString();
        auto r = table_service::deleteColumn(tableName, colName);
        if (r.success) return {true, {}, "Column dropped"};
        return {false, r.errorMessage};
    }
    if (action == "MODIFY_COLUMN") {
        ColumnDefinition colDef;
        colDef.column.name = p.payload["columnName"].toString();
        auto r = table_service::modifyColumn(tableName, colDef.column.name, colDef);
        if (r.success) return {true, {}, "Column modified"};
        return {false, r.errorMessage};
    }
    if (action == "ADD_CONSTRAINT") {
        tabledef::Constraint con;
        auto r = table_service::addConstraint(tableName, con);
        if (r.success) return {true, {}, "Constraint added"};
        return {false, r.errorMessage};
    }
    if (action == "DROP_CONSTRAINT") {
        QString conName = p.payload["constraintName"].toString();
        auto r = table_service::deleteConstraint(tableName, conName);
        if (r.success) return {true, {}, "Constraint dropped"};
        return {false, r.errorMessage};
    }

    return {false, "ALTER TABLE: unsupported action " + action};
}

SqlExecResult SqlDispatcher::execShowTables(const sqlparser::ParseResult&) {
    auto r = table_service::showTables();
    if (r.success)
        return {true, {}, formatSelectResult(r), 0, r};
    return {false, r.errorMessage};
}

SqlExecResult SqlDispatcher::execDescTable(const sqlparser::ParseResult& p) {
    QString tableName = p.payload["tableName"].toString();
    auto r = table_service::describeTable(tableName);
    if (r.success)
        return {true, {}, r.text};
    return {false, r.errorMessage};
}

SqlExecResult SqlDispatcher::execShowCreateTable(const sqlparser::ParseResult& p) {
    QString tableName = p.payload["tableName"].toString();
    auto r = table_service::showCreateTable(tableName);
    if (r.success)
        return {true, {}, r.text};
    return {false, r.errorMessage};
}

// ============================================================
//  元组级
// ============================================================
SqlExecResult SqlDispatcher::execSelect(const sqlparser::ParseResult& p) {
    if (currentDatabase.isEmpty())
        return {false, "No database selected. Use USE database_name;"};

    QString table = p.payload["tableName"].toString();
    QStringList projection = p.payload["projection"].toStringList();
    // WHERE 尚未完整实现，暂不传递条件
    QList<SimpleCondition> conditions;

    auto r = tuple_service::selectRows(table, projection, conditions);
    if (r.success)
        return {true, {}, formatSelectResult(r), r.affectedRowCount, r};
    return {false, r.errorMessage};
}

SqlExecResult SqlDispatcher::execInsert(const sqlparser::ParseResult& p) {
    if (currentDatabase.isEmpty())
        return {false, "No database selected"};

    QString table = p.payload["tableName"].toString();
    int rowCount = p.payload["rowCount"].toInt();
    auto rows = p.payload["rows"].value<QVector<QVector<QVariant>>>();
    auto colNamesFromParser = p.payload["columnNames"].value<QStringList>();

    // 加载表 schema 以获取真实列名（用于无列名列表的 INSERT）
    QStringList colNames = colNamesFromParser;
    if (colNames.isEmpty()) {
        QString err;
        tabledef::TableSchema schema = service::loadUserTableSchema(table, &err);
        if (!err.isEmpty())
            return {false, err};
        colNames = tabledef::schemaColumnNames(schema);
    }

    QList<QMap<QString, QString>> rowData;
    for (const auto& r : rows) {
        QMap<QString, QString> row;
        for (int i = 0; i < r.size(); ++i) {
            // 有显式列名时用列名；无列名时用 schema 的真实列名
            QString key = (i < colNames.size()) ? colNames[i] : QString::number(i);
            row[key] = r[i].toString();
        }
        rowData.append(row);
    }

    auto r = tuple_service::insertRows(table, rowData);
    if (r.success)
        return {true, {}, QString("%1 row(s) inserted into '%2'").arg(rowCount).arg(table),
                r.affectedRowCount};
    return {false, r.errorMessage};
}

SqlExecResult SqlDispatcher::execUpdate(const sqlparser::ParseResult& p) {
    if (currentDatabase.isEmpty())
        return {false, "No database selected"};

    QString table = p.payload["tableName"].toString();
    auto assignments = p.payload["assignments"].value<QMap<QString, QVariant>>();
    // WHERE 尚未完整实现，暂不传递条件
    QList<SimpleCondition> conditions;

    QMap<QString, QString> assignMap;
    for (auto it = assignments.begin(); it != assignments.end(); ++it)
        assignMap[it.key()] = it.value().toString();

    auto r = tuple_service::updateRows(table, assignMap, conditions);
    if (r.success)
        return {true, {}, QString("%1 row(s) updated in '%2'").arg(r.affectedRowCount).arg(table),
                r.affectedRowCount};
    return {false, r.errorMessage};
}

SqlExecResult SqlDispatcher::execDelete(const sqlparser::ParseResult& p) {
    if (currentDatabase.isEmpty())
        return {false, "No database selected"};

    QString table = p.payload["tableName"].toString();
    // WHERE 尚未完整实现，暂不传递条件
    QList<SimpleCondition> conditions;

    auto r = tuple_service::deleteRows(table, conditions);
    if (r.success)
        return {true, {}, QString("%1 row(s) deleted from '%2'").arg(r.affectedRowCount).arg(table),
                r.affectedRowCount};
    return {false, r.errorMessage};
}

// ============================================================
//  辅助
// ============================================================
QString SqlDispatcher::formatSelectResult(const SelectRowsResult& r) {
    if (!r.success) return "Error: " + r.errorMessage;

    QString out;
    // 表头
    const auto& cols = r.resultTable.columns;
    for (int i = 0; i < cols.size(); ++i)
        out += cols[i] + "\t";
    out += "\n";

    // 分隔线
    for (int i = 0; i < cols.size(); ++i)
        out += QString(cols[i].length(), '-') + "\t";
    out += "\n";

    // 数据
    for (const auto& row : r.resultTable.rows) {
        for (const auto& val : row)
            out += val + "\t";
        out += "\n";
    }

    out += QString("%1 row(s) in set").arg(r.resultTable.rows.size());
    return out;
}

} // namespace service
