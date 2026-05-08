#include "catalog_cache.h"
#include "../../constants/thread_perf_def.h"
#include "../../repo/repo.h"

#include <QDir>

namespace thread_runtime {

namespace {

QString cleanRoot(const QString &dataRoot)
{
    return QDir::cleanPath(dataRoot.trimmed());
}

QString cleanName(const QString &name)
{
    return name.trimmed();
}

void touchKey(QList<QString> *lru, const QString &key)
{
    if (lru == nullptr) {
        return;
    }
    lru->removeAll(key);
    lru->append(key);
}

} // namespace

CatalogCache &CatalogCache::instance()
{
    static CatalogCache cache;
    return cache;
}

QString CatalogCache::tableKey(const QString &dataRoot,
                               const QString &databaseName,
                               const QString &tableName) const
{
    return cleanRoot(dataRoot) + QLatin1Char('|') + cleanName(databaseName) + QLatin1Char('|') + cleanName(tableName);
}

QString CatalogCache::databaseKey(const QString &dataRoot, const QString &databaseName) const
{
    return cleanRoot(dataRoot) + QLatin1Char('|') + cleanName(databaseName);
}

QString CatalogCache::rootKey(const QString &dataRoot) const
{
    return cleanRoot(dataRoot);
}

void CatalogCache::trimTableCacheIfNeeded()
{
    while (m_tableCatalogs.size() > threadperf::kCatalogCacheMaxTableEntries && !m_tableLru.isEmpty()) {
        const QString oldestKey = m_tableLru.takeFirst();
        m_tableCatalogs.remove(oldestKey);
    }
}

void CatalogCache::trimDatabaseCacheIfNeeded()
{
    while (m_databaseCatalogs.size() > threadperf::kCatalogCacheMaxDatabaseEntries && !m_databaseLru.isEmpty()) {
        const QString oldestKey = m_databaseLru.takeFirst();
        m_databaseCatalogs.remove(oldestKey);
    }
}

TableCatalogSnapshot CatalogCache::getTableCatalog(const QString &dataRoot,
                                                   const QString &databaseName,
                                                   const QString &tableName,
                                                   QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    const QString key = tableKey(dataRoot, databaseName, tableName);
    if (threadperf::kEnableCatalogCache) {
        QReadLocker reader(&m_lock);
        const auto it = m_tableCatalogs.constFind(key);
        if (it != m_tableCatalogs.constEnd()) {
            return it.value();
        }
    }

    TableCatalogSnapshot snapshot;
    snapshot.dataRoot = cleanRoot(dataRoot);
    snapshot.databaseName = cleanName(databaseName);
    snapshot.tableName = cleanName(tableName);
    snapshot.schema.tableName = snapshot.tableName;

    repo::MetaRepo metaRepo(snapshot.databaseName, snapshot.tableName, snapshot.dataRoot);
    snapshot.schema.columns = metaRepo.listColumns(error);
    if (error != nullptr && !error->isEmpty()) {
        return {};
    }

    repo::ConstraintRepo constraintRepo(snapshot.databaseName, snapshot.tableName, snapshot.dataRoot);
    snapshot.schema.constraints = constraintRepo.listConstraints(error);
    if (error != nullptr && !error->isEmpty()) {
        return {};
    }

    repo::IndexRepo indexRepo(snapshot.databaseName, snapshot.tableName, snapshot.dataRoot);
    snapshot.schema.indexes = indexRepo.listIndexes(error);
    if (error != nullptr && !error->isEmpty()) {
        return {};
    }

    snapshot.fullyLoaded = true;
    if (threadperf::kEnableCatalogCache && snapshot.fullyLoaded) {
        QWriteLocker writer(&m_lock);
        m_tableCatalogs.insert(key, snapshot);
        touchKey(&m_tableLru, key);
        trimTableCacheIfNeeded();
    }

    return snapshot;
}

DatabaseCatalogSnapshot CatalogCache::getDatabaseCatalog(const QString &dataRoot,
                                                         const QString &databaseName,
                                                         QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    const QString key = databaseKey(dataRoot, databaseName);
    if (threadperf::kEnableCatalogCache) {
        QReadLocker reader(&m_lock);
        const auto it = m_databaseCatalogs.constFind(key);
        if (it != m_databaseCatalogs.constEnd()) {
            return it.value();
        }
    }

    DatabaseCatalogSnapshot snapshot;
    snapshot.dataRoot = cleanRoot(dataRoot);
    snapshot.databaseName = cleanName(databaseName);
    repo::TabRepo tabRepo(snapshot.databaseName, snapshot.dataRoot);
    const QList<repo::TableEntry> tables = tabRepo.listTables(error);
    if (error != nullptr && !error->isEmpty()) {
        return {};
    }
    for (const repo::TableEntry &table : tables) {
        snapshot.tableNames.append(table.name);
    }

    if (threadperf::kEnableCatalogCache) {
        QWriteLocker writer(&m_lock);
        m_databaseCatalogs.insert(key, snapshot);
        touchKey(&m_databaseLru, key);
        trimDatabaseCacheIfNeeded();
    }
    return snapshot;
}

RootCatalogSnapshot CatalogCache::getRootCatalog(const QString &dataRoot, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    const QString key = rootKey(dataRoot);
    if (threadperf::kEnableCatalogCache) {
        QReadLocker reader(&m_lock);
        const auto it = m_rootCatalogs.constFind(key);
        if (it != m_rootCatalogs.constEnd()) {
            return it.value();
        }
    }

    RootCatalogSnapshot snapshot;
    snapshot.dataRoot = cleanRoot(dataRoot);
    repo::DatabaseRepo databaseRepo(snapshot.dataRoot);
    const QList<repo::DatabaseEntry> databases = databaseRepo.listDatabases(error);
    if (error != nullptr && !error->isEmpty()) {
        return {};
    }
    for (const repo::DatabaseEntry &database : databases) {
        snapshot.databaseNames.append(database.name);
    }

    if (threadperf::kEnableCatalogCache) {
        QWriteLocker writer(&m_lock);
        m_rootCatalogs.insert(key, snapshot);
    }
    return snapshot;
}

void CatalogCache::invalidateTableCatalog(const QString &dataRoot,
                                          const QString &databaseName,
                                          const QString &tableName)
{
    QWriteLocker writer(&m_lock);
    const QString key = tableKey(dataRoot, databaseName, tableName);
    m_tableCatalogs.remove(key);
    m_tableLru.removeAll(key);
}

void CatalogCache::invalidateDatabaseCatalog(const QString &dataRoot, const QString &databaseName)
{
    QWriteLocker writer(&m_lock);
    const QString dbKey = databaseKey(dataRoot, databaseName);
    m_databaseCatalogs.remove(dbKey);
    m_databaseLru.removeAll(dbKey);
}

void CatalogCache::invalidateAllForDataRoot(const QString &dataRoot)
{
    QWriteLocker writer(&m_lock);
    const QString prefix = cleanRoot(dataRoot);
    m_rootCatalogs.remove(rootKey(dataRoot));

    for (auto it = m_databaseCatalogs.begin(); it != m_databaseCatalogs.end();) {
        if (it.key().startsWith(prefix + QLatin1Char('|'))) {
            m_databaseLru.removeAll(it.key());
            it = m_databaseCatalogs.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = m_tableCatalogs.begin(); it != m_tableCatalogs.end();) {
        if (it.key().startsWith(prefix + QLatin1Char('|'))) {
            m_tableLru.removeAll(it.key());
            it = m_tableCatalogs.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace thread_runtime
