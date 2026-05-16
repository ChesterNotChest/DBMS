#include "../service/service.h"

#include <QDir>
#include <QFile>
#include <QtTest>

#include "test_entry.h"

using namespace service;

namespace {

QString testDataRoot()
{
    return QDir(QDir::tempPath()).absoluteFilePath(QStringLiteral("DBMS_test_service_common_cache"));
}

void removeTestDataRoot(const QString &path)
{
    QDir directory(path);
    if (directory.exists()) {
        directory.removeRecursively();
    }
}

tabledef::TableSchema rowIdSchema(const QString &tableName)
{
    tabledef::TableSchema schema;
    schema.tableName = tableName;
    schema.columns = {
        tabledef::Column{QStringLiteral("id"), tabledef::ColumnType::Int, 0, true},
        tabledef::Column{QStringLiteral("name"), tabledef::ColumnType::Varchar, 64, false},
    };
    schema.constraints = {
        tabledef::Constraint{QStringLiteral("pk_%1").arg(tableName),
                             tabledef::ConstraintType::PrimaryKey,
                             {QStringLiteral("id")},
                             QString(),
                             {},
                             QString()},
    };
    return schema;
}

void prepareTable(const QString &databaseName, const QString &tableName)
{
    TaskResult result = database_service::createDatabase(databaseName);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    result = database_service::useDatabase(databaseName);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    result = table_service::createTable(tableName, rowIdSchema(tableName));
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    result = tuple_service::insertRows(tableName,
                                       {QMap<QString, QString>{{QStringLiteral("id"), QStringLiteral("1")},
                                                               {QStringLiteral("name"), QStringLiteral("alpha")}}});
    QVERIFY2(result.success, qPrintable(result.errorMessage));
}

} // namespace

class ServiceCommonCacheTest : public QObject
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

    void test_readPathDoesNotPersistMissingRowIdSidecar()
    {
        const QString databaseName = QStringLiteral("rowid_read_db");
        const QString tableName = QStringLiteral("rowid_read_table");
        prepareTable(databaseName, tableName);

        repo::FlatFileTableStore store(currentDataRoot);
        const QString rowIdPath = store.getRowIdFilePath(databaseName, tableName);
        QVERIFY(QFile::remove(rowIdPath));
        QVERIFY(!QFile::exists(rowIdPath));

        SelectRowsResult selected = tuple_service::selectRows(tableName,
                                                              {QStringLiteral("*")},
                                                              {},
                                                              -1);
        QVERIFY2(selected.success, qPrintable(selected.errorMessage));
        QVERIFY(!QFile::exists(rowIdPath));
    }

    void test_writePathRepairsMissingRowIdSidecar()
    {
        const QString databaseName = QStringLiteral("rowid_write_db");
        const QString tableName = QStringLiteral("rowid_write_table");
        prepareTable(databaseName, tableName);

        repo::FlatFileTableStore store(currentDataRoot);
        const QString rowIdPath = store.getRowIdFilePath(databaseName, tableName);
        QVERIFY(QFile::remove(rowIdPath));
        QVERIFY(!QFile::exists(rowIdPath));

        TaskResult updated = tuple_service::updateRows(tableName,
                                                       {{QStringLiteral("name"), QStringLiteral("beta")}},
                                                       {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY2(updated.success, qPrintable(updated.errorMessage));
        QVERIFY(QFile::exists(rowIdPath));
    }

    void test_loadUserTableSchemaUsesCatalogCache()
    {
        const QString databaseName = QStringLiteral("schema_cache_db");
        const QString tableName = QStringLiteral("schema_cache_table");
        prepareTable(databaseName, tableName);

        QString error;
        const tabledef::TableSchema first = loadUserTableSchema(tableName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(first.columns.size(), 2);

        repo::MetaRepo metaRepo(databaseName, tableName, currentDataRoot);
        QVERIFY2(metaRepo.deleteColumn(QStringLiteral("name")).ok, "delete column directly");

        const tabledef::TableSchema second = loadUserTableSchema(tableName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(second.columns.size(), 2);
        QVERIFY(tabledef::hasColumn(second, QStringLiteral("name")));
    }

    void test_loadUserTableIndexesUsesCatalogCache()
    {
        const QString databaseName = QStringLiteral("index_cache_db");
        const QString tableName = QStringLiteral("index_cache_table");
        prepareTable(databaseName, tableName);

        QString error;
        const QList<tabledef::IndexMeta> first = loadUserTableIndexes(tableName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!first.isEmpty());

        repo::IndexRepo indexRepo(databaseName, tableName, currentDataRoot);
        QVERIFY2(indexRepo.deleteIndex(first.first().indexName).ok, "remove index directly");

        const QList<tabledef::IndexMeta> second = loadUserTableIndexes(tableName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(second.size(), first.size());
    }

    void test_loadUserTableConstraintsUsesCatalogCache()
    {
        const QString databaseName = QStringLiteral("constraint_cache_db");
        const QString tableName = QStringLiteral("constraint_cache_table");
        prepareTable(databaseName, tableName);

        QString error;
        const QList<tabledef::Constraint> first = loadUserTableConstraints(tableName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!first.isEmpty());

        repo::ConstraintRepo constraintRepo(databaseName, tableName, currentDataRoot);
        QVERIFY2(constraintRepo.deleteConstraint(first.first().name).ok, "remove constraint directly");

        const QList<tabledef::Constraint> second = loadUserTableConstraints(tableName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(second.size(), first.size());
    }

    void test_showTablesUsesDatabaseCatalog()
    {
        const QString databaseName = QStringLiteral("show_tables_cache_db");
        const QString tableName = QStringLiteral("show_tables_cache_table");
        prepareTable(databaseName, tableName);

        SelectRowsResult first = table_service::showTables();
        QVERIFY2(first.success, qPrintable(first.errorMessage));
        QCOMPARE(first.resultTable.rows.size(), 1);

        repo::TabRepo tabRepo(databaseName, currentDataRoot);
        QVERIFY2(tabRepo.createTableEntry(QStringLiteral("show_tables_direct_added")).ok, "add table directly");

        SelectRowsResult second = table_service::showTables();
        QVERIFY2(second.success, qPrintable(second.errorMessage));
        QCOMPARE(second.resultTable.rows.size(), 1);
    }

    void test_showDatabasesUsesRootCatalog()
    {
        const QString databaseName = QStringLiteral("show_databases_cache_db");
        prepareTable(databaseName, QStringLiteral("show_databases_cache_table"));

        SelectRowsResult first = database_service::showDatabases();
        QVERIFY2(first.success, qPrintable(first.errorMessage));
        QCOMPARE(first.resultTable.rows.size(), 1);

        repo::DatabaseRepo databaseRepo(currentDataRoot);
        QVERIFY2(databaseRepo.createDatabase(QStringLiteral("show_databases_direct_added")).ok, "add database directly");

        SelectRowsResult second = database_service::showDatabases();
        QVERIFY2(second.success, qPrintable(second.errorMessage));
        QCOMPARE(second.resultTable.rows.size(), 1);
    }

private:
    QString m_dataRoot;
};

int service_tests::runServiceCommonCacheTests()
{
    ServiceCommonCacheTest test;
    return QTest::qExec(&test);
}

#include "test_service_common_cache.moc"
