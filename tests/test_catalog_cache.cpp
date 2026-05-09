#include "../service/service.h"
#include "../utils/thread_runtime/catalog_cache.h"

#include <QDir>
#include <QtTest>

#include "test_entry.h"

using namespace service;

namespace {

QString testDataRoot()
{
    return QDir(QDir::tempPath()).absoluteFilePath(QStringLiteral("DBMS_test_catalog_cache"));
}

void removeTestDataRoot(const QString &path)
{
    QDir directory(path);
    if (directory.exists()) {
        directory.removeRecursively();
    }
}

tabledef::TableSchema cacheSchema(const QString &tableName)
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

void prepareDatabase(const QString &databaseName)
{
    TaskResult result = database_service::createDatabase(databaseName);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    result = database_service::useDatabase(databaseName);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
}

} // namespace

class CatalogCacheTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_dataRoot = testDataRoot();
        removeTestDataRoot(m_dataRoot);
        setDataRoot(m_dataRoot);
        currentDatabase.clear();
        thread_runtime::CatalogCache::instance().invalidateAllForDataRoot(m_dataRoot);
    }

    void cleanup()
    {
        thread_runtime::CatalogCache::instance().invalidateAllForDataRoot(m_dataRoot);
        currentDatabase.clear();
        removeTestDataRoot(m_dataRoot);
        setDataRoot(QString());
    }

    void test_tableCatalogMissThenHit()
    {
        const QString databaseName = QStringLiteral("cache_table_hit_db");
        const QString tableName = QStringLiteral("cache_table_hit_table");
        prepareDatabase(databaseName);
        TaskResult result = table_service::createTable(tableName, cacheSchema(tableName));
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        QString error;
        const thread_runtime::TableCatalogSnapshot first =
            thread_runtime::CatalogCache::instance().getTableCatalog(currentDataRoot, databaseName, tableName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(first.schema.columns.size(), 2);

        repo::MetaRepo metaRepo(databaseName, tableName, currentDataRoot);
        const repo::RepositoryResult deleted = metaRepo.deleteColumn(QStringLiteral("name"));
        QVERIFY2(deleted.ok, qPrintable(deleted.error));

        const thread_runtime::TableCatalogSnapshot second =
            thread_runtime::CatalogCache::instance().getTableCatalog(currentDataRoot, databaseName, tableName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(second.schema.columns.size(), 2);
        QVERIFY(tabledef::hasColumn(second.schema, QStringLiteral("name")));
    }

    void test_databaseCatalogMissThenHit()
    {
        const QString databaseName = QStringLiteral("cache_db_hit_db");
        const QString firstTableName = QStringLiteral("cache_db_hit_a");
        const QString secondTableName = QStringLiteral("cache_db_hit_b");
        prepareDatabase(databaseName);
        TaskResult result = table_service::createTable(firstTableName, cacheSchema(firstTableName));
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        QString error;
        const thread_runtime::DatabaseCatalogSnapshot first =
            thread_runtime::CatalogCache::instance().getDatabaseCatalog(currentDataRoot, databaseName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(first.tableNames, QStringList{firstTableName});

        repo::TabRepo tabRepo(databaseName, currentDataRoot);
        const repo::RepositoryResult inserted = tabRepo.createTableEntry(secondTableName);
        QVERIFY2(inserted.ok, qPrintable(inserted.error));

        const thread_runtime::DatabaseCatalogSnapshot second =
            thread_runtime::CatalogCache::instance().getDatabaseCatalog(currentDataRoot, databaseName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(second.tableNames, QStringList{firstTableName});
    }

    void test_getRootCatalogMissThenHit()
    {
        const QString firstDatabaseName = QStringLiteral("cache_root_hit_a");
        const QString secondDatabaseName = QStringLiteral("cache_root_hit_b");
        TaskResult result = database_service::createDatabase(firstDatabaseName);
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        QString error;
        const thread_runtime::RootCatalogSnapshot first =
            thread_runtime::CatalogCache::instance().getRootCatalog(currentDataRoot, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(first.databaseNames, QStringList{firstDatabaseName});

        repo::DatabaseRepo databaseRepo(currentDataRoot);
        const repo::RepositoryResult inserted = databaseRepo.createDatabase(secondDatabaseName);
        QVERIFY2(inserted.ok, qPrintable(inserted.error));

        const thread_runtime::RootCatalogSnapshot second =
            thread_runtime::CatalogCache::instance().getRootCatalog(currentDataRoot, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(second.databaseNames, QStringList{firstDatabaseName});
    }

    void test_rootCatalogInvalidatesAfterDatabaseCreate()
    {
        QString error;
        thread_runtime::RootCatalogSnapshot empty =
            thread_runtime::CatalogCache::instance().getRootCatalog(currentDataRoot, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(empty.databaseNames.isEmpty());

        const QString databaseName = QStringLiteral("cache_root_db");
        TaskResult result = database_service::createDatabase(databaseName);
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        thread_runtime::RootCatalogSnapshot populated =
            thread_runtime::CatalogCache::instance().getRootCatalog(currentDataRoot, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(populated.databaseNames.contains(databaseName));
    }

    void test_tableCatalogInvalidatesAfterAddColumn()
    {
        const QString databaseName = QStringLiteral("cache_table_db");
        const QString tableName = QStringLiteral("cache_table");
        prepareDatabase(databaseName);
        TaskResult result = table_service::createTable(tableName, cacheSchema(tableName));
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        QString error;
        thread_runtime::TableCatalogSnapshot before =
            thread_runtime::CatalogCache::instance().getTableCatalog(currentDataRoot, databaseName, tableName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(before.schema.columns.size(), 2);

        ColumnDefinition definition;
        definition.column = tabledef::Column{QStringLiteral("extra"), tabledef::ColumnType::Varchar, 64, false};
        result = table_service::addColumn(tableName, definition);
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        thread_runtime::TableCatalogSnapshot after =
            thread_runtime::CatalogCache::instance().getTableCatalog(currentDataRoot, databaseName, tableName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(after.schema.columns.size(), 3);
    }

    void test_tableCatalogMissPreloadsSchemaConstraintAndIndexTogether()
    {
        const QString databaseName = QStringLiteral("cache_preload_db");
        const QString tableName = QStringLiteral("cache_preload_table");
        prepareDatabase(databaseName);
        TaskResult result = table_service::createTable(tableName, cacheSchema(tableName));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        result = table_service::createIndex(tableName,
                                            QStringLiteral("idx_cache_preload_name"),
                                            {QStringLiteral("name")},
                                            false);
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        thread_runtime::CatalogCache::instance().invalidateTableCatalog(currentDataRoot, databaseName, tableName);
        QString error;
        const thread_runtime::TableCatalogSnapshot snapshot =
            thread_runtime::CatalogCache::instance().getTableCatalog(currentDataRoot, databaseName, tableName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(snapshot.fullyLoaded);
        QCOMPARE(snapshot.schema.columns.size(), 2);
        QCOMPARE(snapshot.schema.constraints.size(), 1);
        QCOMPARE(snapshot.schema.indexes.size(), 2);

        repo::MetaRepo metaRepo(databaseName, tableName, currentDataRoot);
        const repo::RepositoryResult deleted = metaRepo.deleteColumn(QStringLiteral("name"));
        QVERIFY2(deleted.ok, qPrintable(deleted.error));

        const thread_runtime::TableCatalogSnapshot cached =
            thread_runtime::CatalogCache::instance().getTableCatalog(currentDataRoot, databaseName, tableName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(cached.schema.columns.size(), 2);
        QVERIFY(tabledef::hasColumn(cached.schema, QStringLiteral("name")));
    }

    void test_failedReadDoesNotPopulateCache()
    {
        const QString databaseName = QStringLiteral("cache_failed_read_db");
        const QString tableName = QStringLiteral("cache_missing_table");
        prepareDatabase(databaseName);

        QString error;
        thread_runtime::TableCatalogSnapshot missing =
            thread_runtime::CatalogCache::instance().getTableCatalog(currentDataRoot, databaseName, tableName, &error);
        Q_UNUSED(missing);
        QVERIFY(error.isEmpty());

        TaskResult result = table_service::createTable(tableName, cacheSchema(tableName));
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        error.clear();
        const thread_runtime::TableCatalogSnapshot loaded =
            thread_runtime::CatalogCache::instance().getTableCatalog(currentDataRoot, databaseName, tableName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(loaded.fullyLoaded);
        QCOMPARE(loaded.schema.columns.size(), 2);
    }

    void test_invalidateTableCatalogOnlyRemovesOneTable()
    {
        const QString databaseName = QStringLiteral("cache_inv_table_db");
        const QString firstTableName = QStringLiteral("cache_inv_table_a");
        const QString secondTableName = QStringLiteral("cache_inv_table_b");
        prepareDatabase(databaseName);
        QVERIFY2(table_service::createTable(firstTableName, cacheSchema(firstTableName)).success, "create first table");
        QVERIFY2(table_service::createTable(secondTableName, cacheSchema(secondTableName)).success, "create second table");

        QString error;
        QVERIFY(thread_runtime::CatalogCache::instance().getTableCatalog(currentDataRoot, databaseName, firstTableName, &error).fullyLoaded);
        QVERIFY(thread_runtime::CatalogCache::instance().getTableCatalog(currentDataRoot, databaseName, secondTableName, &error).fullyLoaded);

        repo::MetaRepo firstMetaRepo(databaseName, firstTableName, currentDataRoot);
        repo::MetaRepo secondMetaRepo(databaseName, secondTableName, currentDataRoot);
        QVERIFY2(firstMetaRepo.deleteColumn(QStringLiteral("name")).ok, "delete first name column");
        QVERIFY2(secondMetaRepo.deleteColumn(QStringLiteral("name")).ok, "delete second name column");

        thread_runtime::CatalogCache::instance().invalidateTableCatalog(currentDataRoot, databaseName, firstTableName);
        const thread_runtime::TableCatalogSnapshot first =
            thread_runtime::CatalogCache::instance().getTableCatalog(currentDataRoot, databaseName, firstTableName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(first.schema.columns.size(), 1);

        const thread_runtime::TableCatalogSnapshot second =
            thread_runtime::CatalogCache::instance().getTableCatalog(currentDataRoot, databaseName, secondTableName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(second.schema.columns.size(), 2);
    }

    void test_invalidateDatabaseCatalogRemovesTableListOnly()
    {
        const QString databaseName = QStringLiteral("cache_inv_db");
        const QString tableName = QStringLiteral("cache_inv_db_table");
        const QString addedTableName = QStringLiteral("cache_inv_db_added");
        prepareDatabase(databaseName);
        QVERIFY2(table_service::createTable(tableName, cacheSchema(tableName)).success, "create table");

        QString error;
        QVERIFY(thread_runtime::CatalogCache::instance().getTableCatalog(currentDataRoot, databaseName, tableName, &error).fullyLoaded);
        const thread_runtime::DatabaseCatalogSnapshot before =
            thread_runtime::CatalogCache::instance().getDatabaseCatalog(currentDataRoot, databaseName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(before.tableNames, QStringList{tableName});

        repo::TabRepo tabRepo(databaseName, currentDataRoot);
        QVERIFY2(tabRepo.createTableEntry(addedTableName).ok, "add table directly");
        repo::MetaRepo metaRepo(databaseName, tableName, currentDataRoot);
        QVERIFY2(metaRepo.deleteColumn(QStringLiteral("name")).ok, "delete column directly");

        thread_runtime::CatalogCache::instance().invalidateDatabaseCatalog(currentDataRoot, databaseName);
        const thread_runtime::DatabaseCatalogSnapshot after =
            thread_runtime::CatalogCache::instance().getDatabaseCatalog(currentDataRoot, databaseName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(after.tableNames.contains(addedTableName));

        const thread_runtime::TableCatalogSnapshot tableSnapshot =
            thread_runtime::CatalogCache::instance().getTableCatalog(currentDataRoot, databaseName, tableName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(tableSnapshot.schema.columns.size(), 2);
    }

    void test_invalidateAllForDataRootClearsCurrentRoot()
    {
        const QString databaseName = QStringLiteral("cache_inv_all_db");
        TaskResult result = database_service::createDatabase(databaseName);
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        QString error;
        QCOMPARE(thread_runtime::CatalogCache::instance().getRootCatalog(currentDataRoot, &error).databaseNames,
                 QStringList{databaseName});
        repo::DatabaseRepo databaseRepo(currentDataRoot);
        QVERIFY2(databaseRepo.createDatabase(QStringLiteral("cache_inv_all_added")).ok, "direct add database");

        thread_runtime::CatalogCache::instance().invalidateAllForDataRoot(currentDataRoot);
        const thread_runtime::RootCatalogSnapshot root =
            thread_runtime::CatalogCache::instance().getRootCatalog(currentDataRoot, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(root.databaseNames.contains(QStringLiteral("cache_inv_all_added")));
    }

    void test_differentDataRootsDoNotShareCache()
    {
        const QString firstRoot = currentDataRoot;
        const QString secondRoot = QDir(QDir::tempPath()).absoluteFilePath(QStringLiteral("DBMS_test_catalog_cache_second_root"));
        removeTestDataRoot(secondRoot);

        setDataRoot(firstRoot);
        QVERIFY2(database_service::createDatabase(QStringLiteral("cache_root_one")).success, "create first root db");
        QString error;
        const thread_runtime::RootCatalogSnapshot first =
            thread_runtime::CatalogCache::instance().getRootCatalog(firstRoot, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(first.databaseNames.contains(QStringLiteral("cache_root_one")));

        setDataRoot(secondRoot);
        QVERIFY2(database_service::createDatabase(QStringLiteral("cache_root_two")).success, "create second root db");
        const thread_runtime::RootCatalogSnapshot second =
            thread_runtime::CatalogCache::instance().getRootCatalog(secondRoot, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(second.databaseNames.contains(QStringLiteral("cache_root_two")));
        QVERIFY(!second.databaseNames.contains(QStringLiteral("cache_root_one")));

        removeTestDataRoot(secondRoot);
        setDataRoot(firstRoot);
    }

private:
    QString m_dataRoot;
};

int service_tests::runCatalogCacheTests()
{
    CatalogCacheTest test;
    return QTest::qExec(&test);
}

#include "test_catalog_cache.moc"
