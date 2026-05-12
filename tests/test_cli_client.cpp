#include "../cli/cli_app.h"
#include "../client/client_session_pool.h"
#include "../client/sql_client_engine.h"
#include "../service/service.h"

#include <QDir>
#include <QtTest>

#include "test_entry.h"

namespace {

QString testDataRoot()
{
    return QDir(QDir::tempPath()).absoluteFilePath(QStringLiteral("DBMS_test_cli_client"));
}

void removeTestDataRoot(const QString &path)
{
    QDir directory(path);
    if (directory.exists()) {
        directory.removeRecursively();
    }
}

} // namespace

class CliClientTest : public QObject
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

    void test_executeOneShotSql()
    {
        QString inputText = QStringLiteral("\n");
        QString outputText;
        QString errorText;
        QTextStream input(&inputText, QIODevice::ReadOnly);
        QTextStream output(&outputText, QIODevice::WriteOnly);
        QTextStream errors(&errorText, QIODevice::WriteOnly);

        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        cli::CliApp app(&pool, &engine, &input, &output, &errors);

        const int exitCode = app.run({QStringLiteral("DBMS_CLI"),
                                      QStringLiteral("--data-root"),
                                      m_dataRoot,
                                      QStringLiteral("-u"),
                                      QStringLiteral("root"),
                                      QStringLiteral("-p"),
                                      QStringLiteral("--execute"),
                                      QStringLiteral("CREATE DATABASE cli_exec_db;")});

        QCOMPARE(exitCode, 0);
        QVERIFY(errorText.isEmpty());
        QVERIFY(outputText.contains(QStringLiteral("created")));
    }

    void test_multilineSqlBufferExecutesOnlyAfterSemicolon()
    {
        QString inputText = QStringLiteral("\nCREATE DATABASE cli_multi_db\n;\nexit\n");
        QString outputText;
        QString errorText;
        QTextStream input(&inputText, QIODevice::ReadOnly);
        QTextStream output(&outputText, QIODevice::WriteOnly);
        QTextStream errors(&errorText, QIODevice::WriteOnly);

        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        cli::CliApp app(&pool, &engine, &input, &output, &errors);

        const int exitCode = app.run({QStringLiteral("DBMS_CLI"),
                                      QStringLiteral("--data-root"),
                                      m_dataRoot,
                                      QStringLiteral("-u"),
                                      QStringLiteral("root"),
                                      QStringLiteral("-p")});

        QCOMPARE(exitCode, 0);
        QVERIFY(errorText.isEmpty());
        QVERIFY(outputText.contains(QStringLiteral("created")));
        QVERIFY(outputText.contains(QStringLiteral("   -> ")));
    }

    void test_quitCommandExitsWithoutExecutingSql()
    {
        QString inputText = QStringLiteral("\nquit\n");
        QString outputText;
        QString errorText;
        QTextStream input(&inputText, QIODevice::ReadOnly);
        QTextStream output(&outputText, QIODevice::WriteOnly);
        QTextStream errors(&errorText, QIODevice::WriteOnly);

        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        cli::CliApp app(&pool, &engine, &input, &output, &errors);

        const int exitCode = app.run({QStringLiteral("DBMS_CLI"),
                                      QStringLiteral("--data-root"),
                                      m_dataRoot,
                                      QStringLiteral("-u"),
                                      QStringLiteral("root"),
                                      QStringLiteral("-p")});

        QCOMPARE(exitCode, 0);
        QCOMPARE(pool.sessionCount(), 1);
        QVERIFY(errorText.isEmpty());
    }

    void test_startupPromptsForCredentials()
    {
        QString inputText = QStringLiteral("root\n\nSHOW DATABASES;\nexit\n");
        QString outputText;
        QString errorText;
        QTextStream input(&inputText, QIODevice::ReadOnly);
        QTextStream output(&outputText, QIODevice::WriteOnly);
        QTextStream errors(&errorText, QIODevice::WriteOnly);

        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        cli::CliApp app(&pool, &engine, &input, &output, &errors);

        const int exitCode = app.run({QStringLiteral("DBMS_CLI"),
                                      QStringLiteral("--data-root"),
                                      m_dataRoot});

        QCOMPARE(exitCode, 0);
        QVERIFY2(errorText.isEmpty(), qPrintable(errorText));
        QVERIFY(outputText.contains(QStringLiteral("Username: ")));
        QVERIFY(outputText.contains(QStringLiteral("Password: ")));
        QVERIFY(outputText.contains(QStringLiteral("Logged in as 'root'")));
    }

    void test_passwordValueDoesNotPrompt()
    {
        client::ClientSessionPool setupPool;
        client::SqlClientEngine setupEngine(&setupPool);
        const QString setupClient = setupPool.createSession(m_dataRoot);
        service::SqlExecResult setupResult =
            setupEngine.login(setupClient, QStringLiteral("root"), QString());
        QVERIFY2(setupResult.success, qPrintable(setupResult.errorMessage));
        setupResult = setupEngine.executeSql(setupClient,
                                             QStringLiteral("CREATE USER cli_user IDENTIFIED BY secret;"));
        QVERIFY2(setupResult.success, qPrintable(setupResult.errorMessage));

        QString inputText;
        QString outputText;
        QString errorText;
        QTextStream input(&inputText, QIODevice::ReadOnly);
        QTextStream output(&outputText, QIODevice::WriteOnly);
        QTextStream errors(&errorText, QIODevice::WriteOnly);

        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        cli::CliApp app(&pool, &engine, &input, &output, &errors);

        const int exitCode = app.run({QStringLiteral("DBMS_CLI"),
                                      QStringLiteral("--data-root"),
                                      m_dataRoot,
                                      QStringLiteral("-u"),
                                      QStringLiteral("cli_user"),
                                      QStringLiteral("-p"),
                                      QStringLiteral("secret"),
                                      QStringLiteral("--execute"),
                                      QStringLiteral("SHOW DATABASES;")});

        QCOMPARE(exitCode, 0);
        QVERIFY2(errorText.isEmpty(), qPrintable(errorText));
        QVERIFY(!outputText.contains(QStringLiteral("Password: ")));
        QVERIFY(outputText.contains(QStringLiteral("Logged in as 'cli_user'")));
        QVERIFY(outputText.contains(QStringLiteral("database_name")));
    }

    void test_displayStatementsUseCliTableFormatting()
    {
        QString inputText =
            QStringLiteral("\n"
                           "CREATE DATABASE cli_display_db;\n"
                           "USE cli_display_db;\n"
                           "CREATE TABLE people (id INT PRIMARY KEY, name VARCHAR(20));\n"
                           "INSERT INTO people (id, name) VALUES (1, 'Ada');\n"
                           "SHOW DATABASES;\n"
                           "SHOW TABLES;\n"
                           "SELECT * FROM people;\n"
                           "DESC people;\n"
                           "SHOW CREATE TABLE people;\n"
                           "exit\n");
        QString outputText;
        QString errorText;
        QTextStream input(&inputText, QIODevice::ReadOnly);
        QTextStream output(&outputText, QIODevice::WriteOnly);
        QTextStream errors(&errorText, QIODevice::WriteOnly);

        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        cli::CliApp app(&pool, &engine, &input, &output, &errors);

        const int exitCode = app.run({QStringLiteral("DBMS_CLI"),
                                      QStringLiteral("--data-root"),
                                      m_dataRoot,
                                      QStringLiteral("-u"),
                                      QStringLiteral("root"),
                                      QStringLiteral("-p")});

        QCOMPARE(exitCode, 0);
        QVERIFY2(errorText.isEmpty(), qPrintable(errorText));
        QVERIFY2(outputText.contains(QStringLiteral("+-")), qPrintable(outputText));
        QVERIFY2(outputText.contains(QStringLiteral("database_name")), qPrintable(outputText));
        QVERIFY2(outputText.contains(QStringLiteral("cli_display_db")), qPrintable(outputText));
        QVERIFY2(outputText.contains(QStringLiteral("| table_name |")), qPrintable(outputText));
        QVERIFY2(outputText.contains(QStringLiteral("| people     |")), qPrintable(outputText));
        QVERIFY2(outputText.contains(QStringLiteral("| id | name |")), qPrintable(outputText));
        QVERIFY2(outputText.contains(QStringLiteral("| 1  | Ada  |")), qPrintable(outputText));
        QVERIFY2(outputText.contains(QStringLiteral("| Definition")), qPrintable(outputText));
        QVERIFY2(outputText.contains(QStringLiteral("| Table  | Create Table")), qPrintable(outputText));
        QVERIFY2(outputText.contains(QStringLiteral("1 row in set")), qPrintable(outputText));
    }

private:
    QString m_dataRoot;
};

int service_tests::runCliClientTests()
{
    CliClientTest test;
    return QTest::qExec(&test);
}

#include "test_cli_client.moc"
