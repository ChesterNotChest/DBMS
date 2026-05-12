#include "../client/client_session_pool.h"
#include "../client/sql_client_engine.h"
#include "../constants/cli_client_def.h"
#include "../service/auth_service.h"
#include "../service/service.h"
#include "../utils/sql_parser/sql_parser.h"

#include <QDir>
#include <QtTest>

#include "test_entry.h"

namespace {

QString testDataRoot()
{
    return QDir(QDir::tempPath()).absoluteFilePath(QStringLiteral("DBMS_test_auth_client"));
}

void removeTestDataRoot(const QString &path)
{
    QDir directory(path);
    if (directory.exists()) {
        directory.removeRecursively();
    }
}

} // namespace

class AuthClientTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_dataRoot = testDataRoot();
        removeTestDataRoot(m_dataRoot);
        service::setDataRoot(m_dataRoot);
        service::currentDatabase.clear();
        service::currentUser.clear();
    }

    void cleanup()
    {
        service::currentDatabase.clear();
        service::currentUser.clear();
        service::setDataRoot(QString());
        removeTestDataRoot(m_dataRoot);
    }

    void test_parseAuthSql()
    {
        sqlparser::ParseResult result =
            sqlparser::parseSql(QStringLiteral("LOGIN root IDENTIFIED BY ''"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.commandType, QStringLiteral("LOGIN"));
        QCOMPARE(result.payload.value(QStringLiteral("userName")).toString(), QStringLiteral("root"));
        QCOMPARE(result.payload.value(QStringLiteral("password")).toString(), QString());

        result = sqlparser::parseSql(QStringLiteral("GRANT ALL ON app_db.* TO alice"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.commandType, QStringLiteral("GRANT_ALL"));
        QCOMPARE(result.payload.value(QStringLiteral("databaseName")).toString(), QStringLiteral("app_db"));
        QCOMPARE(result.payload.value(QStringLiteral("userName")).toString(), QStringLiteral("alice"));
    }

    void test_unauthenticatedSqlIsRejected()
    {
        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        const QString clientId = pool.createSession(m_dataRoot);

        const service::SqlExecResult result =
            engine.executeSql(clientId, QStringLiteral("CREATE DATABASE blocked_db;"));

        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("authentication required")));
    }

    void test_rootCanCreateUserAndGrantDatabasePrivilege()
    {
        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        const QString rootClient = pool.createSession(m_dataRoot);
        const QString aliceClient = pool.createSession(m_dataRoot);

        service::SqlExecResult result =
            engine.login(rootClient, QStringLiteral("root"), QString());
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        result = engine.executeSql(rootClient,
                              QStringLiteral("CREATE DATABASE app_db;"
                                             "CREATE USER alice IDENTIFIED BY secret;"
                                             "GRANT ALL ON app_db.* TO alice;"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        result = engine.login(aliceClient, QStringLiteral("alice"), QStringLiteral("secret"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        result = engine.executeSql(aliceClient,
                                   QStringLiteral("USE app_db;"
                                                  "SHOW TABLES;"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(pool.session(aliceClient)->currentDatabase, QStringLiteral("app_db"));
    }

    void test_revokeRemovesDatabasePrivilege()
    {
        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        const QString rootClient = pool.createSession(m_dataRoot);
        const QString aliceClient = pool.createSession(m_dataRoot);

        service::SqlExecResult result = engine.login(rootClient, QStringLiteral("root"), QString());
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        result = engine.executeSql(rootClient,
                                   QStringLiteral("CREATE DATABASE revoked_db;"
                                                  "CREATE USER alice IDENTIFIED BY secret;"
                                                  "GRANT ALL ON revoked_db.* TO alice;"
                                                  "REVOKE ALL ON revoked_db.* FROM alice;"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        result = engine.login(aliceClient, QStringLiteral("alice"), QStringLiteral("secret"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        result = engine.executeSql(aliceClient, QStringLiteral("USE revoked_db;"));
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("permission denied")));
    }

    void test_authDatabaseIsProtectedFromNonRootAccess()
    {
        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        const QString rootClient = pool.createSession(m_dataRoot);
        const QString aliceClient = pool.createSession(m_dataRoot);
        const QString authDatabase = QString::fromLatin1(cliclient::kAuthDatabaseName);

        service::SqlExecResult result = engine.login(rootClient, QStringLiteral("root"), QString());
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        result = engine.executeSql(rootClient,
                                   QStringLiteral("CREATE USER alice IDENTIFIED BY secret;"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        result = engine.executeSql(rootClient,
                                   QStringLiteral("GRANT ALL ON %1.* TO alice;").arg(authDatabase));
        QVERIFY(!result.success);
        QVERIFY2(result.errorMessage.contains(QStringLiteral("system database")),
                 qPrintable(result.errorMessage));

        result = engine.login(aliceClient, QStringLiteral("alice"), QStringLiteral("secret"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        result = engine.executeSql(aliceClient, QStringLiteral("USE %1;").arg(authDatabase));
        QVERIFY(!result.success);
        QVERIFY2(result.errorMessage.contains(QStringLiteral("system database")),
                 qPrintable(result.errorMessage));
    }

    void test_nonRootShowDatabasesHidesAuthDatabase()
    {
        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        const QString rootClient = pool.createSession(m_dataRoot);
        const QString aliceClient = pool.createSession(m_dataRoot);
        const QString authDatabase = QString::fromLatin1(cliclient::kAuthDatabaseName);

        service::SqlExecResult result = engine.login(rootClient, QStringLiteral("root"), QString());
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        result = engine.executeSql(rootClient,
                                   QStringLiteral("CREATE DATABASE app_db;"
                                                  "CREATE USER alice IDENTIFIED BY secret;"
                                                  "GRANT ALL ON app_db.* TO alice;"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        result = engine.login(aliceClient, QStringLiteral("alice"), QStringLiteral("secret"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        result = engine.executeSql(aliceClient, QStringLiteral("SHOW DATABASES;"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QVERIFY2(!result.text.contains(authDatabase), qPrintable(result.text));
        for (const QStringList &row : result.selectResult.resultTable.rows) {
            QVERIFY(!row.contains(authDatabase));
        }
    }

    void test_grantAndRevokeReportActualAffectedRows()
    {
        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        const QString rootClient = pool.createSession(m_dataRoot);

        service::SqlExecResult result = engine.login(rootClient, QStringLiteral("root"), QString());
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        result = engine.executeSql(rootClient,
                                   QStringLiteral("CREATE DATABASE app_db;"
                                                  "CREATE USER alice IDENTIFIED BY secret;"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        result = engine.executeSql(rootClient, QStringLiteral("GRANT ALL ON app_db.* TO alice;"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.affectedRows, 1);

        result = engine.executeSql(rootClient, QStringLiteral("GRANT ALL ON app_db.* TO alice;"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.affectedRows, 0);

        result = engine.executeSql(rootClient, QStringLiteral("REVOKE ALL ON app_db.* FROM alice;"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.affectedRows, 1);

        result = engine.executeSql(rootClient, QStringLiteral("REVOKE ALL ON app_db.* FROM alice;"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.affectedRows, 0);
    }

    void test_dropUserRemovesLogin()
    {
        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        const QString rootClient = pool.createSession(m_dataRoot);
        const QString aliceClient = pool.createSession(m_dataRoot);

        service::SqlExecResult result = engine.login(rootClient, QStringLiteral("root"), QString());
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        result = engine.executeSql(rootClient,
                                   QStringLiteral("CREATE USER alice IDENTIFIED BY secret;"
                                                  "DROP USER alice;"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        result = engine.login(aliceClient, QStringLiteral("alice"), QStringLiteral("secret"));
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("does not exist")));
    }

    void test_nonRootCannotCreateUsers()
    {
        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        const QString rootClient = pool.createSession(m_dataRoot);
        const QString aliceClient = pool.createSession(m_dataRoot);

        service::SqlExecResult result =
            engine.login(rootClient, QStringLiteral("root"), QString());
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        result = engine.executeSql(rootClient,
                              QStringLiteral("CREATE USER alice IDENTIFIED BY secret;"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        result = engine.login(aliceClient, QStringLiteral("alice"), QStringLiteral("secret"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        result = engine.executeSql(aliceClient,
                                   QStringLiteral("CREATE USER bob IDENTIFIED BY secret;"));
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("root user required")));
    }

private:
    QString m_dataRoot;
};

int service_tests::runAuthClientTests()
{
    AuthClientTest test;
    return QTest::qExec(&test);
}

#include "test_auth_client.moc"
