#ifndef REPO_REPO_H
#define REPO_REPO_H

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <utility>

namespace repo {

using TableRow = QStringList;

/// 表示一个持久化到磁盘上的规则二维表。
struct TableData
{
    /// 表的列名。为空时表示该表不强制要求固定表头。
    QStringList columns;
    /// 按列顺序存储的每一行数据。
    QList<TableRow> rows;

    /// 当每一行的列数都与表宽一致时返回 true。
    bool isRectangular() const;
};

/// 仓储层写操作统一使用的结果对象。
struct RepositoryResult
{
    /// 表示操作是否成功。
    bool ok = false;
    /// 当 `ok` 为 false 时，保存失败原因。
    QString error;

    /// 构造一个成功结果。
    static RepositoryResult success();
    /// 构造一个带错误信息的失败结果。
    static RepositoryResult failure(const QString &errorMessage);
};

/// `root.dbf` 中的一条数据库记录。
struct DatabaseEntry
{
    /// 数据库逻辑名称。
    QString name;
    /// 对应 `.meta` 文件的相对路径。
    QString metaFile;
};

/// `[database].meta` 中的一条表记录。
struct TableEntry
{
    /// 表的逻辑名称。
    QString name;
    /// 对应 `.dat` 文件的相对路径。
    QString tableFile;
};

/// 描述一个已持久化排序索引的记录。
struct SortIndexEntry
{
    /// 排序索引的逻辑名称。
    QString name;
    /// 该索引来源于哪张表。
    QString sourceTable;
    /// 对应索引文件的相对路径。
    QString indexFile;
};

/// 低层平面文件存储辅助类，负责统一读写各类表状文件。
class FlatFileTableStore
{
public:
    /// 使用 `dataRoot` 作为根目录创建存储器。
    explicit FlatFileTableStore(QString dataRoot = defaultDataRoot());

    /// 返回默认数据目录：`当前工作目录/data`。
    static QString defaultDataRoot();

    /// 返回所有持久化文件使用的数据根目录绝对路径。
    QString dataRoot() const;
    /// 返回 `root.dbf` 的绝对路径。
    QString rootFilePath() const;
    /// 返回 `[database].meta` 的绝对路径。
    QString metaFilePath(const QString &databaseName) const;
    /// 返回数据库目录 `data/[database]` 的绝对路径。
    QString databaseDirectory(const QString &databaseName) const;
    /// 返回 `data/[database]/[table].dat` 的绝对路径。
    QString tableFilePath(const QString &databaseName, const QString &tableName) const;
    /// 返回某个数据库排序索引目录的绝对路径。
    QString sortIndexDirectory(const QString &databaseName) const;
    /// 返回某个排序索引文件的绝对路径。
    QString sortIndexFilePath(const QString &databaseName,
                              const QString &indexName,
                              const QString &sourceTable = QString()) const;

    /// 将绝对路径转换为相对存储根目录父级的相对路径。
    QString toStorageRelativePath(const QString &absolutePath) const;

    /// 当目标文件或目录存在时返回 true。
    bool exists(const QString &path) const;
    /// 确保数据根目录存在。
    RepositoryResult ensureDataRoot() const;
    /// 确保任意目标目录存在。
    RepositoryResult ensureDirectory(const QString &path) const;
    /// 删除指定文件；若文件不存在也视为成功。
    RepositoryResult removeFile(const QString &path) const;

    /// 从磁盘读取一个已持久化的二维表。
    TableData readTable(const QString &path, QString *error = nullptr) const;
    /// 使用统一存储格式将一个二维表写回磁盘。
    RepositoryResult writeTable(const QString &path, const TableData &table) const;
    /// 创建一个带预定义列名的空表文件。
    RepositoryResult createEmptyTable(const QString &path, const QStringList &columns) const;
    /// 向已有表文件追加一行。
    RepositoryResult appendRow(const QString &path, const TableRow &row) const;
    /// 替换已有表文件中的某一行。
    RepositoryResult updateRow(const QString &path, int rowIndex, const TableRow &row) const;
    /// 删除已有表文件中的某一行。
    RepositoryResult deleteRow(const QString &path, int rowIndex) const;

private:
    QString m_dataRoot;
};

/// `root.dbf` 的仓储类，用于跟踪系统中的所有数据库。
class DatabaseRepo
{
public:
    /// 使用 `dataRoot` 创建数据库仓储对象。
    explicit DatabaseRepo(QString dataRoot = FlatFileTableStore::defaultDataRoot());

