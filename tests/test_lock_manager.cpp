#include "../constants/thread_perf_def.h"
#include "../utils/thread_runtime/lock_manager.h"

#include <QDir>
#include <future>
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

    void test_exclusiveLockAllowsSameThreadSharedReentry()
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
        QVERIFY2(shared.isValid(), qPrintable(error));
        QVERIFY(error.isEmpty());
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

    void test_exclusiveLockAllowsSameThreadExclusiveReentry()
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
        QVERIFY2(second.isValid(), qPrintable(error));
        QVERIFY(error.isEmpty());
    }

    void test_exclusiveLockBlocksOtherThreadSharedLock()
    {
        QString error;
        thread_runtime::ScopedRuntimeLock exclusive = thread_runtime::RuntimeLockManager::instance().acquireLock(
            testKey(QStringLiteral("other_thread_shared_timeout_table")),
            thread_runtime::RuntimeLockMode::Exclusive,
            threadperf::kTableLockAcquireTimeoutMs,
            &error);
        QVERIFY2(exclusive.isValid(), qPrintable(error));

        std::future<QString> future = std::async(std::launch::async, []() {
            QString lockError;
            thread_runtime::ScopedRuntimeLock shared = thread_runtime::RuntimeLockManager::instance().acquireLock(
                testKey(QStringLiteral("other_thread_shared_timeout_table")),
                thread_runtime::RuntimeLockMode::Shared,
                1,
                &lockError);
            return shared.isValid() ? QString() : lockError;
        });

        const QString lockError = future.get();
        QVERIFY(lockError.contains(QStringLiteral("failed to acquire runtime lock")));
        QVERIFY(lockError.contains(QStringLiteral("other_thread_shared_timeout_table")));
    }

    void test_exclusiveLockBlocksOtherThreadExclusiveLock()
    {
        QString error;
        thread_runtime::ScopedRuntimeLock exclusive = thread_runtime::RuntimeLockManager::instance().acquireLock(
            testKey(QStringLiteral("other_thread_exclusive_timeout_table")),
            thread_runtime::RuntimeLockMode::Exclusive,
            threadperf::kTableLockAcquireTimeoutMs,
            &error);
        QVERIFY2(exclusive.isValid(), qPrintable(error));

        std::future<QString> future = std::async(std::launch::async, []() {
            QString lockError;
            thread_runtime::ScopedRuntimeLock second = thread_runtime::RuntimeLockManager::instance().acquireLock(
                testKey(QStringLiteral("other_thread_exclusive_timeout_table")),
                thread_runtime::RuntimeLockMode::Exclusive,
                1,
                &lockError);
            return second.isValid() ? QString() : lockError;
        });

        const QString lockError = future.get();
        QVERIFY(lockError.contains(QStringLiteral("failed to acquire runtime lock")));
        QVERIFY(lockError.contains(QStringLiteral("other_thread_exclusive_timeout_table")));
    }

    void test_acquireOrderedLocksDeduplicatesAndSorts()
    {
        QList<thread_runtime::RuntimeLockKey> keys{
            testKey(QStringLiteral("z_table")),
            testKey(QStringLiteral("a_table")),
            testKey(QStringLiteral("z_table")),
        };

        QString error;
        std::vector<thread_runtime::ScopedRuntimeLock> locks =
            thread_runtime::RuntimeLockManager::instance().acquireOrderedLocks(
                keys,
                thread_runtime::RuntimeLockMode::Exclusive,
                threadperf::kTableLockAcquireTimeoutMs,
                &error);
        QVERIFY2(!locks.empty(), qPrintable(error));
        QCOMPARE(locks.size(), 2);
    }

    void test_failedOrderedAcquireRollsBackPartialLocks()
    {
        QString error;
        std::promise<QString> lockAcquired;
        std::promise<void> releaseSignal;
        std::shared_future<void> releaseLock = releaseSignal.get_future().share();
        std::future<void> blockerFuture = std::async(std::launch::async, [&lockAcquired, releaseLock]() {
            QString lockError;
            thread_runtime::ScopedRuntimeLock blocker = thread_runtime::RuntimeLockManager::instance().acquireLock(
                testKey(QStringLiteral("ordered_z_blocked")),
                thread_runtime::RuntimeLockMode::Exclusive,
                threadperf::kTableLockAcquireTimeoutMs,
                &lockError);
            lockAcquired.set_value(blocker.isValid() ? QString() : lockError);
            if (blocker.isValid()) {
                releaseLock.wait();
            }
        });
        const QString blockerError = lockAcquired.get_future().get();
        QVERIFY2(blockerError.isEmpty(), qPrintable(blockerError));

        QList<thread_runtime::RuntimeLockKey> keys{
            testKey(QStringLiteral("ordered_a_partial")),
            testKey(QStringLiteral("ordered_z_blocked")),
        };
        std::vector<thread_runtime::ScopedRuntimeLock> locks =
            thread_runtime::RuntimeLockManager::instance().acquireOrderedLocks(
                keys,
                thread_runtime::RuntimeLockMode::Exclusive,
                1,
                &error);
        QVERIFY(locks.empty());
        QVERIFY(error.contains(QStringLiteral("runtime lock")));

        releaseSignal.set_value();
        blockerFuture.wait();

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
