#include "../client/client_session_pool.h"
#include "../client/sql_client_engine.h"
#include "../service/service.h"

#include <QDir>
#include <QtTest>

#include "test_entry.h"

namespace {

QString testDataRoot()
{
    return QDir(QDir::tempPath()).absoluteFilePath(QStringLiteral("DBMS_test_client_session"));
}

void removeTestDataRoot(const QString &path)
{
    QDir directory(path);
    if (directory.exists()) {
        directory.removeRecursively();
    }
}

} // namespace

class ClientSessionTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_dataRoot = testDataRoot();
        removeTestDataRoot(m_dataRoot);
        service::setDataRoot(m_dataRoot);
        service::currentDatabase.clear();
    }

    void cleanup()
    {
        service::currentDatabase.clear();
        service::setDataRoot(QString());
        removeTestDataRoot(m_dataRoot);
    }

    void test_createSessionHasIndependentCurrentDatabase()
    {
        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);

        const QString firstClient = pool.createSession(m_dataRoot);
        const QString secondClient = pool.createSession(m_dataRoot);

        service::SqlExecResult result = engine.executeSql(firstClient, QStringLiteral("CREATE DATABASE db_a;"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        result = engine.executeSql(secondClient, QStringLiteral("CREATE DATABASE db_b;"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        result = engine.executeSql(firstClient, QStringLiteral("USE db_a;"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        result = engine.executeSql(secondClient, QStringLiteral("USE db_b;"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        QString error;
        const client::ClientSession *firstSession = pool.session(firstClient, &error);
        QVERIFY2(firstSession != nullptr, qPrintable(error));
        const client::ClientSession *secondSession = pool.session(secondClient, &error);
        QVERIFY2(secondSession != nullptr, qPrintable(error));

        QCOMPARE(firstSession->currentDatabase, QStringLiteral("db_a"));
        QCOMPARE(secondSession->currentDatabase, QStringLiteral("db_b"));
    }

    void test_executeSqlRestoresPreviousServiceContext()
    {
        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);

        const QString clientId = pool.createSession(m_dataRoot);
        service::setDataRoot(QStringLiteral("previous_root"));
        service::currentDatabase = QStringLiteral("previous_db");

        const service::SqlExecResult result = engine.executeSql(clientId, QStringLiteral("CREATE DATABASE isolated_db;"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        QCOMPARE(service::getDataRoot(), QDir::cleanPath(QStringLiteral("previous_root")));
        QCOMPARE(service::currentDatabase, QStringLiteral("previous_db"));
    }

    void test_closeSessionRemovesClient()
    {
        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);

        const QString clientId = pool.createSession(m_dataRoot);
        QVERIFY(pool.closeSession(clientId));

        const service::SqlExecResult result = engine.executeSql(clientId, QStringLiteral("SHOW DATABASES;"));
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("does not exist")));
    }

private:
    QString m_dataRoot;
};

int service_tests::runClientSessionTests()
{
    ClientSessionTest test;
    return QTest::qExec(&test);
}

#include "test_client_session.moc"