    /// 当 `root.dbf` 不存在时创建它及其父目录。
    RepositoryResult initialize() const;
    /// 返回当前 `root.dbf` 中记录的全部数据库条目。
    QList<DatabaseEntry> listDatabases(QString *error = nullptr) const;
    /// 当 `root.dbf` 已包含 `databaseName` 时返回 true。
    bool hasDatabase(const QString &databaseName, QString *error = nullptr) const;
    /// 新增一个数据库条目，并初始化其 `.meta` 文件和数据目录。
    RepositoryResult createDatabase(const QString &databaseName) const;
    /// 重命名数据库，并同步更新相关 `.meta` 文件和表路径。
    RepositoryResult renameDatabase(const QString &databaseName,
                                    const QString &newDatabaseName) const;
    /// 删除数据库条目及其 `.meta` 文件和数据目录。
    RepositoryResult deleteDatabase(const QString &databaseName) const;
    /// 直接读取原始 `root.dbf` 二维表。
    TableData rootTable(QString *error = nullptr) const;
    /// 返回 `root.dbf` 的绝对路径。
    QString rootFilePath() const;

private:
    FlatFileTableStore m_store;
};

/// 单个 `[database].meta` 文件的仓储类，用于跟踪数据库中的所有表。
class MetaRepo
{
public:
    /// 为一个数据库创建元数据仓储对象。
    MetaRepo(QString databaseName,
             QString dataRoot = FlatFileTableStore::defaultDataRoot());

    /// 当数据库元数据文件不存在时创建它。
    RepositoryResult initialize() const;
    /// 返回当前 `[database].meta` 中记录的全部表条目。
    QList<TableEntry> listTables(QString *error = nullptr) const;
    /// 当元数据文件已包含 `tableName` 时返回 true。
    bool hasTable(const QString &tableName, QString *error = nullptr) const;
    /// 向 `[database].meta` 中新增一条表记录。
    RepositoryResult createTableEntry(const QString &tableName) const;
    /// 重命名一条表记录及其底层 `.dat` 文件。
    RepositoryResult renameTableEntry(const QString &tableName,
                                      const QString &newTableName) const;
    /// 从 `[database].meta` 中删除一条表记录。
    RepositoryResult deleteTableEntry(const QString &tableName) const;
    /// 直接读取原始 `[database].meta` 二维表。
    TableData metaTable(QString *error = nullptr) const;
    /// 返回元数据文件的绝对路径。
    QString metaFilePath() const;

private:
    QString m_databaseName;
    FlatFileTableStore m_store;
};

/// 单个用户表文件 `data/[database]/[table].dat` 的仓储类。
class TableRepo
{
public:
    /// 创建一个绑定到指定数据库和表的仓储对象。
    TableRepo(QString databaseName,
              QString tableName,
              QString dataRoot = FlatFileTableStore::defaultDataRoot());

    /// 按给定列定义创建表文件。
    RepositoryResult createTable(const QStringList &columns) const;
    /// 删除底层 `.dat` 文件。
    RepositoryResult dropTable() const;
    /// 从磁盘读取整张表内容。
    TableData readTable(QString *error = nullptr) const;
    /// 用新内容整体替换磁盘上的表。
    RepositoryResult replaceTable(const TableData &table) const;
    /// 向表中追加一行。
    RepositoryResult insertRow(const TableRow &row) const;
    /// 按行号更新一行数据。
    RepositoryResult updateRow(int rowIndex, const TableRow &row) const;
    /// 按行号删除一行数据。
    RepositoryResult deleteRow(int rowIndex) const;
    /// 返回表文件的绝对路径。
    QString tableFilePath() const;

private:
    QString m_databaseName;
    QString m_tableName;
    FlatFileTableStore m_store;
};

/// `data/[database]/indexes` 下单个排序索引文件的仓储类。
class SortIndexRepo
{
public:
    /// 创建一个绑定到指定数据库、索引和可选源表的仓储对象。
    SortIndexRepo(QString databaseName,
                  QString indexName,
                  QString sourceTable = QString(),
                  QString dataRoot = FlatFileTableStore::defaultDataRoot());

    /// 按给定列定义创建索引文件。
    RepositoryResult createIndex(const QStringList &columns) const;
    /// 删除底层排序索引文件。
    RepositoryResult dropIndex() const;
    /// 从磁盘读取完整索引内容。
    TableData readIndex(QString *error = nullptr) const;
    /// 用新内容整体替换磁盘上的索引文件。
    RepositoryResult replaceIndex(const TableData &table) const;
    /// 向索引文件追加一行。
    RepositoryResult insertRow(const TableRow &row) const;
    /// 修改索引文件中的某一行。
    RepositoryResult updateRow(int rowIndex, const TableRow &row) const;
    /// 删除索引文件中的某一行。
    RepositoryResult deleteRow(int rowIndex) const;
    /// 返回排序索引文件的绝对路径。
    QString indexFilePath() const;

private:
    QString m_databaseName;
    QString m_indexName;
    QString m_sourceTable;
    FlatFileTableStore m_store;
};

} // namespace repo

#endif // REPO_REPO_H
