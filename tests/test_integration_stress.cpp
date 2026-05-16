#include "../client/client_session_pool.h"
#include "../client/sql_client_engine.h"
#include "../repo/repo.h"
#include "../service/service.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTextStream>
#include <QtTest>

#include <algorithm>

#include "test_entry.h"

namespace {

QString testDataRoot()
{
    return QDir(QDir::tempPath()).absoluteFilePath(QStringLiteral("DBMS_test_integration_stress"));
}

QString csvEscape(QString value)
{
    if (value.contains(QLatin1Char('"'))
        || value.contains(QLatin1Char(','))
        || value.contains(QLatin1Char('\n'))
        || value.contains(QLatin1Char('\r'))) {
        value.replace(QStringLiteral("\""), QStringLiteral("\"\""));
        return QStringLiteral("\"%1\"").arg(value);
    }
    return value;
}

void appendPerformanceCsvRow(const QString &stageName,
                             int rowCount,
                             qint64 elapsedMs,
                             const QDateTime &startedAt,
                             const QDateTime &endedAt)
{
    const QString csvPath = QString::fromLocal8Bit(qgetenv("DBMS_PERF_CSV_PATH")).trimmed();
    if (csvPath.isEmpty()) {
        return;
    }

    const QFileInfo fileInfo(csvPath);
    QDir directory = fileInfo.dir();
    if (!directory.exists()) {
        directory.mkpath(QStringLiteral("."));
    }

    const bool writeHeader = !fileInfo.exists() || fileInfo.size() == 0;
    QFile file(csvPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream(stderr) << "[integration-stress] WARN unable to open performance CSV "
                            << csvPath << ": " << file.errorString() << Qt::endl;
        return;
    }

    QTextStream out(&file);
    if (writeHeader) {
        out << "stage,row_count,elapsed_ms,started_at_utc,ended_at_utc" << Qt::endl;
    }
    out << csvEscape(stageName) << ','
        << (rowCount >= 0 ? QString::number(rowCount) : QString()) << ','
        << elapsedMs << ','
        << startedAt.toUTC().toString(Qt::ISODateWithMs) << ','
        << endedAt.toUTC().toString(Qt::ISODateWithMs) << Qt::endl;
}

class ScopedStageTimer
{
public:
    ScopedStageTimer(QString stageName, int rowCount = -1)
        : m_stageName(std::move(stageName))
        , m_rowCount(rowCount)
        , m_startedAt(QDateTime::currentDateTimeUtc())
    {
        m_timer.start();
        QTextStream(stdout) << "[integration-stress] START " << m_stageName
                            << rowCountText() << Qt::endl;
    }

    ~ScopedStageTimer()
    {
        const qint64 elapsedMs = m_timer.elapsed();
        const QDateTime endedAt = QDateTime::currentDateTimeUtc();
        QTextStream(stdout) << "[integration-stress] END " << m_stageName
                            << rowCountText()
                            << " elapsed_ms=" << elapsedMs << Qt::endl;
        appendPerformanceCsvRow(m_stageName, m_rowCount, elapsedMs, m_startedAt, endedAt);
    }

private:
    QString rowCountText() const
    {
        return m_rowCount >= 0 ? QStringLiteral(" rows=%1").arg(m_rowCount) : QString();
    }

    QString m_stageName;
    int m_rowCount = -1;
    QDateTime m_startedAt;
    QElapsedTimer m_timer;
};

void removeTestDataRoot(const QString &path)
{
    QDir directory(path);
    if (directory.exists()) {
        directory.removeRecursively();
    }
}

bool stressEnabled()
{
    return !qEnvironmentVariableIsSet("DBMS_SKIP_STRESS_TESTS");
}

QString cliExecutablePath()
{
    const QString executableName =
#ifdef Q_OS_WIN
        QStringLiteral("DBMS_CLI.exe");
#else
        QStringLiteral("DBMS_CLI");
#endif

    const QStringList candidates = {
        QDir::current().absoluteFilePath(QStringLiteral("bin/%1").arg(executableName)),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(executableName),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../../bin/%1").arg(executableName)),
    };

    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isFile()) {
            return info.absoluteFilePath();
        }
    }
    return {};
}

int stressRowCount(int defaultValue)
{
    bool ok = false;
    const int configured = QString::fromLocal8Bit(qgetenv("DBMS_STRESS_ROW_COUNT")).toInt(&ok);
    return (ok && configured > 0) ? configured : defaultValue;
}

