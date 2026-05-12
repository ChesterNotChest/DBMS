#ifndef CONSTANTS_THREAD_PERF_DEF_H
#define CONSTANTS_THREAD_PERF_DEF_H

namespace threadperf {

inline constexpr int kTableLockAcquireTimeoutMs = 3000;
inline constexpr int kDatabaseLockAcquireTimeoutMs = 5000;
inline constexpr bool kEnableSharedReadLock = true;
inline constexpr int kCatalogCacheMaxTableEntries = 256;
inline constexpr int kCatalogCacheMaxDatabaseEntries = 64;
inline constexpr bool kEnableCatalogCache = true;
inline constexpr bool kEnableTableMetadataPreload = true;

} // namespace threadperf

#endif // CONSTANTS_THREAD_PERF_DEF_H
