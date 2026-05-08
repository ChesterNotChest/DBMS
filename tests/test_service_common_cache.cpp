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

private:
    QString m_dataRoot;
};

int service_tests::runServiceCommonCacheTests()
{
    ServiceCommonCacheTest test;
    return QTest::qExec(&test);
}

#include "test_service_common_cache.moc"