QList<int> performanceSampleRowCounts()
{
    const QString configured = QString::fromLocal8Bit(qgetenv("DBMS_PERF_ROW_COUNTS")).trimmed();
    if (configured.isEmpty()) {
        return {100, 500, 1000};
    }

    QList<int> rowCounts;
    const QStringList parts = configured.split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                                               Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        bool ok = false;
        const int value = part.toInt(&ok);
        if (ok && value > 0 && !rowCounts.contains(value)) {
            rowCounts.append(value);
        }
    }
    return rowCounts.isEmpty() ? QList<int>{100, 500, 1000} : rowCounts;
}

bool execOk(client::SqlClientEngine &engine,
            const QString &clientId,
            const QString &sql,
            const char *context,
            service::SqlExecResult *result = nullptr)
{
    service::SqlExecResult localResult = engine.executeSql(clientId, sql);
    if (!localResult.success) {
        localResult.errorMessage = QStringLiteral("%1 failed: %2").arg(QString::fromLatin1(context),
                                                                        localResult.errorMessage);
    }
    if (result != nullptr) {
        *result = localResult;
    }
    return localResult.success;
}

bool queryOk(client::SqlClientEngine &engine,
             const QString &clientId,
             const QString &sql,
             const char *context,
             service::SqlExecResult *result)
{
    if (result == nullptr) {
        return false;
    }

    *result = engine.executeSql(clientId, sql);
    if (!result->success) {
        result->errorMessage = QStringLiteral("%1 failed: %2").arg(QString::fromLatin1(context),
                                                                    result->errorMessage);
    }
    return result->success;
}

QString makeInsertBatch(const QString &tableName, int firstId, int rowCount)
{
    QStringList rows;
    rows.reserve(rowCount);
    for (int offset = 0; offset < rowCount; ++offset) {
        const int id = firstId + offset;
        rows.append(QStringLiteral("(%1, 'code_%2', %3)").arg(id).arg(id).arg(id % 100));
    }
    return QStringLiteral("INSERT INTO %1 (id, code, value) VALUES %2;")
        .arg(tableName, rows.join(QStringLiteral(", ")));
}

void insertBulkRows(client::SqlClientEngine &engine,
                    const QString &clientId,
                    const QString &tableName,
                    int rowCount,
                    int batchSize)
{
    for (int firstId = 1; firstId <= rowCount; firstId += batchSize) {
        const int currentBatchSize = std::min(batchSize, rowCount - firstId + 1);
        service::SqlExecResult result;
        QVERIFY2(execOk(engine,
                        clientId,
                        makeInsertBatch(tableName, firstId, currentBatchSize),
                        "bulk insert",
                        &result),
                 qPrintable(result.errorMessage));
    }
}

QString firstIndexPath(const QString &databaseName, const QString &tableName)
{
    QString schemaError;
    const tabledef::TableSchema schema = service::loadUserTableSchema(tableName, &schemaError);
    if (!schemaError.isEmpty() || schema.indexes.isEmpty()) {
        return {};
    }

    return repo::SortIndexRepo(databaseName,
                               schema.indexes.first().indexName,
                               tableName,
                               service::currentDataRoot)
        .getIndexFilePath();
}

QStringList parallelClientArguments(const QString &dataRoot, int clientIndex)
{
    const QString databaseName = QStringLiteral("stress_parallel_%1").arg(clientIndex);
    const QString sql = QStringLiteral("USE %1;"
                                       "CREATE TABLE events (id INT PRIMARY KEY, code VARCHAR(32), value INT);"
                                       "%2"
                                       "UPDATE events SET value = 999 WHERE id = 20;"
                                       "DELETE FROM events WHERE id = 1;"
                                       "SELECT value FROM events WHERE id = 20;")
                            .arg(databaseName, makeInsertBatch(QStringLiteral("events"), 1, 40));

    return {QStringLiteral("--data-root"),
            dataRoot,
            QStringLiteral("-u"),
            QStringLiteral("root"),
            QStringLiteral("-p"),
            QStringLiteral("--execute"),
            sql};
}

} // namespace

class IntegrationStressFixture
{
protected:
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

