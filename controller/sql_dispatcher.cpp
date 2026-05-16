/**
 * sql_dispatcher.cpp — SQL 命令分发器实现
 *
 * 所有业务操作都走 service 层，不直接操作 repo 或文件。
 */
#include "sql_dispatcher.h"
#include "nest_query.h"
#include "../service/auth_service.h"
#include "../utils/service_common/service_common.h"
#include <QDebug>

namespace service {

namespace {

tabledef::ColumnType columnTypeFromSql(const QString &type)
{
    const QString normalized = type.toUpper();
    if (normalized == QStringLiteral("INT") || normalized == QStringLiteral("INTEGER")) {
        return tabledef::ColumnType::Int;
    }
    if (normalized == QStringLiteral("FLOAT")
        || normalized == QStringLiteral("DOUBLE")
        || normalized == QStringLiteral("REAL")) {
        return tabledef::ColumnType::Float;
    }
    return tabledef::ColumnType::Varchar;
}

tabledef::ConstraintType constraintTypeFromPayload(const QString &type)
{
    const QString normalized = type.toUpper();
    if (normalized == QStringLiteral("PRIMARY_KEY")) return tabledef::ConstraintType::PrimaryKey;
    if (normalized == QStringLiteral("UNIQUE")) return tabledef::ConstraintType::Unique;
    if (normalized == QStringLiteral("FOREIGN_KEY")) return tabledef::ConstraintType::ForeignKey;
    return tabledef::ConstraintType::Check;
}

tabledef::ForeignKeyAction foreignKeyActionFromPayload(const QString &value)
{
    const QString normalized = value.trimmed().toUpper();
    if (normalized == QStringLiteral("CASCADE")) return tabledef::ForeignKeyAction::Cascade;
    if (normalized == QStringLiteral("RESTRICT")) return tabledef::ForeignKeyAction::Restrict;
    if (normalized == QStringLiteral("SET NULL")) return tabledef::ForeignKeyAction::SetNull;
    if (normalized == QStringLiteral("SET DEFAULT")) return tabledef::ForeignKeyAction::SetDefault;
    return tabledef::ForeignKeyAction::NoAction;
}

QString generatedTableConstraintName(const QString &tableName,
                                     const QString &prefix,
                                     const QStringList &columns,
                                     int ordinal)
{
    const QString columnPart = columns.isEmpty()
                                   ? QString::number(ordinal)
                                   : columns.join(QStringLiteral("_"));
    return QStringLiteral("%1_%2_%3").arg(prefix, tableName, columnPart);
}

tabledef::Column columnFromPayload(const QVariantMap &columnMap)
{
    tabledef::Column column;
    column.name = columnMap.value(QStringLiteral("name")).toString();
    column.type = columnTypeFromSql(columnMap.value(QStringLiteral("type")).toString());
    column.length = columnMap.value(QStringLiteral("length"), 255).toInt();
    if (column.length < 0 && column.type == tabledef::ColumnType::Varchar) {
        column.length = 255;
    }
    column.notNull = columnMap.value(QStringLiteral("notNull")).toBool();
    column.defaultValue = columnMap.value(QStringLiteral("defaultValue")).toString();
    column.autoIncrement = columnMap.value(QStringLiteral("autoIncrement")).toBool();
    column.check = columnMap.value(QStringLiteral("checkClause")).toString();
    return column;
}

ColumnDefinition columnDefinitionFromPayload(const QVariantMap &columnMap)
{
    ColumnDefinition definition;
    definition.column = columnFromPayload(columnMap);
    definition.primaryKey = columnMap.value(QStringLiteral("primaryKey")).toBool();
    definition.unique = columnMap.value(QStringLiteral("unique")).toBool();
    definition.referencedTable = columnMap.value(QStringLiteral("referencesTable")).toString();
    definition.referencedColumns = columnMap.value(QStringLiteral("referencedColumns")).toStringList();
    definition.onDeleteAction = foreignKeyActionFromPayload(columnMap.value(QStringLiteral("onDeleteAction")).toString());
    definition.onUpdateAction = foreignKeyActionFromPayload(columnMap.value(QStringLiteral("onUpdateAction")).toString());
    definition.checkClause = columnMap.value(QStringLiteral("checkClause")).toString();
    return definition;
}

tabledef::Constraint constraintFromPayload(const QVariantMap &constraintMap,
                                           const QString &tableName,
                                           int ordinal)
{
    tabledef::Constraint constraint;
    constraint.type = constraintTypeFromPayload(constraintMap.value(QStringLiteral("type")).toString());
    constraint.columns = constraintMap.value(QStringLiteral("columns")).toStringList();
    constraint.referencedTable = constraintMap.value(QStringLiteral("referencedTable")).toString();
    constraint.referencedColumns = constraintMap.value(QStringLiteral("referencedColumns")).toStringList();
    constraint.checkClause = constraintMap.value(QStringLiteral("checkClause")).toString();
    constraint.onDeleteAction = foreignKeyActionFromPayload(constraintMap.value(QStringLiteral("onDeleteAction")).toString());
    constraint.onUpdateAction = foreignKeyActionFromPayload(constraintMap.value(QStringLiteral("onUpdateAction")).toString());

    constraint.name = constraintMap.value(QStringLiteral("name")).toString().trimmed();
    if (constraint.name.isEmpty()) {
        switch (constraint.type) {
        case tabledef::ConstraintType::PrimaryKey:
            constraint.name = generatedTableConstraintName(tableName, QStringLiteral("pk"), constraint.columns, ordinal);
            break;
        case tabledef::ConstraintType::Unique:
            constraint.name = generatedTableConstraintName(tableName, QStringLiteral("uq"), constraint.columns, ordinal);
            break;
        case tabledef::ConstraintType::ForeignKey:
            constraint.name = generatedTableConstraintName(tableName, QStringLiteral("fk"), constraint.columns, ordinal);
            break;
        case tabledef::ConstraintType::Check:
            constraint.name = generatedTableConstraintName(tableName, QStringLiteral("ck"), constraint.columns, ordinal);
            break;
        }
    }

    return constraint;
}

bool payloadHasMap(const sqlparser::ParseResult &p, const QString &key)
{
    return p.payload.contains(key) && p.payload.value(key).typeId() == QMetaType::QVariantMap;
}

bool simpleConditionsFromPayload(const QVariantList &conditionsPayload,
                                 QList<SimpleCondition> *conditions,
                                 QString *error)
{
    if (conditions != nullptr) conditions->clear();
    if (error != nullptr) error->clear();

    for (const QVariant &conditionValue : conditionsPayload) {
        if (conditionValue.typeId() != QMetaType::QVariantMap) {
            if (error != nullptr) *error = QStringLiteral("WHERE payload is incomplete");
            return false;
        }

        const QVariantMap conditionMap = conditionValue.toMap();
        const QString columnName = conditionMap.value(QStringLiteral("columnName")).toString().trimmed();
        if (columnName.isEmpty()) {
            if (error != nullptr) *error = QStringLiteral("WHERE payload is incomplete");
            return false;
        }

        if (conditions != nullptr) {
            conditions->append(SimpleCondition{
                columnName,
                conditionMap.value(QStringLiteral("value")).toString()
            });
        }
    }

    return true;
}

} // namespace

// ============================================================
//  统一入口
// ============================================================
QStringList SqlDispatcher::splitStatements(const QString &sqlScript)
{
    QStringList statements;
    QString current;
    bool inSingleQuote = false;
    bool inDoubleQuote = false;

    for (int i = 0; i < sqlScript.size(); ++i) {
        const QChar ch = sqlScript.at(i);

        if (ch == QLatin1Char('\'') && !inDoubleQuote) {
            current.append(ch);
            if (inSingleQuote && i + 1 < sqlScript.size() && sqlScript.at(i + 1) == QLatin1Char('\'')) {
                current.append(sqlScript.at(i + 1));
                ++i;
                continue;
            }
            inSingleQuote = !inSingleQuote;
            continue;
        }

        if (ch == QLatin1Char('"') && !inSingleQuote) {
            current.append(ch);
            if (inDoubleQuote && i + 1 < sqlScript.size() && sqlScript.at(i + 1) == QLatin1Char('"')) {
                current.append(sqlScript.at(i + 1));
                ++i;
                continue;
            }
            inDoubleQuote = !inDoubleQuote;
            continue;
        }

        if (ch == QLatin1Char(';') && !inSingleQuote && !inDoubleQuote) {
            const QString statement = current.trimmed();
            if (!statement.isEmpty()) {
                statements.append(statement);
            }
            current.clear();
            continue;
        }

        current.append(ch);
    }

    const QString tail = current.trimmed();
    if (!tail.isEmpty()) {
        statements.append(tail);
    }

    return statements;
}

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
    if (cmd == "CREATE_INDEX")    return fillMeta(execCreateIndex(p));
    if (cmd == "DROP_INDEX")      return fillMeta(execDropIndex(p));
    if (cmd == "SHOW_TABLES")        return fillMeta(execShowTables(p));
    if (cmd == "DESC_TABLE")        return fillMeta(execDescTable(p));
    if (cmd == "SHOW_CREATE_TABLE") return fillMeta(execShowCreateTable(p));

