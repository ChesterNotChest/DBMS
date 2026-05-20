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

void appendPerformanceMetricRow(const QString &testId,
                                const QString &stageName,
                                int rowCount,
                                const QString &variant,
                                const QString &metric,
                                qint64 value,
                                const QString &unit,
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
        out << "test_id,stage,row_count,variant,metric,value,unit,started_at_utc,ended_at_utc" << Qt::endl;
    }
    out << csvEscape(testId) << ','
        << csvEscape(stageName) << ','
        << (rowCount >= 0 ? QString::number(rowCount) : QString()) << ','
        << csvEscape(variant) << ','
        << csvEscape(metric) << ','
        << value << ','
        << csvEscape(unit) << ','
        << startedAt.toUTC().toString(Qt::ISODateWithMs) << ','
        << endedAt.toUTC().toString(Qt::ISODateWithMs) << Qt::endl;
}

void appendPerformanceMetricRow(const QString &testId,
                                const QString &stageName,
                                int rowCount,
                                const QString &variant,
                                const QString &metric,
                                qint64 value,
                                const QString &unit)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    appendPerformanceMetricRow(testId, stageName, rowCount, variant, metric, value, unit, now, now);
}

class ScopedStageTimer
{
public:
    ScopedStageTimer(QString stageName,
                     int rowCount = -1,
                     QString variant = QString(),
                     QString testId = QString())
        : m_stageName(std::move(stageName))
        , m_rowCount(rowCount)
        , m_variant(std::move(variant))
        , m_testId(std::move(testId))
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
        appendPerformanceMetricRow(m_testId,
                                   m_stageName,
                                   m_rowCount,
                                   m_variant,
                                   QStringLiteral("elapsed_ms"),
                                   elapsedMs,
                                   QStringLiteral("ms"),
                                   m_startedAt,
                                   endedAt);
    }

private:
    QString rowCountText() const
    {
        return m_rowCount >= 0 ? QStringLiteral(" rows=%1").arg(m_rowCount) : QString();
    }

    QString m_stageName;
    int m_rowCount = -1;
    QString m_variant;
    QString m_testId;
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

QString serverExecutablePath()
{
    const QString executableName =
#ifdef Q_OS_WIN
        QStringLiteral("DBMS_SERVER.exe");
#else
        QStringLiteral("DBMS_SERVER");
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

bool startServerProcess(QProcess *process,
                        const QString &serverPath,
                        QString *host,
                        quint16 *port,
                        QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    if (process == nullptr) {
        if (error != nullptr) *error = QStringLiteral("server process pointer is null");
        return false;
    }

    const QString listenHost = QStringLiteral("127.0.0.1");
    process->setProgram(serverPath);
    process->setArguments({QStringLiteral("--host"), listenHost, QStringLiteral("--port"), QStringLiteral("0")});
    process->start();
    if (!process->waitForStarted(10000)) {
        if (error != nullptr) {
            *error = QStringLiteral("failed to start DBMS_SERVER: %1").arg(process->errorString());
        }
        return false;
    }

    QByteArray output;
    const QRegularExpression listenPattern(QStringLiteral("listening on ([^:]+):(\\d+)"));
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 10000) {
        if (process->waitForReadyRead(100)) {
            output.append(process->readAllStandardOutput());
            const QString text = QString::fromLocal8Bit(output);
            const QRegularExpressionMatch match = listenPattern.match(text);
            if (match.hasMatch()) {
                if (host != nullptr) {
                    *host = listenHost;
                }
                if (port != nullptr) {
                    *port = static_cast<quint16>(match.captured(2).toUShort());
                }
                return true;
            }
        }
        if (process->state() == QProcess::NotRunning) {
            break;
        }
    }

    if (error != nullptr) {
        *error = QStringLiteral("DBMS_SERVER did not report a listening port: stdout=%1 stderr=%2")
                     .arg(QString::fromLocal8Bit(output),
                          QString::fromLocal8Bit(process->readAllStandardError()));
    }
    return false;
}

