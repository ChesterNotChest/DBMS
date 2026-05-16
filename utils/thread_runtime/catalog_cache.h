#ifndef UTILS_THREAD_RUNTIME_CATALOG_CACHE_H
#define UTILS_THREAD_RUNTIME_CATALOG_CACHE_H

#include "../../constants/table_def.h"

#include <QList>
#include <QMap>
#include <QReadWriteLock>
#include <QString>
#include <QStringList>

namespace thread_runtime {

struct TableCatalogSnapshot {
    QString dataRoot;
    QString databaseName;
    QString tableName;
    tabledef::TableSchema schema;
    bool fullyLoaded = false;
};

struct DatabaseCatalogSnapshot {
    QString dataRoot;
    QString databaseName;
    QStringList tableNames;
};

struct RootCatalogSnapshot {
    QString dataRoot;
    QStringList databaseNames;
};

class CatalogCache
{
public:
    static CatalogCache &instance();

    TableCatalogSnapshot getTableCatalog(const QString &dataRoot,
                                         const QString &databaseName,
                                         const QString &tableName,
                                         QString *error);

    DatabaseCatalogSnapshot getDatabaseCatalog(const QString &dataRoot,
                                               const QString &databaseName,
                                               QString *error);

    RootCatalogSnapshot getRootCatalog(const QString &dataRoot,
                                       QString *error);

    void invalidateTableCatalog(const QString &dataRoot,
                                const QString &databaseName,
                                const QString &tableName);

    void invalidateDatabaseCatalog(const QString &dataRoot,
                                   const QString &databaseName);

    void invalidateAllForDataRoot(const QString &dataRoot);

private:
    CatalogCache() = default;

    QString tableKey(const QString &dataRoot,
                     const QString &databaseName,
                     const QString &tableName) const;
    QString databaseKey(const QString &dataRoot, const QString &databaseName) const;
    QString rootKey(const QString &dataRoot) const;
    void trimTableCacheIfNeeded();
    void trimDatabaseCacheIfNeeded();

    mutable QReadWriteLock m_lock;
    QMap<QString, TableCatalogSnapshot> m_tableCatalogs;
    QMap<QString, DatabaseCatalogSnapshot> m_databaseCatalogs;
    QMap<QString, RootCatalogSnapshot> m_rootCatalogs;
    QList<QString> m_tableLru;
    QList<QString> m_databaseLru;
};

} // namespace thread_runtime

#endif // UTILS_THREAD_RUNTIME_CATALOG_CACHE_H