    void test_crudFullChain()
    {
        ScopedStageTimer timer(QStringLiteral("integration.crud_full_chain"));
        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        const QString clientId = pool.createSession(m_dataRoot);

        service::SqlExecResult result = engine.login(clientId, QStringLiteral("root"), QString());
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QVERIFY2(execOk(engine,
                        clientId,
                        QStringLiteral("CREATE DATABASE it_crud_db;"
                                       "USE it_crud_db;"
                                       "CREATE TABLE people (id INT PRIMARY KEY, name VARCHAR(32), score INT);"
                                       "INSERT INTO people (id, name, score) "
                                       "VALUES (1, 'Ada', 10), (2, 'Bob', 20), (3, 'Cy', 30);"),
                        "prepare CRUD chain",
                        &result),
                 qPrintable(result.errorMessage));

        QVERIFY2(queryOk(engine,
                         clientId,
                         QStringLiteral("SELECT name, score FROM people WHERE id = 2;"),
                         "select inserted row",
                         &result),
                 qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 1);
        QCOMPARE(result.selectResult.resultTable.rows.first(),
                 QStringList({QStringLiteral("Bob"), QStringLiteral("20")}));

        QVERIFY2(execOk(engine,
                        clientId,
                        QStringLiteral("UPDATE people SET score = 25 WHERE id = 2;"),
                        "update row",
                        &result),
                 qPrintable(result.errorMessage));
        QVERIFY2(queryOk(engine,
                         clientId,
                         QStringLiteral("SELECT score FROM people WHERE id = 2;"),
                         "select updated row",
                         &result),
                 qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.rows.first().value(0), QStringLiteral("25"));

        QVERIFY2(execOk(engine,
                        clientId,
                        QStringLiteral("DELETE FROM people WHERE id = 1;"),
                        "delete row",
                        &result),
                 qPrintable(result.errorMessage));
        QVERIFY2(queryOk(engine,
                         clientId,
                         QStringLiteral("SELECT * FROM people;"),
                         "select remaining rows",
                         &result),
                 qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 2);
    }

    void test_multiClientDatabaseIsolation()
    {
        ScopedStageTimer timer(QStringLiteral("integration.multi_client_database_isolation"));
        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        const QString firstClient = pool.createSession(m_dataRoot);
        const QString secondClient = pool.createSession(m_dataRoot);

        service::SqlExecResult result = engine.login(firstClient, QStringLiteral("root"), QString());
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        result = engine.login(secondClient, QStringLiteral("root"), QString());
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        QVERIFY2(execOk(engine,
                        firstClient,
                        QStringLiteral("CREATE DATABASE it_client_a;"
                                       "USE it_client_a;"
                                       "CREATE TABLE marker (id INT PRIMARY KEY, name VARCHAR(32));"
                                       "INSERT INTO marker (id, name) VALUES (1, 'alpha');"),
                        "prepare first client",
                        &result),
                 qPrintable(result.errorMessage));
        QVERIFY2(execOk(engine,
                        secondClient,
                        QStringLiteral("CREATE DATABASE it_client_b;"
                                       "USE it_client_b;"
                                       "CREATE TABLE marker (id INT PRIMARY KEY, name VARCHAR(32));"
                                       "INSERT INTO marker (id, name) VALUES (1, 'beta');"),
                        "prepare second client",
                        &result),
                 qPrintable(result.errorMessage));

        QVERIFY2(queryOk(engine,
                         firstClient,
                         QStringLiteral("SELECT name FROM marker WHERE id = 1;"),
                         "select first client marker",
                         &result),
                 qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.rows.first().value(0), QStringLiteral("alpha"));
        QVERIFY2(queryOk(engine,
                         secondClient,
                         QStringLiteral("SELECT name FROM marker WHERE id = 1;"),
                         "select second client marker",
                         &result),
                 qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.rows.first().value(0), QStringLiteral("beta"));
        QCOMPARE(pool.session(firstClient)->currentDatabase, QStringLiteral("it_client_a"));
        QCOMPARE(pool.session(secondClient)->currentDatabase, QStringLiteral("it_client_b"));
    }

    void test_authorizedUserCrudChain()
    {
        ScopedStageTimer timer(QStringLiteral("integration.authorized_user_crud_chain"));
        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        const QString rootClient = pool.createSession(m_dataRoot);
        const QString aliceClient = pool.createSession(m_dataRoot);

        service::SqlExecResult result = engine.login(rootClient, QStringLiteral("root"), QString());
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QVERIFY2(execOk(engine,
                        rootClient,
                        QStringLiteral("CREATE DATABASE it_auth_db;"
                                       "CREATE USER alice IDENTIFIED BY secret;"
                                       "GRANT ALL ON it_auth_db.* TO alice;"),
                        "prepare authorized user",
                        &result),
                 qPrintable(result.errorMessage));

        result = engine.login(aliceClient, QStringLiteral("alice"), QStringLiteral("secret"));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QVERIFY2(execOk(engine,
                        aliceClient,
                        QStringLiteral("USE it_auth_db;"
                                       "CREATE TABLE notes (id INT PRIMARY KEY, body VARCHAR(64));"
                                       "INSERT INTO notes (id, body) VALUES (1, 'draft'), (2, 'done');"
                                       "UPDATE notes SET body = 'published' WHERE id = 1;"
                                       "DELETE FROM notes WHERE id = 2;"),
                        "authorized CRUD chain",
                        &result),
                 qPrintable(result.errorMessage));
        QVERIFY2(queryOk(engine,
                         aliceClient,
                         QStringLiteral("SELECT body FROM notes WHERE id = 1;"),
                         "select authorized row",
                         &result),
                 qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 1);
        QCOMPARE(result.selectResult.resultTable.rows.first().value(0), QStringLiteral("published"));
    }