void stopServerProcess(QProcess *process)
{
    if (process == nullptr || process->state() == QProcess::NotRunning) {
        return;
    }
    process->terminate();
    if (!process->waitForFinished(5000)) {
        process->kill();
        process->waitForFinished(5000);
    }
}

int stressRowCount(int defaultValue)
{
    bool ok = false;
    const int configured = QString::fromLocal8Bit(qgetenv("DBMS_STRESS_ROW_COUNT")).toInt(&ok);
    return (ok && configured > 0) ? configured : defaultValue;
}

QList<int> stressScaleRowCounts()
{
    const QString configured = QString::fromLocal8Bit(qgetenv("DBMS_STRESS_ROW_COUNTS")).trimmed();
    if (configured.isEmpty()) {
        return {50, 100, 200, 500};
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
    return rowCounts.isEmpty() ? QList<int>{50, 100, 200, 500} : rowCounts;
}

QList<int> indexOrderByRowCounts()
{
    QList<int> rowCounts = stressScaleRowCounts();
    if (!rowCounts.contains(1000)) {
        rowCounts.append(1000);
    }
    if (!rowCounts.contains(5000)) {
        rowCounts.append(5000);
    }
    std::sort(rowCounts.begin(), rowCounts.end());
    return rowCounts;
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

QString clientCrudSql(const QString &databaseName, int rowCount)
{
    const int targetId = std::min(20, rowCount);
    return QStringLiteral("USE %1;"
                          "CREATE TABLE events (id INT PRIMARY KEY, code VARCHAR(32), value INT);"
                          "%2"
                          "UPDATE events SET value = 999 WHERE id = %3;"
                          "DELETE FROM events WHERE id = 1;"
                          "SELECT value FROM events WHERE id = %3;")
        .arg(databaseName, makeInsertBatch(QStringLiteral("events"), 1, rowCount))
        .arg(targetId);
}

QStringList parallelClientArguments(const QString &host,
                                    quint16 port,
                                    const QString &dataRoot,
                                    const QString &databaseName,
                                    int rowCount)
{
    const QString sql = clientCrudSql(databaseName, rowCount);
    return {QStringLiteral("--host"),
            host,
            QStringLiteral("--port"),
            QString::number(port),
            QStringLiteral("--data-root"),
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

        const QString cliPath = cliExecutablePath();
        QVERIFY2(!cliPath.isEmpty(), "DBMS_CLI executable is required for multi-client stress tests.");
        const QString serverPath = serverExecutablePath();
        QVERIFY2(!serverPath.isEmpty(), "DBMS_SERVER executable is required for C/S multi-client stress tests.");

        for (const int rowCount : stressScaleRowCounts()) {
            {
                client::ClientSessionPool pool;
                client::SqlClientEngine engine(&pool);
                const QString clientId = pool.createSession(m_dataRoot);
                service::SqlExecResult result = engine.login(clientId, QStringLiteral("root"), QString());
                QVERIFY2(result.success, qPrintable(result.errorMessage));
                const QString databaseName = QStringLiteral("stress_p2_sequential_%1").arg(rowCount);
                QVERIFY2(execOk(engine,
                                clientId,
                                QStringLiteral("CREATE DATABASE %1;").arg(databaseName),
                                "prepare sequential CRUD database",
                                &result),
                         qPrintable(result.errorMessage));
                {
                    ScopedStageTimer timer(QStringLiteral("concurrent_crud.total"),
                                           rowCount,
                                           QStringLiteral("single_client_sequential"),
                                           QStringLiteral("concurrent_crud"));
                    QVERIFY2(execOk(engine,
                                    clientId,
                                    clientCrudSql(databaseName, rowCount),
                                    "single-client sequential CRUD",
                                    &result),
                             qPrintable(result.errorMessage));
                }
                appendPerformanceMetricRow(QStringLiteral("concurrent_crud"),
                                           QStringLiteral("concurrent_crud.success_count"),
                                           rowCount,
                                           QStringLiteral("single_client_sequential"),
                                           QStringLiteral("success_count"),
                                           1,
                                           QStringLiteral("count"));
            }

            QProcess serverProcess;
            QString serverHost;
            quint16 serverPort = 0;
            QString serverError;
            QVERIFY2(startServerProcess(&serverProcess,
                                        serverPath,
                                        &serverHost,
                                        &serverPort,
                                        &serverError),
                     qPrintable(serverError));

            client::ClientSessionPool setupPool;
            client::SqlClientEngine setupEngine(&setupPool);
            const QString setupClient = setupPool.createSession(m_dataRoot);
            service::SqlExecResult setupResult = setupEngine.login(setupClient, QStringLiteral("root"), QString());
            QVERIFY2(setupResult.success, qPrintable(setupResult.errorMessage));
            for (int i = 0; i < 4; ++i) {
                QVERIFY2(execOk(setupEngine,
                                setupClient,
                                QStringLiteral("CREATE DATABASE stress_parallel_%1_%2;").arg(rowCount).arg(i + 1),
                                "prepare parallel database",
                                &setupResult),
                         qPrintable(setupResult.errorMessage));
            }

            QVector<QProcess *> processes;
            QVector<QElapsedTimer> clientTimers(4);
            {
                ScopedStageTimer timer(QStringLiteral("concurrent_crud.total"),
                                       rowCount,
                                       QStringLiteral("four_client_parallel"),
                                       QStringLiteral("concurrent_crud"));
                for (int i = 0; i < 4; ++i) {
                    QProcess *process = new QProcess;
                    process->setProgram(cliPath);
                    process->setArguments(parallelClientArguments(
                        serverHost,
                        serverPort,
                        m_dataRoot,
                        QStringLiteral("stress_parallel_%1_%2").arg(rowCount).arg(i + 1),
                        rowCount));
                    clientTimers[i].start();
                    process->start();
                    QVERIFY2(process->waitForStarted(10000),
                             qPrintable(QStringLiteral("failed to start DBMS_CLI: %1").arg(process->errorString())));
                    process->write("\n");
                    process->closeWriteChannel();
                    processes.append(process);
                }

                int successCount = 0;
                for (int i = 0; i < processes.size(); ++i) {
                    QProcess *process = processes[i];
                    QVERIFY2(process->waitForFinished(60000),
                             qPrintable(QStringLiteral("DBMS_CLI timed out: %1").arg(process->errorString())));

                    const qint64 perClientMs = clientTimers[i].elapsed();
                    const QString output = QString::fromLocal8Bit(process->readAllStandardOutput());
                    const QString errorOutput = QString::fromLocal8Bit(process->readAllStandardError());
                    const bool ok = process->exitStatus() == QProcess::NormalExit
                                    && process->exitCode() == 0
                                    && output.contains(QStringLiteral("999"));
                    if (ok) {
                        ++successCount;
                    }
                    appendPerformanceMetricRow(QStringLiteral("concurrent_crud"),
                                               QStringLiteral("concurrent_crud.per_client"),
                                               rowCount,
                                               QStringLiteral("four_client_parallel"),
                                               QStringLiteral("per_client_elapsed_ms"),
                                               perClientMs,
                                               QStringLiteral("ms"));
                    QVERIFY2(ok,
                             qPrintable(QStringLiteral("DBMS_CLI failed with code %1: %2 %3")
                                            .arg(process->exitCode())
                                            .arg(output, errorOutput)));
                    delete process;
                }
                stopServerProcess(&serverProcess);
                appendPerformanceMetricRow(QStringLiteral("concurrent_crud"),
                                           QStringLiteral("concurrent_crud.success_count"),
                                           rowCount,
                                           QStringLiteral("four_client_parallel"),
                                               QStringLiteral("success_count"),
                                               successCount,
                                               QStringLiteral("count"));
            }
            stopServerProcess(&serverProcess);
        }
    }

    void test_stressMassiveDataCrud()
    {
        if (!stressEnabled()) {
            QSKIP("Stress tests are disabled by DBMS_SKIP_STRESS_TESTS or --skip-stress-tests.");
        }

        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        const QString clientId = pool.createSession(m_dataRoot);

        service::SqlExecResult result = engine.login(clientId, QStringLiteral("root"), QString());
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        for (const int rowCount : stressScaleRowCounts()) {
            const QString databaseName = QStringLiteral("stress_p3_crud_%1").arg(rowCount);
            const QString tableName = QStringLiteral("bulk_rows_%1").arg(rowCount);
            QVERIFY2(execOk(engine,
                            clientId,
                            QStringLiteral("CREATE DATABASE %1;"
                                           "USE %1;"
                                           "CREATE TABLE %2 (id INT PRIMARY KEY, code VARCHAR(32), value INT);")
                                .arg(databaseName, tableName),
                            "prepare bulk table",
                            &result),
                     qPrintable(result.errorMessage));
            {
                ScopedStageTimer stepTimer(QStringLiteral("massive_crud.insert"),
                                           rowCount,
                                           QStringLiteral("row_count"),
                                           QStringLiteral("massive_crud"));
                insertBulkRows(engine, clientId, tableName, rowCount, 100);
            }

            {
                ScopedStageTimer stepTimer(QStringLiteral("massive_crud.select_all"),
                                           rowCount,
                                           QStringLiteral("row_count"),
                                           QStringLiteral("massive_crud"));
                QVERIFY2(queryOk(engine,
                                 clientId,
                                 QStringLiteral("SELECT * FROM %1;").arg(tableName),
                                 "select bulk rows",
                                 &result),
                         qPrintable(result.errorMessage));
                QCOMPARE(result.selectResult.resultTable.rows.size(), rowCount);
            }

            const int targetId = std::min(rowCount, std::max(1, rowCount / 2));
            {
                ScopedStageTimer stepTimer(QStringLiteral("massive_crud.update_one"),
                                           rowCount,
                                           QStringLiteral("row_count"),
                                           QStringLiteral("massive_crud"));
                QVERIFY2(execOk(engine,
                                clientId,
                                QStringLiteral("UPDATE %1 SET value = 4242 WHERE id = %2;")
                                    .arg(tableName)
                                    .arg(targetId),
                                "update bulk row",
                                &result),
                         qPrintable(result.errorMessage));
            }

            {
                ScopedStageTimer stepTimer(QStringLiteral("massive_crud.delete_one"),
                                           rowCount,
                                           QStringLiteral("row_count"),
                                           QStringLiteral("massive_crud"));
                QVERIFY2(execOk(engine,
                                clientId,
                                QStringLiteral("DELETE FROM %1 WHERE id = %2;").arg(tableName).arg(targetId),
                                "delete bulk row",
                                &result),
                         qPrintable(result.errorMessage));
            }

            {
                ScopedStageTimer stepTimer(QStringLiteral("massive_crud.select_after_delete"),
                                           rowCount,
                                           QStringLiteral("row_count"),
                                           QStringLiteral("massive_crud"));
                QVERIFY2(queryOk(engine,
                                 clientId,
                                 QStringLiteral("SELECT * FROM %1;").arg(tableName),
                                 "select after bulk delete",
                                 &result),
                         qPrintable(result.errorMessage));
                QCOMPARE(result.selectResult.resultTable.rows.size(), rowCount - 1);
            }
        }
    }

    void test_stressMassiveIndexBuild()
    {
        if (!stressEnabled()) {
            QSKIP("Stress tests are disabled by DBMS_SKIP_STRESS_TESTS or --skip-stress-tests.");
        }

        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        const QString clientId = pool.createSession(m_dataRoot);

        service::SqlExecResult result = engine.login(clientId, QStringLiteral("root"), QString());
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        for (const int rowCount : stressScaleRowCounts()) {
            const QString databaseName = QStringLiteral("stress_p4_index_%1").arg(rowCount);
            const QString tableName = QStringLiteral("indexed_rows_%1").arg(rowCount);
            QVERIFY2(execOk(engine,
                            clientId,
                            QStringLiteral("CREATE DATABASE %1;"
                                           "USE %1;"
                                           "CREATE TABLE %2 (id INT PRIMARY KEY, code VARCHAR(32), value INT);")
                                .arg(databaseName, tableName),
                            "prepare indexed table",
                            &result),
                     qPrintable(result.errorMessage));
            insertBulkRows(engine, clientId, tableName, rowCount, 100);

            {
                ScopedStageTimer timer(QStringLiteral("index_build.create_index"),
                                       rowCount,
                                       QStringLiteral("row_count"),
                                       QStringLiteral("index_build"));
                QVERIFY2(execOk(engine,
                                clientId,
                                QStringLiteral("CREATE INDEX idx_%1_code ON %1(code);").arg(tableName),
                                "create massive index",
                                &result),
                         qPrintable(result.errorMessage));
            }
            const int targetId = std::min(rowCount, std::max(1, rowCount / 2));
            QVERIFY2(queryOk(engine,
                             clientId,
                             QStringLiteral("SELECT id, value FROM %1 WHERE code = 'code_%2';")
                                 .arg(tableName)
                                 .arg(targetId),
                             "select massive index row",
                             &result),
                     qPrintable(result.errorMessage));
            QCOMPARE(result.selectResult.resultTable.rows.size(), 1);
            QCOMPARE(result.selectResult.resultTable.rows.first().value(0), QString::number(targetId));
        }
    }

    void test_stressIndexAblation()
    {
        if (!stressEnabled()) {
            QSKIP("Stress tests are disabled by DBMS_SKIP_STRESS_TESTS or --skip-stress-tests.");
        }

        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        const QString clientId = pool.createSession(m_dataRoot);

        service::SqlExecResult result = engine.login(clientId, QStringLiteral("root"), QString());
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        for (const int rowCount : stressScaleRowCounts()) {
            const int targetId = std::min(rowCount, std::max(1, rowCount / 2));
            const QString healthyDatabase = QStringLiteral("stress_p5_healthy_%1").arg(rowCount);
            const QString healthyTable = QStringLiteral("healthy_rows_%1").arg(rowCount);
            QVERIFY2(execOk(engine,
                            clientId,
                            QStringLiteral("CREATE DATABASE %1;"
                                           "USE %1;"
                                           "CREATE TABLE %2 (id INT PRIMARY KEY, code VARCHAR(32), value INT);")
                                .arg(healthyDatabase, healthyTable),
                            "prepare healthy index table",
                            &result),
                     qPrintable(result.errorMessage));
            insertBulkRows(engine, clientId, healthyTable, rowCount, 100);
            QVERIFY2(execOk(engine,
                            clientId,
                            QStringLiteral("CREATE INDEX idx_%1_code ON %1(code);").arg(healthyTable),
                            "create healthy index",
                            &result),
                     qPrintable(result.errorMessage));
            {
                ScopedStageTimer timer(QStringLiteral("index_repair.update"),
                                       rowCount,
                                       QStringLiteral("healthy_index_file"),
                                       QStringLiteral("index_repair"));
                QVERIFY2(execOk(engine,
                                clientId,
                                QStringLiteral("UPDATE %1 SET value = 31337 WHERE id = %2;")
                                    .arg(healthyTable)
                                    .arg(targetId),
                                "update with healthy index",
                                &result),
                         qPrintable(result.errorMessage));
            }

            const QString ablatedDatabase = QStringLiteral("stress_p5_ablated_%1").arg(rowCount);
            const QString ablatedTable = QStringLiteral("ablated_rows_%1").arg(rowCount);
            QVERIFY2(execOk(engine,
                            clientId,
                            QStringLiteral("CREATE DATABASE %1;"
                                           "USE %1;"
                                           "CREATE TABLE %2 (id INT PRIMARY KEY, code VARCHAR(32), value INT);")
                                .arg(ablatedDatabase, ablatedTable),
                            "prepare ablation table",
                            &result),
                     qPrintable(result.errorMessage));
            insertBulkRows(engine, clientId, ablatedTable, rowCount, 100);
            QVERIFY2(execOk(engine,
                            clientId,
                            QStringLiteral("CREATE INDEX idx_%1_code ON %1(code);").arg(ablatedTable),
                            "create ablation index",
                            &result),
                     qPrintable(result.errorMessage));

            service::setDataRoot(m_dataRoot);
            service::currentDatabase = ablatedDatabase;
            const QString indexPath = firstIndexPath(ablatedDatabase, ablatedTable);
            QVERIFY2(!indexPath.isEmpty(), "index path should be available");
            QVERIFY(QFile::exists(indexPath));
            QVERIFY2(QFile::remove(indexPath), qPrintable(QStringLiteral("failed to remove %1").arg(indexPath)));

            {
                ScopedStageTimer timer(QStringLiteral("index_repair.update"),
                                       rowCount,
                                       QStringLiteral("deleted_index_file"),
                                       QStringLiteral("index_repair"));
                QVERIFY2(execOk(engine,
                                clientId,
                                QStringLiteral("UPDATE %1 SET value = 31337 WHERE id = %2;")
                                    .arg(ablatedTable)
                                    .arg(targetId),
                                "trigger index repair after ablation",
                                &result),
                         qPrintable(result.errorMessage));
            }
            QVERIFY2(queryOk(engine,
                             clientId,
                             QStringLiteral("SELECT value FROM %1 WHERE code = 'code_%2';")
                                 .arg(ablatedTable)
                                 .arg(targetId),
                             "select after index ablation repair",
                             &result),
                     qPrintable(result.errorMessage));
            QCOMPARE(result.selectResult.resultTable.rows.size(), 1);
            QCOMPARE(result.selectResult.resultTable.rows.first().value(0), QStringLiteral("31337"));
            QVERIFY(QFile::exists(indexPath));
        }
    }

    void test_stressIndexLookupBenefit()
    {
        if (!stressEnabled()) {
            QSKIP("Stress tests are disabled by DBMS_SKIP_STRESS_TESTS or --skip-stress-tests.");
        }

        client::ClientSessionPool pool;
        client::SqlClientEngine engine(&pool);
        const QString clientId = pool.createSession(m_dataRoot);

        service::SqlExecResult result = engine.login(clientId, QStringLiteral("root"), QString());
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        for (const int rowCount : indexOrderByRowCounts()) {
            const QString databaseName = QStringLiteral("stress_index_order_by_%1").arg(rowCount);
            const QString tableName = QStringLiteral("ordered_rows_%1").arg(rowCount);
            QVERIFY2(execOk(engine,
                            clientId,
                            QStringLiteral("CREATE DATABASE %1;"
                                           "USE %1;"
                                           "CREATE TABLE %2 (id INT PRIMARY KEY, sort_key VARCHAR(32), value INT);")
                                .arg(databaseName, tableName),
                            "prepare index ORDER BY table",
                            &result),
                     qPrintable(result.errorMessage));

            QStringList rows;
            rows.reserve(rowCount);
            for (int id = 1; id <= rowCount; ++id) {
                const int sortKey = ((id * 37) % rowCount) + 1;
                rows.append(QStringLiteral("(%1, 'key_%2', %3)")
                                .arg(id)
                                .arg(sortKey, 6, 10, QLatin1Char('0'))
                                .arg(id % 100));
            }
            QVERIFY2(execOk(engine,
                            clientId,
                            QStringLiteral("INSERT INTO %1 (id, sort_key, value) VALUES %2;")
                                .arg(tableName, rows.join(QStringLiteral(", "))),
                            "insert ORDER BY rows",
                            &result),
                     qPrintable(result.errorMessage));

            auto assertAscending = [](const service::SqlExecResult &queryResult) {
                QString previous;
                for (const repo::TableRow &row : queryResult.selectResult.resultTable.rows) {
                    const QString current = row.value(0);
                    QVERIFY(current >= previous);
                    previous = current;
                }
            };
            auto assertDescending = [](const service::SqlExecResult &queryResult) {
                QString previous = QStringLiteral("key_999999");
                for (const repo::TableRow &row : queryResult.selectResult.resultTable.rows) {
                    const QString current = row.value(0);
                    QVERIFY(current <= previous);
                    previous = current;
                }
            };

            {
                ScopedStageTimer timer(QStringLiteral("index_order_by_impact.order_by_asc"),
                                       rowCount,
                                       QStringLiteral("without_secondary_index"),
                                       QStringLiteral("index_order_by_impact"));
                QVERIFY2(queryOk(engine,
                                 clientId,
                                 QStringLiteral("SELECT sort_key FROM %1 ORDER BY sort_key ASC;").arg(tableName),
                                 "select ORDER BY ASC without index",
                                 &result),
                         qPrintable(result.errorMessage));
                QCOMPARE(result.selectResult.resultTable.rows.size(), rowCount);
                assertAscending(result);
            }

            {
                ScopedStageTimer timer(QStringLiteral("index_order_by_impact.order_by_desc"),
                                       rowCount,
                                       QStringLiteral("without_secondary_index"),
                                       QStringLiteral("index_order_by_impact"));
                QVERIFY2(queryOk(engine,
                                 clientId,
                                 QStringLiteral("SELECT sort_key FROM %1 ORDER BY sort_key DESC;").arg(tableName),
                                 "select ORDER BY DESC without index",
                                 &result),
                         qPrintable(result.errorMessage));
                QCOMPARE(result.selectResult.resultTable.rows.size(), rowCount);
                assertDescending(result);
            }

            QVERIFY2(execOk(engine,
                            clientId,
                            QStringLiteral("CREATE INDEX idx_%1_sort_key ON %1(sort_key);").arg(tableName),
                            "create ORDER BY secondary index",
                            &result),
                     qPrintable(result.errorMessage));

            {
                ScopedStageTimer timer(QStringLiteral("index_order_by_impact.order_by_asc"),
                                       rowCount,
                                       QStringLiteral("with_secondary_index"),
                                       QStringLiteral("index_order_by_impact"));
                QVERIFY2(queryOk(engine,
                                 clientId,
                                 QStringLiteral("SELECT sort_key FROM %1 ORDER BY sort_key ASC;").arg(tableName),
                                 "select ORDER BY ASC with index",
                                 &result),
                         qPrintable(result.errorMessage));
                QCOMPARE(result.selectResult.resultTable.rows.size(), rowCount);
                assertAscending(result);
            }

            {
                ScopedStageTimer timer(QStringLiteral("index_order_by_impact.order_by_desc"),
                                       rowCount,
                                       QStringLiteral("with_secondary_index"),
                                       QStringLiteral("index_order_by_impact"));
                QVERIFY2(queryOk(engine,
                                 clientId,
                                 QStringLiteral("SELECT sort_key FROM %1 ORDER BY sort_key DESC;").arg(tableName),
                                 "select ORDER BY DESC with index",
                                 &result),
                         qPrintable(result.errorMessage));
                QCOMPARE(result.selectResult.resultTable.rows.size(), rowCount);
                assertDescending(result);
            }
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

        for (const int rowCount : stressScaleRowCounts()) {
            const QString databaseName = QStringLiteral("stress_p6_fk_%1").arg(rowCount);
            const QString parentTable = QStringLiteral("parent_%1").arg(rowCount);
            const QString childTable = QStringLiteral("child_%1").arg(rowCount);
            QVERIFY2(execOk(engine,
                            clientId,
                            QStringLiteral("CREATE DATABASE %1;"
                                           "USE %1;"
                                           "CREATE TABLE %2 (id INT PRIMARY KEY, name VARCHAR(32));"
                                           "CREATE TABLE %3 ("
                                           "id INT PRIMARY KEY, "
                                           "parent_id INT, "
                                           "note VARCHAR(32), "
                                           "CONSTRAINT fk_%3_%2 FOREIGN KEY (parent_id) "
                                           "REFERENCES %2(id) ON DELETE CASCADE ON UPDATE CASCADE);")
                                .arg(databaseName, parentTable, childTable),
                            "prepare cascade tables",
                            &result),
                     qPrintable(result.errorMessage));

            QStringList parentRows;
            QStringList childRows;
            parentRows.reserve(rowCount);
            childRows.reserve(rowCount);
            for (int id = 1; id <= rowCount; ++id) {
                parentRows.append(QStringLiteral("(%1, 'p_%1')").arg(id));
                childRows.append(QStringLiteral("(%1, %1, 'c_%1')").arg(id));
            }
            QVERIFY2(execOk(engine,
                            clientId,
                            QStringLiteral("INSERT INTO %1 (id, name) VALUES %2;")
                                .arg(parentTable, parentRows.join(QStringLiteral(", "))),
                            "insert cascade parents",
                            &result),
                     qPrintable(result.errorMessage));
            QVERIFY2(execOk(engine,
                            clientId,
                            QStringLiteral("INSERT INTO %1 (id, parent_id, note) VALUES %2;")
                                .arg(childTable, childRows.join(QStringLiteral(", "))),
                            "insert cascade children",
                            &result),
                     qPrintable(result.errorMessage));

            const int targetId = std::min(rowCount, std::max(1, rowCount / 2));
            const int updatedId = rowCount + targetId;
            {
                ScopedStageTimer timer(QStringLiteral("foreign_key_cascade.cascade_update"),
                                       rowCount,
                                       QStringLiteral("row_count"),
                                       QStringLiteral("foreign_key_cascade"));
                QVERIFY2(execOk(engine,
                                clientId,
                                QStringLiteral("UPDATE %1 SET id = %2 WHERE id = %3;")
                                    .arg(parentTable)
                                    .arg(updatedId)
                                    .arg(targetId),
                                "cascade parent update",
                                &result),
                         qPrintable(result.errorMessage));
            }
            QVERIFY2(queryOk(engine,
                             clientId,
                             QStringLiteral("SELECT parent_id FROM %1 WHERE id = %2;").arg(childTable).arg(targetId),
                             "select cascade update result",
                             &result),
                     qPrintable(result.errorMessage));
            QCOMPARE(result.selectResult.resultTable.rows.first().value(0), QString::number(updatedId));

            {
                ScopedStageTimer timer(QStringLiteral("foreign_key_cascade.cascade_delete"),
                                       rowCount,
                                       QStringLiteral("row_count"),
                                       QStringLiteral("foreign_key_cascade"));
                QVERIFY2(execOk(engine,
                                clientId,
                                QStringLiteral("DELETE FROM %1 WHERE id = %2;").arg(parentTable).arg(updatedId),
                                "cascade parent delete",
                                &result),
                         qPrintable(result.errorMessage));
            }
            QVERIFY2(queryOk(engine,
                             clientId,
                             QStringLiteral("SELECT * FROM %1 WHERE id = %2;").arg(childTable).arg(targetId),
                             "select cascade delete result",
                             &result),
                     qPrintable(result.errorMessage));
            QCOMPARE(result.selectResult.resultTable.rows.size(), 0);
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
