#ifndef REPO_REPO_H
#define REPO_REPO_H

#include "../constants/table_def.h"

#include <QFile>
#include <QList>
#include <QString>
#include <QStringList>

namespace repo {

using TableRow = QStringList;

// 仓储层路径配置。只开放目录级调整，文件后缀保持固定。
class RepoPathConfig
{
public:
    static QString getDataDirectoryName()
    {
        return s_dataDirectoryName;
    }

    static void setDataDirectoryName(const QString &dataDirectoryName)
    {
        s_dataDirectoryName = dataDirectoryName;
    }

    static QString getSortIndexDirectoryName()
    {
        return s_sortIndexDirectoryName;
    }

    static void setSortIndexDirectoryName(const QString &sortIndexDirectoryName)
    {
        s_sortIndexDirectoryName = sortIndexDirectoryName;
    }

    static QString getSortIndexNameSeparator()
    {
        return s_sortIndexNameSeparator;
    }

    static void setSortIndexNameSeparator(const QString &sortIndexNameSeparator)
    {
        s_sortIndexNameSeparator = sortIndexNameSeparator;
    }

private:
    inline static QString s_dataDirectoryName = QStringLiteral("data");
    inline static QString s_sortIndexDirectoryName = QStringLiteral("indexes");
    inline static QString s_sortIndexNameSeparator = QStringLiteral("__");
};

// 统一的二维表数据结构。
struct TableData
{
    QStringList columns;
    QList<TableRow> rows;

    bool isRectangular() const;
};

// 仓储层返回值。
struct RepositoryResult
{
    bool ok = false;
    QString error;

    static RepositoryResult success();
    static RepositoryResult failure(const QString &errorMessage);
};

// root.dbf 中的数据库记录。
struct DatabaseEntry
{
    QString name;
    QString metaFile;
};

// [database].meta 中的表记录。
struct TableEntry
{
    QString name;
    QString tableFile;
};

// 排序索引记录。
struct SortIndexEntry
{
    QString name;
    QString sourceTable;
    QString indexFile;
};

// 统一的表文件读写器。
class FlatFileTableStore
{
public:
    explicit FlatFileTableStore(QString dataRoot = defaultDataRoot());

    static QString defaultDataRoot();

    QString getDataRoot() const;
    QString getRootFilePath() const;

    QString getMetaFileName(const QString &databaseName) const;
    QString getMetaFilePath(const QString &databaseName) const;

    QString getDatabaseDirectory(const QString &databaseName) const;

    QString getTableFileName(const QString &tableName) const;
    QString getTableFilePath(const QString &databaseName, const QString &tableName) const;

    QString getConstraintFileName(const QString &tableName) const;
    QString getConstraintFilePath(const QString &databaseName, const QString &tableName) const;

    QString getSortIndexDirectory(const QString &databaseName) const;
    QString getSortIndexFileName(const QString &indexName,
                                 const QString &sourceTable = QString()) const;
    QString getSortIndexFilePath(const QString &databaseName,
                                 const QString &indexName,
                                 const QString &sourceTable = QString()) const;

    QString toStorageRelativePath(const QString &absolutePath) const;

    bool exists(const QString &path) const;
    RepositoryResult ensureDataRoot() const;
    RepositoryResult ensureDirectory(const QString &path) const;
    RepositoryResult removeFile(const QString &path) const;

    TableData readTable(const QString &path, QString *error = nullptr) const;
    RepositoryResult writeTable(const QString &path, const TableData &table) const;
    RepositoryResult createEmptyTable(const QString &path, const QStringList &columns) const;
    RepositoryResult appendRow(const QString &path, const TableRow &row) const;
    RepositoryResult updateRow(const QString &path, int rowIndex, const TableRow &row) const;
    RepositoryResult deleteRow(const QString &path, int rowIndex) const;

private:
    QString m_dataRoot;
};

// root.dbf 仓储。
class DatabaseRepo
{
public:
    explicit DatabaseRepo(QString dataRoot = FlatFileTableStore::defaultDataRoot());