    void test_ddlIndexLifecycleChain()
    {
        ScopedStageTimer timer(QStringLiteral("integration.ddl_index_lifecycle_chain"));
        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        const QString clientId = pool.createSession(m_dataRoot);

        service::SqlExecResult result = engine.login(clientId, QStringLiteral("root"), QString());
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QVERIFY2(execOk(engine,
                        clientId,
                        QStringLiteral("CREATE DATABASE it_index_db;"
                                       "USE it_index_db;"
                                       "CREATE TABLE items (id INT PRIMARY KEY, code VARCHAR(32), qty INT);"
                                       "INSERT INTO items (id, code, qty) "
                                       "VALUES (1, 'c1', 10), (2, 'c2', 20), (3, 'c3', 30);"
                                       "CREATE INDEX idx_items_code ON items(code);"
                                       "UPDATE items SET code = 'c2b' WHERE id = 2;"),
                        "prepare index lifecycle",
                        &result),
                 qPrintable(result.errorMessage));

        QVERIFY2(queryOk(engine,
                         clientId,
                         QStringLiteral("SELECT id, qty FROM items WHERE code = 'c2b';"),
                         "select through indexed column",
                         &result),
                 qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 1);
        QCOMPARE(result.selectResult.resultTable.rows.first(),
                 QStringList({QStringLiteral("2"), QStringLiteral("20")}));

        QVERIFY2(execOk(engine,
                        clientId,
                        QStringLiteral("DROP INDEX idx_items_code ON items;"
                                       "DELETE FROM items WHERE id = 3;"),
                        "drop index and delete row",
                        &result),
                 qPrintable(result.errorMessage));
        QVERIFY2(queryOk(engine,
                         clientId,
                         QStringLiteral("SELECT * FROM items;"),
                         "select after index drop",
                         &result),
                 qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 2);
    }

    void test_foreignKeyCascadeChain()
    {
        ScopedStageTimer timer(QStringLiteral("integration.foreign_key_cascade_chain"));
        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        const QString clientId = pool.createSession(m_dataRoot);

        service::SqlExecResult result = engine.login(clientId, QStringLiteral("root"), QString());
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QVERIFY2(execOk(engine,
                        clientId,
                        QStringLiteral("CREATE DATABASE it_fk_db;"
                                       "USE it_fk_db;"
                                       "CREATE TABLE parent (id INT PRIMARY KEY, name VARCHAR(32));"
                                       "CREATE TABLE child ("
                                       "id INT PRIMARY KEY, "
                                       "parent_id INT, "
                                       "note VARCHAR(32), "
                                       "CONSTRAINT fk_child_parent FOREIGN KEY (parent_id) "
                                       "REFERENCES parent(id) ON DELETE CASCADE ON UPDATE CASCADE"
                                       ");"
                                       "INSERT INTO parent (id, name) VALUES (1, 'p');"
                                       "INSERT INTO child (id, parent_id, note) VALUES (10, 1, 'c');"),
                        "prepare foreign key cascade",
                        &result),
                 qPrintable(result.errorMessage));

        QVERIFY2(execOk(engine,
                        clientId,
                        QStringLiteral("UPDATE parent SET id = 2 WHERE id = 1;"),
                        "cascade update",
                        &result),
                 qPrintable(result.errorMessage));
        QVERIFY2(queryOk(engine,
                         clientId,
                         QStringLiteral("SELECT parent_id FROM child WHERE id = 10;"),
                         "select cascade update",
                         &result),
                 qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.rows.first().value(0), QStringLiteral("2"));

        QVERIFY2(execOk(engine,
                        clientId,
                        QStringLiteral("DELETE FROM parent WHERE id = 2;"),
                        "cascade delete",
                        &result),
                 qPrintable(result.errorMessage));
        QVERIFY2(queryOk(engine,
                         clientId,
                         QStringLiteral("SELECT * FROM child;"),
                         "select cascade delete",
                         &result),
                 qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 0);
    }

