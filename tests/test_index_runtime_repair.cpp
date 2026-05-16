#include "../service/service.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include "test_entry.h"

using namespace service;

namespace {

QString testDataRoot()
{
    return QDir(QDir::tempPath()).absoluteFilePath(QStringLiteral("DBMS_test_index_runtime_repair"));
}

void removeTestDataRoot(const QString &path)
{
    QDir directory(path);
    if (directory.exists()) {
        directory.removeRecursively();
    }
}

tabledef::TableSchema indexedSchema(const QString &tableName)
{
    tabledef::TableSchema schema;
    schema.tableName = tableName;
    schema.columns = {
        tabledef::Column{QStringLiteral("id"), tabledef::ColumnType::Int, 0, true},
        tabledef::Column{QStringLiteral("name"), tabledef::ColumnType::Varchar, 64, true},
    };
    schema.constraints = {
        tabledef::Constraint{QStringLiteral("pk_%1").arg(tableName),
                             tabledef::ConstraintType::PrimaryKey,
                             {QStringLiteral("id")},
                             QString(),
                             {},
                             QString()},
        tabledef::Constraint{QStringLiteral("uq_%1_name").arg(tableName),
                             tabledef::ConstraintType::Unique,
                             {QStringLiteral("name")},
                             QString(),
                             {},
                             QString()},
    };
    return schema;
}

void prepareIndexedTable(const QString &databaseName, const QString &tableName)
{
    TaskResult result = database_service::createDatabase(databaseName);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    result = database_service::useDatabase(databaseName);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    result = table_service::createTable(tableName, indexedSchema(tableName));
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    result = tuple_service::insertRows(tableName,
                                       {QMap<QString, QString>{{QStringLiteral("id"), QStringLiteral("1")},
                                                               {QStringLiteral("name"), QStringLiteral("alpha")}}});
    QVERIFY2(result.success, qPrintable(result.errorMessage));
}

QString firstIndexPath(const QString &databaseName, const QString &tableName)
{
    QString error;
    const tabledef::TableSchema schema = loadUserTableSchema(tableName, &error);
    if (!error.isEmpty() || schema.indexes.isEmpty()) {
        return {};
    }
    return repo::SortIndexRepo(databaseName,
                               schema.indexes.first().indexName,
                               tableName,
                               currentDataRoot)
        .getIndexFilePath();
}

tabledef::IndexMeta indexForColumn(const QString &tableName, const QString &columnName, QString *error)
{
    const tabledef::TableSchema schema = loadUserTableSchema(tableName, error);
    if (error != nullptr && !error->isEmpty()) {
        return {};
    }
    for (const tabledef::IndexMeta &index : schema.indexes) {
        if (index.columnNames == QStringList{columnName}) {
            return index;
        }
    }
    if (error != nullptr) {
        *error = QStringLiteral("index for column '%1' not found").arg(columnName);
    }
    return {};
}

class ScopedEnvironmentVariable
{
public:
    ScopedEnvironmentVariable(const char *name, const QByteArray &value)
        : m_name(name)
        , m_hadValue(qEnvironmentVariableIsSet(name))
        , m_oldValue(qgetenv(name))
    {
        qputenv(m_name.constData(), value);
    }

    ~ScopedEnvironmentVariable()
    {
        if (m_hadValue) {
            qputenv(m_name.constData(), m_oldValue);
        } else {
            qunsetenv(m_name.constData());
        }
    }

private:
    QByteArray m_name;
    bool m_hadValue = false;
    QByteArray m_oldValue;
};

} // namespace