    if (cmd == "SELECT") return fillMeta(execSelect(p));
    if (cmd == "INSERT") return fillMeta(execInsert(p));
    if (cmd == "UPDATE") return fillMeta(execUpdate(p));
    if (cmd == "DELETE") return fillMeta(execDelete(p));

    if (cmd == "LOGIN") return fillMeta(execLogin(p));
    if (cmd == "CREATE_USER") return fillMeta(execCreateUser(p));
    if (cmd == "DROP_USER") return fillMeta(execDropUser(p));
    if (cmd == "ALTER_USER") return fillMeta(execAlterUser(p));
    if (cmd == "GRANT_ALL") return fillMeta(execGrantAll(p));
    if (cmd == "REVOKE_ALL") return fillMeta(execRevokeAll(p));

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

    const QVariantList columns = p.payload.value(QStringLiteral("columns")).toList();
    if (columns.isEmpty()) {
        return {false, QStringLiteral("CREATE TABLE: columns payload is empty or incomplete")};
    }

    for (const QVariant &columnValue : columns) {
        const QVariantMap columnMap = columnValue.toMap();
        if (columnMap.value(QStringLiteral("name")).toString().trimmed().isEmpty()) {
            return {false, QStringLiteral("CREATE TABLE: column payload is incomplete")};
        }

        const ColumnDefinition definition = columnDefinitionFromPayload(columnMap);
        schema.columns.append(definition.column);
        const QList<tabledef::Constraint> generatedConstraints = buildGeneratedConstraints(definition);
        for (const tabledef::Constraint &constraint : generatedConstraints) {
            schema.constraints.append(constraint);
        }
    }