    void test_stressMultiClientParallel()
    {
        if (!stressEnabled()) {
            QSKIP("Stress tests are disabled by DBMS_SKIP_STRESS_TESTS or --skip-stress-tests.");
        }
        ScopedStageTimer timer(QStringLiteral("stress.multi_client_parallel"), 4);

        client::ClientSessionPool setupPool;
        client::SqlClientEngine setupEngine(&setupPool);
        const QString setupClient = setupPool.createSession(m_dataRoot);
        service::SqlExecResult setupResult = setupEngine.login(setupClient, QStringLiteral("root"), QString());
        QVERIFY2(setupResult.success, qPrintable(setupResult.errorMessage));

        const QString cliPath = cliExecutablePath();
        QVERIFY2(!cliPath.isEmpty(), "DBMS_CLI executable is required for multi-client stress tests.");
        for (int i = 0; i < 4; ++i) {
            QVERIFY2(execOk(setupEngine,
                            setupClient,
                            QStringLiteral("CREATE DATABASE stress_parallel_%1;").arg(i + 1),
                            "prepare parallel database",
                            &setupResult),
                     qPrintable(setupResult.errorMessage));
        }

        QVector<QProcess *> processes;
        for (int i = 0; i < 4; ++i) {
            QProcess *process = new QProcess;
            process->setProgram(cliPath);
            process->setArguments(parallelClientArguments(m_dataRoot, i + 1));
            process->start();
            QVERIFY2(process->waitForStarted(10000),
                     qPrintable(QStringLiteral("failed to start DBMS_CLI: %1").arg(process->errorString())));
            process->write("\n");
            process->closeWriteChannel();
            processes.append(process);
        }

        for (QProcess *process : processes) {
            QVERIFY2(process->waitForFinished(60000),
                     qPrintable(QStringLiteral("DBMS_CLI timed out: %1").arg(process->errorString())));

            const QString output = QString::fromLocal8Bit(process->readAllStandardOutput());
            const QString errorOutput = QString::fromLocal8Bit(process->readAllStandardError());
            QVERIFY2(process->exitStatus() == QProcess::NormalExit && process->exitCode() == 0,
                     qPrintable(QStringLiteral("DBMS_CLI failed with code %1: %2 %3")
                                    .arg(process->exitCode())
                                    .arg(output, errorOutput)));
            QVERIFY2(output.contains(QStringLiteral("999")),
                     qPrintable(QStringLiteral("DBMS_CLI output did not contain updated value: %1 %2")
                                    .arg(output, errorOutput)));
            delete process;
        }
    }

    void test_stressMassiveDataCrud()
    {
        if (!stressEnabled()) {
            QSKIP("Stress tests are disabled by DBMS_SKIP_STRESS_TESTS or --skip-stress-tests.");
        }

        const int rowCount = stressRowCount(500);
        ScopedStageTimer timer(QStringLiteral("stress.massive_data_crud"), rowCount);
        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        const QString clientId = pool.createSession(m_dataRoot);

        service::SqlExecResult result = engine.login(clientId, QStringLiteral("root"), QString());
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QVERIFY2(execOk(engine,
                        clientId,
                        QStringLiteral("CREATE DATABASE stress_data_db;"
                                       "USE stress_data_db;"
                                       "CREATE TABLE bulk_rows (id INT PRIMARY KEY, code VARCHAR(32), value INT);"),
                        "prepare bulk table",
                        &result),
                 qPrintable(result.errorMessage));
        {
            ScopedStageTimer stepTimer(QStringLiteral("stress.massive_data_crud.insert"), rowCount);
            insertBulkRows(engine, clientId, QStringLiteral("bulk_rows"), rowCount, 100);
        }

        {
            ScopedStageTimer stepTimer(QStringLiteral("stress.massive_data_crud.select_all"), rowCount);
            QVERIFY2(queryOk(engine,
                             clientId,
                             QStringLiteral("SELECT * FROM bulk_rows;"),
                             "select bulk rows",
                             &result),
                     qPrintable(result.errorMessage));
            QCOMPARE(result.selectResult.resultTable.rows.size(), rowCount);
        }

        const int targetId = std::min(777, rowCount);
        {
            ScopedStageTimer stepTimer(QStringLiteral("stress.massive_data_crud.update_then_select"), 1);
            QVERIFY2(execOk(engine,
                            clientId,
                            QStringLiteral("UPDATE bulk_rows SET value = 4242 WHERE id = %1;").arg(targetId),
                            "update bulk row",
                            &result),
                     qPrintable(result.errorMessage));
            QVERIFY2(queryOk(engine,
                             clientId,
                             QStringLiteral("SELECT value FROM bulk_rows WHERE id = %1;").arg(targetId),
                             "select updated bulk row",
                             &result),
                     qPrintable(result.errorMessage));
            QCOMPARE(result.selectResult.resultTable.rows.first().value(0), QStringLiteral("4242"));
        }

        {
            ScopedStageTimer stepTimer(QStringLiteral("stress.massive_data_crud.delete_then_select"), rowCount - 1);
            QVERIFY2(execOk(engine,
                            clientId,
                            QStringLiteral("DELETE FROM bulk_rows WHERE id = %1;").arg(targetId),
                            "delete bulk row",
                            &result),
                     qPrintable(result.errorMessage));
            QVERIFY2(queryOk(engine,
                             clientId,
                             QStringLiteral("SELECT * FROM bulk_rows;"),
                             "select after bulk delete",
                             &result),
                     qPrintable(result.errorMessage));
            QCOMPARE(result.selectResult.resultTable.rows.size(), rowCount - 1);
        }
    }

