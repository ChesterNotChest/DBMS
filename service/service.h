#ifndef SERVICE_SERVICE_H
#define SERVICE_SERVICE_H

#include "../constants/table_def.h"
#include "../repo/repo.h"

#include <QDir>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace service {

inline QString currentDatabase;
inline QString currentDataRoot = repo::FlatFileTableStore::defaultDataRoot();

inline void setDataRoot(const QString &dataRoot)
{
    currentDataRoot = dataRoot.trimmed().isEmpty() ? repo::FlatFileTableStore::defaultDataRoot()
                                                   : QDir::cleanPath(dataRoot);
}

inline QString getDataRoot()
{
    return currentDataRoot;
}

enum class TargetTableKind {
    RootDbf,
    DatabaseTab,
    TableMeta,
    TableCon,
    TableDat
};

enum class ValidationMode {
    SystemMeta,
    UserData
};

struct SimpleCondition
{
    QString columnName;
    QString value;
};

struct TaskResult
{
    bool success = false;
    QString errorMessage;
    int affectedRowCount = 0;
};

struct TextResult
{
    bool success = false;
    QString errorMessage;
    QString text;
};

struct ColumnDefinition
{
    tabledef::Column column;
    bool primaryKey = false;
    bool unique = false;
    QString referencedTable;
    QStringList referencedColumns;
    QString checkClause;
};

struct SelectRowsResult
{
    bool success = false;
    QString errorMessage;
    repo::TableData resultTable;
    int affectedRowCount = 0;
};

struct TableDmlResult
{
    bool success = false;
    QString errorMessage;
    int affectedRowCount = 0;
};

inline bool isUserDataTarget(TargetTableKind kind)
{
    return kind == TargetTableKind::TableDat;
}

class TableDmlService
{
public:
    TableDmlService() = default;

    SelectRowsResult selectRows(const QString &targetDatabaseName,
                                const QString &targetTableName,
                                TargetTableKind targetTableKind,
                                const tabledef::TableSchema &targetSchema,
                                const QStringList &projectionColumns,
                                const QList<SimpleCondition> &simpleConditions) const;

    TableDmlResult insertRows(const QString &targetDatabaseName,
                              const QString &targetTableName,
                              TargetTableKind targetTableKind,
                              const tabledef::TableSchema &targetSchema,
                              const QList<QMap<QString, QString>> &rows,
                              ValidationMode validationMode) const;

    TableDmlResult updateRows(const QString &targetDatabaseName,
                              const QString &targetTableName,
                              TargetTableKind targetTableKind,
                              const tabledef::TableSchema &targetSchema,
                              const QMap<QString, QString> &assignmentMap,
                              const QList<SimpleCondition> &simpleConditions,
                              ValidationMode validationMode) const;

    TableDmlResult deleteRows(const QString &targetDatabaseName,
                              const QString &targetTableName,
                              TargetTableKind targetTableKind,
                              const tabledef::TableSchema &targetSchema,
                              const QList<SimpleCondition> &simpleConditions,
                              ValidationMode validationMode) const;

private:
};

namespace database_service {

TaskResult createDatabase(const QString &databaseName);

TaskResult dropDatabase(const QString &databaseName);

TaskResult useDatabase(const QString &databaseName);

SelectRowsResult showDatabases();

} // namespace database_service

namespace table_service {

TaskResult createTable(const QString &tableName,
                       const tabledef::TableSchema &schema);

TaskResult dropTable(const QString &tableName);

TaskResult addColumn(const QString &tableName,
                     const ColumnDefinition &definition);

TaskResult deleteColumn(const QString &tableName,
                        const QString &columnName);

TaskResult modifyColumn(const QString &tableName,
                        const QString &columnName,
                        const ColumnDefinition &definition);

TaskResult addConstraint(const QString &tableName,
                         const tabledef::Constraint &constraint);

TaskResult modifyConstraint(const QString &tableName,
                            const QString &constraintName,
                            const tabledef::Constraint &constraint);

TaskResult deleteConstraint(const QString &tableName,
                         const QString &constraintName);

SelectRowsResult showTables();

TextResult describeTable(const QString &tableName);

TextResult showCreateTable(const QString &tableName);

} // namespace table_service

namespace tuple_service {

SelectRowsResult selectRows(const QString &tableName,
                            const QStringList &projectionColumns,
                            const QList<SimpleCondition> &conditions);

TaskResult insertRows(const QString &tableName,
                      const QList<QMap<QString, QString>> &rows);

TaskResult deleteRows(const QString &tableName,
                      const QList<SimpleCondition> &conditions);

TaskResult updateRows(const QString &tableName,
                      const QMap<QString, QString> &assignmentMap,
                      const QList<SimpleCondition> &conditions);

} // namespace tuple_service

} // namespace service

 // 这里是因为service_common会用到service.h中定义的内容
 // 但是service.h又需要包含service_common.h中定义的内容，所以放在最后面避免循环依赖问题

#include "../utils/service_common/service_common.h"

#endif // SERVICE_SERVICE_H
