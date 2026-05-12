#include "../client/client_session_pool.h"
#include "../client/sql_client_engine.h"
#include "../mainwindow.h"
#include "../service/service.h"

#include <QDir>
#include <QtTest>

#include "test_entry.h"

namespace {

QString testDataRoot()
{
    return QDir(QDir::tempPath()).absoluteFilePath(QStringLiteral("DBMS_test_gui_client_runtime"));
}

void removeTestDataRoot(const QString &path)
{
    QDir directory(path);
    if (directory.exists()) {
        directory.removeRecursively();
    }
}

} // namespace

class GuiClientRuntimeTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_dataRoot = testDataRoot();
        removeTestDataRoot(m_dataRoot);
        qputenv("DBMS_GUI_DATA_ROOT", m_dataRoot.toUtf8());
        service::setDataRoot(m_dataRoot);
        service::currentDatabase.clear();
        service::currentUser.clear();
    }

    void cleanup()
    {
        service::currentDatabase.clear();
        service::currentUser.clear();
        service::setDataRoot(QString());
        qunsetenv("DBMS_GUI_DATA_ROOT");
        removeTestDataRoot(m_dataRoot);
    }

    void test_mainWindowCreatesGuiClient()
    {
        MainWindow window;

        QVERIFY(!window.guiClientId().isEmpty());
        const client::ClientSession *session = window.guiClientSession();
        QVERIFY(session != nullptr);
        QVERIFY(session->authenticated);
        QCOMPARE(session->userName, QStringLiteral("root"));
    }

    void test_guiAutoRootCanRunDdl()
    {
        MainWindow window;

        const service::SqlExecResult result =
            window.executeSqlForGui(QStringLiteral("CREATE DATABASE gui_runtime_db;"));

        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QVERIFY(result.text.contains(QStringLiteral("created")));
    }

    void test_guiExecuteSqlUsesOwnSession()
    {
        MainWindow window;
        client::ClientSessionPool cliPool;
        client::SqlClientEngine cliEngine(&cliPool);
        const QString cliClientId = cliPool.createSession(window.guiClientSession()->dataRoot);

        service::SqlExecResult result = cliEngine.login(cliClientId, QStringLiteral("root"), QString());
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        result = window.executeSqlForGui(QStringLiteral("CREATE DATABASE db_gui;"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        result = cliEngine.executeSql(cliClientId, QStringLiteral("CREATE DATABASE db_cli;"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        result = window.executeSqlForGui(QStringLiteral("USE db_gui;"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        result = cliEngine.executeSql(cliClientId, QStringLiteral("USE db_cli;"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        const client::ClientSession *guiSession = window.guiClientSession();
        QVERIFY(guiSession != nullptr);
        const client::ClientSession *cliSession = cliPool.session(cliClientId);
        QVERIFY(cliSession != nullptr);
        QCOMPARE(guiSession->currentDatabase, QStringLiteral("db_gui"));
        QCOMPARE(cliSession->currentDatabase, QStringLiteral("db_cli"));
        QVERIFY(service::currentDatabase.isEmpty());
    }

    void test_structureRefreshUsesGuiClientContext()
    {
        MainWindow window;

        service::SqlExecResult result =
            window.executeSqlForGui(QStringLiteral("CREATE DATABASE gui_tree_db;"
                                                  "USE gui_tree_db;"
                                                  "CREATE TABLE gui_tree_table (id INT PRIMARY KEY);"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        result = window.executeSqlForGui(QStringLiteral("USE gui_tree_db; SHOW TABLES;"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QVERIFY(result.selectResult.success);
        QCOMPARE(result.selectResult.resultTable.rows.size(), 1);
        QCOMPARE(result.selectResult.resultTable.rows.first().first(), QStringLiteral("gui_tree_table"));

        const client::ClientSession *session = window.guiClientSession();
        QVERIFY(session != nullptr);
        QCOMPARE(session->currentDatabase, QStringLiteral("gui_tree_db"));
    }

    void test_guiExecuteSqlRestoresPreviousServiceContext()
    {
        service::setDataRoot(QStringLiteral("outside_root"));
        service::currentDatabase = QStringLiteral("outside_db");
        service::currentUser = QStringLiteral("outside_user");

        MainWindow window;
        service::SqlExecResult result =
            window.executeSqlForGui(QStringLiteral("CREATE DATABASE gui_context_db;"
                                                  "USE gui_context_db;"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        QCOMPARE(service::getDataRoot(), QDir::cleanPath(QStringLiteral("outside_root")));
        QCOMPARE(service::currentDatabase, QStringLiteral("outside_db"));
        QCOMPARE(service::currentUser, QStringLiteral("outside_user"));
        const client::ClientSession *session = window.guiClientSession();
        QVERIFY(session != nullptr);
        QCOMPARE(session->currentDatabase, QStringLiteral("gui_context_db"));
    }

    void test_guiExecuteSqlReportsPermissionFailure()
    {
        MainWindow window;
        service::SqlExecResult result =
            window.executeSqlForGui(QStringLiteral("CREATE USER limited IDENTIFIED BY secret;"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        const QString limitedClient = pool.createSession(window.guiClientSession()->dataRoot);
        result = engine.login(limitedClient, QStringLiteral("limited"), QStringLiteral("secret"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        result = engine.executeSql(limitedClient, QStringLiteral("CREATE DATABASE forbidden_db;"));
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("permission denied"))
                || result.errorMessage.contains(QStringLiteral("root user required")));
    }

private:
    QString m_dataRoot;
};

int service_tests::runGuiClientRuntimeTests()
{
    GuiClientRuntimeTest test;
    return QTest::qExec(&test);
}

#include "test_gui_client_runtime.moc"
