#include "../service/service.h"
#include "../constants/thread_perf_def.h"
#include "../utils/thread_runtime/lock_manager.h"

#include <QDir>
#include <future>
#include <QtTest>

#include "test_entry.h"

using namespace service;

namespace {

QString testDataRoot()
{
    return QDir(QDir::tempPath()).absoluteFilePath(QStringLiteral("DBMS_test_table_runtime_pipeline"));
}

void removeTestDataRoot(const QString &path)
{
    QDir directory(path);
    if (directory.exists()) {
        directory.removeRecursively();
    }
}

tabledef::Column column(const QString &name, tabledef::ColumnType type, bool notNull = false)
{
    return tabledef::Column{name, type, 64, notNull, QString(), false, QString()};
}

tabledef::Constraint primaryKey(const QString &name, const QStringList &columns)
{
    return tabledef::Constraint{name, tabledef::ConstraintType::PrimaryKey, columns, QString(), {}, QString()};
}

tabledef::Constraint foreignKey(const QString &name,
                                const QStringList &columns,
                                const QString &referencedTable,
                                const QStringList &referencedColumns)
{
    tabledef::Constraint constraint;
    constraint.name = name;
    constraint.type = tabledef::ConstraintType::ForeignKey;
    constraint.columns = columns;
    constraint.referencedTable = referencedTable;
    constraint.referencedColumns = referencedColumns;
    constraint.onDeleteAction = tabledef::ForeignKeyAction::Cascade;
    constraint.onUpdateAction = tabledef::ForeignKeyAction::Cascade;
    return constraint;
}

tabledef::TableSchema parentSchema(const QString &tableName)
{
    tabledef::TableSchema schema;
    schema.tableName = tableName;
    schema.columns = {
        column(QStringLiteral("id"), tabledef::ColumnType::Int, true),
        column(QStringLiteral("name"), tabledef::ColumnType::Varchar),
    };
    schema.constraints = {primaryKey(QStringLiteral("pk_%1").arg(tableName), {QStringLiteral("id")})};
    return schema;
}

tabledef::TableSchema childSchema(const QString &tableName, const QString &parentTableName)
{
    tabledef::TableSchema schema;
    schema.tableName = tableName;
    schema.columns = {
        column(QStringLiteral("id"), tabledef::ColumnType::Int, true),
        column(QStringLiteral("parent_id"), tabledef::ColumnType::Int, true),
        column(QStringLiteral("note"), tabledef::ColumnType::Varchar),
    };
    schema.constraints = {
        primaryKey(QStringLiteral("pk_%1").arg(tableName), {QStringLiteral("id")}),
        foreignKey(QStringLiteral("fk_%1_parent").arg(tableName),
                   {QStringLiteral("parent_id")},
                   parentTableName,
                   {QStringLiteral("id")}),
    };
    return schema;
}

void prepareCascadeTables(const QString &databaseName,
                          const QString &parentTableName,
                          const QString &childTableName)
{
    TaskResult result = database_service::createDatabase(databaseName);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    result = database_service::useDatabase(databaseName);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    result = table_service::createTable(parentTableName, parentSchema(parentTableName));
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    result = table_service::createTable(childTableName, childSchema(childTableName, parentTableName));
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    result = tuple_service::insertRows(parentTableName,
                                       {QMap<QString, QString>{{QStringLiteral("id"), QStringLiteral("1")},
                                                               {QStringLiteral("name"), QStringLiteral("parent")}}});
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    result = tuple_service::insertRows(childTableName,
                                       {QMap<QString, QString>{{QStringLiteral("id"), QStringLiteral("10")},
                                                               {QStringLiteral("parent_id"), QStringLiteral("1")},
                                                               {QStringLiteral("note"), QStringLiteral("child")}}});
    QVERIFY2(result.success, qPrintable(result.errorMessage));
}

} // namespace

class TableRuntimePipelineTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_dataRoot = testDataRoot();
        removeTestDataRoot(m_dataRoot);
        setDataRoot(m_dataRoot);
        currentDatabase.clear();
    }

    void cleanup()
    {
        currentDatabase.clear();
        removeTestDataRoot(m_dataRoot);
        setDataRoot(QString());
    }

    void test_multiTableCascadeCommitsEachAffectedTable()
    {
        const QString databaseName = QStringLiteral("pipeline_db");
        const QString parentTableName = QStringLiteral("pipeline_parent");
        const QString childTableName = QStringLiteral("pipeline_child");
        prepareCascadeTables(databaseName, parentTableName, childTableName);

        TaskResult updated = tuple_service::updateRows(parentTableName,
                                                       {{QStringLiteral("id"), QStringLiteral("2")}},
                                                       {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY2(updated.success, qPrintable(updated.errorMessage));

        SelectRowsResult selected = tuple_service::selectRows(childTableName,
                                                              {QStringLiteral("parent_id")},
                                                              {SimpleCondition{QStringLiteral("id"), QStringLiteral("10")}},
                                                              -1);
        QVERIFY2(selected.success, qPrintable(selected.errorMessage));
        QCOMPARE(selected.resultTable.rows.size(), 1);
        QCOMPARE(selected.resultTable.rows.first().first(), QStringLiteral("2"));
    }

    void test_fkCascadeMutationLocksAllDependentTablesAndReleasesOnFailure()
    {
        const QString databaseName = QStringLiteral("pipeline_lock_order_db");
        const QString parentTableName = QStringLiteral("pipeline_lock_order_parent");
        const QString childTableName = QStringLiteral("pipeline_lock_order_child");
        prepareCascadeTables(databaseName, parentTableName, childTableName);

        std::promise<QString> lockAcquired;
        std::promise<void> releaseSignal;
        std::shared_future<void> releaseLock = releaseSignal.get_future().share();
        std::future<void> blockerFuture = std::async(std::launch::async, [&]() {
            QString lockError;
            thread_runtime::ScopedRuntimeLock childLock = thread_runtime::RuntimeLockManager::instance().acquireLock(
                thread_runtime::tableLockKey(currentDataRoot, databaseName, childTableName),
                thread_runtime::RuntimeLockMode::Exclusive,
                threadperf::kTableLockAcquireTimeoutMs,
                &lockError);
            lockAcquired.set_value(childLock.isValid() ? QString() : lockError);
            if (childLock.isValid()) {
                releaseLock.wait();
            }
        });
        const QString lockError = lockAcquired.get_future().get();
        QVERIFY2(lockError.isEmpty(), qPrintable(lockError));

        TaskResult updated = tuple_service::updateRows(parentTableName,
                                                       {{QStringLiteral("id"), QStringLiteral("2")}},
                                                       {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY(!updated.success);
        QVERIFY(updated.errorMessage.contains(QStringLiteral("runtime lock")));

        releaseSignal.set_value();
        blockerFuture.wait();
        QString error;
        thread_runtime::ScopedRuntimeLock parentLock = thread_runtime::RuntimeLockManager::instance().acquireLock(
            thread_runtime::tableLockKey(currentDataRoot, databaseName, parentTableName),
            thread_runtime::RuntimeLockMode::Exclusive,
            threadperf::kTableLockAcquireTimeoutMs,
            &error);
        QVERIFY2(parentLock.isValid(), qPrintable(error));
    }

private:
    QString m_dataRoot;
};

int service_tests::runTableRuntimePipelineTests()
{
    TableRuntimePipelineTest test;
    return QTest::qExec(&test);
}

#include "test_table_runtime_pipeline.moc"
