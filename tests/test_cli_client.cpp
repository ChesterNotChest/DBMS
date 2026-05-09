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
    }

    void cleanup()
    {
        service::currentDatabase.clear();
        service::setDataRoot(QString());
        removeTestDataRoot(m_dataRoot);
    }

    void test_executeOneShotSql()
    {
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
                                      QStringLiteral("--execute"),
                                      QStringLiteral("LOGIN root IDENTIFIED BY ''; CREATE DATABASE cli_exec_db;")});

        QCOMPARE(exitCode, 0);
        QVERIFY(errorText.isEmpty());
        QVERIFY(outputText.contains(QStringLiteral("created")));
    }

    void test_multilineSqlBufferExecutesOnlyAfterSemicolon()
    {
        QString inputText = QStringLiteral("LOGIN root IDENTIFIED BY '';\nCREATE DATABASE cli_multi_db\n;\nexit\n");
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
        QVERIFY(errorText.isEmpty());
        QVERIFY(outputText.contains(QStringLiteral("created")));
        QVERIFY(outputText.contains(QStringLiteral("   -> ")));
    }

    void test_quitCommandExitsWithoutExecutingSql()
    {
        QString inputText = QStringLiteral("quit\n");
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
        QCOMPARE(pool.sessionCount(), 1);
        QVERIFY(errorText.isEmpty());
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