    void test_stressMassiveIndexBuild()
    {
        if (!stressEnabled()) {
            QSKIP("Stress tests are disabled by DBMS_SKIP_STRESS_TESTS or --skip-stress-tests.");
        }

        const int rowCount = stressRowCount(500);
        ScopedStageTimer timer(QStringLiteral("stress.massive_index_build"), rowCount);
        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        const QString clientId = pool.createSession(m_dataRoot);

        service::SqlExecResult result = engine.login(clientId, QStringLiteral("root"), QString());
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QVERIFY2(execOk(engine,
                        clientId,
                        QStringLiteral("CREATE DATABASE stress_index_db;"
                                       "USE stress_index_db;"
                                       "CREATE TABLE indexed_rows (id INT PRIMARY KEY, code VARCHAR(32), value INT);"),
                        "prepare indexed table",
                        &result),
                 qPrintable(result.errorMessage));
        insertBulkRows(engine, clientId, QStringLiteral("indexed_rows"), rowCount, 100);

        QVERIFY2(execOk(engine,
                        clientId,
                        QStringLiteral("CREATE INDEX idx_indexed_rows_code ON indexed_rows(code);"),
                        "create massive index",
                        &result),
                 qPrintable(result.errorMessage));
        const int targetId = std::min(999, rowCount);
        QVERIFY2(queryOk(engine,
                         clientId,
                         QStringLiteral("SELECT id, value FROM indexed_rows WHERE code = 'code_%1';").arg(targetId),
                         "select massive index row",
                         &result),
                 qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 1);
        QCOMPARE(result.selectResult.resultTable.rows.first().value(0), QString::number(targetId));
    }

    void test_stressIndexAblation()
    {
        if (!stressEnabled()) {
            QSKIP("Stress tests are disabled by DBMS_SKIP_STRESS_TESTS or --skip-stress-tests.");
        }

        const int rowCount = stressRowCount(500);
        ScopedStageTimer timer(QStringLiteral("stress.index_ablation"), rowCount);
        const QString databaseName = QStringLiteral("stress_ablation_db");
        const QString tableName = QStringLiteral("ablation_rows");
        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        const QString clientId = pool.createSession(m_dataRoot);

        service::SqlExecResult result = engine.login(clientId, QStringLiteral("root"), QString());
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QVERIFY2(execOk(engine,
                        clientId,
                        QStringLiteral("CREATE DATABASE %1;"
                                       "USE %1;"
                                       "CREATE TABLE %2 (id INT PRIMARY KEY, code VARCHAR(32), value INT);")
                            .arg(databaseName, tableName),
                        "prepare ablation table",
                        &result),
                 qPrintable(result.errorMessage));
        insertBulkRows(engine, clientId, tableName, rowCount, 100);
        QVERIFY2(execOk(engine,
                        clientId,
                        QStringLiteral("CREATE INDEX idx_ablation_code ON %1(code);").arg(tableName),
                        "create ablation index",
                        &result),
                 qPrintable(result.errorMessage));

        service::setDataRoot(m_dataRoot);
        service::currentDatabase = databaseName;
        const QString indexPath = firstIndexPath(databaseName, tableName);
        QVERIFY2(!indexPath.isEmpty(), "index path should be available");
        QVERIFY(QFile::exists(indexPath));
        QVERIFY2(QFile::remove(indexPath), qPrintable(QStringLiteral("failed to remove %1").arg(indexPath)));

        const int targetId = std::min(400, rowCount);
        QVERIFY2(execOk(engine,
                        clientId,
                        QStringLiteral("UPDATE %1 SET value = 31337 WHERE id = %2;").arg(tableName).arg(targetId),
                        "trigger index repair after ablation",
                        &result),
                 qPrintable(result.errorMessage));
        QVERIFY2(queryOk(engine,
                         clientId,
                         QStringLiteral("SELECT value FROM %1 WHERE code = 'code_%2';").arg(tableName).arg(targetId),
                         "select after index ablation repair",
                         &result),
                 qPrintable(result.errorMessage));
        QCOMPARE(result.selectResult.resultTable.rows.size(), 1);
        QCOMPARE(result.selectResult.resultTable.rows.first().value(0), QStringLiteral("31337"));
        QVERIFY(QFile::exists(indexPath));
    }