    const QVariantList constraints = p.payload.value(QStringLiteral("constraints")).toList();
    for (int i = 0; i < constraints.size(); ++i) {
        const QVariantMap constraintMap = constraints.at(i).toMap();
        if (constraintMap.value(QStringLiteral("type")).toString().trimmed().isEmpty()) {
            return {false, QStringLiteral("CREATE TABLE: constraint payload is incomplete")};
        }
        schema.constraints.append(constraintFromPayload(constraintMap, tableName, i + 1));
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
        if (!payloadHasMap(p, QStringLiteral("column"))) {
            return {false, QStringLiteral("ALTER TABLE ADD COLUMN requires a complete column payload")};
        }
        const QVariantMap columnMap = p.payload.value(QStringLiteral("column")).toMap();
        if (columnMap.value(QStringLiteral("name")).toString().trimmed().isEmpty()) {
            return {false, QStringLiteral("ALTER TABLE ADD COLUMN requires a complete column payload")};
        }
        ColumnDefinition colDef = columnDefinitionFromPayload(columnMap);
        auto r = table_service::addColumn(tableName, colDef);
        if (r.success) return {true, {}, "Column added"};
        return {false, r.errorMessage};
    }
    if (action == "DROP_COLUMN") {
        QString colName = p.payload["columnName"].toString();
        if (colName.trimmed().isEmpty()) {
            return {false, QStringLiteral("ALTER TABLE DROP COLUMN requires columnName")};
        }
        auto r = table_service::deleteColumn(tableName, colName);
        if (r.success) return {true, {}, "Column dropped"};
        return {false, r.errorMessage};
    }
    if (action == "MODIFY_COLUMN") {
        if (!payloadHasMap(p, QStringLiteral("column"))) {
            return {false, QStringLiteral("ALTER TABLE MODIFY COLUMN requires a complete column payload")};
        }
        const QVariantMap columnMap = p.payload.value(QStringLiteral("column")).toMap();
        if (columnMap.value(QStringLiteral("name")).toString().trimmed().isEmpty()) {
            return {false, QStringLiteral("ALTER TABLE MODIFY COLUMN requires a complete column payload")};
        }
        ColumnDefinition colDef = columnDefinitionFromPayload(columnMap);
        auto r = table_service::modifyColumn(tableName, colDef.column.name, colDef);
        if (r.success) return {true, {}, "Column modified"};
        return {false, r.errorMessage};
    }
    if (action == "ADD_CONSTRAINT") {
        if (!payloadHasMap(p, QStringLiteral("constraint"))) {
            return {false, QStringLiteral("ALTER TABLE ADD CONSTRAINT requires a complete constraint payload")};
        }
        const QVariantMap constraintMap = p.payload.value(QStringLiteral("constraint")).toMap();
        if (constraintMap.value(QStringLiteral("type")).toString().trimmed().isEmpty()) {
            return {false, QStringLiteral("ALTER TABLE ADD CONSTRAINT requires a complete constraint payload")};
        }
        tabledef::Constraint con = constraintFromPayload(constraintMap, tableName, 1);
        auto r = table_service::addConstraint(tableName, con);
        if (r.success) return {true, {}, "Constraint added"};
        return {false, r.errorMessage};
    }
    if (action == "MODIFY_CONSTRAINT") {
        if (!payloadHasMap(p, QStringLiteral("constraint"))
            || p.payload.value(QStringLiteral("constraintName")).toString().trimmed().isEmpty()) {
            return {false, QStringLiteral("ALTER TABLE MODIFY CONSTRAINT requires complete constraint payload")};
        }
        const QString constraintName = p.payload.value(QStringLiteral("constraintName")).toString();
        QVariantMap constraintMap = p.payload.value(QStringLiteral("constraint")).toMap();
        if (constraintMap.value(QStringLiteral("type")).toString().trimmed().isEmpty()) {
            return {false, QStringLiteral("ALTER TABLE MODIFY CONSTRAINT requires complete constraint payload")};
        }
        if (constraintMap.value(QStringLiteral("name")).toString().trimmed().isEmpty()) {
            constraintMap.insert(QStringLiteral("name"), constraintName);
        }
        tabledef::Constraint con = constraintFromPayload(constraintMap, tableName, 1);
        auto r = table_service::modifyConstraint(tableName, constraintName, con);
        if (r.success) return {true, {}, "Constraint modified"};
        return {false, r.errorMessage};
    }
    if (action == "DROP_CONSTRAINT") {
        QString conName = p.payload["constraintName"].toString();
        if (conName.trimmed().isEmpty()) {
            return {false, QStringLiteral("ALTER TABLE DROP CONSTRAINT requires constraintName")};
        }
        auto r = table_service::deleteConstraint(tableName, conName);
        if (r.success) return {true, {}, "Constraint dropped"};
        return {false, r.errorMessage};
    }

