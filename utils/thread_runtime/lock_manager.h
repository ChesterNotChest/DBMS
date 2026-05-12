#ifndef UTILS_THREAD_RUNTIME_LOCK_MANAGER_H
#define UTILS_THREAD_RUNTIME_LOCK_MANAGER_H

#include <QList>
#include <QMap>
#include <QReadWriteLock>
#include <QSharedPointer>
#include <QString>
#include <vector>

namespace thread_runtime {

enum class RuntimeLockMode {
    Shared,
    Exclusive
};

struct RuntimeLockKey {
    QString dataRoot;
    QString databaseName;
    QString tableName;
};

class ScopedRuntimeLock
{
public:
    ScopedRuntimeLock() = default;
    ScopedRuntimeLock(const ScopedRuntimeLock &) = delete;
    ScopedRuntimeLock &operator=(const ScopedRuntimeLock &) = delete;
    ScopedRuntimeLock(ScopedRuntimeLock &&) noexcept = default;
    ScopedRuntimeLock &operator=(ScopedRuntimeLock &&) noexcept = default;

    bool isValid() const;
    void unlock();

private:
    friend class RuntimeLockManager;

    struct Lease;
    explicit ScopedRuntimeLock(QSharedPointer<Lease> lease);

    QSharedPointer<Lease> m_lease;
};

class RuntimeLockManager
{
public:
    static RuntimeLockManager &instance();

    ScopedRuntimeLock acquireLock(const RuntimeLockKey &key,
                                  RuntimeLockMode mode,
                                  int timeoutMs,
                                  QString *error);

    std::vector<ScopedRuntimeLock> acquireOrderedLocks(const QList<RuntimeLockKey> &keys,
                                                       RuntimeLockMode mode,
                                                       int timeoutMs,
                                                       QString *error);

    QString normalizeKeyString(const RuntimeLockKey &key) const;

private:
    RuntimeLockManager() = default;

    QSharedPointer<QReadWriteLock> lockForKey(const QString &keyString);

    mutable QReadWriteLock m_registryLock;
    QMap<QString, QSharedPointer<QReadWriteLock>> m_locks;
};

RuntimeLockKey databaseLockKey(const QString &dataRoot, const QString &databaseName);
RuntimeLockKey tableLockKey(const QString &dataRoot,
                            const QString &databaseName,
                            const QString &tableName);

} // namespace thread_runtime

#endif // UTILS_THREAD_RUNTIME_LOCK_MANAGER_H