    void test_stressIndexLookupBenefit()
    {
        if (!stressEnabled()) {
            QSKIP("Stress tests are disabled by DBMS_SKIP_STRESS_TESTS or --skip-stress-tests.");
        }

        const int rowCount = stressRowCount(500);
        ScopedStageTimer timer(QStringLiteral("stress.index_lookup_benefit"), rowCount);
        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        const QString clientId = pool.createSession(m_dataRoot);

        service::SqlExecResult result = engine.login(clientId, QStringLiteral("root"), QString());
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QVERIFY2(execOk(engine,
                        clientId,
                        QStringLiteral("CREATE DATABASE stress_lookup_db;"
                                       "USE stress_lookup_db;"
                                       "CREATE TABLE lookup_rows (id INT PRIMARY KEY, code VARCHAR(32), value INT);"),
                        "prepare lookup table",
                        &result),
                 qPrintable(result.errorMessage));
        insertBulkRows(engine, clientId, QStringLiteral("lookup_rows"), rowCount, 100);

        const int targetId = std::min(rowCount, std::max(1, rowCount / 2));
        {
            ScopedStageTimer stepTimer(QStringLiteral("stress.index_lookup_benefit.unindexed_select_indexed_column"),
                                       rowCount);
            QVERIFY2(queryOk(engine,
                             clientId,
                             QStringLiteral("SELECT id, value FROM lookup_rows WHERE code = 'code_%1';").arg(targetId),
                             "select indexed column before index",
                             &result),
                     qPrintable(result.errorMessage));
            QCOMPARE(result.selectResult.resultTable.rows.size(), 1);
            QCOMPARE(result.selectResult.resultTable.rows.first().value(0), QString::number(targetId));
        }

        {
            ScopedStageTimer stepTimer(QStringLiteral("stress.index_lookup_benefit.unindexed_select_non_indexed_column"),
                                       rowCount);
            QVERIFY2(queryOk(engine,
                             clientId,
                             QStringLiteral("SELECT id, code FROM lookup_rows WHERE value = %1;").arg(targetId % 100),
                             "select non-indexed column before index",
                             &result),
                     qPrintable(result.errorMessage));
            QVERIFY(!result.selectResult.resultTable.rows.isEmpty());
        }

        {
            ScopedStageTimer stepTimer(QStringLiteral("stress.index_lookup_benefit.create_index"), rowCount);
            QVERIFY2(execOk(engine,
                            clientId,
                            QStringLiteral("CREATE INDEX idx_lookup_rows_code ON lookup_rows(code);"),
                            "create lookup index",
                            &result),
                     qPrintable(result.errorMessage));
        }

        {
            ScopedStageTimer stepTimer(QStringLiteral("stress.index_lookup_benefit.indexed_select_indexed_column"),
                                       rowCount);
            QVERIFY2(queryOk(engine,
                             clientId,
                             QStringLiteral("SELECT id, value FROM lookup_rows WHERE code = 'code_%1';").arg(targetId),
                             "select indexed column after index",
                             &result),
                     qPrintable(result.errorMessage));
            QCOMPARE(result.selectResult.resultTable.rows.size(), 1);
            QCOMPARE(result.selectResult.resultTable.rows.first().value(0), QString::number(targetId));
        }

        {
            ScopedStageTimer stepTimer(QStringLiteral("stress.index_lookup_benefit.indexed_select_non_indexed_column"),
                                       rowCount);
            QVERIFY2(queryOk(engine,
                             clientId,
                             QStringLiteral("SELECT id, code FROM lookup_rows WHERE value = %1;").arg(targetId % 100),
                             "select non-indexed column after index",
                             &result),
                     qPrintable(result.errorMessage));
            QVERIFY(!result.selectResult.resultTable.rows.isEmpty());
        }
    }

