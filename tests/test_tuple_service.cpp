#include "../service/service.h"

#include <QDir>
#include <QtTest>

#include "test_entry.h"

using namespace service;

namespace {

QString testDataRoot()
{
    return QDir(QDir::tempPath()).absoluteFilePath(QStringLiteral("DBMS_test_tuple_service"));
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

tabledef::Constraint makeForeignKey(const QString &name,
                                    const QStringList &columns,
                                    const QString &referencedTable,
                                    const QStringList &referencedColumns,
                                    tabledef::ForeignKeyAction onDeleteAction = tabledef::ForeignKeyAction::NoAction,
                                    tabledef::ForeignKeyAction onUpdateAction = tabledef::ForeignKeyAction::NoAction)
{
    tabledef::Constraint constraint;
    constraint.name = name;
    constraint.type = tabledef::ConstraintType::ForeignKey;
    constraint.columns = columns;
    constraint.referencedTable = referencedTable;
    constraint.referencedColumns = referencedColumns;
    constraint.onDeleteAction = onDeleteAction;
    constraint.onUpdateAction = onUpdateAction;
    return constraint;
}

tabledef::Constraint makeUnique(const QString &name, const QStringList &columns)
{
    return tabledef::Constraint{name, tabledef::ConstraintType::Unique, columns, QString(), {}, QString()};
}

tabledef::TableSchema parentSchema(const QString &tableName)
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

tabledef::TableSchema childSchema(const QString &tableName,
                                  const QString &parentTableName,
                                  bool parentIdNotNull,
                                  const QString &parentIdDefaultValue,
                                  tabledef::ForeignKeyAction onDeleteAction,
                                  tabledef::ForeignKeyAction onUpdateAction)
{
    tabledef::TableSchema schema;
    schema.tableName = tableName;
    schema.columns = {
        makeColumn(QStringLiteral("id"), tabledef::ColumnType::Int, 0, true),
        makeColumn(QStringLiteral("parent_id"),
                   tabledef::ColumnType::Int,
                   0,
                   parentIdNotNull,
                   parentIdDefaultValue),
        makeColumn(QStringLiteral("note"), tabledef::ColumnType::Varchar, 64, false),
    };
    schema.constraints = {
        makePrimaryKey(QStringLiteral("pk_%1_id").arg(tableName), {QStringLiteral("id")}),
        makeForeignKey(QStringLiteral("fk_%1_parent").arg(tableName),
                       {QStringLiteral("parent_id")},
                       parentTableName,
                       {QStringLiteral("id")},
                       onDeleteAction,
                       onUpdateAction),
    };
    return schema;
}

tabledef::TableSchema childSchema(const QString &tableName, const QString &parentTableName)
{
    return childSchema(tableName,
                       parentTableName,
                       true,
                       QString(),
                       tabledef::ForeignKeyAction::NoAction,
                       tabledef::ForeignKeyAction::NoAction);
}

tabledef::TableSchema parentIdReferenceSchema(const QString &tableName,
                                              const QString &referencedTableName,
                                              const QStringList &referencedColumns,
                                              bool uniqueParentId,
                                              tabledef::ForeignKeyAction onDeleteAction,
                                              tabledef::ForeignKeyAction onUpdateAction)
{
    tabledef::TableSchema schema;
    schema.tableName = tableName;
    schema.columns = {
        makeColumn(QStringLiteral("id"), tabledef::ColumnType::Int, 0, true),
        makeColumn(QStringLiteral("parent_id"), tabledef::ColumnType::Int, 0, true),
        makeColumn(QStringLiteral("note"), tabledef::ColumnType::Varchar, 64, false),
    };
    schema.constraints = {
        makePrimaryKey(QStringLiteral("pk_%1_id").arg(tableName), {QStringLiteral("id")}),
        makeForeignKey(QStringLiteral("fk_%1_parent").arg(tableName),
                       {QStringLiteral("parent_id")},
                       referencedTableName,
                       referencedColumns,
                       onDeleteAction,
                       onUpdateAction),
    };
    if (uniqueParentId) {
        schema.constraints.append(makeUnique(QStringLiteral("uq_%1_parent_id").arg(tableName),
                                             {QStringLiteral("parent_id")}));
    }
    return schema;
}

tabledef::TableSchema selfReferenceSchema(const QString &tableName,
                                          tabledef::ForeignKeyAction onDeleteAction,
                                          tabledef::ForeignKeyAction onUpdateAction)
{
    tabledef::TableSchema schema;
    schema.tableName = tableName;
    schema.columns = {
        makeColumn(QStringLiteral("id"), tabledef::ColumnType::Int, 0, true),
        makeColumn(QStringLiteral("parent_id"), tabledef::ColumnType::Int, 0, false),
        makeColumn(QStringLiteral("note"), tabledef::ColumnType::Varchar, 64, false),
    };
    schema.constraints = {
        makePrimaryKey(QStringLiteral("pk_%1_id").arg(tableName), {QStringLiteral("id")}),
        makeForeignKey(QStringLiteral("fk_%1_self_parent").arg(tableName),
                       {QStringLiteral("parent_id")},
                       tableName,
                       {QStringLiteral("id")},
                       onDeleteAction,
                       onUpdateAction),
    };
    return schema;
}

tabledef::TableSchema selfReferenceSchema(const QString &tableName)
{
    return selfReferenceSchema(tableName,
                               tabledef::ForeignKeyAction::Cascade,
                               tabledef::ForeignKeyAction::Cascade);
}

tabledef::TableSchema diamondLeafSchema(const QString &tableName,
                                        const QString &leftParentTableName,
                                        const QString &rightParentTableName)
{
    tabledef::TableSchema schema;
    schema.tableName = tableName;
    schema.columns = {
        makeColumn(QStringLiteral("id"), tabledef::ColumnType::Int, 0, true),
        makeColumn(QStringLiteral("left_parent_id"), tabledef::ColumnType::Int, 0, true),
        makeColumn(QStringLiteral("right_parent_id"), tabledef::ColumnType::Int, 0, true),
        makeColumn(QStringLiteral("note"), tabledef::ColumnType::Varchar, 64, false),
    };
    schema.constraints = {
        makePrimaryKey(QStringLiteral("pk_%1_id").arg(tableName), {QStringLiteral("id")}),
        makeForeignKey(QStringLiteral("fk_%1_left").arg(tableName),
                       {QStringLiteral("left_parent_id")},
                       leftParentTableName,
                       {QStringLiteral("id")},
                       tabledef::ForeignKeyAction::Cascade,
                       tabledef::ForeignKeyAction::Cascade),
        makeForeignKey(QStringLiteral("fk_%1_right").arg(tableName),
                       {QStringLiteral("right_parent_id")},
                       rightParentTableName,
                       {QStringLiteral("id")},
                       tabledef::ForeignKeyAction::Cascade,
                       tabledef::ForeignKeyAction::Cascade),
    };
    return schema;
}

tabledef::TableSchema indexedSchema(const QString &tableName)
{
    tabledef::TableSchema schema = parentSchema(tableName);
    schema.constraints.append(makeUnique(QStringLiteral("uq_%1_name").arg(tableName), {QStringLiteral("name")}));
    return schema;
}

void ensureDatabase(const QString &databaseName, const QString &dataRoot)
{
    Q_UNUSED(dataRoot);
    TaskResult result = database_service::createDatabase(databaseName);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    result = database_service::useDatabase(databaseName);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
}

void ensureTable(const QString &databaseName,
                 const QString &tableName,
                 const tabledef::TableSchema &schema,
                 const QString &dataRoot)
{
    Q_UNUSED(databaseName);
    Q_UNUSED(dataRoot);
    TaskResult result = table_service::createTable(tableName, schema);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
}

void seedRow(const QString &databaseName,
             const QString &tableName,
             const QStringList &row,
             const QString &dataRoot)
{
    repo::TableRepo tableRepo(databaseName, tableName, dataRoot);
    const repo::RepositoryResult result = tableRepo.insertRow(row);
    QVERIFY2(result.ok, qPrintable(result.error));
}

QMap<QString, QString> makeRow(std::initializer_list<QPair<QString, QString>> pairs)
{
    QMap<QString, QString> row;
    for (const QPair<QString, QString> &pair : pairs) {
        row.insert(pair.first, pair.second);
    }
    return row;
}

QMap<QString, QString> makeAssignment(const QString &column, const QString &value)
{
    return QMap<QString, QString>{{column, value}};
}

QList<QMap<QString, QString>> makeRows(std::initializer_list<QMap<QString, QString>> rows)
{
    return QList<QMap<QString, QString>>(rows.begin(), rows.end());
}

QStringList rowValues(const repo::TableData &table, int rowIndex)
{
    return table.rows.at(rowIndex);
}

repo::TableData loadTable(const QString &databaseName, const QString &tableName, const QString &dataRoot)
{
    repo::TableRepo tableRepo(databaseName, tableName, dataRoot);
    QString error;
    const repo::TableData table = tableRepo.readTable(&error);
    Q_ASSERT_X(error.isEmpty(), "loadTable", qPrintable(error));
    return table;
}

QStringList loadRowIds(const QString &databaseName,
                      const QString &tableName,
                      const QString &dataRoot,
                      QString *error = nullptr)
{
    repo::FlatFileTableStore store(dataRoot);
    const repo::TableData rowIdTable = store.readTable(store.getRowIdFilePath(databaseName, tableName), error);
    QStringList rowIds;
    for (const repo::TableRow &row : rowIdTable.rows) {
        if (!row.isEmpty()) {
            rowIds.append(row.first());
        }
    }
    return rowIds;
}

QString findIndexNameByColumns(const QString &databaseName,
                               const QString &tableName,
                               const QString &dataRoot,
                               const QStringList &columns,
                               QString *error = nullptr)
{
    repo::IndexRepo indexRepo(databaseName, tableName, dataRoot);
    const QList<tabledef::IndexMeta> indexes = indexRepo.listIndexes(error);
    for (const tabledef::IndexMeta &index : indexes) {
        if (index.columnNames == columns) {
            return index.indexName;
        }
    }
    return {};
}

QStringList searchIndex(const QString &databaseName,
                        const QString &tableName,
                        const QString &indexName,
                        const QStringList &keyValues,
                        const QString &dataRoot,
                        QString *error = nullptr)
{
    repo::SortIndexRepo sortIndexRepo(databaseName, indexName, tableName, dataRoot);
    return sortIndexRepo.search(keyValues, error);
}

} // namespace

class TupleServiceTest : public QObject
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