    RepositoryResult initialize() const;
    QList<DatabaseEntry> listDatabases(QString *error = nullptr) const;
    bool hasDatabase(const QString &databaseName, QString *error = nullptr) const;
    RepositoryResult createDatabase(const QString &databaseName) const;
    RepositoryResult renameDatabase(const QString &databaseName,
                                    const QString &newDatabaseName) const;
    RepositoryResult deleteDatabase(const QString &databaseName) const;
    TableData rootTable(QString *error = nullptr) const;
    QString getRootFilePath() const;
    tabledef::TableSchema getSchema() const;

private:
    FlatFileTableStore m_store;
};

// [database].meta 仓储。
class MetaRepo
{
public:
    MetaRepo(QString databaseName,
             QString dataRoot = FlatFileTableStore::defaultDataRoot());

    RepositoryResult initialize() const;
    QList<TableEntry> listTables(QString *error = nullptr) const;
    bool hasTable(const QString &tableName, QString *error = nullptr) const;
    RepositoryResult createTableEntry(const QString &tableName) const;
    RepositoryResult renameTableEntry(const QString &tableName,
                                      const QString &newTableName) const;
    RepositoryResult deleteTableEntry(const QString &tableName) const;
    TableData metaTable(QString *error = nullptr) const;
    QString getMetaFilePath() const;
    tabledef::TableSchema getSchema() const;

private:
    QString m_databaseName;
    FlatFileTableStore m_store;
};

// [table].con 仓储，用于存储约束定义。
class ConstraintRepo
{
public:
    ConstraintRepo(QString databaseName,
                   QString tableName,
                   QString dataRoot = FlatFileTableStore::defaultDataRoot());

    RepositoryResult initialize() const;
    QList<tabledef::Constraint> listConstraints(QString *error = nullptr) const;
    bool hasConstraint(const QString &constraintName, QString *error = nullptr) const;
    RepositoryResult createConstraint(const tabledef::Constraint &constraint) const;
    RepositoryResult updateConstraint(const QString &constraintName,
                                      const tabledef::Constraint &constraint) const;
    RepositoryResult deleteConstraint(const QString &constraintName) const;
    TableData constraintTable(QString *error = nullptr) const;
    QString getConstraintFilePath() const;
    tabledef::TableSchema getSchema() const;

private:
    QString m_databaseName;
    QString m_tableName;
    FlatFileTableStore m_store;
};

// [table].dat 仓储。
class TableRepo
{
public:
    TableRepo(QString databaseName,
              QString tableName,
              QString dataRoot = FlatFileTableStore::defaultDataRoot());

    RepositoryResult createTable(const QStringList &columns) const;
    RepositoryResult dropTable() const;
    TableData readTable(QString *error = nullptr) const;
    RepositoryResult replaceTable(const TableData &table) const;
    RepositoryResult insertRow(const TableRow &row) const;
    RepositoryResult updateRow(int rowIndex, const TableRow &row) const;
    RepositoryResult deleteRow(int rowIndex) const;
    QString getTableFilePath() const;

private:
    QString m_databaseName;
    QString m_tableName;
    FlatFileTableStore m_store;
};

// 排序索引文件仓储。
class SortIndexRepo
{
public:
    SortIndexRepo(QString databaseName,
                  QString indexName,
                  QString sourceTable = QString(),
                  QString dataRoot = FlatFileTableStore::defaultDataRoot());

    RepositoryResult createIndex(const QStringList &columns) const;
    RepositoryResult dropIndex() const;
    TableData readIndex(QString *error = nullptr) const;
    RepositoryResult replaceIndex(const TableData &table) const;
    RepositoryResult insertRow(const TableRow &row) const;
    RepositoryResult updateRow(int rowIndex, const TableRow &row) const;
    RepositoryResult deleteRow(int rowIndex) const;
    QString getIndexFilePath() const;

private:
    QString m_databaseName;
    QString m_indexName;
    QString m_sourceTable;
    FlatFileTableStore m_store;
};

} // namespace repo

#endif // REPO_REPO_H
