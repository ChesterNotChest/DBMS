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

private:
    QString m_dataRoot;
};

int service_tests::runCatalogCacheTests()
{
    CatalogCacheTest test;
    return QTest::qExec(&test);
}

#include "test_catalog_cache.moc"
