#include "../service/service.h"
#include "../constants/thread_perf_def.h"
#include "../utils/thread_runtime/lock_manager.h"

#include <QDir>
#include <future>
#include <QtTest>

#include "test_entry.h"

using namespace service;

namespace {

QString testDataRoot()
{
    return QDir(QDir::tempPath()).absoluteFilePath(QStringLiteral("DBMS_test_threaded_service"));
}

void removeTestDataRoot(const QString &path)
{
    QDir directory(path);
    if (directory.exists()) {
        directory.removeRecursively();
    }
}

tabledef::Column column(const QString &name, tabledef::ColumnType type, bool notNull = false)
{
    return tabledef::Column{name, type, 64, notNull, QString(), false, QString()};
}

tabledef::Constraint primaryKey(const QString &name, const QStringList &columns)
{
    return tabledef::Constraint{name, tabledef::ConstraintType::PrimaryKey, columns, QString(), {}, QString()};
}

tabledef::TableSchema simpleSchema(const QString &tableName)
{
    tabledef::TableSchema schema;
    schema.tableName = tableName;
    schema.columns = {
        column(QStringLiteral("id"), tabledef::ColumnType::Int, true),
        column(QStringLiteral("name"), tabledef::ColumnType::Varchar),
    };
    schema.constraints = {primaryKey(QStringLiteral("pk_%1").arg(tableName), {QStringLiteral("id")})};
    return schema;
}

void createReadyTable(const QString &databaseName, const QString &tableName)
{
    TaskResult result = database_service::createDatabase(databaseName);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    result = database_service::useDatabase(databaseName);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    result = table_service::createTable(tableName, simpleSchema(tableName));
    QVERIFY2(result.success, qPrintable(result.errorMessage));
}

} // namespace

class ThreadedServiceTest : public QObject
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

    void test_writePathFailsWhenRelatedTableLockIsHeld()
    {
        const QString databaseName = QStringLiteral("threaded_db");
        const QString tableName = QStringLiteral("threaded_table");
        createReadyTable(databaseName, tableName);

        QString error;
        thread_runtime::ScopedRuntimeLock heldLock = thread_runtime::RuntimeLockManager::instance().acquireLock(
            thread_runtime::tableLockKey(currentDataRoot, databaseName, tableName),
            thread_runtime::RuntimeLockMode::Exclusive,
            threadperf::kTableLockAcquireTimeoutMs,
            &error);
        QVERIFY2(heldLock.isValid(), qPrintable(error));

        std::future<TaskResult> future = std::async(std::launch::async, [tableName]() {
            currentDatabase = QStringLiteral("threaded_db");
            return tuple_service::insertRows(tableName,
                                             {QMap<QString, QString>{{QStringLiteral("id"), QStringLiteral("1")},
                                                                     {QStringLiteral("name"), QStringLiteral("alpha")}}});
        });
        TaskResult result = future.get();
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("runtime lock")));
    }

private:
    QString m_dataRoot;
};

int service_tests::runThreadedServiceTests()
{
    ThreadedServiceTest test;
    return QTest::qExec(&test);
}

#include "test_threaded_service.moc"
