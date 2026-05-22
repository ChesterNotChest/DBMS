#include "lock_manager.h"

#include <QDir>
#include <algorithm>

namespace thread_runtime {

namespace {

QString normalizedDataRoot(const QString &dataRoot)
{
    return QDir::cleanPath(dataRoot.trimmed());
}

QString normalizedName(const QString &name)
{
    return name.trimmed();
}

struct ThreadHeldLockState
{
    int sharedCount = 0;
    int exclusiveCount = 0;
    bool sharedActual = false;
    bool exclusiveActual = false;
};

QMap<QString, ThreadHeldLockState> &threadHeldLocks()
{
    static thread_local QMap<QString, ThreadHeldLockState> heldLocks;
    return heldLocks;
}

bool hasSameThreadLock(const QString &keyString, RuntimeLockMode mode)
{
    const auto it = threadHeldLocks().constFind(keyString);
    if (it == threadHeldLocks().constEnd()) {
        return false;
    }

    if (mode == RuntimeLockMode::Shared) {
        return it.value().sharedCount > 0 || it.value().exclusiveCount > 0;
    }
    return it.value().exclusiveCount > 0;
}

bool hasSameThreadSharedOnlyLock(const QString &keyString)
{
    const auto it = threadHeldLocks().constFind(keyString);
    return it != threadHeldLocks().constEnd()
           && it.value().sharedCount > 0
           && it.value().exclusiveCount == 0;
}

void recordThreadLock(const QString &keyString, RuntimeLockMode mode, bool acquiredActualLock)
{
    ThreadHeldLockState &state = threadHeldLocks()[keyString];
    if (mode == RuntimeLockMode::Shared) {
        ++state.sharedCount;
        state.sharedActual = state.sharedActual || acquiredActualLock;
    } else {
        ++state.exclusiveCount;
        state.exclusiveActual = state.exclusiveActual || acquiredActualLock;
    }
}

void releaseThreadLock(const QString &keyString,
                       RuntimeLockMode mode,
                       const QSharedPointer<QReadWriteLock> &lock)
{
    auto it = threadHeldLocks().find(keyString);
    if (it == threadHeldLocks().end()) {
        return;
    }

    ThreadHeldLockState &state = it.value();
    if (mode == RuntimeLockMode::Shared) {
        state.sharedCount = std::max(0, state.sharedCount - 1);
    } else {
        state.exclusiveCount = std::max(0, state.exclusiveCount - 1);
    }

    const bool shouldReleaseShared = state.sharedActual && state.sharedCount == 0;
    const bool shouldReleaseExclusive = state.exclusiveActual
                                        && state.sharedCount == 0
                                        && state.exclusiveCount == 0;
    if (shouldReleaseShared) {
        state.sharedActual = false;
    }
    if (shouldReleaseExclusive) {
        state.exclusiveActual = false;
    }

    if (state.sharedCount == 0
        && state.exclusiveCount == 0
        && !state.sharedActual
        && !state.exclusiveActual) {
        threadHeldLocks().erase(it);
    }

    if (lock.isNull()) {
        return;
    }
    if (shouldReleaseShared || shouldReleaseExclusive) {
        lock->unlock();
    }
}

} // namespace

struct ScopedRuntimeLock::Lease
{
    Lease(QSharedPointer<QReadWriteLock> lock, RuntimeLockMode mode, QString keyString)
        : lock(std::move(lock))
        , mode(mode)
        , keyString(std::move(keyString))
    {
    }

    ~Lease()
    {
        unlock();
    }

    void unlock()
    {
        if (!valid || lock.isNull()) {
            return;
        }
        releaseThreadLock(keyString, mode, lock);
        valid = false;
    }

