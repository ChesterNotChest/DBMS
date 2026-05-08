#include "../constants/thread_perf_def.h"
#include "../utils/thread_runtime/lock_manager.h"

#include <QDir>
#include <QtTest>

#include "test_entry.h"

namespace {

thread_runtime::RuntimeLockKey testKey(const QString &tableName)
{
    return thread_runtime::tableLockKey(QDir::tempPath(),
                                        QStringLiteral("lock_test_db"),
                                        tableName);
}

} // namespace

class LockManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void test_acquireSharedLockSucceeds()
    {
        QString error;
        thread_runtime::ScopedRuntimeLock lock = thread_runtime::RuntimeLockManager::instance().acquireLock(
            testKey(QStringLiteral("shared_success_table")),
            thread_runtime::RuntimeLockMode::Shared,
            threadperf::kTableLockAcquireTimeoutMs,
            &error);
        QVERIFY2(lock.isValid(), qPrintable(error));
        QVERIFY(error.isEmpty());
    }

    void test_acquireExclusiveLockSucceeds()
    {
        QString error;
        thread_runtime::ScopedRuntimeLock lock = thread_runtime::RuntimeLockManager::instance().acquireLock(
            testKey(QStringLiteral("exclusive_success_table")),
            thread_runtime::RuntimeLockMode::Exclusive,
            threadperf::kTableLockAcquireTimeoutMs,
            &error);
        QVERIFY2(lock.isValid(), qPrintable(error));
        QVERIFY(error.isEmpty());
    }

    void test_sharedLocksCanCoexist()
    {
        QString error;
        thread_runtime::ScopedRuntimeLock first = thread_runtime::RuntimeLockManager::instance().acquireLock(
            testKey(QStringLiteral("shared_table")),
            thread_runtime::RuntimeLockMode::Shared,
            threadperf::kTableLockAcquireTimeoutMs,
            &error);
        QVERIFY2(first.isValid(), qPrintable(error));

        thread_runtime::ScopedRuntimeLock second = thread_runtime::RuntimeLockManager::instance().acquireLock(
            testKey(QStringLiteral("shared_table")),
            thread_runtime::RuntimeLockMode::Shared,
            threadperf::kTableLockAcquireTimeoutMs,
            &error);
        QVERIFY2(second.isValid(), qPrintable(error));
    }

    void test_exclusiveLockBlocksSharedLock()
    {
        QString error;
        thread_runtime::ScopedRuntimeLock exclusive = thread_runtime::RuntimeLockManager::instance().acquireLock(
            testKey(QStringLiteral("exclusive_table")),
            thread_runtime::RuntimeLockMode::Exclusive,
            threadperf::kTableLockAcquireTimeoutMs,
            &error);
        QVERIFY2(exclusive.isValid(), qPrintable(error));

        thread_runtime::ScopedRuntimeLock shared = thread_runtime::RuntimeLockManager::instance().acquireLock(
            testKey(QStringLiteral("exclusive_table")),
            thread_runtime::RuntimeLockMode::Shared,
            1,
            &error);
        QVERIFY(!shared.isValid());
        QVERIFY(!error.isEmpty());
    }

    void test_sharedExclusiveBlocks()
    {
        QString error;
        thread_runtime::ScopedRuntimeLock shared = thread_runtime::RuntimeLockManager::instance().acquireLock(
            testKey(QStringLiteral("shared_exclusive_table")),
            thread_runtime::RuntimeLockMode::Shared,
            threadperf::kTableLockAcquireTimeoutMs,
            &error);
        QVERIFY2(shared.isValid(), qPrintable(error));

        thread_runtime::ScopedRuntimeLock exclusive = thread_runtime::RuntimeLockManager::instance().acquireLock(
            testKey(QStringLiteral("shared_exclusive_table")),
            thread_runtime::RuntimeLockMode::Exclusive,
            1,
            &error);
        QVERIFY(!exclusive.isValid());
        QVERIFY(error.contains(QStringLiteral("runtime lock")));
    }

    void test_exclusiveExclusiveBlocks()
    {
        QString error;
        thread_runtime::ScopedRuntimeLock first = thread_runtime::RuntimeLockManager::instance().acquireLock(
            testKey(QStringLiteral("exclusive_exclusive_table")),
            thread_runtime::RuntimeLockMode::Exclusive,
            threadperf::kTableLockAcquireTimeoutMs,
            &error);
        QVERIFY2(first.isValid(), qPrintable(error));

        thread_runtime::ScopedRuntimeLock second = thread_runtime::RuntimeLockManager::instance().acquireLock(
            testKey(QStringLiteral("exclusive_exclusive_table")),
            thread_runtime::RuntimeLockMode::Exclusive,
            1,
            &error);
        QVERIFY(!second.isValid());
        QVERIFY(error.contains(QStringLiteral("runtime lock")));
    }

    void test_timeoutReturnsError()
    {
        QString error;
        thread_runtime::ScopedRuntimeLock exclusive = thread_runtime::RuntimeLockManager::instance().acquireLock(
            testKey(QStringLiteral("timeout_table")),
            thread_runtime::RuntimeLockMode::Exclusive,
            threadperf::kTableLockAcquireTimeoutMs,
            &error);
        QVERIFY2(exclusive.isValid(), qPrintable(error));

        thread_runtime::ScopedRuntimeLock shared = thread_runtime::RuntimeLockManager::instance().acquireLock(
            testKey(QStringLiteral("timeout_table")),
            thread_runtime::RuntimeLockMode::Shared,
            1,
            &error);
        QVERIFY(!shared.isValid());
        QVERIFY(error.contains(QStringLiteral("failed to acquire runtime lock")));
        QVERIFY(error.contains(QStringLiteral("timeout_table")));
    }

    void test_acquireOrderedLocksDeduplicatesAndSorts()
    {
        QList<thread_runtime::RuntimeLockKey> keys{
            testKey(QStringLiteral("z_table")),
            testKey(QStringLiteral("a_table")),
            testKey(QStringLiteral("z_table")),
        };

        QString error;
        QList<thread_runtime::ScopedRuntimeLock> locks =
            thread_runtime::RuntimeLockManager::instance().acquireOrderedLocks(
                keys,
                thread_runtime::RuntimeLockMode::Exclusive,
                threadperf::kTableLockAcquireTimeoutMs,
                &error);
        QVERIFY2(!locks.isEmpty(), qPrintable(error));
        QCOMPARE(locks.size(), 2);
    }

    void test_failedOrderedAcquireRollsBackPartialLocks()
    {
        QString error;
        thread_runtime::ScopedRuntimeLock blocker = thread_runtime::RuntimeLockManager::instance().acquireLock(
            testKey(QStringLiteral("ordered_z_blocked")),
            thread_runtime::RuntimeLockMode::Exclusive,
            threadperf::kTableLockAcquireTimeoutMs,
            &error);
        QVERIFY2(blocker.isValid(), qPrintable(error));

        QList<thread_runtime::RuntimeLockKey> keys{
            testKey(QStringLiteral("ordered_a_partial")),
            testKey(QStringLiteral("ordered_z_blocked")),
        };
        QList<thread_runtime::ScopedRuntimeLock> locks =
            thread_runtime::RuntimeLockManager::instance().acquireOrderedLocks(
                keys,
                thread_runtime::RuntimeLockMode::Exclusive,
                1,
                &error);
        QVERIFY(locks.isEmpty());
        QVERIFY(error.contains(QStringLiteral("runtime lock")));

        thread_runtime::ScopedRuntimeLock partialKeyLock = thread_runtime::RuntimeLockManager::instance().acquireLock(
            testKey(QStringLiteral("ordered_a_partial")),
            thread_runtime::RuntimeLockMode::Exclusive,
            threadperf::kTableLockAcquireTimeoutMs,
            &error);
        QVERIFY2(partialKeyLock.isValid(), qPrintable(error));
    }
};

int service_tests::runLockManagerTests()
{
    LockManagerTest test;
    return QTest::qExec(&test);
}

#include "test_lock_manager.moc"
