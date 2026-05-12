#ifndef REPO_REPO_H
#define REPO_REPO_H

#include "../constants/table_def.h"
#include "../utils/table_manu/table_manu.h"

#include <QFile>
#include <QList>
#include <QString>
#include <QStringList>
#include <utility>

namespace repo {

using TableRow = QStringList;

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

struct TableData
{
    QStringList columns;
    QList<TableRow> rows;

    bool isRectangular() const;
};

struct RepositoryResult
{
    bool ok = false;
    QString error;

    static RepositoryResult success();
    static RepositoryResult failure(const QString &errorMessage);
};

// root.dbf 中的一条数据库记录。
struct DatabaseEntry
{
    QString name;
};

// [database].tab 中的一条表清单记录。
struct TableEntry
{
    QString name;
};

struct SortIndexEntry
{
    QString name;
    QString sourceTable;
    QString indexFile;
};

class FlatFileTableStore
{
public:
    explicit FlatFileTableStore(QString dataRoot = defaultDataRoot());

    static QString defaultDataRoot();

    QString getDataRoot() const;
    QString getRootFilePath() const;

    QString getTabFileName(const QString &databaseName) const;
    QString getTabFilePath(const QString &databaseName) const;

    QString getDatabaseDirectory(const QString &databaseName) const;
    QString getTableDirectory(const QString &databaseName, const QString &tableName) const;

    QString getMetaFileName() const;
    QString getMetaFilePath(const QString &databaseName, const QString &tableName) const;

    QString getTableFileName() const;
    QString getTableFilePath(const QString &databaseName, const QString &tableName) const;

    QString getRowIdFileName() const;
    QString getRowIdFilePath(const QString &databaseName, const QString &tableName) const;

    QString getConstraintFileName() const;
    QString getConstraintFilePath(const QString &databaseName, const QString &tableName) const;

    QString getIndexMetaFileName() const;
    QString getIndexMetaFilePath(const QString &databaseName, const QString &tableName) const;

    QString getSortIndexDirectory(const QString &databaseName, const QString &tableName) const;
    QString getSortIndexFileName(const QString &indexName) const;
    QString getSortIndexFilePath(const QString &databaseName,
                                 const QString &tableName,
                                 const QString &indexName) const;

    QString toStorageRelativePath(const QString &absolutePath) const;

    bool exists(const QString &path) const;
    RepositoryResult ensureDataRoot() const;
    RepositoryResult ensureDirectory(const QString &path) const;
    RepositoryResult removeFile(const QString &path) const;
    RepositoryResult removeDirectoryRecursively(const QString &path) const;

    TableData readTable(const QString &path, QString *error = nullptr) const;
    RepositoryResult writeTable(const QString &path, const TableData &table) const;
    RepositoryResult createEmptyTable(const QString &path, const QStringList &columns) const;
    RepositoryResult appendRow(const QString &path, const TableRow &row) const;
    RepositoryResult updateRow(const QString &path, int rowIndex, const TableRow &row) const;
    RepositoryResult deleteRow(const QString &path, int rowIndex) const;

private:
    QString m_dataRoot;
};

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

// [database].tab 仓储，用于维护数据库中的表清单。
class TabRepo
{
public:
    TabRepo(QString databaseName,
            QString dataRoot = FlatFileTableStore::defaultDataRoot());

    RepositoryResult initialize() const;
    QList<TableEntry> listTables(QString *error = nullptr) const;
    bool hasTable(const QString &tableName, QString *error = nullptr) const;
    RepositoryResult createTableEntry(const QString &tableName) const;
    RepositoryResult renameTableEntry(const QString &tableName,
                                      const QString &newTableName) const;
    RepositoryResult deleteTableEntry(const QString &tableName) const;
    TableData tabTable(QString *error = nullptr) const;
    QString getTabFilePath() const;
    tabledef::TableSchema getSchema() const;

private:
    QString m_databaseName;
    FlatFileTableStore m_store;
};

// [table].meta 仓储，用于维护单张表的列定义。
class MetaRepo
{
public:
    MetaRepo(QString databaseName,
             QString tableName,
             QString dataRoot = FlatFileTableStore::defaultDataRoot());

    RepositoryResult initialize() const;
    QList<tabledef::Column> listColumns(QString *error = nullptr) const;
    bool hasColumn(const QString &columnName, QString *error = nullptr) const;
    RepositoryResult createColumn(const tabledef::Column &column) const;
    RepositoryResult updateColumn(const QString &columnName,
                                  const tabledef::Column &column) const;
    RepositoryResult deleteColumn(const QString &columnName) const;
    TableData metaTable(QString *error = nullptr) const;
    QString getMetaFilePath() const;
    tabledef::TableSchema getSchema() const;

private:
    QString m_databaseName;
    QString m_tableName;
    FlatFileTableStore m_store;
};

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

class IndexRepo
{
public:
    IndexRepo(QString databaseName,
              QString tableName,
              QString dataRoot = FlatFileTableStore::defaultDataRoot());

    RepositoryResult initialize() const;
    QList<tabledef::IndexMeta> listIndexes(QString *error = nullptr) const;
    bool hasIndex(const QString &indexName, QString *error = nullptr) const;
    RepositoryResult createIndex(const tabledef::IndexMeta &index) const;
    RepositoryResult updateIndex(const QString &indexName, const tabledef::IndexMeta &index) const;
    RepositoryResult deleteIndex(const QString &indexName) const;
    TableData indexTable(QString *error = nullptr) const;
    QString getIndexMetaFilePath() const;
    tabledef::TableSchema getSchema() const;

private:
    QString m_databaseName;
    QString m_tableName;
    FlatFileTableStore m_store;
};

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

class SortIndexRepo
{
public:
    SortIndexRepo(QString databaseName,
                  QString indexName,
                  QString sourceTable = QString(),
                  QString dataRoot = FlatFileTableStore::defaultDataRoot());

    RepositoryResult createIndex(const QStringList &columns) const;
    RepositoryResult createIndex(const tabledef::IndexMeta &index,
                                const TableData &table,
                                const QStringList &rowLocators) const;
        RepositoryResult rebuild(const tabledef::IndexMeta &index,
                                 const TableData &table,
                                 const QStringList &rowLocators) const;
    RepositoryResult dropIndex() const;
    TableData readIndex(QString *error = nullptr) const;
    RepositoryResult replaceIndex(const TableData &table) const;
    RepositoryResult insertRow(const TableRow &row) const;
    RepositoryResult updateRow(int rowIndex, const TableRow &row) const;
    RepositoryResult deleteRow(int rowIndex) const;
    RepositoryResult rebuild(const TableData &table, const QStringList &rowLocators) const;
    RepositoryResult insertIndexEntry(const QStringList &keyValues, const QString &rowLocator) const;
    RepositoryResult insertIndexEntries(const QList<QStringList> &keyValuesList,
                                        const QStringList &rowLocators) const;
    RepositoryResult updateIndexEntry(const QStringList &oldKeyValues,
                                      const QStringList &newKeyValues,
                                      const QString &rowLocator) const;
    RepositoryResult deleteIndexEntry(const QStringList &keyValues, const QString &rowLocator) const;
    QStringList search(const QStringList &keyValues, QString *error = nullptr) const;
    bool validateUniqueKeys(QString *error = nullptr) const;
    QString getIndexFilePath() const;

private:
    QString m_databaseName;
    QString m_indexName;
    QString m_sourceTable;
    FlatFileTableStore m_store;
};

} // namespace repo

#endif // REPO_REPO_H