    QSharedPointer<QReadWriteLock> lock;
    RuntimeLockMode mode = RuntimeLockMode::Shared;
    QString keyString;
    bool valid = true;
};

ScopedRuntimeLock::ScopedRuntimeLock(QSharedPointer<Lease> lease)
    : m_lease(std::move(lease))
{
}

bool ScopedRuntimeLock::isValid() const
{
    return !m_lease.isNull() && m_lease->valid;
}

void ScopedRuntimeLock::unlock()
{
    if (!m_lease.isNull()) {
        m_lease->unlock();
    }
}

RuntimeLockManager &RuntimeLockManager::instance()
{
    static RuntimeLockManager manager;
    return manager;
}

QString RuntimeLockManager::normalizeKeyString(const RuntimeLockKey &key) const
{
    return normalizedDataRoot(key.dataRoot)
           + QLatin1Char('|')
           + normalizedName(key.databaseName)
           + QLatin1Char('|')
           + normalizedName(key.tableName);
}

QSharedPointer<QReadWriteLock> RuntimeLockManager::lockForKey(const QString &keyString)
{
    {
        QReadLocker reader(&m_registryLock);
        const auto it = m_locks.constFind(keyString);
        if (it != m_locks.constEnd()) {
            return it.value();
        }
    }

    QWriteLocker writer(&m_registryLock);
    auto it = m_locks.find(keyString);
    if (it == m_locks.end()) {
        it = m_locks.insert(keyString, QSharedPointer<QReadWriteLock>::create());
    }
    return it.value();
}

ScopedRuntimeLock RuntimeLockManager::acquireLock(const RuntimeLockKey &key,
                                                  RuntimeLockMode mode,
                                                  int timeoutMs,
                                                  QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    const QString keyString = normalizeKeyString(key);
    QSharedPointer<QReadWriteLock> lock = lockForKey(keyString);

    if (hasSameThreadLock(keyString, mode)) {
        recordThreadLock(keyString, mode, false);
        return ScopedRuntimeLock(QSharedPointer<ScopedRuntimeLock::Lease>::create(lock, mode, keyString));
    }
    if (mode == RuntimeLockMode::Exclusive && hasSameThreadSharedOnlyLock(keyString)) {
        if (error != nullptr) {
            *error = QStringLiteral("failed to acquire runtime lock for '%1'").arg(keyString);
        }
        return {};
    }

    const bool acquired = mode == RuntimeLockMode::Shared
                              ? lock->tryLockForRead(timeoutMs)
                              : lock->tryLockForWrite(timeoutMs);
    if (!acquired) {
        if (error != nullptr) {
            *error = QStringLiteral("failed to acquire runtime lock for '%1'").arg(keyString);
        }
        return {};
    }

    recordThreadLock(keyString, mode, true);
    return ScopedRuntimeLock(QSharedPointer<ScopedRuntimeLock::Lease>::create(lock, mode, keyString));
}

std::vector<ScopedRuntimeLock> RuntimeLockManager::acquireOrderedLocks(const QList<RuntimeLockKey> &keys,
                                                                       RuntimeLockMode mode,
                                                                       int timeoutMs,
                                                                       QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    QMap<QString, RuntimeLockKey> uniqueKeys;
    for (RuntimeLockKey key : keys) {
        if (key.tableName.trimmed().isEmpty()) {
            key.tableName.clear();
        }
        uniqueKeys.insert(normalizeKeyString(key), key);
    }

    std::vector<ScopedRuntimeLock> locks;
    locks.reserve(static_cast<size_t>(uniqueKeys.size()));
    for (auto it = uniqueKeys.constBegin(); it != uniqueKeys.constEnd(); ++it) {
        QString lockError;
        ScopedRuntimeLock lock = acquireLock(it.value(), mode, timeoutMs, &lockError);
        if (!lock.isValid()) {
            for (ScopedRuntimeLock &heldLock : locks) {
                heldLock.unlock();
            }
            locks.clear();
            if (error != nullptr) {
                *error = lockError;
            }
            return locks;
        }
        locks.push_back(std::move(lock));
    }

    return locks;
}

RuntimeLockKey databaseLockKey(const QString &dataRoot, const QString &databaseName)
{
    return RuntimeLockKey{normalizedDataRoot(dataRoot), normalizedName(databaseName), QString()};
}

RuntimeLockKey tableLockKey(const QString &dataRoot,
                            const QString &databaseName,
                            const QString &tableName)
{
    return RuntimeLockKey{normalizedDataRoot(dataRoot), normalizedName(databaseName), normalizedName(tableName)};
}

} // namespace thread_runtime