    return {false, "ALTER TABLE: unsupported action " + action};
}

SqlExecResult SqlDispatcher::execCreateIndex(const sqlparser::ParseResult& p) {
    const QString tableName = p.payload.value(QStringLiteral("tableName")).toString().trimmed();
    const QString indexName = p.payload.value(QStringLiteral("indexName")).toString().trimmed();
    const QStringList columnNames = p.payload.value(QStringLiteral("columnNames")).toStringList();
    const bool isUnique = p.payload.value(QStringLiteral("isUnique")).toBool();

    if (indexName.isEmpty()) return {false, QStringLiteral("CREATE INDEX: expected index name")};
    if (tableName.isEmpty()) return {false, QStringLiteral("CREATE INDEX: expected table name")};
    if (columnNames.isEmpty()) return {false, QStringLiteral("CREATE INDEX: expected column list")};

    auto r = table_service::createIndex(tableName, indexName, columnNames, isUnique);
    if (r.success) return {true, {}, QString("Index '%1' created on '%2'").arg(indexName, tableName)};
    return {false, r.errorMessage};
}

SqlExecResult SqlDispatcher::execDropIndex(const sqlparser::ParseResult& p) {
    const QString tableName = p.payload.value(QStringLiteral("tableName")).toString().trimmed();
    const QString indexName = p.payload.value(QStringLiteral("indexName")).toString().trimmed();

    if (indexName.isEmpty()) return {false, QStringLiteral("DROP INDEX: expected index name")};
    if (tableName.isEmpty()) return {false, QStringLiteral("DROP INDEX: expected table name")};

    auto r = table_service::dropIndex(tableName, indexName);
    if (r.success) return {true, {}, QString("Index '%1' dropped from '%2'").arg(indexName, tableName)};
    return {false, r.errorMessage};
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

    if (p.payload.value(QStringLiteral("hasComplexWhere")).toBool()) {
        QueryExecutor executor;
        const QueryExecuteResult queryResult = executor.executeParsed(p,
                                                                     QueryExecuteContext{currentDatabase,
                                                                                         getDataRoot()});
        if (queryResult.success) {
            return {true,
                    {},
                    formatSelectResult(queryResult.selectResult),
                    queryResult.affectedRows,
                    queryResult.selectResult,
                    p.commandType,
                    p.payload};
        }
        return {false,
                queryResult.errorMessage,
                queryResult.errorMessage,
                queryResult.affectedRows,
                queryResult.selectResult,
                p.commandType,
                p.payload};
    }

    QString table = p.payload["tableName"].toString();
    QStringList projection = p.payload["projection"].toStringList();
    const int limit = p.payload.value(QStringLiteral("limit"), -1).toInt();
    OrderByClause orderBy;
    orderBy.columnName = p.payload.value(QStringLiteral("orderByColumn")).toString().trimmed();
    orderBy.descending = p.payload.value(QStringLiteral("orderByDescending"), false).toBool();
    // WHERE 尚未完整实现，暂不传递条件
    QList<SimpleCondition> conditions;
    QString conditionError;
    if (!simpleConditionsFromPayload(p.payload.value(QStringLiteral("conditions")).toList(), &conditions, &conditionError)) {
        return {false, conditionError};
    }

    auto r = tuple_service::selectRows(table, projection, conditions, limit, orderBy);
    if (r.success)
        return {true, {}, formatSelectResult(r), r.affectedRowCount, r};
    return {false, r.errorMessage};
}

