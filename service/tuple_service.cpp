#include "service.h"

namespace service::tuple_service {

SelectRowsResult selectRows(const QString &tableName,
                            const QStringList &projectionColumns,
                            const QList<SimpleCondition> &conditions,
                            int limit)
{
    TableDmlService dmlService;
    const QString databaseName = normalizeDatabaseName(QString());
    return dmlService.selectRows(databaseName,
                                 tableName,
                                 TargetTableKind::TableDat,
                                 loadUserTableSchema(tableName, nullptr),
                                 projectionColumns.isEmpty() ? QStringList{QStringLiteral("*")} : projectionColumns,
                                 conditions,
                                 limit);
}

TaskResult insertRows(const QString &tableName,
                      const QList<QMap<QString, QString>> &rows)
{
    TaskResult result;
    TableDmlService dmlService;
    QString error;
    const tabledef::TableSchema schema = loadUserTableSchema(tableName, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    const QString databaseName = normalizeDatabaseName(QString());
    const TableDmlResult dmlResult = dmlService.insertRows(databaseName,
                                                          tableName,
                                                          TargetTableKind::TableDat,
                                                          schema,
                                                          rows,
                                                          ValidationMode::UserData);
    result.success = dmlResult.success;
    result.errorMessage = dmlResult.errorMessage;
    result.affectedRowCount = dmlResult.affectedRowCount;
    return result;
}

TaskResult deleteRows(const QString &tableName,
                      const QList<SimpleCondition> &conditions)
{
    TaskResult result;
    TableDmlService dmlService;
    QString error;
    const tabledef::TableSchema schema = loadUserTableSchema(tableName, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    const QString databaseName = normalizeDatabaseName(QString());
    const TableDmlResult dmlResult = dmlService.deleteRows(databaseName,
                                                          tableName,
                                                          TargetTableKind::TableDat,
                                                          schema,
                                                          conditions,
                                                          ValidationMode::UserData);
    result.success = dmlResult.success;
    result.errorMessage = dmlResult.errorMessage;
    result.affectedRowCount = dmlResult.affectedRowCount;
    return result;
}

TaskResult updateRows(const QString &tableName,
                      const QMap<QString, QString> &assignmentMap,
                      const QList<SimpleCondition> &conditions)
{
    TaskResult result;
    TableDmlService dmlService;
    QString error;
    const tabledef::TableSchema schema = loadUserTableSchema(tableName, &error);
    if (!error.isEmpty()) {
        result.errorMessage = error;
        return result;
    }

    const QString databaseName = normalizeDatabaseName(QString());
    const TableDmlResult dmlResult = dmlService.updateRows(databaseName,
                                                          tableName,
                                                          TargetTableKind::TableDat,
                                                          schema,
                                                          assignmentMap,
                                                          conditions,
                                                          ValidationMode::UserData);
    result.success = dmlResult.success;
    result.errorMessage = dmlResult.errorMessage;
    result.affectedRowCount = dmlResult.affectedRowCount;
    return result;
}

} // namespace service::tuple_service
