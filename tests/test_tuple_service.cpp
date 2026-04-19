#include "../service/service.h"

#include <QDir>
#include <QtTest>

#include "service_test_entry.h"

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
                                    const QStringList &referencedColumns)
{
    return tabledef::Constraint{name,
                                tabledef::ConstraintType::ForeignKey,
                                columns,
                                referencedTable,
                                referencedColumns,
                                QString()};
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

tabledef::TableSchema childSchema(const QString &tableName, const QString &parentTableName)
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
                       parentTableName,
                       {QStringLiteral("id")}),
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
                                          })).success);

        QString error;
        const QStringList rowIdsAfterInsert = loadRowIds(databaseName, tableName, m_dataRoot, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(rowIdsAfterInsert.size(), 2);

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
                                     makeAssignment(QStringLiteral("name"), QStringLiteral("alice_updated")),
                                     {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY2(updateResult.success, qPrintable(updateResult.errorMessage));

        aliceMatches = searchIndex(databaseName,
                                   tableName,
                                   indexName,
                                   {QStringLiteral("alice")},
                                   m_dataRoot,
                                   &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(aliceMatches.size(), 0);

        const QStringList updatedMatches = searchIndex(databaseName,
                                                       tableName,
                                                       indexName,
                                                       {QStringLiteral("alice_updated")},
                                                       m_dataRoot,
                                                       &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(updatedMatches.size(), 1);

        const QStringList rowIdsAfterUpdate = loadRowIds(databaseName, tableName, m_dataRoot, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(rowIdsAfterUpdate.size(), 2);

        TaskResult deleteResult = tuple_service::deleteRows(tableName,
                                    {SimpleCondition{QStringLiteral("id"), QStringLiteral("2")}});
        QVERIFY2(deleteResult.success, qPrintable(deleteResult.errorMessage));

        const QStringList rowIdsAfterDelete = loadRowIds(databaseName, tableName, m_dataRoot, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(rowIdsAfterDelete.size(), 1);

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