#include "../service/service.h"

#include <QDir>
#include <QStringList>
#include <QtTest>

#include "service_test_entry.h"

using namespace service;

namespace {

QString testDataRoot()
{
    return QDir(QDir::tempPath()).absoluteFilePath(QStringLiteral("DBMS_test_database_service"));
}

void removeTestDataRoot(const QString &path)
{
    QDir directory(path);
    if (directory.exists()) {
        directory.removeRecursively();
    }
}

QStringList databaseNames(const SelectRowsResult &result)
{
    QStringList names;
    for (const repo::TableRow &row : result.resultTable.rows) {
        if (!row.isEmpty()) {
            names.append(row.first());
        }
    }
    return names;
}

} // namespace

class DatabaseServiceTest : public QObject
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

    void test_createDatabase()
    {
        const QString emptyDatabaseName = QStringLiteral("   ");
        TaskResult emptyResult = database_service::createDatabase(emptyDatabaseName);
        QVERIFY(!emptyResult.success);
        QVERIFY(emptyResult.errorMessage.contains(QStringLiteral("cannot be empty")));

        const QString firstDatabaseName = QStringLiteral("test_database_service_create_alpha");
        const QString secondDatabaseName = QStringLiteral("test_database_service_create_beta");

        TaskResult firstResult = database_service::createDatabase(firstDatabaseName);
        QVERIFY2(firstResult.success, qPrintable(firstResult.errorMessage));

        TaskResult secondResult = database_service::createDatabase(secondDatabaseName);
        QVERIFY2(secondResult.success, qPrintable(secondResult.errorMessage));

        TaskResult duplicateResult = database_service::createDatabase(firstDatabaseName);
        QVERIFY(!duplicateResult.success);
        QVERIFY(duplicateResult.errorMessage.contains(QStringLiteral("already exists")));

        SelectRowsResult listed = database_service::showDatabases();
        QVERIFY2(listed.success, qPrintable(listed.errorMessage));
        const QStringList expectedDatabases{firstDatabaseName, secondDatabaseName};
        QCOMPARE(databaseNames(listed), expectedDatabases);
    }

    void test_dropDatabase()
    {
        const QString firstDatabaseName = QStringLiteral("test_database_service_drop_alpha");
        const QString secondDatabaseName = QStringLiteral("test_database_service_drop_beta");

        QVERIFY(database_service::createDatabase(firstDatabaseName).success);
        QVERIFY(database_service::createDatabase(secondDatabaseName).success);
        QVERIFY(database_service::useDatabase(firstDatabaseName).success);
        QCOMPARE(currentDatabase, firstDatabaseName);

        TaskResult missingResult = database_service::dropDatabase(QStringLiteral("test_database_service_drop_missing"));
        QVERIFY(!missingResult.success);
        QVERIFY(missingResult.errorMessage.contains(QStringLiteral("does not exist")));

        TaskResult dropResult = database_service::dropDatabase(firstDatabaseName);
        QVERIFY2(dropResult.success, qPrintable(dropResult.errorMessage));
        QVERIFY(currentDatabase.isEmpty());

        SelectRowsResult listed = database_service::showDatabases();
        QVERIFY2(listed.success, qPrintable(listed.errorMessage));
        const QStringList expectedDatabases{secondDatabaseName};
        QCOMPARE(databaseNames(listed), expectedDatabases);

        TaskResult secondDropResult = database_service::dropDatabase(firstDatabaseName);
        QVERIFY(!secondDropResult.success);
        QVERIFY(secondDropResult.errorMessage.contains(QStringLiteral("does not exist")));
    }

    void test_useDatabase()
    {
        const QString firstDatabaseName = QStringLiteral("test_database_service_use_alpha");
        const QString secondDatabaseName = QStringLiteral("test_database_service_use_beta");

        QVERIFY(database_service::createDatabase(firstDatabaseName).success);
        QVERIFY(database_service::createDatabase(secondDatabaseName).success);

        TaskResult missingResult = database_service::useDatabase(QStringLiteral("test_database_service_use_missing"));
        QVERIFY(!missingResult.success);
        QVERIFY(missingResult.errorMessage.contains(QStringLiteral("does not exist")));

        TaskResult firstUseResult = database_service::useDatabase(QStringLiteral("  test_database_service_use_alpha  "));
        QVERIFY2(firstUseResult.success, qPrintable(firstUseResult.errorMessage));
        QCOMPARE(currentDatabase, firstDatabaseName);

        TaskResult secondUseResult = database_service::useDatabase(secondDatabaseName);
        QVERIFY2(secondUseResult.success, qPrintable(secondUseResult.errorMessage));
        QCOMPARE(currentDatabase, secondDatabaseName);
    }

    void test_showDatabases()
    {
        SelectRowsResult emptyResult = database_service::showDatabases();
        QVERIFY2(emptyResult.success, qPrintable(emptyResult.errorMessage));
        QCOMPARE(emptyResult.resultTable.rows.size(), 0);

        const QString firstDatabaseName = QStringLiteral("test_database_service_show_alpha");
        const QString secondDatabaseName = QStringLiteral("test_database_service_show_beta");

        QVERIFY(database_service::createDatabase(firstDatabaseName).success);
        QVERIFY(database_service::createDatabase(secondDatabaseName).success);

        SelectRowsResult populatedResult = database_service::showDatabases();
        QVERIFY2(populatedResult.success, qPrintable(populatedResult.errorMessage));
        const QStringList expectedDatabases{firstDatabaseName, secondDatabaseName};
        QCOMPARE(databaseNames(populatedResult), expectedDatabases);
    }

private:
    QString m_dataRoot;
};

int service_tests::runDatabaseServiceTests()
{
    DatabaseServiceTest test;
    return QTest::qExec(&test);
}

#include "test_database_service.moc"