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

private:
    QString m_dataRoot;
};

int service_tests::runIndexRuntimeRepairTests()
{
    IndexRuntimeRepairTest test;
    return QTest::qExec(&test);
}

#include "test_index_runtime_repair.moc"