    void test_stressPerformanceCsvSamples()
    {
        if (!stressEnabled()) {
            QSKIP("Stress tests are disabled by DBMS_SKIP_STRESS_TESTS or --skip-stress-tests.");
        }

        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        const QString clientId = pool.createSession(m_dataRoot);

        service::SqlExecResult result = engine.login(clientId, QStringLiteral("root"), QString());
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        for (const int rowCount : performanceSampleRowCounts()) {
            const QString databaseName = QStringLiteral("stress_perf_sample_%1").arg(rowCount);
            const QString tableName = QStringLiteral("sample_rows_%1").arg(rowCount);

            {
                ScopedStageTimer timer(QStringLiteral("perf.sample.prepare"), rowCount);
                QVERIFY2(execOk(engine,
                                clientId,
                                QStringLiteral("CREATE DATABASE %1;"
                                               "USE %1;"
                                               "CREATE TABLE %2 ("
                                               "id INT PRIMARY KEY, "
                                               "code VARCHAR(32), "
                                               "value INT);")
                                    .arg(databaseName, tableName),
                                "prepare performance sample",
                                &result),
                         qPrintable(result.errorMessage));
            }

            {
                ScopedStageTimer timer(QStringLiteral("perf.sample.insert"), rowCount);
                insertBulkRows(engine, clientId, tableName, rowCount, 100);
            }

            {
                ScopedStageTimer timer(QStringLiteral("perf.sample.select_all"), rowCount);
                QVERIFY2(queryOk(engine,
                                 clientId,
                                 QStringLiteral("SELECT * FROM %1;").arg(tableName),
                                 "select all performance sample rows",
                                 &result),
                         qPrintable(result.errorMessage));
                QCOMPARE(result.selectResult.resultTable.rows.size(), rowCount);
            }

            const int targetId = std::min(rowCount, std::max(1, rowCount / 2));
            {
                ScopedStageTimer timer(QStringLiteral("perf.sample.update_one"), rowCount);
                QVERIFY2(execOk(engine,
                                clientId,
                                QStringLiteral("UPDATE %1 SET value = 777 WHERE id = %2;")
                                    .arg(tableName)
                                    .arg(targetId),
                                "update performance sample row",
                                &result),
                         qPrintable(result.errorMessage));
            }

            {
                ScopedStageTimer timer(QStringLiteral("perf.sample.create_index"), rowCount);
                QVERIFY2(execOk(engine,
                                clientId,
                                QStringLiteral("CREATE INDEX idx_%1_code ON %1(code);").arg(tableName),
                                "create performance sample index",
                                &result),
                         qPrintable(result.errorMessage));
            }

            {
                ScopedStageTimer timer(QStringLiteral("perf.sample.indexed_select"), rowCount);
                QVERIFY2(queryOk(engine,
                                 clientId,
                                 QStringLiteral("SELECT id, value FROM %1 WHERE code = 'code_%2';")
                                     .arg(tableName)
                                     .arg(targetId),
                                 "indexed select performance sample row",
                                 &result),
                         qPrintable(result.errorMessage));
                QCOMPARE(result.selectResult.resultTable.rows.size(), 1);
                QCOMPARE(result.selectResult.resultTable.rows.first().value(0), QString::number(targetId));
            }
        }
    }

private:
    QString m_dataRoot;
};

class IntegrationTest : public QObject, private IntegrationStressFixture
{
    Q_OBJECT

private slots:
    void init()
    {
        IntegrationStressFixture::init();
    }

    void cleanup()
    {
        IntegrationStressFixture::cleanup();
    }

    void test_crudFullChain()
    {
        IntegrationStressFixture::test_crudFullChain();
    }

    void test_multiClientDatabaseIsolation()
    {
        IntegrationStressFixture::test_multiClientDatabaseIsolation();
    }

    void test_authorizedUserCrudChain()
    {
        IntegrationStressFixture::test_authorizedUserCrudChain();
    }

    void test_ddlIndexLifecycleChain()
    {
        IntegrationStressFixture::test_ddlIndexLifecycleChain();
    }

    void test_foreignKeyCascadeChain()
    {
        IntegrationStressFixture::test_foreignKeyCascadeChain();
    }
};

class StressTest : public QObject, private IntegrationStressFixture
{
    Q_OBJECT

private slots:
    void init()
    {
        IntegrationStressFixture::init();
    }

    void cleanup()
    {
        IntegrationStressFixture::cleanup();
    }

    void test_stressMultiClientParallel()
    {
        IntegrationStressFixture::test_stressMultiClientParallel();
    }

    void test_stressMassiveDataCrud()
    {
        IntegrationStressFixture::test_stressMassiveDataCrud();
    }

    void test_stressMassiveIndexBuild()
    {
        IntegrationStressFixture::test_stressMassiveIndexBuild();
    }

    void test_stressIndexAblation()
    {
        IntegrationStressFixture::test_stressIndexAblation();
    }

    void test_stressIndexLookupBenefit()
    {
        IntegrationStressFixture::test_stressIndexLookupBenefit();
    }

    void test_stressPerformanceCsvSamples()
    {
        IntegrationStressFixture::test_stressPerformanceCsvSamples();
    }
};

int service_tests::runIntegrationTests()
{
    IntegrationTest test;
    return QTest::qExec(&test);
}

int service_tests::runStressTests()
{
    StressTest test;
    return QTest::qExec(&test);
}

#include "test_integration_stress.moc"