SqlExecResult SqlDispatcher::execInsert(const sqlparser::ParseResult& p) {
    if (currentDatabase.isEmpty())
        return {false, "No database selected"};

    QString table = p.payload["tableName"].toString();
    int rowCount = p.payload["rowCount"].toInt();
    const QVariantList parsedRows = p.payload.value(QStringLiteral("rows")).toList();
    auto colNamesFromParser = p.payload["columnNames"].value<QStringList>();

    QVariantList rows;
    if (!parsedRows.isEmpty() && parsedRows.first().typeId() != QMetaType::QVariantList) {
        // Be tolerant if a single row payload is flattened into a plain QVariantList.
        rows.append(parsedRows);
    } else {
        rows = parsedRows;
    }

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
    for (const QVariant& rowValue : rows) {
        const QVariantList values = rowValue.toList();
        QMap<QString, QString> row;
        for (int i = 0; i < values.size(); ++i) {
            // 有显式列名时用列名；无列名时用 schema 的真实列名
            QString key = (i < colNames.size()) ? colNames[i] : QString::number(i);
            row[key] = values[i].toString();
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
    const QVariantMap assignments = p.payload.value(QStringLiteral("assignments")).toMap();
    QList<SimpleCondition> conditions;
    QString conditionError;
    const bool hasComplexWhere = p.payload.value(QStringLiteral("hasComplexWhere")).toBool();
    if (!hasComplexWhere) {
        if (!simpleConditionsFromPayload(p.payload.value(QStringLiteral("conditions")).toList(), &conditions, &conditionError)) {
            return {false, conditionError};
        }
    }

    QMap<QString, QString> assignMap;
    for (auto it = assignments.begin(); it != assignments.end(); ++it)
        assignMap[it.key()] = it.value().toString();

    TableDmlService dmlService;
    if (hasComplexWhere) {
        const tabledef::TableSchema schema = loadUserTableSchema(table, &conditionError);
        if (!conditionError.isEmpty()) {
            return {false, conditionError};
        }

        QueryExecutor executor;
        logic::LogicEvalContext evalContext;
        evalContext.subqueryExecutor = &executor;
        evalContext.currentDatabase = currentDatabase;
        evalContext.dataRoot = getDataRoot();
        evalContext.allowSubquery = true;

        const logic::LogicNode whereAst = p.payload.value(QStringLiteral("whereAst")).value<logic::LogicNode>();
        const TableDmlResult r = dmlService.updateRows(currentDatabase,
                                                       table,
                                                       TargetTableKind::TableDat,
                                                       schema,
                                                       assignMap,
                                                       conditions,
                                                       ValidationMode::UserData,
                                                       &whereAst,
                                                       &evalContext);
        if (r.success)
            return {true, {}, QString("%1 row(s) updated in '%2'").arg(r.affectedRowCount).arg(table),
                    r.affectedRowCount};
        return {false, r.errorMessage};
    }

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
    QList<SimpleCondition> conditions;
    QString conditionError;
    const bool hasComplexWhere = p.payload.value(QStringLiteral("hasComplexWhere")).toBool();
    if (!hasComplexWhere) {
        if (!simpleConditionsFromPayload(p.payload.value(QStringLiteral("conditions")).toList(), &conditions, &conditionError)) {
            return {false, conditionError};
        }
    }

    if (hasComplexWhere) {
        const tabledef::TableSchema schema = loadUserTableSchema(table, &conditionError);
        if (!conditionError.isEmpty()) {
            return {false, conditionError};
        }

        QueryExecutor executor;
        logic::LogicEvalContext evalContext;
        evalContext.subqueryExecutor = &executor;
        evalContext.currentDatabase = currentDatabase;
        evalContext.dataRoot = getDataRoot();
        evalContext.allowSubquery = true;

        const logic::LogicNode whereAst = p.payload.value(QStringLiteral("whereAst")).value<logic::LogicNode>();
        TableDmlService dmlService;
        const TableDmlResult r = dmlService.deleteRows(currentDatabase,
                                                       table,
                                                       TargetTableKind::TableDat,
                                                       schema,
                                                       conditions,
                                                       ValidationMode::UserData,
                                                       &whereAst,
                                                       &evalContext);
        if (r.success)
            return {true, {}, QString("%1 row(s) deleted from '%2'").arg(r.affectedRowCount).arg(table),
                    r.affectedRowCount};
        return {false, r.errorMessage};
    }

    auto r = tuple_service::deleteRows(table, conditions);
    if (r.success)
        return {true, {}, QString("%1 row(s) deleted from '%2'").arg(r.affectedRowCount).arg(table),
                r.affectedRowCount};
    return {false, r.errorMessage};
}

// ============================================================
//  辅助
// ============================================================
SqlExecResult SqlDispatcher::execLogin(const sqlparser::ParseResult& p) {
    const QString userName = p.payload.value(QStringLiteral("userName")).toString();
    const QString password = p.payload.value(QStringLiteral("password")).toString();
    const auth_service::AuthResult result = auth_service::authenticate(userName, password, getDataRoot());
    if (!result.success) {
        return {false, result.errorMessage};
    }
    currentUser = result.userName;
    return {true, {}, QStringLiteral("Logged in as '%1'").arg(result.userName)};
}

SqlExecResult SqlDispatcher::execCreateUser(const sqlparser::ParseResult& p) {
    const QString userName = p.payload.value(QStringLiteral("userName")).toString();
    const QString password = p.payload.value(QStringLiteral("password")).toString();
    const TaskResult result = auth_service::createUser(currentUser, userName, password, getDataRoot());
    if (!result.success) {
        return {false, result.errorMessage};
    }
    return {true, {}, QStringLiteral("User '%1' created").arg(userName), result.affectedRowCount};
}

SqlExecResult SqlDispatcher::execDropUser(const sqlparser::ParseResult& p) {
    const QString userName = p.payload.value(QStringLiteral("userName")).toString();
    const TaskResult result = auth_service::dropUser(currentUser, userName, getDataRoot());
    if (!result.success) {
        return {false, result.errorMessage};
    }
    return {true, {}, QStringLiteral("User '%1' dropped").arg(userName), result.affectedRowCount};
}

SqlExecResult SqlDispatcher::execAlterUser(const sqlparser::ParseResult& p) {
    const QString userName = p.payload.value(QStringLiteral("userName")).toString();
    const QString password = p.payload.value(QStringLiteral("password")).toString();
    const TaskResult result = auth_service::alterUserPassword(currentUser, userName, password, getDataRoot());
    if (!result.success) {
        return {false, result.errorMessage};
    }
    return {true, {}, QStringLiteral("User '%1' altered").arg(userName), result.affectedRowCount};
}

SqlExecResult SqlDispatcher::execGrantAll(const sqlparser::ParseResult& p) {
    const QString userName = p.payload.value(QStringLiteral("userName")).toString();
    const QString databaseName = p.payload.value(QStringLiteral("databaseName")).toString();
    const TaskResult result = auth_service::grantDatabaseAll(currentUser, userName, databaseName, getDataRoot());
    if (!result.success) {
        return {false, result.errorMessage};
    }
    return {true, {}, QStringLiteral("Granted ALL on '%1' to '%2'").arg(databaseName, userName), result.affectedRowCount};
}

SqlExecResult SqlDispatcher::execRevokeAll(const sqlparser::ParseResult& p) {
    const QString userName = p.payload.value(QStringLiteral("userName")).toString();
    const QString databaseName = p.payload.value(QStringLiteral("databaseName")).toString();
    const TaskResult result = auth_service::revokeDatabaseAll(currentUser, userName, databaseName, getDataRoot());
    if (!result.success) {
        return {false, result.errorMessage};
    }
    return {true, {}, QStringLiteral("Revoked ALL on '%1' from '%2'").arg(databaseName, userName), result.affectedRowCount};
}

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