    void test_selectRows()
    {
        const QString databaseName = QStringLiteral("test_tuple_service_select_db");
        const QString tableName = QStringLiteral("test_tuple_service_select_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, parentSchema(tableName), m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("1"), QStringLiteral("alice")}, m_dataRoot);
        seedRow(databaseName, tableName, {QStringLiteral("2"), QStringLiteral("bob")}, m_dataRoot);

        SelectRowsResult allRows = tuple_service::selectRows(tableName,
                                     {QStringLiteral("*")},
                                     {});
        QVERIFY2(allRows.success, qPrintable(allRows.errorMessage));
        const QStringList expectedColumns{QStringLiteral("id"), QStringLiteral("name")};
        QCOMPARE(allRows.resultTable.columns, expectedColumns);
        QCOMPARE(allRows.resultTable.rows.size(), 2);

        SelectRowsResult projectedRows = tuple_service::selectRows(tableName,
                                       {QStringLiteral("name")},
                                       {SimpleCondition{QStringLiteral("id"), QStringLiteral("2")}});
        QVERIFY2(projectedRows.success, qPrintable(projectedRows.errorMessage));
        const QStringList projectedColumns{QStringLiteral("name")};
        QCOMPARE(projectedRows.resultTable.columns, projectedColumns);
        QCOMPARE(projectedRows.resultTable.rows.size(), 1);
        const QStringList projectedRow{QStringLiteral("bob")};
        QCOMPARE(rowValues(projectedRows.resultTable, 0), projectedRow);

        SelectRowsResult limitedRows = tuple_service::selectRows(tableName,
                                                                 {QStringLiteral("*")},
                                                                 {},
                                                                 1);
        QVERIFY2(limitedRows.success, qPrintable(limitedRows.errorMessage));
        QCOMPARE(limitedRows.resultTable.columns, expectedColumns);
        QCOMPARE(limitedRows.resultTable.rows.size(), 1);
        QCOMPARE(rowValues(limitedRows.resultTable, 0),
                 QStringList({QStringLiteral("1"), QStringLiteral("alice")}));

        SelectRowsResult zeroRows = tuple_service::selectRows(tableName,
                                                              {QStringLiteral("*")},
                                                              {},
                                                              0);
        QVERIFY2(zeroRows.success, qPrintable(zeroRows.errorMessage));
        QCOMPARE(zeroRows.resultTable.columns, expectedColumns);
        QCOMPARE(zeroRows.resultTable.rows.size(), 0);
    }

    void test_insertRows()
    {
        const QString databaseName = QStringLiteral("test_tuple_service_insert_db");
        const QString parentTableName = QStringLiteral("test_tuple_service_insert_parent");
        const QString childTableName = QStringLiteral("test_tuple_service_insert_child");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, parentTableName, parentSchema(parentTableName), m_dataRoot);
        ensureTable(databaseName, childTableName, childSchema(childTableName, parentTableName), m_dataRoot);

        TaskResult parentInsert = tuple_service::insertRows(parentTableName,
                                                            makeRows({
                                                                makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                                                                         {QStringLiteral("name"), QStringLiteral("alice")}}),
                                                                makeRow({{QStringLiteral("id"), QStringLiteral("2")},
                                                                         {QStringLiteral("name"), QStringLiteral("bob")}}),
                                                            }));
        QVERIFY2(parentInsert.success, qPrintable(parentInsert.errorMessage));

        TaskResult validChildInsert = tuple_service::insertRows(childTableName,
                                                                makeRows({
                                                                    makeRow({{QStringLiteral("id"), QStringLiteral("10")},
                                                                             {QStringLiteral("parent_id"), QStringLiteral("1")},
                                                                             {QStringLiteral("note"), QStringLiteral("ok")}}),
                                                                }));
        QVERIFY2(validChildInsert.success, qPrintable(validChildInsert.errorMessage));

        TaskResult invalidChildInsert = tuple_service::insertRows(childTableName,
                                                                  makeRows({
                                                                      makeRow({{QStringLiteral("id"), QStringLiteral("11")},
                                                                               {QStringLiteral("parent_id"), QStringLiteral("999")},
                                                                               {QStringLiteral("note"), QStringLiteral("broken")}}),
                                                                  }));
        QVERIFY(!invalidChildInsert.success);
        QVERIFY(invalidChildInsert.errorMessage.contains(QStringLiteral("missing parent row")));
    }

    void test_insertRowsSelfReferenceBatch()
    {
        const QString databaseName = QStringLiteral("test_tuple_service_insert_self_ref_db");
        const QString tableName = QStringLiteral("test_tuple_service_insert_self_ref_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, selfReferenceSchema(tableName), m_dataRoot);

        TaskResult insertResult = tuple_service::insertRows(tableName,
                                                            makeRows({
                                                                makeRow({{QStringLiteral("id"), QStringLiteral("2")},
                                                                         {QStringLiteral("parent_id"), QStringLiteral("1")},
                                                                         {QStringLiteral("note"), QStringLiteral("child")}}),
                                                                makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                                                                         {QStringLiteral("note"), QStringLiteral("root")}}),
                                                            }));
        QVERIFY2(insertResult.success, qPrintable(insertResult.errorMessage));

        const repo::TableData table = loadTable(databaseName, tableName, m_dataRoot);
        QCOMPARE(table.rows.size(), 2);
        QCOMPARE(rowValues(table, 0),
                 QStringList({QStringLiteral("2"), QStringLiteral("1"), QStringLiteral("child")}));
        QCOMPARE(rowValues(table, 1),
                 QStringList({QStringLiteral("1"), QString(), QStringLiteral("root")}));
    }

    void test_deleteRows()
    {
        const QString databaseName = QStringLiteral("test_tuple_service_delete_db");
        const QString parentTableName = QStringLiteral("test_tuple_service_delete_parent");
        const QString childTableName = QStringLiteral("test_tuple_service_delete_child");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, parentTableName, parentSchema(parentTableName), m_dataRoot);
        ensureTable(databaseName, childTableName, childSchema(childTableName, parentTableName), m_dataRoot);

        QVERIFY(tuple_service::insertRows(parentTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                                                       {QStringLiteral("name"), QStringLiteral("alice")}}),
                                              makeRow({{QStringLiteral("id"), QStringLiteral("2")},
                                                       {QStringLiteral("name"), QStringLiteral("bob")}}),
                                          })).success);

        QVERIFY(tuple_service::insertRows(childTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("10")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("1")},
                                                       {QStringLiteral("note"), QStringLiteral("child")}}),
                                          })).success);

        TaskResult restrictedDelete = tuple_service::deleteRows(parentTableName,
                                     {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY(!restrictedDelete.success);
        QVERIFY(restrictedDelete.errorMessage.contains(QStringLiteral("would be broken")));

        TaskResult childDelete = tuple_service::deleteRows(childTableName,
                                    {SimpleCondition{QStringLiteral("id"), QStringLiteral("10")}});
        QVERIFY2(childDelete.success, qPrintable(childDelete.errorMessage));

        TaskResult parentDelete = tuple_service::deleteRows(parentTableName,
                                    {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY2(parentDelete.success, qPrintable(parentDelete.errorMessage));
    }

    void test_updateRows()
    {
        const QString databaseName = QStringLiteral("test_tuple_service_update_db");
        const QString parentTableName = QStringLiteral("test_tuple_service_update_parent");
        const QString childTableName = QStringLiteral("test_tuple_service_update_child");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, parentTableName, parentSchema(parentTableName), m_dataRoot);
        ensureTable(databaseName, childTableName, childSchema(childTableName, parentTableName), m_dataRoot);

        QVERIFY(tuple_service::insertRows(parentTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                                                       {QStringLiteral("name"), QStringLiteral("alice")}}),
                                              makeRow({{QStringLiteral("id"), QStringLiteral("2")},
                                                       {QStringLiteral("name"), QStringLiteral("bob")}}),
                                          })).success);

        QVERIFY(tuple_service::insertRows(childTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("10")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("1")},
                                                       {QStringLiteral("note"), QStringLiteral("child")}}),
                                          })).success);

        TaskResult parentNameUpdate = tuple_service::updateRows(parentTableName,
                                    makeAssignment(QStringLiteral("name"), QStringLiteral("alice_updated")),
                                    {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY2(parentNameUpdate.success, qPrintable(parentNameUpdate.errorMessage));

        TaskResult childFkUpdate = tuple_service::updateRows(childTableName,
                                     makeAssignment(QStringLiteral("parent_id"), QStringLiteral("2")),
                                     {SimpleCondition{QStringLiteral("id"), QStringLiteral("10")}});
        QVERIFY2(childFkUpdate.success, qPrintable(childFkUpdate.errorMessage));

        TaskResult restrictedParentKeyUpdate = tuple_service::updateRows(parentTableName,
                                         makeAssignment(QStringLiteral("id"), QStringLiteral("3")),
                                         {SimpleCondition{QStringLiteral("id"), QStringLiteral("2")}});
        QVERIFY(!restrictedParentKeyUpdate.success);
        QVERIFY(restrictedParentKeyUpdate.errorMessage.contains(QStringLiteral("would be broken")));
    }

    void test_deleteRowsCascadeRecursively()
    {
        const QString databaseName = QStringLiteral("test_tuple_service_cascade_delete_db");
        const QString parentTableName = QStringLiteral("test_tuple_service_cascade_delete_parent");
        const QString childTableName = QStringLiteral("test_tuple_service_cascade_delete_child");
        const QString grandChildTableName = QStringLiteral("test_tuple_service_cascade_delete_grandchild");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, parentTableName, parentSchema(parentTableName), m_dataRoot);
        ensureTable(databaseName,
                    childTableName,
                    childSchema(childTableName,
                                parentTableName,
                                true,
                                QString(),
                                tabledef::ForeignKeyAction::Cascade,
                                tabledef::ForeignKeyAction::NoAction),
                    m_dataRoot);
        ensureTable(databaseName,
                    grandChildTableName,
                    childSchema(grandChildTableName,
                                childTableName,
                                true,
                                QString(),
                                tabledef::ForeignKeyAction::Cascade,
                                tabledef::ForeignKeyAction::NoAction),
                    m_dataRoot);

        QVERIFY(tuple_service::insertRows(parentTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                                                       {QStringLiteral("name"), QStringLiteral("alice")}}),
                                              makeRow({{QStringLiteral("id"), QStringLiteral("2")},
                                                       {QStringLiteral("name"), QStringLiteral("bob")}}),
                                          })).success);
        QVERIFY(tuple_service::insertRows(childTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("10")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("1")},
                                                       {QStringLiteral("note"), QStringLiteral("child_a")}}),
                                              makeRow({{QStringLiteral("id"), QStringLiteral("11")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("2")},
                                                       {QStringLiteral("note"), QStringLiteral("child_b")}}),
                                          })).success);
        QVERIFY(tuple_service::insertRows(grandChildTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("100")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("10")},
                                                       {QStringLiteral("note"), QStringLiteral("grand_a")}}),
                                              makeRow({{QStringLiteral("id"), QStringLiteral("101")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("11")},
                                                       {QStringLiteral("note"), QStringLiteral("grand_b")}}),
                                          })).success);

        TaskResult deleteResult = tuple_service::deleteRows(parentTableName,
                                                            {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY2(deleteResult.success, qPrintable(deleteResult.errorMessage));

        const repo::TableData parentTable = loadTable(databaseName, parentTableName, m_dataRoot);
        QCOMPARE(parentTable.rows.size(), 1);
        QCOMPARE(rowValues(parentTable, 0), QStringList({QStringLiteral("2"), QStringLiteral("bob")}));

        const repo::TableData childTable = loadTable(databaseName, childTableName, m_dataRoot);
        QCOMPARE(childTable.rows.size(), 1);
        QCOMPARE(rowValues(childTable, 0),
                 QStringList({QStringLiteral("11"), QStringLiteral("2"), QStringLiteral("child_b")}));

        const repo::TableData grandChildTable = loadTable(databaseName, grandChildTableName, m_dataRoot);
        QCOMPARE(grandChildTable.rows.size(), 1);
        QCOMPARE(rowValues(grandChildTable, 0),
                 QStringList({QStringLiteral("101"), QStringLiteral("11"), QStringLiteral("grand_b")}));
    }

    void test_deleteRowsCascadeMultipleParentRows()
    {
        const QString databaseName = QStringLiteral("test_tuple_service_cascade_multi_delete_db");
        const QString parentTableName = QStringLiteral("test_tuple_service_cascade_multi_delete_parent");
        const QString childTableName = QStringLiteral("test_tuple_service_cascade_multi_delete_child");
        const QString grandChildTableName = QStringLiteral("test_tuple_service_cascade_multi_delete_grandchild");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, parentTableName, parentSchema(parentTableName), m_dataRoot);
        ensureTable(databaseName,
                    childTableName,
                    childSchema(childTableName,
                                parentTableName,
                                true,
                                QString(),
                                tabledef::ForeignKeyAction::Cascade,
                                tabledef::ForeignKeyAction::NoAction),
                    m_dataRoot);
        ensureTable(databaseName,
                    grandChildTableName,
                    childSchema(grandChildTableName,
                                childTableName,
                                true,
                                QString(),
                                tabledef::ForeignKeyAction::Cascade,
                                tabledef::ForeignKeyAction::NoAction),
                    m_dataRoot);

        QVERIFY(tuple_service::insertRows(parentTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                                                       {QStringLiteral("name"), QStringLiteral("alice")}}),
                                              makeRow({{QStringLiteral("id"), QStringLiteral("2")},
                                                       {QStringLiteral("name"), QStringLiteral("bob")}}),
                                          })).success);
        QVERIFY(tuple_service::insertRows(childTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("10")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("1")},
                                                       {QStringLiteral("note"), QStringLiteral("child_a")}}),
                                              makeRow({{QStringLiteral("id"), QStringLiteral("11")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("2")},
                                                       {QStringLiteral("note"), QStringLiteral("child_b")}}),
                                          })).success);
        QVERIFY(tuple_service::insertRows(grandChildTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("100")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("10")},
                                                       {QStringLiteral("note"), QStringLiteral("grand_a")}}),
                                              makeRow({{QStringLiteral("id"), QStringLiteral("101")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("11")},
                                                       {QStringLiteral("note"), QStringLiteral("grand_b")}}),
                                          })).success);

        TaskResult deleteResult = tuple_service::deleteRows(parentTableName, {});
        QVERIFY2(deleteResult.success, qPrintable(deleteResult.errorMessage));
        QCOMPARE(deleteResult.affectedRowCount, 2);

        QCOMPARE(loadTable(databaseName, parentTableName, m_dataRoot).rows.size(), 0);
        QCOMPARE(loadTable(databaseName, childTableName, m_dataRoot).rows.size(), 0);
        QCOMPARE(loadTable(databaseName, grandChildTableName, m_dataRoot).rows.size(), 0);
    }

    void test_deleteRowsCascadeDoesNotTouchUnrelatedRowIdSidecar()
    {
        const QString databaseName = QStringLiteral("test_tuple_service_cascade_unrelated_sidecar_db");
        const QString parentTableName = QStringLiteral("test_tuple_service_cascade_unrelated_sidecar_parent");
        const QString childTableName = QStringLiteral("test_tuple_service_cascade_unrelated_sidecar_child");
        const QString unrelatedTableName = QStringLiteral("test_tuple_service_cascade_unrelated_sidecar_unrelated");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, parentTableName, parentSchema(parentTableName), m_dataRoot);
        ensureTable(databaseName,
                    childTableName,
                    childSchema(childTableName,
                                parentTableName,
                                true,
                                QString(),
                                tabledef::ForeignKeyAction::Cascade,
                                tabledef::ForeignKeyAction::NoAction),
                    m_dataRoot);
        ensureTable(databaseName, unrelatedTableName, parentSchema(unrelatedTableName), m_dataRoot);

        QVERIFY(tuple_service::insertRows(parentTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                                                       {QStringLiteral("name"), QStringLiteral("alice")}}),
                                          })).success);
        QVERIFY(tuple_service::insertRows(childTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("10")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("1")},
                                                       {QStringLiteral("note"), QStringLiteral("child")}}),
                                          })).success);
        QVERIFY(tuple_service::insertRows(unrelatedTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("99")},
                                                       {QStringLiteral("name"), QStringLiteral("sidecar")}}),
                                          })).success);

        repo::FlatFileTableStore store(m_dataRoot);
        const QString unrelatedRowIdPath = store.getRowIdFilePath(databaseName, unrelatedTableName);
        QVERIFY(QFile::exists(unrelatedRowIdPath));
        QVERIFY(QFile::remove(unrelatedRowIdPath));
        QVERIFY(!QFile::exists(unrelatedRowIdPath));

        TaskResult deleteResult = tuple_service::deleteRows(parentTableName,
                                                            {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY2(deleteResult.success, qPrintable(deleteResult.errorMessage));
        QVERIFY(!QFile::exists(unrelatedRowIdPath));

        const repo::TableData unrelatedTable = loadTable(databaseName, unrelatedTableName, m_dataRoot);
        QCOMPARE(unrelatedTable.rows.size(), 1);
        QCOMPARE(rowValues(unrelatedTable, 0),
                 QStringList({QStringLiteral("99"), QStringLiteral("sidecar")}));
    }

    void test_updateRowsCascade()
    {
        const QString databaseName = QStringLiteral("test_tuple_service_cascade_update_db");
        const QString parentTableName = QStringLiteral("test_tuple_service_cascade_update_parent");
        const QString childTableName = QStringLiteral("test_tuple_service_cascade_update_child");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, parentTableName, parentSchema(parentTableName), m_dataRoot);
        ensureTable(databaseName,
                    childTableName,
                    childSchema(childTableName,
                                parentTableName,
                                true,
                                QString(),
                                tabledef::ForeignKeyAction::NoAction,
                                tabledef::ForeignKeyAction::Cascade),
                    m_dataRoot);

        QVERIFY(tuple_service::insertRows(parentTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                                                       {QStringLiteral("name"), QStringLiteral("alice")}}),
                                          })).success);
        QVERIFY(tuple_service::insertRows(childTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("10")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("1")},
                                                       {QStringLiteral("note"), QStringLiteral("child")}}),
                                          })).success);

        TaskResult updateResult = tuple_service::updateRows(parentTableName,
                                                            makeAssignment(QStringLiteral("id"), QStringLiteral("3")),
                                                            {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY2(updateResult.success, qPrintable(updateResult.errorMessage));

        const repo::TableData childTable = loadTable(databaseName, childTableName, m_dataRoot);
        QCOMPARE(childTable.rows.size(), 1);
        QCOMPARE(rowValues(childTable, 0),
                 QStringList({QStringLiteral("10"), QStringLiteral("3"), QStringLiteral("child")}));
    }

    void test_updateRowsCascadeRecursively()
    {
        const QString databaseName = QStringLiteral("test_tuple_service_recursive_update_db");
        const QString parentTableName = QStringLiteral("test_tuple_service_recursive_update_parent");
        const QString childTableName = QStringLiteral("test_tuple_service_recursive_update_child");
        const QString grandChildTableName = QStringLiteral("test_tuple_service_recursive_update_grandchild");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, parentTableName, parentSchema(parentTableName), m_dataRoot);
        ensureTable(databaseName,
                    childTableName,
                    parentIdReferenceSchema(childTableName,
                                            parentTableName,
                                            {QStringLiteral("id")},
                                            true,
                                            tabledef::ForeignKeyAction::NoAction,
                                            tabledef::ForeignKeyAction::Cascade),
                    m_dataRoot);
        ensureTable(databaseName,
                    grandChildTableName,
                    parentIdReferenceSchema(grandChildTableName,
                                            childTableName,
                                            {QStringLiteral("parent_id")},
                                            false,
                                            tabledef::ForeignKeyAction::NoAction,
                                            tabledef::ForeignKeyAction::Cascade),
                    m_dataRoot);

        QVERIFY(tuple_service::insertRows(parentTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                                                       {QStringLiteral("name"), QStringLiteral("alice")}}),
                                          })).success);
        QVERIFY(tuple_service::insertRows(childTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("10")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("1")},
                                                       {QStringLiteral("note"), QStringLiteral("child")}}),
                                          })).success);
        QVERIFY(tuple_service::insertRows(grandChildTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("100")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("1")},
                                                       {QStringLiteral("note"), QStringLiteral("grandchild")}}),
                                          })).success);

        TaskResult updateResult = tuple_service::updateRows(parentTableName,
                                                            makeAssignment(QStringLiteral("id"), QStringLiteral("3")),
                                                            {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY2(updateResult.success, qPrintable(updateResult.errorMessage));

        const repo::TableData parentTable = loadTable(databaseName, parentTableName, m_dataRoot);
        QCOMPARE(rowValues(parentTable, 0),
                 QStringList({QStringLiteral("3"), QStringLiteral("alice")}));

        const repo::TableData childTable = loadTable(databaseName, childTableName, m_dataRoot);
        QCOMPARE(rowValues(childTable, 0),
                 QStringList({QStringLiteral("10"), QStringLiteral("3"), QStringLiteral("child")}));

        const repo::TableData grandChildTable = loadTable(databaseName, grandChildTableName, m_dataRoot);
        QCOMPARE(rowValues(grandChildTable, 0),
                 QStringList({QStringLiteral("100"), QStringLiteral("3"), QStringLiteral("grandchild")}));
    }

    void test_updateRowsGraphPlanMixedBranches()
    {
        const QString databaseName = QStringLiteral("test_tuple_service_graph_mixed_update_db");
        const QString parentTableName = QStringLiteral("test_tuple_service_graph_mixed_update_parent");
        const QString cascadeChildTableName = QStringLiteral("test_tuple_service_graph_mixed_update_cascade_child");
        const QString nullChildTableName = QStringLiteral("test_tuple_service_graph_mixed_update_null_child");
        const QString grandChildTableName = QStringLiteral("test_tuple_service_graph_mixed_update_grandchild");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, parentTableName, parentSchema(parentTableName), m_dataRoot);
        ensureTable(databaseName,
                    cascadeChildTableName,
                    parentIdReferenceSchema(cascadeChildTableName,
                                            parentTableName,
                                            {QStringLiteral("id")},
                                            true,
                                            tabledef::ForeignKeyAction::NoAction,
                                            tabledef::ForeignKeyAction::Cascade),
                    m_dataRoot);
        ensureTable(databaseName,
                    nullChildTableName,
                    childSchema(nullChildTableName,
                                parentTableName,
                                false,
                                QString(),
                                tabledef::ForeignKeyAction::NoAction,
                                tabledef::ForeignKeyAction::SetNull),
                    m_dataRoot);
        ensureTable(databaseName,
                    grandChildTableName,
                    parentIdReferenceSchema(grandChildTableName,
                                            cascadeChildTableName,
                                            {QStringLiteral("parent_id")},
                                            false,
                                            tabledef::ForeignKeyAction::NoAction,
                                            tabledef::ForeignKeyAction::Cascade),
                    m_dataRoot);

        QVERIFY(tuple_service::insertRows(parentTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                                                       {QStringLiteral("name"), QStringLiteral("alice")}}),
                                          })).success);
        QVERIFY(tuple_service::insertRows(cascadeChildTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("10")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("1")},
                                                       {QStringLiteral("note"), QStringLiteral("cascade_child")}}),
                                          })).success);
        QVERIFY(tuple_service::insertRows(nullChildTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("20")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("1")},
                                                       {QStringLiteral("note"), QStringLiteral("null_child")}}),
                                          })).success);
        QVERIFY(tuple_service::insertRows(grandChildTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("100")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("1")},
                                                       {QStringLiteral("note"), QStringLiteral("grandchild")}}),
                                          })).success);

        TaskResult updateResult = tuple_service::updateRows(parentTableName,
                                                            makeAssignment(QStringLiteral("id"), QStringLiteral("3")),
                                                            {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY2(updateResult.success, qPrintable(updateResult.errorMessage));

        const repo::TableData cascadeChildTable = loadTable(databaseName, cascadeChildTableName, m_dataRoot);
        QCOMPARE(rowValues(cascadeChildTable, 0),
                 QStringList({QStringLiteral("10"), QStringLiteral("3"), QStringLiteral("cascade_child")}));

        const repo::TableData nullChildTable = loadTable(databaseName, nullChildTableName, m_dataRoot);
        QCOMPARE(rowValues(nullChildTable, 0),
                 QStringList({QStringLiteral("20"), QString(), QStringLiteral("null_child")}));

        const repo::TableData grandChildTable = loadTable(databaseName, grandChildTableName, m_dataRoot);
        QCOMPARE(rowValues(grandChildTable, 0),
                 QStringList({QStringLiteral("100"), QStringLiteral("3"), QStringLiteral("grandchild")}));
    }

    void test_updateRowsSelfReferenceCascadeTerminates()
    {
        const QString databaseName = QStringLiteral("test_tuple_service_self_reference_update_db");
        const QString tableName = QStringLiteral("test_tuple_service_self_reference_update_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, selfReferenceSchema(tableName), m_dataRoot);

        QVERIFY(tuple_service::insertRows(tableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                                                       {QStringLiteral("note"), QStringLiteral("root")}}),
                                              makeRow({{QStringLiteral("id"), QStringLiteral("2")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("1")},
                                                       {QStringLiteral("note"), QStringLiteral("child")}}),
                                          })).success);

        TaskResult updateResult = tuple_service::updateRows(tableName,
                                                            makeAssignment(QStringLiteral("id"), QStringLiteral("3")),
                                                            {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY2(updateResult.success, qPrintable(updateResult.errorMessage));

        const repo::TableData table = loadTable(databaseName, tableName, m_dataRoot);
        QCOMPARE(table.rows.size(), 2);
        QCOMPARE(rowValues(table, 0),
                 QStringList({QStringLiteral("3"), QString(), QStringLiteral("root")}));
        QCOMPARE(rowValues(table, 1),
                 QStringList({QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("child")}));
    }

    void test_deleteRowsSelfReferenceCascadeTerminates()
    {
        const QString databaseName = QStringLiteral("test_tuple_service_self_reference_delete_db");
        const QString tableName = QStringLiteral("test_tuple_service_self_reference_delete_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, selfReferenceSchema(tableName), m_dataRoot);

        QVERIFY(tuple_service::insertRows(tableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                                                       {QStringLiteral("note"), QStringLiteral("root")}}),
                                              makeRow({{QStringLiteral("id"), QStringLiteral("2")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("1")},
                                                       {QStringLiteral("note"), QStringLiteral("child")}}),
                                          })).success);

        TaskResult deleteResult = tuple_service::deleteRows(tableName,
                                                            {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY2(deleteResult.success, qPrintable(deleteResult.errorMessage));

        const repo::TableData table = loadTable(databaseName, tableName, m_dataRoot);
        QCOMPARE(table.rows.size(), 0);
    }

    void test_deleteRowsSelfReferenceNoActionAllowsDeletingAllRows()
    {
        const QString databaseName = QStringLiteral("test_tuple_service_self_reference_delete_all_db");
        const QString tableName = QStringLiteral("test_tuple_service_self_reference_delete_all_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName,
                    tableName,
                    selfReferenceSchema(tableName,
                                        tabledef::ForeignKeyAction::NoAction,
                                        tabledef::ForeignKeyAction::NoAction),
                    m_dataRoot);

        QVERIFY2(tuple_service::insertRows(tableName,
                                           makeRows({
                                               makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                                                        {QStringLiteral("note"), QStringLiteral("root")}}),
                                               makeRow({{QStringLiteral("id"), QStringLiteral("2")},
                                                        {QStringLiteral("parent_id"), QStringLiteral("1")},
                                                        {QStringLiteral("note"), QStringLiteral("child")}}),
                                           })).success,
                 "self-reference seed insert should succeed");

        TaskResult deleteResult = tuple_service::deleteRows(tableName, {});
        QVERIFY2(deleteResult.success, qPrintable(deleteResult.errorMessage));

        const repo::TableData table = loadTable(databaseName, tableName, m_dataRoot);
        QCOMPARE(table.rows.size(), 0);
    }

    void test_deleteRowsCascadeBlockedByRecursiveNoActionRollsBack()
    {
        const QString databaseName = QStringLiteral("test_tuple_service_recursive_no_action_db");
        const QString parentTableName = QStringLiteral("test_tuple_service_recursive_no_action_parent");
        const QString childTableName = QStringLiteral("test_tuple_service_recursive_no_action_child");
        const QString grandChildTableName = QStringLiteral("test_tuple_service_recursive_no_action_grandchild");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, parentTableName, parentSchema(parentTableName), m_dataRoot);
        ensureTable(databaseName,
                    childTableName,
                    childSchema(childTableName,
                                parentTableName,
                                true,
                                QString(),
                                tabledef::ForeignKeyAction::Cascade,
                                tabledef::ForeignKeyAction::NoAction),
                    m_dataRoot);
        ensureTable(databaseName,
                    grandChildTableName,
                    childSchema(grandChildTableName,
                                childTableName,
                                true,
                                QString(),
                                tabledef::ForeignKeyAction::NoAction,
                                tabledef::ForeignKeyAction::NoAction),
                    m_dataRoot);

        QVERIFY(tuple_service::insertRows(parentTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                                                       {QStringLiteral("name"), QStringLiteral("alice")}}),
                                          })).success);
        QVERIFY(tuple_service::insertRows(childTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("10")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("1")},
                                                       {QStringLiteral("note"), QStringLiteral("child")}}),
                                          })).success);
        QVERIFY(tuple_service::insertRows(grandChildTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("100")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("10")},
                                                       {QStringLiteral("note"), QStringLiteral("grandchild")}}),
                                          })).success);

        TaskResult deleteResult = tuple_service::deleteRows(parentTableName,
                                                            {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY(!deleteResult.success);
        QVERIFY(deleteResult.errorMessage.contains(QStringLiteral("would be broken")));

        const repo::TableData parentTable = loadTable(databaseName, parentTableName, m_dataRoot);
        QCOMPARE(parentTable.rows.size(), 1);
        QCOMPARE(rowValues(parentTable, 0),
                 QStringList({QStringLiteral("1"), QStringLiteral("alice")}));

        const repo::TableData childTable = loadTable(databaseName, childTableName, m_dataRoot);
        QCOMPARE(childTable.rows.size(), 1);
        QCOMPARE(rowValues(childTable, 0),
                 QStringList({QStringLiteral("10"), QStringLiteral("1"), QStringLiteral("child")}));

        const repo::TableData grandChildTable = loadTable(databaseName, grandChildTableName, m_dataRoot);
        QCOMPARE(grandChildTable.rows.size(), 1);
        QCOMPARE(rowValues(grandChildTable, 0),
                 QStringList({QStringLiteral("100"), QStringLiteral("10"), QStringLiteral("grandchild")}));
    }

    void test_deleteRowsRestrict()
    {
        const QString databaseName = QStringLiteral("test_tuple_service_restrict_delete_db");
        const QString parentTableName = QStringLiteral("test_tuple_service_restrict_delete_parent");
        const QString childTableName = QStringLiteral("test_tuple_service_restrict_delete_child");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, parentTableName, parentSchema(parentTableName), m_dataRoot);
        ensureTable(databaseName,
                    childTableName,
                    childSchema(childTableName,
                                parentTableName,
                                true,
                                QString(),
                                tabledef::ForeignKeyAction::Restrict,
                                tabledef::ForeignKeyAction::NoAction),
                    m_dataRoot);

        QVERIFY(tuple_service::insertRows(parentTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                                                       {QStringLiteral("name"), QStringLiteral("alice")}}),
                                          })).success);
        QVERIFY(tuple_service::insertRows(childTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("10")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("1")},
                                                       {QStringLiteral("note"), QStringLiteral("child")}}),
                                          })).success);

        TaskResult deleteResult = tuple_service::deleteRows(parentTableName,
                                                            {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY(!deleteResult.success);
        QVERIFY(deleteResult.errorMessage.contains(QStringLiteral("would be broken")));
    }

    void test_updateRowsRestrict()
    {
        const QString databaseName = QStringLiteral("test_tuple_service_restrict_update_db");
        const QString parentTableName = QStringLiteral("test_tuple_service_restrict_update_parent");
        const QString childTableName = QStringLiteral("test_tuple_service_restrict_update_child");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, parentTableName, parentSchema(parentTableName), m_dataRoot);
        ensureTable(databaseName,
                    childTableName,
                    childSchema(childTableName,
                                parentTableName,
                                true,
                                QString(),
                                tabledef::ForeignKeyAction::NoAction,
                                tabledef::ForeignKeyAction::Restrict),
                    m_dataRoot);

        QVERIFY(tuple_service::insertRows(parentTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                                                       {QStringLiteral("name"), QStringLiteral("alice")}}),
                                          })).success);
        QVERIFY(tuple_service::insertRows(childTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("10")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("1")},
                                                       {QStringLiteral("note"), QStringLiteral("child")}}),
                                          })).success);

        TaskResult updateResult = tuple_service::updateRows(parentTableName,
                                                            makeAssignment(QStringLiteral("id"), QStringLiteral("2")),
                                                            {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY(!updateResult.success);
        QVERIFY(updateResult.errorMessage.contains(QStringLiteral("would be broken")));
    }

    void test_deleteRowsSetNull()
    {
        const QString databaseName = QStringLiteral("test_tuple_service_set_null_db");
        const QString parentTableName = QStringLiteral("test_tuple_service_set_null_parent");
        const QString childTableName = QStringLiteral("test_tuple_service_set_null_child");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, parentTableName, parentSchema(parentTableName), m_dataRoot);
        ensureTable(databaseName,
                    childTableName,
                    childSchema(childTableName,
                                parentTableName,
                                false,
                                QString(),
                                tabledef::ForeignKeyAction::SetNull,
                                tabledef::ForeignKeyAction::NoAction),
                    m_dataRoot);

        QVERIFY(tuple_service::insertRows(parentTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                                                       {QStringLiteral("name"), QStringLiteral("alice")}}),
                                          })).success);
        QVERIFY(tuple_service::insertRows(childTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("10")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("1")},
                                                       {QStringLiteral("note"), QStringLiteral("child")}}),
                                          })).success);

        TaskResult deleteResult = tuple_service::deleteRows(parentTableName,
                                                            {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY2(deleteResult.success, qPrintable(deleteResult.errorMessage));

        const repo::TableData childTable = loadTable(databaseName, childTableName, m_dataRoot);
        QCOMPARE(childTable.rows.size(), 1);
        QCOMPARE(rowValues(childTable, 0),
                 QStringList({QStringLiteral("10"), QString(), QStringLiteral("child")}));
    }

    void test_updateRowsSetNull()
    {
        const QString databaseName = QStringLiteral("test_tuple_service_update_set_null_db");
        const QString parentTableName = QStringLiteral("test_tuple_service_update_set_null_parent");
        const QString childTableName = QStringLiteral("test_tuple_service_update_set_null_child");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, parentTableName, parentSchema(parentTableName), m_dataRoot);
        ensureTable(databaseName,
                    childTableName,
                    childSchema(childTableName,
                                parentTableName,
                                false,
                                QString(),
                                tabledef::ForeignKeyAction::NoAction,
                                tabledef::ForeignKeyAction::SetNull),
                    m_dataRoot);

        QVERIFY(tuple_service::insertRows(parentTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                                                       {QStringLiteral("name"), QStringLiteral("alice")}}),
                                          })).success);
        QVERIFY(tuple_service::insertRows(childTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("10")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("1")},
                                                       {QStringLiteral("note"), QStringLiteral("child")}}),
                                          })).success);

        TaskResult updateResult = tuple_service::updateRows(parentTableName,
                                                            makeAssignment(QStringLiteral("id"), QStringLiteral("2")),
                                                            {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY2(updateResult.success, qPrintable(updateResult.errorMessage));

        const repo::TableData childTable = loadTable(databaseName, childTableName, m_dataRoot);
        QCOMPARE(childTable.rows.size(), 1);
        QCOMPARE(rowValues(childTable, 0),
                 QStringList({QStringLiteral("10"), QString(), QStringLiteral("child")}));
    }

    void test_updateRowsSetDefault()
    {
        const QString databaseName = QStringLiteral("test_tuple_service_set_default_db");
        const QString parentTableName = QStringLiteral("test_tuple_service_set_default_parent");
        const QString childTableName = QStringLiteral("test_tuple_service_set_default_child");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, parentTableName, parentSchema(parentTableName), m_dataRoot);
        ensureTable(databaseName,
                    childTableName,
                    childSchema(childTableName,
                                parentTableName,
                                true,
                                QStringLiteral("0"),
                                tabledef::ForeignKeyAction::NoAction,
                                tabledef::ForeignKeyAction::SetDefault),
                    m_dataRoot);

        QVERIFY(tuple_service::insertRows(parentTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("0")},
                                                       {QStringLiteral("name"), QStringLiteral("root")}}),
                                              makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                                                       {QStringLiteral("name"), QStringLiteral("alice")}}),
                                          })).success);
        QVERIFY(tuple_service::insertRows(childTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("10")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("1")},
                                                       {QStringLiteral("note"), QStringLiteral("child")}}),
                                          })).success);

        TaskResult updateResult = tuple_service::updateRows(parentTableName,
                                                            makeAssignment(QStringLiteral("id"), QStringLiteral("2")),
                                                            {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY2(updateResult.success, qPrintable(updateResult.errorMessage));

        const repo::TableData childTable = loadTable(databaseName, childTableName, m_dataRoot);
        QCOMPARE(childTable.rows.size(), 1);
        QCOMPARE(rowValues(childTable, 0),
                 QStringList({QStringLiteral("10"), QStringLiteral("0"), QStringLiteral("child")}));
    }

    void test_deleteRowsSetDefault()
    {
        const QString databaseName = QStringLiteral("test_tuple_service_delete_set_default_db");
        const QString parentTableName = QStringLiteral("test_tuple_service_delete_set_default_parent");
        const QString childTableName = QStringLiteral("test_tuple_service_delete_set_default_child");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, parentTableName, parentSchema(parentTableName), m_dataRoot);
        ensureTable(databaseName,
                    childTableName,
                    childSchema(childTableName,
                                parentTableName,
                                true,
                                QStringLiteral("0"),
                                tabledef::ForeignKeyAction::SetDefault,
                                tabledef::ForeignKeyAction::NoAction),
                    m_dataRoot);

        QVERIFY(tuple_service::insertRows(parentTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("0")},
                                                       {QStringLiteral("name"), QStringLiteral("root")}}),
                                              makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                                                       {QStringLiteral("name"), QStringLiteral("alice")}}),
                                          })).success);
        QVERIFY(tuple_service::insertRows(childTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("10")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("1")},
                                                       {QStringLiteral("note"), QStringLiteral("child")}}),
                                          })).success);

        TaskResult deleteResult = tuple_service::deleteRows(parentTableName,
                                                            {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY2(deleteResult.success, qPrintable(deleteResult.errorMessage));

        const repo::TableData parentTable = loadTable(databaseName, parentTableName, m_dataRoot);
        QCOMPARE(parentTable.rows.size(), 1);
        QCOMPARE(rowValues(parentTable, 0),
                 QStringList({QStringLiteral("0"), QStringLiteral("root")}));

        const repo::TableData childTable = loadTable(databaseName, childTableName, m_dataRoot);
        QCOMPARE(childTable.rows.size(), 1);
        QCOMPARE(rowValues(childTable, 0),
                 QStringList({QStringLiteral("10"), QStringLiteral("0"), QStringLiteral("child")}));
    }

    void test_deleteRowsSetDefaultRejectsMissingDefaultParentAndRollsBack()
    {
        const QString databaseName = QStringLiteral("test_tuple_service_delete_set_default_missing_parent_db");
        const QString parentTableName = QStringLiteral("test_tuple_service_delete_set_default_missing_parent_parent");
        const QString childTableName = QStringLiteral("test_tuple_service_delete_set_default_missing_parent_child");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, parentTableName, parentSchema(parentTableName), m_dataRoot);
        ensureTable(databaseName,
                    childTableName,
                    childSchema(childTableName,
                                parentTableName,
                                true,
                                QStringLiteral("0"),
                                tabledef::ForeignKeyAction::SetDefault,
                                tabledef::ForeignKeyAction::NoAction),
                    m_dataRoot);

        QVERIFY(tuple_service::insertRows(parentTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                                                       {QStringLiteral("name"), QStringLiteral("alice")}}),
                                          })).success);
        QVERIFY(tuple_service::insertRows(childTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("10")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("1")},
                                                       {QStringLiteral("note"), QStringLiteral("child")}}),
                                          })).success);

        TaskResult deleteResult = tuple_service::deleteRows(parentTableName,
                                                            {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY(!deleteResult.success);
        QVERIFY(deleteResult.errorMessage.contains(QStringLiteral("missing parent row")));

        const repo::TableData parentTable = loadTable(databaseName, parentTableName, m_dataRoot);
        QCOMPARE(parentTable.rows.size(), 1);
        QCOMPARE(rowValues(parentTable, 0),
                 QStringList({QStringLiteral("1"), QStringLiteral("alice")}));

        const repo::TableData childTable = loadTable(databaseName, childTableName, m_dataRoot);
        QCOMPARE(childTable.rows.size(), 1);
        QCOMPARE(rowValues(childTable, 0),
                 QStringList({QStringLiteral("10"), QStringLiteral("1"), QStringLiteral("child")}));
    }

    void test_updateRowsSetDefaultRejectsMissingDefaultParentAndRollsBack()
    {
        const QString databaseName = QStringLiteral("test_tuple_service_set_default_missing_parent_db");
        const QString parentTableName = QStringLiteral("test_tuple_service_set_default_missing_parent_parent");
        const QString childTableName = QStringLiteral("test_tuple_service_set_default_missing_parent_child");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, parentTableName, parentSchema(parentTableName), m_dataRoot);
        ensureTable(databaseName,
                    childTableName,
                    childSchema(childTableName,
                                parentTableName,
                                true,
                                QStringLiteral("0"),
                                tabledef::ForeignKeyAction::NoAction,
                                tabledef::ForeignKeyAction::SetDefault),
                    m_dataRoot);

        QVERIFY(tuple_service::insertRows(parentTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                                                       {QStringLiteral("name"), QStringLiteral("alice")}}),
                                          })).success);
        QVERIFY(tuple_service::insertRows(childTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("10")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("1")},
                                                       {QStringLiteral("note"), QStringLiteral("child")}}),
                                          })).success);

        TaskResult updateResult = tuple_service::updateRows(parentTableName,
                                                            makeAssignment(QStringLiteral("id"), QStringLiteral("2")),
                                                            {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY(!updateResult.success);
        QVERIFY(updateResult.errorMessage.contains(QStringLiteral("missing parent row")));

        const repo::TableData parentTable = loadTable(databaseName, parentTableName, m_dataRoot);
        QCOMPARE(rowValues(parentTable, 0),
                 QStringList({QStringLiteral("1"), QStringLiteral("alice")}));

        const repo::TableData childTable = loadTable(databaseName, childTableName, m_dataRoot);
        QCOMPARE(rowValues(childTable, 0),
                 QStringList({QStringLiteral("10"), QStringLiteral("1"), QStringLiteral("child")}));
    }

    void test_deleteRowsCascadeDiamondTopology()
    {
        const QString databaseName = QStringLiteral("test_tuple_service_cascade_diamond_db");
        const QString parentTableName = QStringLiteral("test_tuple_service_cascade_diamond_parent");
        const QString leftChildTableName = QStringLiteral("test_tuple_service_cascade_diamond_left");
        const QString rightChildTableName = QStringLiteral("test_tuple_service_cascade_diamond_right");
        const QString leafTableName = QStringLiteral("test_tuple_service_cascade_diamond_leaf");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, parentTableName, parentSchema(parentTableName), m_dataRoot);
        ensureTable(databaseName,
                    leftChildTableName,
                    childSchema(leftChildTableName,
                                parentTableName,
                                true,
                                QString(),
                                tabledef::ForeignKeyAction::Cascade,
                                tabledef::ForeignKeyAction::NoAction),
                    m_dataRoot);
        ensureTable(databaseName,
                    rightChildTableName,
                    childSchema(rightChildTableName,
                                parentTableName,
                                true,
                                QString(),
                                tabledef::ForeignKeyAction::Cascade,
                                tabledef::ForeignKeyAction::NoAction),
                    m_dataRoot);
        ensureTable(databaseName,
                    leafTableName,
                    diamondLeafSchema(leafTableName, leftChildTableName, rightChildTableName),
                    m_dataRoot);

        QVERIFY(tuple_service::insertRows(parentTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                                                       {QStringLiteral("name"), QStringLiteral("alice")}}),
                                          })).success);
        QVERIFY(tuple_service::insertRows(leftChildTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("10")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("1")},
                                                       {QStringLiteral("note"), QStringLiteral("left")}}),
                                          })).success);
        QVERIFY(tuple_service::insertRows(rightChildTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("20")},
                                                       {QStringLiteral("parent_id"), QStringLiteral("1")},
                                                       {QStringLiteral("note"), QStringLiteral("right")}}),
                                          })).success);
        QVERIFY(tuple_service::insertRows(leafTableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("100")},
                                                       {QStringLiteral("left_parent_id"), QStringLiteral("10")},
                                                       {QStringLiteral("right_parent_id"), QStringLiteral("20")},
                                                       {QStringLiteral("note"), QStringLiteral("leaf")}}),
                                          })).success);

        TaskResult deleteResult = tuple_service::deleteRows(parentTableName,
                                                            {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY2(deleteResult.success, qPrintable(deleteResult.errorMessage));

        QCOMPARE(loadTable(databaseName, parentTableName, m_dataRoot).rows.size(), 0);
        QCOMPARE(loadTable(databaseName, leftChildTableName, m_dataRoot).rows.size(), 0);
        QCOMPARE(loadTable(databaseName, rightChildTableName, m_dataRoot).rows.size(), 0);
        QCOMPARE(loadTable(databaseName, leafTableName, m_dataRoot).rows.size(), 0);
    }

    void test_uniqueConstraintRejectsDuplicateDml()
    {
        const QString databaseName = QStringLiteral("test_tuple_service_unique_dml_db");
        const QString tableName = QStringLiteral("test_tuple_service_unique_dml_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, indexedSchema(tableName), m_dataRoot);

        QVERIFY(tuple_service::insertRows(tableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                                                       {QStringLiteral("name"), QStringLiteral("alice")}}),
                                              makeRow({{QStringLiteral("id"), QStringLiteral("2")},
                                                       {QStringLiteral("name"), QStringLiteral("bob")}}),
                                          })).success);

        TaskResult duplicateInsert = tuple_service::insertRows(tableName,
                                                               makeRows({
                                                                   makeRow({{QStringLiteral("id"), QStringLiteral("3")},
                                                                            {QStringLiteral("name"), QStringLiteral("alice")}}),
                                                               }));
        QVERIFY(!duplicateInsert.success);
        QVERIFY(duplicateInsert.errorMessage.contains(QStringLiteral("duplicate")));

        TaskResult duplicateUpdate = tuple_service::updateRows(tableName,
                                                               makeAssignment(QStringLiteral("name"), QStringLiteral("alice")),
                                                               {SimpleCondition{QStringLiteral("id"), QStringLiteral("2")}});
        QVERIFY(!duplicateUpdate.success);
        QVERIFY(duplicateUpdate.errorMessage.contains(QStringLiteral("duplicate")));
    }

    void test_uniqueConstraintStillRejectsDuplicatesWithoutIndexMetadata()
    {
        const QString databaseName = QStringLiteral("test_tuple_service_unique_no_index_db");
        const QString tableName = QStringLiteral("test_tuple_service_unique_no_index_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, indexedSchema(tableName), m_dataRoot);

        QVERIFY(tuple_service::insertRows(tableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                                                       {QStringLiteral("name"), QStringLiteral("alice")}}),
                                              makeRow({{QStringLiteral("id"), QStringLiteral("2")},
                                                       {QStringLiteral("name"), QStringLiteral("bob")}}),
                                          })).success);

        QString error;
        const QString uniqueIndexName = findIndexNameByColumns(databaseName,
                                                               tableName,
                                                               m_dataRoot,
                                                               {QStringLiteral("name")},
                                                               &error);
        QVERIFY(error.isEmpty());
        QVERIFY(!uniqueIndexName.isEmpty());

        repo::IndexRepo indexRepo(databaseName, tableName, m_dataRoot);
        const repo::RepositoryResult metadataRemovalResult = indexRepo.deleteIndex(uniqueIndexName);
        QVERIFY2(metadataRemovalResult.ok, qPrintable(metadataRemovalResult.error));

        TaskResult duplicateInsert = tuple_service::insertRows(tableName,
                                                               makeRows({
                                                                   makeRow({{QStringLiteral("id"), QStringLiteral("3")},
                                                                            {QStringLiteral("name"), QStringLiteral("alice")}}),
                                                               }));
        QVERIFY(!duplicateInsert.success);
        QVERIFY(duplicateInsert.errorMessage.contains(QStringLiteral("duplicate")));

        TaskResult duplicateUpdate = tuple_service::updateRows(tableName,
                                                               makeAssignment(QStringLiteral("name"), QStringLiteral("alice")),
                                                               {SimpleCondition{QStringLiteral("id"), QStringLiteral("2")}});
        QVERIFY(!duplicateUpdate.success);
        QVERIFY(duplicateUpdate.errorMessage.contains(QStringLiteral("duplicate")));
    }

    void test_incrementalIndexMaintenance()
    {
        const QString databaseName = QStringLiteral("test_tuple_service_incremental_index_db");
        const QString tableName = QStringLiteral("test_tuple_service_incremental_index_table");
        ensureDatabase(databaseName, m_dataRoot);
        ensureTable(databaseName, tableName, indexedSchema(tableName), m_dataRoot);

        QVERIFY(tuple_service::insertRows(tableName,
                                          makeRows({
                                              makeRow({{QStringLiteral("id"), QStringLiteral("1")},
                                                       {QStringLiteral("name"), QStringLiteral("alice")}}),
                                              makeRow({{QStringLiteral("id"), QStringLiteral("2")},
                                                       {QStringLiteral("name"), QStringLiteral("bob")}}),
                                              makeRow({{QStringLiteral("id"), QStringLiteral("3")},
                                                       {QStringLiteral("name"), QStringLiteral("carol")}}),
                                              makeRow({{QStringLiteral("id"), QStringLiteral("4")},
                                                       {QStringLiteral("name"), QStringLiteral("diana")}}),
                                          })).success);

        QString error;
        const QStringList rowIdsAfterInsert = loadRowIds(databaseName, tableName, m_dataRoot, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(rowIdsAfterInsert.size(), 4);

        const QString indexName = findIndexNameByColumns(databaseName,
                                                         tableName,
                                                         m_dataRoot,
                                                         {QStringLiteral("name")},
                                                         &error);
        QVERIFY(error.isEmpty());
        QVERIFY(!indexName.isEmpty());

        QStringList aliceMatches = searchIndex(databaseName,
                                               tableName,
                                               indexName,
                                               {QStringLiteral("alice")},
                                               m_dataRoot,
                                               &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(aliceMatches.size(), 1);

        TaskResult updateResult = tuple_service::updateRows(tableName,
                                                                         makeAssignment(QStringLiteral("name"), QStringLiteral("diana_updated")),
                                                                         {SimpleCondition{QStringLiteral("id"), QStringLiteral("4")}});
        QVERIFY2(updateResult.success, qPrintable(updateResult.errorMessage));

        aliceMatches = searchIndex(databaseName,
                                   tableName,
                                   indexName,
                                                                     {QStringLiteral("diana")},
                                   m_dataRoot,
                                   &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(aliceMatches.size(), 0);

        const QStringList updatedMatches = searchIndex(databaseName,
                                                       tableName,
                                                       indexName,
                                                                                                             {QStringLiteral("diana_updated")},
                                                       m_dataRoot,
                                                       &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(updatedMatches.size(), 1);

        const QStringList rowIdsAfterUpdate = loadRowIds(databaseName, tableName, m_dataRoot, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(rowIdsAfterUpdate.size(), 4);

        TaskResult deleteResult = tuple_service::deleteRows(tableName,
                                    {SimpleCondition{QStringLiteral("id"), QStringLiteral("2")}});
        QVERIFY2(deleteResult.success, qPrintable(deleteResult.errorMessage));

        const QStringList rowIdsAfterDelete = loadRowIds(databaseName, tableName, m_dataRoot, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(rowIdsAfterDelete.size(), 3);

        const QStringList bobMatches = searchIndex(databaseName,
                                                   tableName,
                                                   indexName,
                                                   {QStringLiteral("bob")},
                                                   m_dataRoot,
                                                   &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(bobMatches.size(), 0);
    }

private:
    QString m_dataRoot;
};

int service_tests::runTupleServiceTests()
{
    TupleServiceTest test;
    return QTest::qExec(&test);
}

#include "test_tuple_service.moc"