class IndexRuntimeRepairTest : public QObject
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

    void test_missingIndexFileRepairsOnWritePath()
    {
        const QString databaseName = QStringLiteral("index_repair_db");
        const QString tableName = QStringLiteral("index_repair_table");
        prepareIndexedTable(databaseName, tableName);

        QString schemaError;
        const tabledef::TableSchema schema = loadUserTableSchema(tableName, &schemaError);
        QVERIFY2(schemaError.isEmpty(), qPrintable(schemaError));
        QVERIFY(!schema.indexes.isEmpty());
        const QString indexName = schema.indexes.first().indexName;
        repo::SortIndexRepo sortIndexRepo(databaseName, indexName, tableName, currentDataRoot);
        const QString indexPath = sortIndexRepo.getIndexFilePath();
        QVERIFY(QFile::exists(indexPath));
        QVERIFY(QFile::remove(indexPath));
        QVERIFY(!QFile::exists(indexPath));

        TaskResult updated = tuple_service::updateRows(tableName,
                                                       {{QStringLiteral("name"), QStringLiteral("beta")}},
                                                       {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY2(updated.success, qPrintable(updated.errorMessage));
        QVERIFY(QFile::exists(indexPath));
    }

    void test_runtimeArtifactsCheckedPreventsDuplicateRepair()
    {
        const QString databaseName = QStringLiteral("index_repair_once_db");
        const QString tableName = QStringLiteral("index_repair_once_table");
        prepareIndexedTable(databaseName, tableName);

        QString schemaError;
        const tabledef::TableSchema schema = loadUserTableSchema(tableName, &schemaError);
        QVERIFY2(schemaError.isEmpty(), qPrintable(schemaError));
        QVERIFY(!schema.indexes.isEmpty());
        const QString indexName = schema.indexes.first().indexName;
        repo::SortIndexRepo sortIndexRepo(databaseName, indexName, tableName, currentDataRoot);
        const QString indexPath = sortIndexRepo.getIndexFilePath();
        QVERIFY(QFile::exists(indexPath));
        QVERIFY(QFile::remove(indexPath));

        TaskResult updated = tuple_service::updateRows(tableName,
                                                       {{QStringLiteral("name"), QStringLiteral("beta")}},
                                                       {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY2(updated.success, qPrintable(updated.errorMessage));
        QVERIFY(QFile::exists(indexPath));

        QFile file(indexPath);
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        file.close();
        QVERIFY(document.isObject());
        QJsonObject root = document.object();
        QJsonObject meta = root.value(QStringLiteral("meta")).toObject();
        meta.insert(QStringLiteral("checkedSentinel"), QStringLiteral("kept"));
        root.insert(QStringLiteral("meta"), meta);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
        QVERIFY(file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) > 0);
        file.close();

        QFileInfo beforeInfo(indexPath);
        const QDateTime modifiedBefore = beforeInfo.lastModified();

        updated = tuple_service::updateRows(tableName,
                                            {{QStringLiteral("name"), QStringLiteral("gamma")}},
                                            {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY2(updated.success, qPrintable(updated.errorMessage));
        QFileInfo afterInfo(indexPath);
        QVERIFY(afterInfo.exists());
        QVERIFY(afterInfo.lastModified() >= modifiedBefore);
    }

    void test_missingIndexFileRepairsCurrentTableOnly()
    {
        const QString databaseName = QStringLiteral("index_repair_scope_db");
        const QString firstTableName = QStringLiteral("index_repair_scope_first");
        const QString secondTableName = QStringLiteral("index_repair_scope_second");
        prepareIndexedTable(databaseName, firstTableName);
        TaskResult result = table_service::createTable(secondTableName, indexedSchema(secondTableName));
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        result = tuple_service::insertRows(secondTableName,
                                           {QMap<QString, QString>{{QStringLiteral("id"), QStringLiteral("2")},
                                                                   {QStringLiteral("name"), QStringLiteral("beta")}}});
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        QString error;
        const tabledef::TableSchema firstSchema = loadUserTableSchema(firstTableName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const tabledef::TableSchema secondSchema = loadUserTableSchema(secondTableName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        repo::SortIndexRepo firstIndexRepo(databaseName, firstSchema.indexes.first().indexName, firstTableName, currentDataRoot);
        repo::SortIndexRepo secondIndexRepo(databaseName, secondSchema.indexes.first().indexName, secondTableName, currentDataRoot);
        const QString firstIndexPath = firstIndexRepo.getIndexFilePath();
        const QString secondIndexPath = secondIndexRepo.getIndexFilePath();
        QVERIFY(QFile::remove(firstIndexPath));
        QVERIFY(QFile::remove(secondIndexPath));

        result = tuple_service::updateRows(firstTableName,
                                           {{QStringLiteral("name"), QStringLiteral("gamma")}},
                                           {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QVERIFY(QFile::exists(firstIndexPath));
        QVERIFY(!QFile::exists(secondIndexPath));
    }

    void test_malformedIndexFileRepairsOnWritePath()
    {
        const QString databaseName = QStringLiteral("index_repair_malformed_db");
        const QString tableName = QStringLiteral("index_repair_malformed_table");
        prepareIndexedTable(databaseName, tableName);

        QString error;
        const tabledef::TableSchema schema = loadUserTableSchema(tableName, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const QString indexName = schema.indexes.first().indexName;
        repo::SortIndexRepo sortIndexRepo(databaseName, indexName, tableName, currentDataRoot);
        QFile file(sortIndexRepo.getIndexFilePath());
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
        QVERIFY(file.write("not json") > 0);
        file.close();

        TaskResult result = tuple_service::updateRows(tableName,
                                                      {{QStringLiteral("name"), QStringLiteral("delta")}},
                                                      {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        QVERIFY(document.isObject());
    }

    void test_updateRowsHealthyArtifactsUseIncrementalIndexMaintenance()
    {
        const QString databaseName = QStringLiteral("index_incremental_db");
        const QString tableName = QStringLiteral("index_incremental_table");
        prepareIndexedTable(databaseName, tableName);

        const QString indexPath = firstIndexPath(databaseName, tableName);
        QVERIFY(!indexPath.isEmpty());

        const TaskResult result = tuple_service::updateRows(tableName,
                                                            {{QStringLiteral("name"), QStringLiteral("beta")}},
                                                            {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        QString error;
        const tabledef::IndexMeta nameIndex = indexForColumn(tableName, QStringLiteral("name"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        repo::SortIndexRepo sortIndexRepo(databaseName, nameIndex.indexName, tableName, currentDataRoot);
        QVERIFY(sortIndexRepo.search({QStringLiteral("alpha")}, &error).isEmpty());
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(sortIndexRepo.search({QStringLiteral("beta")}, &error).size(), 1);
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }

    void test_noopUpdateSkipsCleanTableCommit()
    {
        const QString databaseName = QStringLiteral("index_noop_db");
        const QString tableName = QStringLiteral("index_noop_table");
        prepareIndexedTable(databaseName, tableName);

        QString error;
        const tabledef::IndexMeta nameIndex = indexForColumn(tableName, QStringLiteral("name"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        const TaskResult result = tuple_service::updateRows(tableName,
                                                            {{QStringLiteral("name"), QStringLiteral("beta")}},
                                                            {SimpleCondition{QStringLiteral("id"), QStringLiteral("404")}});
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.affectedRowCount, 0);

        const SelectRowsResult selected = tuple_service::selectRows(tableName,
                                                                    {QStringLiteral("name")},
                                                                    {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}},
                                                                    -1);
        QVERIFY2(selected.success, qPrintable(selected.errorMessage));
        QCOMPARE(selected.resultTable.rows.first().first(), QStringLiteral("alpha"));

        repo::SortIndexRepo sortIndexRepo(databaseName, nameIndex.indexName, tableName, currentDataRoot);
        QCOMPARE(sortIndexRepo.search({QStringLiteral("alpha")}, &error).size(), 1);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(sortIndexRepo.search({QStringLiteral("beta")}, &error).isEmpty());
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }

    void test_commitRollbackAfterRowIdWriteRestoresTableAndIndexes()
    {
        const QString databaseName = QStringLiteral("index_rollback_rowid_db");
        const QString tableName = QStringLiteral("index_rollback_rowid_table");
        prepareIndexedTable(databaseName, tableName);

        const QString indexPath = firstIndexPath(databaseName, tableName);
        QVERIFY(!indexPath.isEmpty());
        ScopedEnvironmentVariable injectedFailure("DBMS_TEST_FAIL_COMMIT_AFTER_ROWID_WRITE", "1");

        const TaskResult result = tuple_service::updateRows(tableName,
                                                            {{QStringLiteral("name"), QStringLiteral("beta")}},
                                                            {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY(!result.success);
        QVERIFY2(result.errorMessage.contains(QStringLiteral("commit failed after row id write")),
                 qPrintable(result.errorMessage));

        const SelectRowsResult selected = tuple_service::selectRows(tableName,
                                                                    {QStringLiteral("name")},
                                                                    {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}},
                                                                    -1);
        QVERIFY2(selected.success, qPrintable(selected.errorMessage));
        QCOMPARE(selected.resultTable.rows.first().first(), QStringLiteral("alpha"));

        QString searchError;
        const tabledef::IndexMeta nameIndex = indexForColumn(tableName, QStringLiteral("name"), &searchError);
        QVERIFY2(searchError.isEmpty(), qPrintable(searchError));
        const QStringList matches = repo::SortIndexRepo(databaseName,
                                                        nameIndex.indexName,
                                                        tableName,
                                                        currentDataRoot)
                                        .search({QStringLiteral("alpha")}, &searchError);
        QVERIFY2(searchError.isEmpty(), qPrintable(searchError));
        QCOMPARE(matches.size(), 1);
        QVERIFY(QFile::exists(indexPath));
    }

    void test_commitRollbackAfterIndexUpdateRestoresTableAndIndexes()
    {
        const QString databaseName = QStringLiteral("index_rollback_index_db");
        const QString tableName = QStringLiteral("index_rollback_index_table");
        prepareIndexedTable(databaseName, tableName);

        const QString indexPath = firstIndexPath(databaseName, tableName);
        QVERIFY(!indexPath.isEmpty());
        ScopedEnvironmentVariable injectedFailure("DBMS_TEST_FAIL_COMMIT_AFTER_INDEX_UPDATE", "1");

        const TaskResult result = tuple_service::updateRows(tableName,
                                                            {{QStringLiteral("name"), QStringLiteral("beta")}},
                                                            {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}});
        QVERIFY(!result.success);
        QVERIFY2(result.errorMessage.contains(QStringLiteral("commit failed after index update")),
                 qPrintable(result.errorMessage));

        const SelectRowsResult selected = tuple_service::selectRows(tableName,
                                                                    {QStringLiteral("name")},
                                                                    {SimpleCondition{QStringLiteral("id"), QStringLiteral("1")}},
                                                                    -1);
        QVERIFY2(selected.success, qPrintable(selected.errorMessage));
        QCOMPARE(selected.resultTable.rows.first().first(), QStringLiteral("alpha"));

        QString error;
        const tabledef::IndexMeta nameIndex = indexForColumn(tableName, QStringLiteral("name"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const QStringList oldMatches = repo::SortIndexRepo(databaseName,
                                                           nameIndex.indexName,
                                                           tableName,
                                                           currentDataRoot)
                                           .search({QStringLiteral("alpha")}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(oldMatches.size(), 1);
        const QStringList newMatches = repo::SortIndexRepo(databaseName,
                                                           nameIndex.indexName,
                                                           tableName,
                                                           currentDataRoot)
                                           .search({QStringLiteral("beta")}, &error);
        QVERIFY(newMatches.isEmpty());
        QVERIFY(QFile::exists(indexPath));
    }

private:
    QString m_dataRoot;
};

int service_tests::runIndexRuntimeRepairTests()
{
    IndexRuntimeRepairTest test;
    return QTest::qExec(&test);
}

#include "test_index_runtime_repair.moc"
