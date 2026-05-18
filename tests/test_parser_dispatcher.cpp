#include "../service/service.h"
#include "../controller/sql_dispatcher.h"
#include "../controller/nest_query.h"
#include "../utils/logic/logic.h"
#include "../utils/sql_parser/sql_parser.h"

#include <QDir>
#include <QtTest>

#include "test_entry.h"

using namespace service;

namespace {

QString testDataRoot()
{
    return QDir(QDir::tempPath()).absoluteFilePath(QStringLiteral("DBMS_test_parser_dispatcher"));
}

void removeTestDataRoot(const QString &path)
{
    QDir directory(path);
    if (directory.exists()) {
        directory.removeRecursively();
    }
}

tabledef::Column makeColumn(const QString &name,
                            tabledef::ColumnType type,
                            int length = 0,
                            bool notNull = false,
                            const QString &defaultValue = QString(),
                            bool autoIncrement = false)
{
    return tabledef::Column{name, type, length, notNull, defaultValue, autoIncrement, QString()};
}

tabledef::Constraint makePrimaryKey(const QString &name, const QStringList &columns)
{
    return tabledef::Constraint{name, tabledef::ConstraintType::PrimaryKey, columns, QString(), {}, QString()};
}

QMap<QString, QString> makeRow(std::initializer_list<QPair<QString, QString>> values)
{
    QMap<QString, QString> row;
    for (const QPair<QString, QString> &value : values) {
        row.insert(value.first, value.second);
    }
    return row;
}

tabledef::TableSchema baseSchema(const QString &tableName)
{
    tabledef::TableSchema schema;
    schema.tableName = tableName;
    schema.columns = {
        makeColumn(QStringLiteral("id"), tabledef::ColumnType::Int, 0, true),
        makeColumn(QStringLiteral("name"), tabledef::ColumnType::Varchar, 64, true),
    };
    schema.constraints = {
        makePrimaryKey(QStringLiteral("pk_%1_id").arg(tableName), {QStringLiteral("id")}),
    };
    return schema;
}

void ensureDatabase(const QString &databaseName)
{
    TaskResult result = database_service::createDatabase(databaseName);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    result = database_service::useDatabase(databaseName);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
}

void ensureTable(const QString &tableName, const tabledef::TableSchema &schema)
{
    TaskResult result = table_service::createTable(tableName, schema);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
}

void seedRows(const QString &tableName, const QList<QMap<QString, QString>> &rows)
{
    TaskResult result = tuple_service::insertRows(tableName, rows);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
}

void seedRows(const QString &tableName, std::initializer_list<QMap<QString, QString>> rows)
{
    QList<QMap<QString, QString>> rowList;
    for (const QMap<QString, QString> &row : rows) {
        rowList.append(row);
    }
    seedRows(tableName, rowList);
}

SelectRowsResult selectAllRows(const QString &tableName)
{
    return tuple_service::selectRows(tableName, {}, {}, -1);
}

QStringList listedIndexes(const QString &databaseName,
                          const QString &tableName,
                          const QString &dataRoot,
                          QString *error = nullptr)
{
    repo::IndexRepo indexRepo(databaseName, tableName, dataRoot);
    const QList<tabledef::IndexMeta> indexes = indexRepo.listIndexes(error);
    QStringList names;
    for (const tabledef::IndexMeta &index : indexes) {
        names.append(index.indexName);
    }
    return names;
}

tabledef::IndexMeta findIndexMeta(const QString &databaseName,
                                  const QString &tableName,
                                  const QString &dataRoot,
                                  const QString &indexName,
                                  QString *error = nullptr)
{
    repo::IndexRepo indexRepo(databaseName, tableName, dataRoot);
    const QList<tabledef::IndexMeta> indexes = indexRepo.listIndexes(error);
    for (const tabledef::IndexMeta &index : indexes) {
        if (index.indexName == indexName) {
            return index;
        }
    }
    return {};
}

tabledef::Column findColumn(const tabledef::TableSchema &schema, const QString &columnName)
{
    for (const tabledef::Column &column : schema.columns) {
        if (column.name == columnName) {
            return column;
        }
    }
    return {};
}

tabledef::Constraint findConstraint(const tabledef::TableSchema &schema, const QString &constraintName)
{
    for (const tabledef::Constraint &constraint : schema.constraints) {
        if (constraint.name == constraintName) {
            return constraint;
        }
    }
    return {};
}

} // namespace

class ParserDispatcherTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_dataRoot = testDataRoot();
        removeTestDataRoot(m_dataRoot);
        setDataRoot(m_dataRoot);
        currentDatabase.clear();
    }

    void cleanup()
    {
        currentDatabase.clear();
        removeTestDataRoot(m_dataRoot);
        setDataRoot(QString());
    }

    void test_parseCreateTableUsesQtBasePayload()
    {
        const QString sql = QStringLiteral(
            "CREATE TABLE child ("
            "id INT PRIMARY KEY, "
            "parent_id INT REFERENCES parent(id) ON DELETE CASCADE ON UPDATE SET NULL, "
            "CONSTRAINT uq_child_pair UNIQUE (id, parent_id), "
            "CONSTRAINT fk_child_parent FOREIGN KEY (parent_id) REFERENCES parent(id) ON DELETE NO ACTION ON UPDATE CASCADE"
            ")");

        const sqlparser::ParseResult result = sqlparser::parseSql(sql);
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.commandType, QStringLiteral("CREATE_TABLE"));
        QCOMPARE(result.payload.value(QStringLiteral("columns")).typeId(), QMetaType::QVariantList);
        QCOMPARE(result.payload.value(QStringLiteral("constraints")).typeId(), QMetaType::QVariantList);

        const QVariantList columns = result.payload.value(QStringLiteral("columns")).toList();
        QCOMPARE(columns.size(), 2);
        QCOMPARE(columns.at(0).typeId(), QMetaType::QVariantMap);
        QCOMPARE(columns.at(1).typeId(), QMetaType::QVariantMap);

        const QVariantMap foreignKeyColumn = columns.at(1).toMap();
        QCOMPARE(foreignKeyColumn.value(QStringLiteral("name")).toString(), QStringLiteral("parent_id"));
        QCOMPARE(foreignKeyColumn.value(QStringLiteral("referencesTable")).toString(), QStringLiteral("parent"));
        QCOMPARE(foreignKeyColumn.value(QStringLiteral("referencedColumns")).toStringList(),
                 QStringList({QStringLiteral("id")}));
        QCOMPARE(foreignKeyColumn.value(QStringLiteral("onDeleteAction")).toString(), QStringLiteral("CASCADE"));
        QCOMPARE(foreignKeyColumn.value(QStringLiteral("onUpdateAction")).toString(), QStringLiteral("SET NULL"));

        const QVariantList constraints = result.payload.value(QStringLiteral("constraints")).toList();
        QCOMPARE(constraints.size(), 2);
        QCOMPARE(constraints.at(0).typeId(), QMetaType::QVariantMap);
        QCOMPARE(constraints.at(1).typeId(), QMetaType::QVariantMap);
        QCOMPARE(constraints.at(0).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("UNIQUE"));
        QCOMPARE(constraints.at(1).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("FOREIGN_KEY"));
        QCOMPARE(constraints.at(1).toMap().value(QStringLiteral("onDeleteAction")).toString(), QStringLiteral("NO ACTION"));
        QCOMPARE(constraints.at(1).toMap().value(QStringLiteral("onUpdateAction")).toString(), QStringLiteral("CASCADE"));
    }

    void test_parseSelectLimitAndSimpleWhere()
    {
        const sqlparser::ParseResult limited = sqlparser::parseSql(
            QStringLiteral("SELECT id FROM student WHERE id = 1 AND name = 'alice' LIMIT 3"));
        QVERIFY2(limited.success, qPrintable(limited.errorMessage));
        QCOMPARE(limited.commandType, QStringLiteral("SELECT"));
        QCOMPARE(limited.payload.value(QStringLiteral("tableName")).toString(), QStringLiteral("student"));
        QCOMPARE(limited.payload.value(QStringLiteral("projection")).toStringList(), QStringList({QStringLiteral("id")}));
        QCOMPARE(limited.payload.value(QStringLiteral("limit")).toInt(), 3);
        QCOMPARE(limited.payload.value(QStringLiteral("hasComplexWhere")).toBool(), false);
        QVERIFY(limited.payload.contains(QStringLiteral("whereAst")));

        const QVariantList conditions = limited.payload.value(QStringLiteral("conditions")).toList();
        QCOMPARE(conditions.size(), 2);
        QCOMPARE(conditions.at(0).toMap().value(QStringLiteral("columnName")).toString(), QStringLiteral("id"));
        QCOMPARE(conditions.at(0).toMap().value(QStringLiteral("value")).toString(), QStringLiteral("1"));
        QCOMPARE(conditions.at(1).toMap().value(QStringLiteral("columnName")).toString(), QStringLiteral("name"));
        QCOMPARE(conditions.at(1).toMap().value(QStringLiteral("value")).toString(), QStringLiteral("alice"));

        const sqlparser::ParseResult qualified = sqlparser::parseSql(
            QStringLiteral("SELECT id FROM student WHERE student.id = 1"));
        QVERIFY2(qualified.success, qPrintable(qualified.errorMessage));
        QCOMPARE(qualified.payload.value(QStringLiteral("hasComplexWhere")).toBool(), true);
        QVERIFY(!qualified.payload.contains(QStringLiteral("conditions")));

        const sqlparser::ParseResult ordered = sqlparser::parseSql(
            QStringLiteral("SELECT * FROM student ORDER BY id DESC"));
        QVERIFY2(ordered.success, qPrintable(ordered.errorMessage));
        QCOMPARE(ordered.payload.value(QStringLiteral("orderByColumn")).toString(), QStringLiteral("id"));
        QCOMPARE(ordered.payload.value(QStringLiteral("orderByDescending")).toBool(), true);

        const sqlparser::ParseResult orderedWithLimit = sqlparser::parseSql(
            QStringLiteral("SELECT * FROM student ORDER BY id DESC LIMIT 2"));
        QVERIFY2(orderedWithLimit.success, qPrintable(orderedWithLimit.errorMessage));
        QCOMPARE(orderedWithLimit.payload.value(QStringLiteral("limit")).toInt(), 2);

        const sqlparser::ParseResult trailingAfterOrder = sqlparser::parseSql(
            QStringLiteral("SELECT * FROM student ORDER BY id DESC garbage"));
        QVERIFY(!trailingAfterOrder.success);

        const sqlparser::ParseResult trailingAfterLimit = sqlparser::parseSql(
            QStringLiteral("SELECT * FROM student LIMIT 2 garbage"));
        QVERIFY(!trailingAfterLimit.success);

        const sqlparser::ParseResult aliasedTable = sqlparser::parseSql(
            QStringLiteral("SELECT s.id AS sid FROM student AS s ORDER BY sid DESC"));
        QVERIFY2(aliasedTable.success, qPrintable(aliasedTable.errorMessage));
        QCOMPARE(aliasedTable.payload.value(QStringLiteral("tableAlias")).toString(), QStringLiteral("s"));
        QCOMPARE(aliasedTable.payload.value(QStringLiteral("projection")).toStringList(),
                 QStringList({QStringLiteral("s.id")}));
        const QVariantMap projectionItem = aliasedTable.payload.value(QStringLiteral("projectionItems")).toList().first().toMap();
        QCOMPARE(projectionItem.value(QStringLiteral("sourceColumn")).toString(), QStringLiteral("s.id"));
        QCOMPARE(projectionItem.value(QStringLiteral("outputColumn")).toString(), QStringLiteral("sid"));
        QCOMPARE(aliasedTable.payload.value(QStringLiteral("orderByColumn")).toString(), QStringLiteral("sid"));

        const sqlparser::ParseResult starAlias = sqlparser::parseSql(
            QStringLiteral("SELECT * AS x FROM student"));
        QVERIFY(!starAlias.success);

        const sqlparser::ParseResult extraAlias = sqlparser::parseSql(
            QStringLiteral("SELECT id FROM student s extra"));
        QVERIFY(!extraAlias.success);

        const sqlparser::ParseResult multiOrder = sqlparser::parseSql(
            QStringLiteral("SELECT id FROM student ORDER BY a, b"));
        QVERIFY(!multiOrder.success);

        const sqlparser::ParseResult grouped = sqlparser::parseSql(
            QStringLiteral("SELECT class_id, COUNT(*) AS n FROM student GROUP BY class_id HAVING n >= 2 ORDER BY n DESC LIMIT 1"));
        QVERIFY2(grouped.success, qPrintable(grouped.errorMessage));
        QCOMPARE(grouped.payload.value(QStringLiteral("isAggregateQuery")).toBool(), true);
        QCOMPARE(grouped.payload.value(QStringLiteral("hasAggregation")).toBool(), true);
        QCOMPARE(grouped.payload.value(QStringLiteral("groupByColumns")).toStringList(), QStringList({QStringLiteral("class_id")}));
        QCOMPARE(grouped.payload.value(QStringLiteral("orderByColumn")).toString(), QStringLiteral("n"));
        QCOMPARE(grouped.payload.value(QStringLiteral("limit")).toInt(), 1);
        const QVariantList aggregateItems = grouped.payload.value(QStringLiteral("aggregateItems")).toList();
        QCOMPARE(aggregateItems.size(), 1);
        QCOMPARE(aggregateItems.first().toMap().value(QStringLiteral("functionName")).toString(), QStringLiteral("COUNT"));
        QCOMPARE(aggregateItems.first().toMap().value(QStringLiteral("isStar")).toBool(), true);
        QVERIFY(grouped.payload.contains(QStringLiteral("havingAst")));

        const sqlparser::ParseResult orderByAggregate = sqlparser::parseSql(
            QStringLiteral("SELECT class_id, COUNT(*) AS n FROM student GROUP BY class_id ORDER BY COUNT(*) DESC"));
        QVERIFY2(orderByAggregate.success, qPrintable(orderByAggregate.errorMessage));
        QCOMPARE(orderByAggregate.payload.value(QStringLiteral("orderByColumn")).toString(), QStringLiteral("COUNT(*)"));
        QCOMPARE(orderByAggregate.payload.value(QStringLiteral("orderByDescending")).toBool(), true);

        QVERIFY(!sqlparser::parseSql(QStringLiteral("SELECT COUNT() FROM student")).success);
        QVERIFY(!sqlparser::parseSql(QStringLiteral("SELECT SUM(a + b) FROM student")).success);
        QVERIFY(!sqlparser::parseSql(QStringLiteral("SELECT id FROM student GROUP")).success);
        QVERIFY(!sqlparser::parseSql(QStringLiteral("SELECT class_id FROM student HAVING COUNT(*) > 1")).success);
        QVERIFY(!sqlparser::parseSql(QStringLiteral("SELECT class_id, COUNT(*) AS n FROM student GROUP BY class_id HAVING EXISTS (SELECT id FROM child)")).success);
        QVERIFY(!sqlparser::parseSql(QStringLiteral("SELECT class_id FROM student GROUP BY class_id HAVING COUNT(DISTINCT id) > 1")).success);

        const sqlparser::ParseResult commaFrom = sqlparser::parseSql(
            QStringLiteral("SELECT s.id, c.name FROM student s, class c WHERE s.class_id = c.id"));
        QVERIFY2(commaFrom.success, qPrintable(commaFrom.errorMessage));
        QCOMPARE(commaFrom.payload.value(QStringLiteral("isMultiTable")).toBool(), true);
        QCOMPARE(commaFrom.payload.value(QStringLiteral("fromSources")).toList().size(), 2);
        QCOMPARE(commaFrom.payload.value(QStringLiteral("joins")).toList().size(), 0);

        const sqlparser::ParseResult leftJoin = sqlparser::parseSql(
            QStringLiteral("SELECT s.id FROM student s LEFT JOIN class c ON s.class_id = c.id"));
        QVERIFY2(leftJoin.success, qPrintable(leftJoin.errorMessage));
        QCOMPARE(leftJoin.payload.value(QStringLiteral("fromSources")).toList().size(), 2);
        const QVariantMap leftJoinPayload = leftJoin.payload.value(QStringLiteral("joins")).toList().first().toMap();
        QCOMPARE(leftJoinPayload.value(QStringLiteral("joinType")).toString(), QStringLiteral("left"));
        QVERIFY(leftJoinPayload.contains(QStringLiteral("onAst")));

        const sqlparser::ParseResult rightJoin = sqlparser::parseSql(
            QStringLiteral("SELECT s.id FROM student s RIGHT JOIN class c ON s.class_id = c.id"));
        QVERIFY2(rightJoin.success, qPrintable(rightJoin.errorMessage));
        QCOMPARE(rightJoin.payload.value(QStringLiteral("joins")).toList().first().toMap().value(QStringLiteral("joinType")).toString(),
                 QStringLiteral("right"));

        const sqlparser::ParseResult fullJoin = sqlparser::parseSql(
            QStringLiteral("SELECT s.id FROM student s FULL JOIN class c ON s.class_id = c.id"));
        QVERIFY2(fullJoin.success, qPrintable(fullJoin.errorMessage));
        QCOMPARE(fullJoin.payload.value(QStringLiteral("joins")).toList().first().toMap().value(QStringLiteral("joinType")).toString(),
                 QStringLiteral("full"));

        QVERIFY(!sqlparser::parseSql(QStringLiteral("SELECT * FROM a JOIN b")).success);
        QVERIFY(!sqlparser::parseSql(QStringLiteral("SELECT * FROM a NATURAL JOIN b")).success);
        QVERIFY(!sqlparser::parseSql(QStringLiteral("SELECT * FROM a JOIN b ON a.id = b.id, c")).success);
        QVERIFY(!sqlparser::parseSql(QStringLiteral("SELECT * FROM a, b JOIN c ON b.id = c.id")).success);
        QVERIFY(!sqlparser::parseSql(QStringLiteral("SELECT INT FROM student")).success);
        QVERIFY(!sqlparser::parseSql(QStringLiteral("SELECT id FROM INT")).success);
        QVERIFY(!sqlparser::parseSql(QStringLiteral("SELECT id FROM student INT")).success);
        QVERIFY(!sqlparser::parseSql(QStringLiteral("SELECT * FROM student s, class student")).success);
    }

    void test_parseUpdateAndDeleteSupportSimpleWhere()
    {
        const sqlparser::ParseResult updated = sqlparser::parseSql(
            QStringLiteral("UPDATE student SET name = 'alice' WHERE id = 1 AND name = 'bob'"));
        QVERIFY2(updated.success, qPrintable(updated.errorMessage));
        QCOMPARE(updated.commandType, QStringLiteral("UPDATE"));
        QCOMPARE(updated.payload.value(QStringLiteral("tableName")).toString(), QStringLiteral("student"));
        QCOMPARE(updated.payload.value(QStringLiteral("assignments")).toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("alice"));
        QCOMPARE(updated.payload.value(QStringLiteral("conditions")).toList().size(), 2);

        const sqlparser::ParseResult deleted = sqlparser::parseSql(
            QStringLiteral("DELETE FROM student WHERE id = 1"));
        QVERIFY2(deleted.success, qPrintable(deleted.errorMessage));
        QCOMPARE(deleted.commandType, QStringLiteral("DELETE"));
        QCOMPARE(deleted.payload.value(QStringLiteral("tableName")).toString(), QStringLiteral("student"));
        QCOMPARE(deleted.payload.value(QStringLiteral("conditions")).toList().size(), 1);

        const sqlparser::ParseResult complexWhere = sqlparser::parseSql(
            QStringLiteral("DELETE FROM student WHERE id > 1"));
        QVERIFY2(complexWhere.success, qPrintable(complexWhere.errorMessage));
        QCOMPARE(complexWhere.payload.value(QStringLiteral("hasComplexWhere")).toBool(), true);
        QVERIFY(!complexWhere.payload.contains(QStringLiteral("conditions")));

        const sqlparser::ParseResult qualifiedUpdate = sqlparser::parseSql(
            QStringLiteral("UPDATE student SET name = 'alice' WHERE student.id = 1"));
        QVERIFY2(qualifiedUpdate.success, qPrintable(qualifiedUpdate.errorMessage));
        QCOMPARE(qualifiedUpdate.payload.value(QStringLiteral("hasComplexWhere")).toBool(), true);
        QVERIFY(!qualifiedUpdate.payload.contains(QStringLiteral("conditions")));

        const sqlparser::ParseResult qualifiedDelete = sqlparser::parseSql(
            QStringLiteral("DELETE FROM student WHERE student.id = 1"));
        QVERIFY2(qualifiedDelete.success, qPrintable(qualifiedDelete.errorMessage));
        QCOMPARE(qualifiedDelete.payload.value(QStringLiteral("hasComplexWhere")).toBool(), true);
        QVERIFY(!qualifiedDelete.payload.contains(QStringLiteral("conditions")));
    }

    void test_dispatcherUpdateAndDeleteApplyComplexWhere()
    {
        const QString databaseName = QStringLiteral("test_parser_dispatcher_complex_dml_db");
        const QString updateTable = QStringLiteral("student_update_complex");
        const QString deleteTable = QStringLiteral("student_delete_complex");
        ensureDatabase(databaseName);
        ensureTable(updateTable, baseSchema(updateTable));
        ensureTable(deleteTable, baseSchema(deleteTable));

        seedRows(updateTable,
                 QList<QMap<QString, QString>>{
                     makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                              {QStringLiteral("name"), QStringLiteral("alice")}}),
                     makeRow({{QStringLiteral("id"), QStringLiteral("2")},
                              {QStringLiteral("name"), QStringLiteral("bob")}}),
                     makeRow({{QStringLiteral("id"), QStringLiteral("3")},
                              {QStringLiteral("name"), QStringLiteral("carol")}}),
                 });

        seedRows(deleteTable,
                 QList<QMap<QString, QString>>{
                     makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                              {QStringLiteral("name"), QStringLiteral("alice")}}),
                     makeRow({{QStringLiteral("id"), QStringLiteral("2")},
                              {QStringLiteral("name"), QStringLiteral("bob")}}),
                     makeRow({{QStringLiteral("id"), QStringLiteral("3")},
                              {QStringLiteral("name"), QStringLiteral("carol")}}),
                 });

        SqlDispatcher dispatcher;
        const SqlExecResult updateResult = dispatcher.execute(
            QStringLiteral("UPDATE %1 SET name = 'updated' WHERE id = 1 OR name = 'bob'").arg(updateTable));
        QVERIFY2(updateResult.success, qPrintable(updateResult.errorMessage));
        QCOMPARE(updateResult.affectedRows, 2);

        const SelectRowsResult updatedRows = selectAllRows(updateTable);
        QVERIFY2(updatedRows.success, qPrintable(updatedRows.errorMessage));
        QCOMPARE(updatedRows.resultTable.rows.size(), 3);
        QCOMPARE(updatedRows.resultTable.rows.at(0), QStringList({QStringLiteral("1"), QStringLiteral("updated")}));
        QCOMPARE(updatedRows.resultTable.rows.at(1), QStringList({QStringLiteral("2"), QStringLiteral("updated")}));
        QCOMPARE(updatedRows.resultTable.rows.at(2), QStringList({QStringLiteral("3"), QStringLiteral("carol")}));

        const SqlExecResult deleteResult = dispatcher.execute(
            QStringLiteral("DELETE FROM %1 WHERE id = 1 OR name = 'bob'").arg(deleteTable));
        QVERIFY2(deleteResult.success, qPrintable(deleteResult.errorMessage));
        QCOMPARE(deleteResult.affectedRows, 2);

        const SelectRowsResult remainingRows = selectAllRows(deleteTable);
        QVERIFY2(remainingRows.success, qPrintable(remainingRows.errorMessage));
        QCOMPARE(remainingRows.resultTable.rows.size(), 1);
        QCOMPARE(remainingRows.resultTable.rows.first(), QStringList({QStringLiteral("3"), QStringLiteral("carol")}));
    }

    void test_dispatchSelectKeepsQualifiedWhereOnAstPath()
    {
        const QString databaseName = QStringLiteral("test_parser_dispatcher_qualified_select_db");
        const QString tableName = QStringLiteral("student_qualified");

        ensureDatabase(databaseName);
        ensureTable(tableName, baseSchema(tableName));
        seedRows(tableName,
                 QList<QMap<QString, QString>>{
                     makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                              {QStringLiteral("name"), QStringLiteral("alice")}}),
                     makeRow({{QStringLiteral("id"), QStringLiteral("2")},
                              {QStringLiteral("name"), QStringLiteral("bob")}}),
                 });

        SqlDispatcher dispatcher;
        const SqlExecResult result = dispatcher.execute(
            QStringLiteral("SELECT id FROM student_qualified WHERE student_qualified.id = 1"));

        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.columns, QStringList({QStringLiteral("id")}));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 1);
        QCOMPARE(result.selectResult.resultTable.rows.first().value(0), QStringLiteral("1"));
    }

    void test_dispatchSelectAliasAndQualifiedColumns()
    {
        const QString databaseName = QStringLiteral("test_parser_dispatcher_select_alias_db");
        const QString tableName = QStringLiteral("test_parser_dispatcher_select_alias_table");

        ensureDatabase(databaseName);
        ensureTable(tableName, baseSchema(tableName));
        seedRows(tableName,
                 QList<QMap<QString, QString>>{
                     makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                              {QStringLiteral("name"), QStringLiteral("alice")}}),
                     makeRow({{QStringLiteral("id"), QStringLiteral("2")},
                              {QStringLiteral("name"), QStringLiteral("bob")}}),
                 });

        SqlDispatcher dispatcher;
        SqlExecResult result = dispatcher.execute(
            QStringLiteral("SELECT s.id AS sid FROM %1 AS s ORDER BY sid DESC").arg(tableName));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.columns, QStringList({QStringLiteral("sid")}));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 2);
        QCOMPARE(result.selectResult.resultTable.rows.at(0).value(0), QStringLiteral("2"));
        QCOMPARE(result.selectResult.resultTable.rows.at(1).value(0), QStringLiteral("1"));

        result = dispatcher.execute(
            QStringLiteral("SELECT * FROM %1 s WHERE s.id = 1").arg(tableName));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 1);
        QCOMPARE(result.selectResult.resultTable.rows.first().value(1), QStringLiteral("alice"));
    }

    void test_dispatchCorrelatedSubqueryUsesOuterAlias()
    {
        const QString databaseName = QStringLiteral("test_parser_dispatcher_correlated_alias_db");
        ensureDatabase(databaseName);

        tabledef::TableSchema parentSchema;
        parentSchema.tableName = QStringLiteral("parent_alias");
        parentSchema.columns = {
            makeColumn(QStringLiteral("id"), tabledef::ColumnType::Int, 0, true),
            makeColumn(QStringLiteral("name"), tabledef::ColumnType::Varchar, 32),
        };
        parentSchema.constraints = {
            makePrimaryKey(QStringLiteral("pk_parent_alias_id"), {QStringLiteral("id")}),
        };
        ensureTable(parentSchema.tableName, parentSchema);

        tabledef::TableSchema childSchema;
        childSchema.tableName = QStringLiteral("child_alias");
        childSchema.columns = {
            makeColumn(QStringLiteral("id"), tabledef::ColumnType::Int, 0, true),
            makeColumn(QStringLiteral("parent_id"), tabledef::ColumnType::Int),
        };
        childSchema.constraints = {
            makePrimaryKey(QStringLiteral("pk_child_alias_id"), {QStringLiteral("id")}),
        };
        ensureTable(childSchema.tableName, childSchema);

        seedRows(parentSchema.tableName,
                 {makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                           {QStringLiteral("name"), QStringLiteral("alice")}}),
                  makeRow({{QStringLiteral("id"), QStringLiteral("2")},
                           {QStringLiteral("name"), QStringLiteral("bob")}}),
                  makeRow({{QStringLiteral("id"), QStringLiteral("3")},
                           {QStringLiteral("name"), QStringLiteral("carol")}})});
        seedRows(childSchema.tableName,
                 {makeRow({{QStringLiteral("id"), QStringLiteral("10")},
                           {QStringLiteral("parent_id"), QStringLiteral("1")}}),
                  makeRow({{QStringLiteral("id"), QStringLiteral("11")},
                           {QStringLiteral("parent_id"), QStringLiteral("1")}}),
                  makeRow({{QStringLiteral("id"), QStringLiteral("12")},
                           {QStringLiteral("parent_id"), QStringLiteral("3")}})});

        SqlDispatcher dispatcher;
        const SqlExecResult result = dispatcher.execute(
            QStringLiteral("SELECT p.id FROM parent_alias p "
                           "WHERE EXISTS (SELECT c.id FROM child_alias c WHERE c.parent_id = p.id)"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.columns, QStringList({QStringLiteral("id")}));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 2);
        QCOMPARE(result.selectResult.resultTable.rows.at(0).value(0), QStringLiteral("1"));
        QCOMPARE(result.selectResult.resultTable.rows.at(1).value(0), QStringLiteral("3"));
    }

    void test_dispatchMultiTableFromAndJoins()
    {
        const QString databaseName = QStringLiteral("test_parser_dispatcher_multi_from_db");
        ensureDatabase(databaseName);

        tabledef::TableSchema studentSchema;
        studentSchema.tableName = QStringLiteral("student_multi");
        studentSchema.columns = {
            makeColumn(QStringLiteral("id"), tabledef::ColumnType::Int, 0, true),
            makeColumn(QStringLiteral("class_id"), tabledef::ColumnType::Int),
            makeColumn(QStringLiteral("name"), tabledef::ColumnType::Varchar, 32),
        };
        studentSchema.constraints = {
            makePrimaryKey(QStringLiteral("pk_student_multi_id"), {QStringLiteral("id")}),
        };
        ensureTable(studentSchema.tableName, studentSchema);

        tabledef::TableSchema classSchema;
        classSchema.tableName = QStringLiteral("class_multi");
        classSchema.columns = {
            makeColumn(QStringLiteral("id"), tabledef::ColumnType::Int, 0, true),
            makeColumn(QStringLiteral("name"), tabledef::ColumnType::Varchar, 32),
        };
        classSchema.constraints = {
            makePrimaryKey(QStringLiteral("pk_class_multi_id"), {QStringLiteral("id")}),
        };
        ensureTable(classSchema.tableName, classSchema);

        seedRows(studentSchema.tableName,
                 {makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                           {QStringLiteral("class_id"), QStringLiteral("10")},
                           {QStringLiteral("name"), QStringLiteral("alice")}}),
                  makeRow({{QStringLiteral("id"), QStringLiteral("2")},
                           {QStringLiteral("class_id"), QStringLiteral("20")},
                           {QStringLiteral("name"), QStringLiteral("bob")}}),
                  makeRow({{QStringLiteral("id"), QStringLiteral("3")},
                           {QStringLiteral("class_id"), QStringLiteral("99")},
                           {QStringLiteral("name"), QStringLiteral("carol")}})});
        seedRows(classSchema.tableName,
                 {makeRow({{QStringLiteral("id"), QStringLiteral("10")},
                           {QStringLiteral("name"), QStringLiteral("cs")}}),
                  makeRow({{QStringLiteral("id"), QStringLiteral("20")},
                           {QStringLiteral("name"), QStringLiteral("math")}}),
                  makeRow({{QStringLiteral("id"), QStringLiteral("30")},
                           {QStringLiteral("name"), QStringLiteral("art")}})});

        SqlDispatcher dispatcher;
        SqlExecResult result = dispatcher.execute(
            QStringLiteral("SELECT s.id, c.name FROM student_multi s, class_multi c "
                           "WHERE s.class_id = c.id ORDER BY s.id ASC"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 2);
        QCOMPARE(result.selectResult.resultTable.rows.at(0).value(0), QStringLiteral("1"));
        QCOMPARE(result.selectResult.resultTable.rows.at(0).value(1), QStringLiteral("cs"));
        QCOMPARE(result.selectResult.resultTable.rows.at(1).value(0), QStringLiteral("2"));
        QCOMPARE(result.selectResult.resultTable.rows.at(1).value(1), QStringLiteral("math"));

        result = dispatcher.execute(
            QStringLiteral("SELECT s.id AS sid, c.name FROM student_multi s "
                           "JOIN class_multi c ON s.class_id = c.id ORDER BY sid DESC"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.columns.first(), QStringLiteral("sid"));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 2);
        QCOMPARE(result.selectResult.resultTable.rows.at(0).value(0), QStringLiteral("2"));

        result = dispatcher.execute(
            QStringLiteral("SELECT s.id, c.name FROM student_multi s "
                           "JOIN class_multi c ON class_id = c.id ORDER BY s.id ASC"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 2);
        QCOMPARE(result.selectResult.resultTable.rows.at(0).value(0), QStringLiteral("1"));

        result = dispatcher.execute(
            QStringLiteral("SELECT s.id, c.name FROM student_multi s "
                           "LEFT JOIN class_multi c ON s.class_id = c.id ORDER BY s.id ASC"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 3);
        QCOMPARE(result.selectResult.resultTable.rows.at(2).value(0), QStringLiteral("3"));
        QCOMPARE(result.selectResult.resultTable.rows.at(2).value(1), QString());

        result = dispatcher.execute(
            QStringLiteral("SELECT s.id, c.name FROM student_multi s "
                           "RIGHT JOIN class_multi c ON s.class_id = c.id ORDER BY c.id ASC"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 3);
        QCOMPARE(result.selectResult.resultTable.rows.at(2).value(0), QString());
        QCOMPARE(result.selectResult.resultTable.rows.at(2).value(1), QStringLiteral("art"));

        result = dispatcher.execute(
            QStringLiteral("SELECT s.id, c.name FROM student_multi s "
                           "FULL JOIN class_multi c ON s.class_id = c.id ORDER BY c.name ASC"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 4);

        result = dispatcher.execute(
            QStringLiteral("SELECT id FROM student_multi s JOIN class_multi c ON s.class_id = c.id"));
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("ambiguous column")));

        result = dispatcher.execute(
            QStringLiteral("SELECT student_multi.id FROM student_multi x "
                           "JOIN student_multi y ON x.id = y.id"));
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("duplicate table qualifier"))
                || result.errorMessage.contains(QStringLiteral("unknown table or alias")));

        result = dispatcher.execute(
            QStringLiteral("SELECT x.id, y.name FROM student_multi x "
                           "JOIN student_multi y ON x.id = y.id ORDER BY x.id ASC"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 3);
    }

    void test_dispatchUpdateAndDeleteKeepQualifiedWhereOnAstPath()
    {
        const QString databaseName = QStringLiteral("test_parser_dispatcher_qualified_dml_db");
        const QString updateTable = QStringLiteral("student_update_qualified");
        const QString deleteTable = QStringLiteral("student_delete_qualified");

        ensureDatabase(databaseName);
        ensureTable(updateTable, baseSchema(updateTable));
        ensureTable(deleteTable, baseSchema(deleteTable));

        seedRows(updateTable,
                 QList<QMap<QString, QString>>{
                     makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                              {QStringLiteral("name"), QStringLiteral("alice")}}),
                     makeRow({{QStringLiteral("id"), QStringLiteral("2")},
                              {QStringLiteral("name"), QStringLiteral("bob")}}),
                 });
        seedRows(deleteTable,
                 QList<QMap<QString, QString>>{
                     makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                              {QStringLiteral("name"), QStringLiteral("alice")}}),
                     makeRow({{QStringLiteral("id"), QStringLiteral("2")},
                              {QStringLiteral("name"), QStringLiteral("bob")}}),
                 });

        SqlDispatcher dispatcher;

        const SqlExecResult updateResult = dispatcher.execute(
            QStringLiteral("UPDATE student_update_qualified SET name = 'updated' "
                           "WHERE student_update_qualified.id = 1"));
        QVERIFY2(updateResult.success, qPrintable(updateResult.errorMessage));
        QCOMPARE(updateResult.affectedRows, 1);

        const SelectRowsResult updatedRows = selectAllRows(updateTable);
        QVERIFY2(updatedRows.success, qPrintable(updatedRows.errorMessage));
        QCOMPARE(updatedRows.resultTable.rows.size(), 2);
        QCOMPARE(updatedRows.resultTable.rows.at(0),
                 QStringList({QStringLiteral("1"), QStringLiteral("updated")}));
        QCOMPARE(updatedRows.resultTable.rows.at(1),
                 QStringList({QStringLiteral("2"), QStringLiteral("bob")}));

        const SqlExecResult deleteResult = dispatcher.execute(
            QStringLiteral("DELETE FROM student_delete_qualified "
                           "WHERE student_delete_qualified.id = 1"));
        QVERIFY2(deleteResult.success, qPrintable(deleteResult.errorMessage));
        QCOMPARE(deleteResult.affectedRows, 1);

        const SelectRowsResult remainingRows = selectAllRows(deleteTable);
        QVERIFY2(remainingRows.success, qPrintable(remainingRows.errorMessage));
        QCOMPARE(remainingRows.resultTable.rows.size(), 1);
        QCOMPARE(remainingRows.resultTable.rows.first(),
                 QStringList({QStringLiteral("2"), QStringLiteral("bob")}));
    }

    void test_parseWhereSupportsLogicOperators()
    {
        const sqlparser::ParseResult orResult = sqlparser::parseSql(
            QStringLiteral("SELECT * FROM student WHERE id = 1 OR name = 'alice';"));
        QVERIFY2(orResult.success, qPrintable(orResult.errorMessage));
        QCOMPARE(orResult.payload.value(QStringLiteral("hasComplexWhere")).toBool(), true);
        const logic::LogicNode orAst = orResult.payload.value(QStringLiteral("whereAst")).value<logic::LogicNode>();
        QCOMPARE(orAst.type, logic::LogicNodeType::Binary);
        QCOMPARE(orAst.binaryOperator, logic::LogicBinaryOperator::Or);

        const sqlparser::ParseResult trailingAndResult = sqlparser::parseSql(
            QStringLiteral("UPDATE student SET name = 'alice' WHERE id = 1 AND;"));
        QVERIFY(!trailingAndResult.success);
        QVERIFY(trailingAndResult.errorMessage.contains(QStringLiteral("expected")));

        const sqlparser::ParseResult missingValueResult = sqlparser::parseSql(
            QStringLiteral("DELETE FROM student WHERE id = ;"));
        QVERIFY(!missingValueResult.success);
        QVERIFY(missingValueResult.errorMessage.contains(QStringLiteral("expected")));
    }

    void test_dispatchSelectAppliesComplexWhere()
    {
        const QString databaseName = QStringLiteral("test_dispatch_select_db");
        const QString tableName = QStringLiteral("student");
        ensureDatabase(databaseName);
        ensureTable(tableName, baseSchema(tableName));
        seedRows(tableName,
                 QList<QMap<QString, QString>>{
                     makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                              {QStringLiteral("name"), QStringLiteral("alice")}}),
                     makeRow({{QStringLiteral("id"), QStringLiteral("2")},
                              {QStringLiteral("name"), QStringLiteral("bob")}}),
                 });

        SqlDispatcher dispatcher;
        const SqlExecResult result = dispatcher.execute(
            QStringLiteral("SELECT id FROM student WHERE id = 1 OR name = 'bob'"));

        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.columns,
                 QStringList({QStringLiteral("id")}));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 2);
        QCOMPARE(result.selectResult.resultTable.rows.at(0).value(0), QStringLiteral("1"));
        QCOMPARE(result.selectResult.resultTable.rows.at(1).value(0), QStringLiteral("2"));
    }

    void test_parseInsertWithoutColumnListProducesSingleRowPayload()
    {
        const sqlparser::ParseResult result = sqlparser::parseSql(
            QStringLiteral("INSERT INTO student VALUES (1, 'alice')"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.commandType, QStringLiteral("INSERT"));
        QCOMPARE(result.payload.value(QStringLiteral("tableName")).toString(), QStringLiteral("student"));
        QCOMPARE(result.payload.value(QStringLiteral("columnNames")).toStringList().size(), 0);
        QCOMPARE(result.payload.value(QStringLiteral("rowCount")).toInt(), 1);

        const QVariantList rows = result.payload.value(QStringLiteral("rows")).toList();
        QCOMPARE(rows.size(), 1);
        const QVariantList values = rows.at(0).toList();
        QCOMPARE(values.size(), 2);
        QCOMPARE(values.at(0).toString(), QStringLiteral("1"));
        QCOMPARE(values.at(1).toString(), QStringLiteral("alice"));
    }

    void test_splitStatementsSupportsStringLiteralsAndEmptySegments()
    {
        const QString script = QStringLiteral(
            "  ;  \n"
            "INSERT INTO demo VALUES (1, 'a;b');\n"
            "SELECT * FROM demo WHERE note = 'x;y';\n"
            "USE demo_db");

        const QStringList statements = SqlDispatcher::splitStatements(script);
        QCOMPARE(statements.size(), 3);
        QCOMPARE(statements.at(0), QStringLiteral("INSERT INTO demo VALUES (1, 'a;b')"));
        QCOMPARE(statements.at(1), QStringLiteral("SELECT * FROM demo WHERE note = 'x;y'"));
        QCOMPARE(statements.at(2), QStringLiteral("USE demo_db"));
    }

    void test_parseAlterAndIndexProduceCompletePayload()
    {
        const sqlparser::ParseResult addColumn = sqlparser::parseSql(
            QStringLiteral("ALTER TABLE student ADD COLUMN age INT NOT NULL DEFAULT 18"));
        QVERIFY2(addColumn.success, qPrintable(addColumn.errorMessage));
        QCOMPARE(addColumn.payload.value(QStringLiteral("alterAction")).toString(), QStringLiteral("ADD_COLUMN"));
        const QVariantMap addColumnMap = addColumn.payload.value(QStringLiteral("column")).toMap();
        QCOMPARE(addColumnMap.value(QStringLiteral("name")).toString(), QStringLiteral("age"));
        QCOMPARE(addColumnMap.value(QStringLiteral("type")).toString(), QStringLiteral("INT"));
        QCOMPARE(addColumnMap.value(QStringLiteral("notNull")).toBool(), true);
        QCOMPARE(addColumnMap.value(QStringLiteral("defaultValue")).toString(), QStringLiteral("18"));

        const sqlparser::ParseResult modifyColumn = sqlparser::parseSql(
            QStringLiteral("ALTER TABLE student MODIFY COLUMN age VARCHAR(20) DEFAULT 'adult'"));
        QVERIFY2(modifyColumn.success, qPrintable(modifyColumn.errorMessage));
        QCOMPARE(modifyColumn.payload.value(QStringLiteral("alterAction")).toString(), QStringLiteral("MODIFY_COLUMN"));
        QCOMPARE(modifyColumn.payload.value(QStringLiteral("column")).toMap().value(QStringLiteral("length")).toInt(), 20);

        const sqlparser::ParseResult addConstraint = sqlparser::parseSql(
            QStringLiteral("ALTER TABLE student ADD CONSTRAINT uq_student_name UNIQUE (name)"));
        QVERIFY2(addConstraint.success, qPrintable(addConstraint.errorMessage));
        QCOMPARE(addConstraint.payload.value(QStringLiteral("alterAction")).toString(), QStringLiteral("ADD_CONSTRAINT"));
        QCOMPARE(addConstraint.payload.value(QStringLiteral("constraint")).toMap().value(QStringLiteral("type")).toString(),
                 QStringLiteral("UNIQUE"));

        const sqlparser::ParseResult modifyConstraint = sqlparser::parseSql(
            QStringLiteral("ALTER TABLE student MODIFY CONSTRAINT uq_student_name "
                           "CONSTRAINT uq_student_name_age UNIQUE (name, age)"));
        QVERIFY2(modifyConstraint.success, qPrintable(modifyConstraint.errorMessage));
        QCOMPARE(modifyConstraint.payload.value(QStringLiteral("alterAction")).toString(), QStringLiteral("MODIFY_CONSTRAINT"));
        QCOMPARE(modifyConstraint.payload.value(QStringLiteral("constraintName")).toString(), QStringLiteral("uq_student_name"));
        QCOMPARE(modifyConstraint.payload.value(QStringLiteral("constraint")).toMap().value(QStringLiteral("columns")).toStringList(),
                 QStringList({QStringLiteral("name"), QStringLiteral("age")}));

        const sqlparser::ParseResult createIndex = sqlparser::parseSql(
            QStringLiteral("CREATE UNIQUE INDEX idx_student_name ON student(name)"));
        QVERIFY2(createIndex.success, qPrintable(createIndex.errorMessage));
        QCOMPARE(createIndex.commandType, QStringLiteral("CREATE_INDEX"));
        QCOMPARE(createIndex.payload.value(QStringLiteral("indexName")).toString(), QStringLiteral("idx_student_name"));
        QCOMPARE(createIndex.payload.value(QStringLiteral("tableName")).toString(), QStringLiteral("student"));
        QCOMPARE(createIndex.payload.value(QStringLiteral("columnNames")).toStringList(),
                 QStringList({QStringLiteral("name")}));
        QCOMPARE(createIndex.payload.value(QStringLiteral("isUnique")).toBool(), true);

        const sqlparser::ParseResult dropIndex = sqlparser::parseSql(
            QStringLiteral("DROP INDEX idx_student_name ON student"));
        QVERIFY2(dropIndex.success, qPrintable(dropIndex.errorMessage));
        QCOMPARE(dropIndex.commandType, QStringLiteral("DROP_INDEX"));
        QCOMPARE(dropIndex.payload.value(QStringLiteral("indexName")).toString(), QStringLiteral("idx_student_name"));
        QCOMPARE(dropIndex.payload.value(QStringLiteral("tableName")).toString(), QStringLiteral("student"));

        const sqlparser::ParseResult setDefault = sqlparser::parseSql(
            QStringLiteral("ALTER TABLE student ALTER COLUMN age SET DEFAULT 18"));
        QVERIFY2(setDefault.success, qPrintable(setDefault.errorMessage));
        QCOMPARE(setDefault.payload.value(QStringLiteral("alterAction")).toString(),
                 QStringLiteral("ALTER_COLUMN_SET_DEFAULT"));
        QCOMPARE(setDefault.payload.value(QStringLiteral("columnName")).toString(), QStringLiteral("age"));
        QCOMPARE(setDefault.payload.value(QStringLiteral("defaultValue")).toString(), QStringLiteral("18"));

        const sqlparser::ParseResult dropDefault = sqlparser::parseSql(
            QStringLiteral("ALTER TABLE student ALTER COLUMN age DROP DEFAULT"));
        QVERIFY2(dropDefault.success, qPrintable(dropDefault.errorMessage));
        QCOMPARE(dropDefault.payload.value(QStringLiteral("alterAction")).toString(),
                 QStringLiteral("ALTER_COLUMN_DROP_DEFAULT"));

        const sqlparser::ParseResult setNotNull = sqlparser::parseSql(
            QStringLiteral("ALTER TABLE student ALTER COLUMN name SET NOT NULL"));
        QVERIFY2(setNotNull.success, qPrintable(setNotNull.errorMessage));
        QCOMPARE(setNotNull.payload.value(QStringLiteral("alterAction")).toString(),
                 QStringLiteral("ALTER_COLUMN_SET_NOT_NULL"));

        const sqlparser::ParseResult dropNotNull = sqlparser::parseSql(
            QStringLiteral("ALTER TABLE student ALTER COLUMN name DROP NOT NULL"));
        QVERIFY2(dropNotNull.success, qPrintable(dropNotNull.errorMessage));
        QCOMPARE(dropNotNull.payload.value(QStringLiteral("alterAction")).toString(),
                 QStringLiteral("ALTER_COLUMN_DROP_NOT_NULL"));

        const sqlparser::ParseResult renameColumn = sqlparser::parseSql(
            QStringLiteral("ALTER TABLE student RENAME COLUMN old_name TO new_name"));
        QVERIFY2(renameColumn.success, qPrintable(renameColumn.errorMessage));
        QCOMPARE(renameColumn.payload.value(QStringLiteral("alterAction")).toString(),
                 QStringLiteral("RENAME_COLUMN"));
        QCOMPARE(renameColumn.payload.value(QStringLiteral("columnName")).toString(), QStringLiteral("old_name"));
        QCOMPARE(renameColumn.payload.value(QStringLiteral("newColumnName")).toString(), QStringLiteral("new_name"));

        const sqlparser::ParseResult setType = sqlparser::parseSql(
            QStringLiteral("ALTER TABLE student ALTER COLUMN name TYPE VARCHAR(64)"));
        QVERIFY2(setType.success, qPrintable(setType.errorMessage));
        QCOMPARE(setType.payload.value(QStringLiteral("alterAction")).toString(),
                 QStringLiteral("ALTER_COLUMN_SET_TYPE"));
        QCOMPARE(setType.payload.value(QStringLiteral("type")).toString(), QStringLiteral("VARCHAR"));
        QCOMPARE(setType.payload.value(QStringLiteral("length")).toInt(), 64);

        QVERIFY(!sqlparser::parseSql(QStringLiteral("ALTER TABLE student ALTER COLUMN age SET DEFAULT 18 garbage")).success);
        QVERIFY(!sqlparser::parseSql(QStringLiteral("ALTER TABLE student ALTER COLUMN age DROP DEFAULT garbage")).success);
        QVERIFY(!sqlparser::parseSql(QStringLiteral("ALTER TABLE student ALTER COLUMN age TYPE VARCHAR(64) garbage")).success);
        QVERIFY(!sqlparser::parseSql(QStringLiteral("ALTER TABLE student ALTER COLUMN age TYPE VARCHAR(64, 2)")).success);
        QVERIFY(!sqlparser::parseSql(QStringLiteral("ALTER TABLE student RENAME COLUMN old_name TO new_name extra")).success);
        QVERIFY(!sqlparser::parseSql(QStringLiteral("ALTER TABLE student DROP COLUMN age extra")).success);
    }

    void test_parseAlterForeignKeyAndMultiColumnIndexPayload()
    {
        const sqlparser::ParseResult foreignKeyConstraint = sqlparser::parseSql(
            QStringLiteral("ALTER TABLE child ADD CONSTRAINT fk_child_parent "
                           "FOREIGN KEY (parent_id) REFERENCES parent(id) "
                           "ON DELETE CASCADE ON UPDATE SET NULL;"));
        QVERIFY2(foreignKeyConstraint.success, qPrintable(foreignKeyConstraint.errorMessage));
        QCOMPARE(foreignKeyConstraint.payload.value(QStringLiteral("alterAction")).toString(),
                 QStringLiteral("ADD_CONSTRAINT"));
        const QVariantMap constraint = foreignKeyConstraint.payload.value(QStringLiteral("constraint")).toMap();
        QCOMPARE(constraint.value(QStringLiteral("type")).toString(), QStringLiteral("FOREIGN_KEY"));
        QCOMPARE(constraint.value(QStringLiteral("columns")).toStringList(), QStringList({QStringLiteral("parent_id")}));
        QCOMPARE(constraint.value(QStringLiteral("referencedTable")).toString(), QStringLiteral("parent"));
        QCOMPARE(constraint.value(QStringLiteral("referencedColumns")).toStringList(), QStringList({QStringLiteral("id")}));
        QCOMPARE(constraint.value(QStringLiteral("onDeleteAction")).toString(), QStringLiteral("CASCADE"));
        QCOMPARE(constraint.value(QStringLiteral("onUpdateAction")).toString(), QStringLiteral("SET NULL"));

        const sqlparser::ParseResult multiIndex = sqlparser::parseSql(
            QStringLiteral("CREATE INDEX idx_student_name_age ON student(name, age);"));
        QVERIFY2(multiIndex.success, qPrintable(multiIndex.errorMessage));
        QCOMPARE(multiIndex.payload.value(QStringLiteral("columnNames")).toStringList(),
                 QStringList({QStringLiteral("name"), QStringLiteral("age")}));
        QCOMPARE(multiIndex.payload.value(QStringLiteral("isUnique")).toBool(), false);
    }

    void test_dispatcherInsertWithoutColumnListUsesSchemaOrder()
    {
        const QString databaseName = QStringLiteral("test_parser_dispatcher_insert_db");
        const QString tableName = QStringLiteral("test_parser_dispatcher_insert_table");
        ensureDatabase(databaseName);
        ensureTable(tableName, baseSchema(tableName));

        SqlDispatcher dispatcher;
        const SqlExecResult insertResult = dispatcher.execute(
            QStringLiteral("INSERT INTO %1 VALUES (1, 'alice')").arg(tableName));
        QVERIFY2(insertResult.success, qPrintable(insertResult.errorMessage));
        QCOMPARE(insertResult.affectedRows, 1);

        const SelectRowsResult selectResult = tuple_service::selectRows(tableName, {}, {}, -1);
        QVERIFY2(selectResult.success, qPrintable(selectResult.errorMessage));
        QCOMPARE(selectResult.resultTable.rows.size(), 1);
        QCOMPARE(selectResult.resultTable.rows.at(0),
                 QStringList({QStringLiteral("1"), QStringLiteral("alice")}));
    }

    void test_dispatcherShowCreateTableReturnsText()
    {
        const QString databaseName = QStringLiteral("test_parser_dispatcher_show_create_db");
        const QString tableName = QStringLiteral("test_parser_dispatcher_show_create_table");
        ensureDatabase(databaseName);
        ensureTable(tableName, baseSchema(tableName));

        SqlDispatcher dispatcher;
        const SqlExecResult result = dispatcher.execute(QStringLiteral("SHOW CREATE TABLE %1").arg(tableName));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.commandType, QStringLiteral("SHOW_CREATE_TABLE"));
        QVERIFY(result.text.startsWith(QStringLiteral("CREATE TABLE")));
        QVERIFY(result.text.contains(tableName));
    }

    void test_dispatcherAlterSqlPathsCallService()
    {
        const QString databaseName = QStringLiteral("test_parser_dispatcher_alter_db");
        const QString tableName = QStringLiteral("test_parser_dispatcher_alter_table");
        ensureDatabase(databaseName);
        ensureTable(tableName, baseSchema(tableName));

        SqlDispatcher dispatcher;

        const SqlExecResult addColumnResult = dispatcher.execute(
            QStringLiteral("ALTER TABLE %1 ADD COLUMN age INT NOT NULL DEFAULT 18").arg(tableName));
        QVERIFY2(addColumnResult.success, qPrintable(addColumnResult.errorMessage));

        const SqlExecResult modifyColumnResult = dispatcher.execute(
            QStringLiteral("ALTER TABLE %1 MODIFY COLUMN age INT DEFAULT 21").arg(tableName));
        QVERIFY2(modifyColumnResult.success, qPrintable(modifyColumnResult.errorMessage));

        const SqlExecResult addConstraintResult = dispatcher.execute(
            QStringLiteral("ALTER TABLE %1 ADD CONSTRAINT uq_%1_name UNIQUE (name)").arg(tableName));
        QVERIFY2(addConstraintResult.success, qPrintable(addConstraintResult.errorMessage));

        const SqlExecResult modifyConstraintResult = dispatcher.execute(
            QStringLiteral("ALTER TABLE %1 MODIFY CONSTRAINT uq_%1_name "
                           "CONSTRAINT uq_%1_name_age UNIQUE (name, age)").arg(tableName));
        QVERIFY2(modifyConstraintResult.success, qPrintable(modifyConstraintResult.errorMessage));

        QString error;
        const tabledef::TableSchema schema = loadUserTableSchema(tableName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        const tabledef::Column ageColumn = findColumn(schema, QStringLiteral("age"));
        QCOMPARE(ageColumn.name, QStringLiteral("age"));
        QCOMPARE(ageColumn.defaultValue, QStringLiteral("21"));
        QCOMPARE(ageColumn.notNull, false);

        const tabledef::Constraint uniqueConstraint =
            findConstraint(schema, QStringLiteral("uq_%1_name_age").arg(tableName));
        QCOMPARE(uniqueConstraint.name, QStringLiteral("uq_%1_name_age").arg(tableName));
        QCOMPARE(uniqueConstraint.columns, QStringList({QStringLiteral("name"), QStringLiteral("age")}));
    }

    void test_dispatcherPartialAlterColumnPreservesAttributes()
    {
        const QString databaseName = QStringLiteral("test_parser_dispatcher_partial_alter_db");
        const QString tableName = QStringLiteral("test_parser_dispatcher_partial_alter_table");
        ensureDatabase(databaseName);

        tabledef::TableSchema schema;
        schema.tableName = tableName;
        schema.columns = {
            makeColumn(QStringLiteral("id"), tabledef::ColumnType::Int, 0, true),
            makeColumn(QStringLiteral("name"), tabledef::ColumnType::Varchar, 20, true),
            makeColumn(QStringLiteral("age"), tabledef::ColumnType::Int, 0, false, QStringLiteral("18")),
        };
        schema.constraints = {
            makePrimaryKey(QStringLiteral("pk_%1_id").arg(tableName), {QStringLiteral("id")}),
        };
        ensureTable(tableName, schema);

        SqlDispatcher dispatcher;
        SqlExecResult result = dispatcher.execute(
            QStringLiteral("ALTER TABLE %1 ALTER COLUMN name SET DEFAULT 'anonymous'").arg(tableName));
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        QString error;
        tabledef::TableSchema loaded = loadUserTableSchema(tableName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        tabledef::Column nameColumn = findColumn(loaded, QStringLiteral("name"));
        QCOMPARE(nameColumn.length, 20);
        QVERIFY(nameColumn.notNull);
        QCOMPARE(nameColumn.defaultValue, QStringLiteral("anonymous"));

        result = dispatcher.execute(
            QStringLiteral("ALTER TABLE %1 ALTER COLUMN age DROP DEFAULT").arg(tableName));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        loaded = loadUserTableSchema(tableName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        tabledef::Column ageColumn = findColumn(loaded, QStringLiteral("age"));
        QCOMPARE(ageColumn.type, tabledef::ColumnType::Int);
        QVERIFY(!ageColumn.notNull);
        QVERIFY(ageColumn.defaultValue.isEmpty());

        result = dispatcher.execute(
            QStringLiteral("ALTER TABLE %1 ALTER COLUMN name DROP NOT NULL").arg(tableName));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        loaded = loadUserTableSchema(tableName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        nameColumn = findColumn(loaded, QStringLiteral("name"));
        QVERIFY(!nameColumn.notNull);
        QCOMPARE(nameColumn.defaultValue, QStringLiteral("anonymous"));

        result = dispatcher.execute(
            QStringLiteral("ALTER TABLE %1 ALTER COLUMN name TYPE VARCHAR(64)").arg(tableName));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        loaded = loadUserTableSchema(tableName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        nameColumn = findColumn(loaded, QStringLiteral("name"));
        QCOMPARE(nameColumn.type, tabledef::ColumnType::Varchar);
        QCOMPARE(nameColumn.length, 64);
        QCOMPARE(nameColumn.defaultValue, QStringLiteral("anonymous"));
    }

    void test_dispatcherPartialAlterSetNotNullRejectsExistingEmptyValues()
    {
        const QString databaseName = QStringLiteral("test_parser_dispatcher_partial_not_null_db");
        const QString tableName = QStringLiteral("test_parser_dispatcher_partial_not_null_table");
        ensureDatabase(databaseName);

        tabledef::TableSchema schema;
        schema.tableName = tableName;
        schema.columns = {
            makeColumn(QStringLiteral("id"), tabledef::ColumnType::Int, 0, true),
            makeColumn(QStringLiteral("nickname"), tabledef::ColumnType::Varchar, 20, false),
        };
        schema.constraints = {
            makePrimaryKey(QStringLiteral("pk_%1_id").arg(tableName), {QStringLiteral("id")}),
        };
        ensureTable(tableName, schema);
        seedRows(tableName, {makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                                      {QStringLiteral("nickname"), QString()}})});

        SqlDispatcher dispatcher;
        const SqlExecResult result = dispatcher.execute(
            QStringLiteral("ALTER TABLE %1 ALTER COLUMN nickname SET NOT NULL").arg(tableName));
        QVERIFY(!result.success);
        QVERIFY2(result.errorMessage.contains(QStringLiteral("cannot be null")),
                 qPrintable(result.errorMessage));
    }

    void test_dispatcherRenameColumnPreservesDataAndIndexMetadata()
    {
        const QString databaseName = QStringLiteral("test_parser_dispatcher_rename_column_db");
        const QString tableName = QStringLiteral("test_parser_dispatcher_rename_column_table");
        ensureDatabase(databaseName);
        ensureTable(tableName, baseSchema(tableName));
        seedRows(tableName, {makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                                      {QStringLiteral("name"), QStringLiteral("alice")}})});

        SqlDispatcher dispatcher;
        SqlExecResult result = dispatcher.execute(
            QStringLiteral("CREATE INDEX idx_%1_name ON %1(name)").arg(tableName));
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        result = dispatcher.execute(
            QStringLiteral("ALTER TABLE %1 RENAME COLUMN name TO full_name").arg(tableName));
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        QString error;
        const tabledef::TableSchema schema = loadUserTableSchema(tableName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(tabledef::hasColumn(schema, QStringLiteral("full_name")));
        QVERIFY(!tabledef::hasColumn(schema, QStringLiteral("name")));

        const tabledef::IndexMeta index = findIndexMeta(databaseName,
                                                        tableName,
                                                        service::getDataRoot(),
                                                        QStringLiteral("idx_%1_name").arg(tableName),
                                                        &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(index.columnNames, QStringList({QStringLiteral("full_name")}));

        const SelectRowsResult rows = selectAllRows(tableName);
        QVERIFY2(rows.success, qPrintable(rows.errorMessage));
        QCOMPARE(rows.resultTable.columns, QStringList({QStringLiteral("id"), QStringLiteral("full_name")}));
        QCOMPARE(rows.resultTable.rows.at(0), QStringList({QStringLiteral("1"), QStringLiteral("alice")}));
    }

    void test_dispatcherAlterRejectsIncompletePayload()
    {
        const QString databaseName = QStringLiteral("test_parser_dispatcher_alter_incomplete_db");
        const QString tableName = QStringLiteral("test_parser_dispatcher_alter_incomplete_table");
        ensureDatabase(databaseName);
        ensureTable(tableName, baseSchema(tableName));

        SqlDispatcher dispatcher;

        sqlparser::ParseResult addColumnParsed;
        addColumnParsed.success = true;
        addColumnParsed.commandType = QStringLiteral("ALTER_TABLE");
        addColumnParsed.payload.insert(QStringLiteral("tableName"), tableName);
        addColumnParsed.payload.insert(QStringLiteral("alterAction"), QStringLiteral("ADD_COLUMN"));
        const SqlExecResult addColumnResult = dispatcher.dispatch(addColumnParsed);
        QVERIFY(!addColumnResult.success);
        QVERIFY(addColumnResult.errorMessage.contains(QStringLiteral("complete column payload")));

        sqlparser::ParseResult addConstraintParsed;
        addConstraintParsed.success = true;
        addConstraintParsed.commandType = QStringLiteral("ALTER_TABLE");
        addConstraintParsed.payload.insert(QStringLiteral("tableName"), tableName);
        addConstraintParsed.payload.insert(QStringLiteral("alterAction"), QStringLiteral("ADD_CONSTRAINT"));
        const SqlExecResult addConstraintResult = dispatcher.dispatch(addConstraintParsed);
        QVERIFY(!addConstraintResult.success);
        QVERIFY(addConstraintResult.errorMessage.contains(QStringLiteral("complete constraint payload")));

        sqlparser::ParseResult setDefaultParsed;
        setDefaultParsed.success = true;
        setDefaultParsed.commandType = QStringLiteral("ALTER_TABLE");
        setDefaultParsed.payload.insert(QStringLiteral("tableName"), tableName);
        setDefaultParsed.payload.insert(QStringLiteral("alterAction"), QStringLiteral("ALTER_COLUMN_SET_DEFAULT"));
        setDefaultParsed.payload.insert(QStringLiteral("columnName"), QStringLiteral("name"));
        const SqlExecResult setDefaultResult = dispatcher.dispatch(setDefaultParsed);
        QVERIFY(!setDefaultResult.success);
        QVERIFY(setDefaultResult.errorMessage.contains(QStringLiteral("defaultValue")));

        sqlparser::ParseResult setTypeParsed;
        setTypeParsed.success = true;
        setTypeParsed.commandType = QStringLiteral("ALTER_TABLE");
        setTypeParsed.payload.insert(QStringLiteral("tableName"), tableName);
        setTypeParsed.payload.insert(QStringLiteral("alterAction"), QStringLiteral("ALTER_COLUMN_SET_TYPE"));
        setTypeParsed.payload.insert(QStringLiteral("columnName"), QStringLiteral("name"));
        setTypeParsed.payload.insert(QStringLiteral("type"), QStringLiteral("VARCHAR"));
        const SqlExecResult setTypeResult = dispatcher.dispatch(setTypeParsed);
        QVERIFY(!setTypeResult.success);
        QVERIFY(setTypeResult.errorMessage.contains(QStringLiteral("length")));
    }

    void test_dispatcherRejectsMalformedConditionsPayload()
    {
        const QString databaseName = QStringLiteral("test_parser_dispatcher_condition_incomplete_db");
        const QString tableName = QStringLiteral("test_parser_dispatcher_condition_incomplete_table");
        ensureDatabase(databaseName);
        ensureTable(tableName, baseSchema(tableName));

        SqlDispatcher dispatcher;

        sqlparser::ParseResult parsed;
        parsed.success = true;
        parsed.commandType = QStringLiteral("SELECT");
        parsed.payload.insert(QStringLiteral("tableName"), tableName);
        parsed.payload.insert(QStringLiteral("projection"), QStringList{QStringLiteral("*")});
        parsed.payload.insert(QStringLiteral("limit"), -1);
        parsed.payload.insert(QStringLiteral("conditions"), QVariantList{QVariantMap{}});

        const SqlExecResult result = dispatcher.dispatch(parsed);
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("WHERE payload is incomplete")));
    }

    void test_dispatcherWhereAndLimitFlowToService()
    {
        const QString databaseName = QStringLiteral("test_parser_dispatcher_where_db");
        const QString tableName = QStringLiteral("test_parser_dispatcher_where_table");
        ensureDatabase(databaseName);
        ensureTable(tableName, baseSchema(tableName));
        seedRows(tableName,
                 QList<QMap<QString, QString>>{
                     makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                              {QStringLiteral("name"), QStringLiteral("alice")}}),
                     makeRow({{QStringLiteral("id"), QStringLiteral("2")},
                              {QStringLiteral("name"), QStringLiteral("bob")}}),
                 });

        SqlDispatcher dispatcher;

        const SqlExecResult selectResult = dispatcher.execute(
            QStringLiteral("SELECT * FROM %1 WHERE id = 1 LIMIT 1").arg(tableName));
        QVERIFY2(selectResult.success, qPrintable(selectResult.errorMessage));
        QCOMPARE(selectResult.selectResult.resultTable.rows.size(), 1);
        QCOMPARE(selectResult.selectResult.resultTable.rows.at(0),
                 QStringList({QStringLiteral("1"), QStringLiteral("alice")}));

        const SqlExecResult updateResult = dispatcher.execute(
            QStringLiteral("UPDATE %1 SET name = 'carol' WHERE id = 2").arg(tableName));
        QVERIFY2(updateResult.success, qPrintable(updateResult.errorMessage));
        QCOMPARE(updateResult.affectedRows, 1);

        const SqlExecResult deleteResult = dispatcher.execute(
            QStringLiteral("DELETE FROM %1 WHERE id = 1").arg(tableName));
        QVERIFY2(deleteResult.success, qPrintable(deleteResult.errorMessage));
        QCOMPARE(deleteResult.affectedRows, 1);

        const SelectRowsResult remaining = tuple_service::selectRows(tableName, {}, {}, -1);
        QVERIFY2(remaining.success, qPrintable(remaining.errorMessage));
        QCOMPARE(remaining.resultTable.rows.size(), 1);
        QCOMPARE(remaining.resultTable.rows.at(0),
                 QStringList({QStringLiteral("2"), QStringLiteral("carol")}));
    }

    void test_splitStatementsCanDriveSequentialExecution()
    {
        const QString databaseName = QStringLiteral("test_parser_dispatcher_batch_db");
        const QString tableName = QStringLiteral("test_parser_dispatcher_batch_table");
        SqlDispatcher dispatcher;

        const QString script = QStringLiteral(
            "CREATE DATABASE %1;\n"
            "USE %1;\n"
            "CREATE TABLE %2 (id INT PRIMARY KEY, name VARCHAR(32) NOT NULL);\n"
            "INSERT INTO %2 VALUES (1, 'alice');\n"
            "UPDATE %2 SET name = 'carol' WHERE id = 1;\n"
            "DELETE FROM %2 WHERE name = 'nobody';")
                                   .arg(databaseName, tableName);

        const QStringList statements = SqlDispatcher::splitStatements(script);
        QCOMPARE(statements.size(), 6);

        for (const QString &statement : statements) {
            const SqlExecResult result = dispatcher.execute(statement);
            QVERIFY2(result.success, qPrintable(result.errorMessage));
        }

        const SelectRowsResult rows = tuple_service::selectRows(tableName, {}, {}, -1);
        QVERIFY2(rows.success, qPrintable(rows.errorMessage));
        QCOMPARE(rows.resultTable.rows.size(), 1);
        QCOMPARE(rows.resultTable.rows.at(0),
                 QStringList({QStringLiteral("1"), QStringLiteral("carol")}));
    }

    void test_queryExecutorSelectUsesServiceWithIsolatedContext()
    {
        const QString databaseName = QStringLiteral("test_query_executor_select_db");
        const QString tableName = QStringLiteral("test_query_executor_select_table");
        ensureDatabase(databaseName);
        ensureTable(tableName, baseSchema(tableName));
        seedRows(tableName,
                 QList<QMap<QString, QString>>{
                     makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                              {QStringLiteral("name"), QStringLiteral("alice")}}),
                     makeRow({{QStringLiteral("id"), QStringLiteral("2")},
                              {QStringLiteral("name"), QStringLiteral("bob")}}),
                 });

        currentDatabase.clear();

        QueryExecutor executor;
        QueryExecuteContext context;
        context.currentDatabase = databaseName;
        context.dataRoot = m_dataRoot;

        const QueryExecuteResult result = executor.executeSelectSql(
            QStringLiteral("SELECT * FROM %1 WHERE id = 2 LIMIT 1").arg(tableName),
            context);
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 1);
        QCOMPARE(result.selectResult.resultTable.rows.at(0),
                 QStringList({QStringLiteral("2"), QStringLiteral("bob")}));
        QVERIFY(currentDatabase.isEmpty());
    }

    void test_queryExecutorRejectsNonSelectCommands()
    {
        QueryExecutor executor;
        QueryExecuteContext context;
        context.currentDatabase = QStringLiteral("unused_db");
        context.dataRoot = m_dataRoot;

        const QueryExecuteResult result = executor.executeSql(
            QStringLiteral("SHOW TABLES;"),
            context);
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("only supports SELECT")));
    }

    void test_dispatcherIndexSqlUsesService()
    {
        const QString databaseName = QStringLiteral("test_parser_dispatcher_index_db");
        const QString tableName = QStringLiteral("test_parser_dispatcher_index_table");
        ensureDatabase(databaseName);
        ensureTable(tableName, baseSchema(tableName));
        seedRows(tableName,
                 QList<QMap<QString, QString>>{
                     makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                              {QStringLiteral("name"), QStringLiteral("alice")}}),
                     makeRow({{QStringLiteral("id"), QStringLiteral("2")},
                              {QStringLiteral("name"), QStringLiteral("bob")}}),
                 });

        SqlDispatcher dispatcher;
        const QString indexName = QStringLiteral("idx_test_parser_dispatcher_name");

        const SqlExecResult createResult = dispatcher.execute(
            QStringLiteral("CREATE INDEX %1 ON %2(name)").arg(indexName, tableName));
        QVERIFY2(createResult.success, qPrintable(createResult.errorMessage));

        QString error;
        const QStringList indexesAfterCreate = listedIndexes(databaseName, tableName, m_dataRoot, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(indexesAfterCreate.contains(indexName));

        const SqlExecResult dropResult = dispatcher.execute(
            QStringLiteral("DROP INDEX %1 ON %2").arg(indexName, tableName));
        QVERIFY2(dropResult.success, qPrintable(dropResult.errorMessage));

        const QStringList indexesAfterDrop = listedIndexes(databaseName, tableName, m_dataRoot, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!indexesAfterDrop.contains(indexName));
    }

    void test_dispatcherUniqueAndMultiColumnIndexSqlUseService()
    {
        const QString databaseName = QStringLiteral("test_parser_dispatcher_unique_index_db");
        const QString tableName = QStringLiteral("test_parser_dispatcher_unique_index_table");
        ensureDatabase(databaseName);

        tabledef::TableSchema schema = baseSchema(tableName);
        schema.columns.append(makeColumn(QStringLiteral("age"), tabledef::ColumnType::Int));
        ensureTable(tableName, schema);
        seedRows(tableName,
                 QList<QMap<QString, QString>>{
                     makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                              {QStringLiteral("name"), QStringLiteral("alice")},
                              {QStringLiteral("age"), QStringLiteral("20")}}),
                     makeRow({{QStringLiteral("id"), QStringLiteral("2")},
                              {QStringLiteral("name"), QStringLiteral("bob")},
                              {QStringLiteral("age"), QStringLiteral("21")}}),
                 });

        SqlDispatcher dispatcher;
        const QString uniqueIndexName = QStringLiteral("uq_test_parser_dispatcher_name_age");
        const QString plainIndexName = QStringLiteral("idx_test_parser_dispatcher_name_age");

        const SqlExecResult uniqueCreate = dispatcher.execute(
            QStringLiteral("CREATE UNIQUE INDEX %1 ON %2(name, age);").arg(uniqueIndexName, tableName));
        QVERIFY2(uniqueCreate.success, qPrintable(uniqueCreate.errorMessage));

        QString error;
        const tabledef::IndexMeta uniqueMeta = findIndexMeta(databaseName, tableName, m_dataRoot, uniqueIndexName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(uniqueMeta.indexName, uniqueIndexName);
        QCOMPARE(uniqueMeta.columnNames, QStringList({QStringLiteral("name"), QStringLiteral("age")}));
        QVERIFY(uniqueMeta.isUnique);

        const SqlExecResult plainCreate = dispatcher.execute(
            QStringLiteral("CREATE INDEX %1 ON %2(name, age);").arg(plainIndexName, tableName));
        QVERIFY2(plainCreate.success, qPrintable(plainCreate.errorMessage));

        const tabledef::IndexMeta plainMeta = findIndexMeta(databaseName, tableName, m_dataRoot, plainIndexName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(plainMeta.indexName, plainIndexName);
        QCOMPARE(plainMeta.columnNames, QStringList({QStringLiteral("name"), QStringLiteral("age")}));
        QVERIFY(!plainMeta.isUnique);
    }

    void test_dispatcherForeignKeyCascadeViaSql()
    {
        const QString databaseName = QStringLiteral("test_parser_dispatcher_fk_cascade_db");
        ensureDatabase(databaseName);

        SqlDispatcher dispatcher;
        QVERIFY2(dispatcher.execute(QStringLiteral(
                     "CREATE TABLE parent ("
                     "id INT PRIMARY KEY, "
                     "name VARCHAR(64) NOT NULL"
                     ");")).success,
                 "create parent failed");
        QVERIFY2(dispatcher.execute(QStringLiteral(
                     "CREATE TABLE child ("
                     "id INT PRIMARY KEY, "
                     "parent_id INT, "
                     "note VARCHAR(64), "
                     "CONSTRAINT fk_child_parent FOREIGN KEY (parent_id) "
                     "REFERENCES parent(id) ON DELETE CASCADE ON UPDATE CASCADE"
                     ");")).success,
                 "create child failed");

        QVERIFY2(dispatcher.execute(QStringLiteral(
                     "INSERT INTO parent (id, name) VALUES (1, 'alice');")).success,
                 "insert parent failed");
        QVERIFY2(dispatcher.execute(QStringLiteral(
                     "INSERT INTO child (id, parent_id, note) VALUES (10, 1, 'child');")).success,
                 "insert child failed");

        const SqlExecResult updateResult = dispatcher.execute(
            QStringLiteral("UPDATE parent SET id = 2 WHERE id = 1;"));
        QVERIFY2(updateResult.success, qPrintable(updateResult.errorMessage));

        SelectRowsResult childRows = selectAllRows(QStringLiteral("child"));
        QVERIFY2(childRows.success, qPrintable(childRows.errorMessage));
        QCOMPARE(childRows.resultTable.rows.size(), 1);
        QCOMPARE(childRows.resultTable.rows.at(0),
                 QStringList({QStringLiteral("10"), QStringLiteral("2"), QStringLiteral("child")}));

        const SqlExecResult deleteResult = dispatcher.execute(
            QStringLiteral("DELETE FROM parent WHERE id = 2;"));
        QVERIFY2(deleteResult.success, qPrintable(deleteResult.errorMessage));

        childRows = selectAllRows(QStringLiteral("child"));
        QVERIFY2(childRows.success, qPrintable(childRows.errorMessage));
        QCOMPARE(childRows.resultTable.rows.size(), 0);
    }

    void test_dispatcherForeignKeySetNullAndSetDefaultViaSql()
    {
        const QString databaseName = QStringLiteral("test_parser_dispatcher_fk_set_actions_db");
        ensureDatabase(databaseName);

        SqlDispatcher dispatcher;
        QVERIFY2(dispatcher.execute(QStringLiteral(
                     "CREATE TABLE parent ("
                     "id INT PRIMARY KEY, "
                     "name VARCHAR(64) NOT NULL"
                     ");")).success,
                 "create parent failed");
        QVERIFY2(dispatcher.execute(QStringLiteral(
                     "CREATE TABLE child_null ("
                     "id INT PRIMARY KEY, "
                     "parent_id INT, "
                     "note VARCHAR(64), "
                     "CONSTRAINT fk_child_null_parent FOREIGN KEY (parent_id) "
                     "REFERENCES parent(id) ON DELETE SET NULL ON UPDATE NO ACTION"
                     ");")).success,
                 "create child_null failed");
        QVERIFY2(dispatcher.execute(QStringLiteral(
                     "CREATE TABLE child_default ("
                     "id INT PRIMARY KEY, "
                     "parent_id INT DEFAULT 0, "
                     "note VARCHAR(64), "
                     "CONSTRAINT fk_child_default_parent FOREIGN KEY (parent_id) "
                     "REFERENCES parent(id) ON DELETE SET DEFAULT ON UPDATE NO ACTION"
                     ");")).success,
                 "create child_default failed");

        QVERIFY2(dispatcher.execute(QStringLiteral(
                     "INSERT INTO parent (id, name) VALUES (0, 'root'), (1, 'alice'), (2, 'bob');")).success,
                 "insert parent rows failed");
        QVERIFY2(dispatcher.execute(QStringLiteral(
                     "INSERT INTO child_null (id, parent_id, note) VALUES (10, 1, 'null_child');")).success,
                 "insert child_null failed");
        QVERIFY2(dispatcher.execute(QStringLiteral(
                     "INSERT INTO child_default (id, parent_id, note) VALUES (20, 2, 'default_child');")).success,
                 "insert child_default failed");

        const SqlExecResult deleteFirstParent = dispatcher.execute(
            QStringLiteral("DELETE FROM parent WHERE id = 1;"));
        QVERIFY2(deleteFirstParent.success, qPrintable(deleteFirstParent.errorMessage));

        SelectRowsResult childNullRows = selectAllRows(QStringLiteral("child_null"));
        QVERIFY2(childNullRows.success, qPrintable(childNullRows.errorMessage));
        QCOMPARE(childNullRows.resultTable.rows.size(), 1);
        QCOMPARE(childNullRows.resultTable.rows.at(0),
                 QStringList({QStringLiteral("10"), QString(), QStringLiteral("null_child")}));

        const SqlExecResult deleteSecondParent = dispatcher.execute(
            QStringLiteral("DELETE FROM parent WHERE id = 2;"));
        QVERIFY2(deleteSecondParent.success, qPrintable(deleteSecondParent.errorMessage));

        SelectRowsResult childDefaultRows = selectAllRows(QStringLiteral("child_default"));
        QVERIFY2(childDefaultRows.success, qPrintable(childDefaultRows.errorMessage));
        QCOMPARE(childDefaultRows.resultTable.rows.size(), 1);
        QCOMPARE(childDefaultRows.resultTable.rows.at(0),
                 QStringList({QStringLiteral("20"), QStringLiteral("0"), QStringLiteral("default_child")}));
    }

    void test_dispatcherForeignKeyNoActionViaSql()
    {
        const QString databaseName = QStringLiteral("test_parser_dispatcher_fk_no_action_db");
        ensureDatabase(databaseName);

        SqlDispatcher dispatcher;
        QVERIFY2(dispatcher.execute(QStringLiteral(
                     "CREATE TABLE parent ("
                     "id INT PRIMARY KEY, "
                     "name VARCHAR(64) NOT NULL"
                     ");")).success,
                 "create parent failed");
        QVERIFY2(dispatcher.execute(QStringLiteral(
                     "CREATE TABLE child ("
                     "id INT PRIMARY KEY, "
                     "parent_id INT, "
                     "note VARCHAR(64), "
                     "CONSTRAINT fk_child_parent FOREIGN KEY (parent_id) "
                     "REFERENCES parent(id) ON DELETE NO ACTION ON UPDATE NO ACTION"
                     ");")).success,
                 "create child failed");

        QVERIFY2(dispatcher.execute(QStringLiteral(
                     "INSERT INTO parent (id, name) VALUES (1, 'alice');")).success,
                 "insert parent failed");
        QVERIFY2(dispatcher.execute(QStringLiteral(
                     "INSERT INTO child (id, parent_id, note) VALUES (10, 1, 'child');")).success,
                 "insert child failed");

        const SqlExecResult deleteResult = dispatcher.execute(
            QStringLiteral("DELETE FROM parent WHERE id = 1;"));
        QVERIFY(!deleteResult.success);
        QVERIFY(deleteResult.errorMessage.contains(QStringLiteral("foreign key")));

        const SelectRowsResult parentRows = selectAllRows(QStringLiteral("parent"));
        QVERIFY2(parentRows.success, qPrintable(parentRows.errorMessage));
        QCOMPARE(parentRows.resultTable.rows.size(), 1);

        const SelectRowsResult childRows = selectAllRows(QStringLiteral("child"));
        QVERIFY2(childRows.success, qPrintable(childRows.errorMessage));
        QCOMPARE(childRows.resultTable.rows.size(), 1);
    }

private:
    QString m_dataRoot;
};

int service_tests::runParserDispatcherTests()
{
    ParserDispatcherTest test;
    return QTest::qExec(&test);
}

#include "test_parser_dispatcher.moc"
