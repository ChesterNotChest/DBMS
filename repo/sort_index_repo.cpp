#include "repo.h"

#include <algorithm>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QReadLocker>
#include <QReadWriteLock>
#include <QSet>
#include <QWriteLocker>
#include <QVector>

namespace {

struct IndexEntry
{
    QStringList keyValues;
    QStringList rowLocators;
};

QVector<IndexEntry> loadEntriesFromDocument(const QJsonObject &document);

struct BPlusNode
{
    int id = -1;
    bool leaf = true;
    QStringList keys;
    QList<QList<QStringList>> values;
    QList<int> children;
    int next = -1;
};

QMap<QString, QJsonObject> &indexDocumentCache()
{
    static QMap<QString, QJsonObject> cache;
    return cache;
}

QMap<QString, QMap<QString, QStringList>> &indexLookupCache()
{
    static QMap<QString, QMap<QString, QStringList>> cache;
    return cache;
}

QMap<QString, QVector<IndexEntry>> &indexEntriesCache()
{
    static QMap<QString, QVector<IndexEntry>> cache;
    return cache;
}

QReadWriteLock &indexCacheLock()
{
    static QReadWriteLock lock;
    return lock;
}

void removeIndexCaches(const QString &path)
{
    QWriteLocker locker(&indexCacheLock());
    indexDocumentCache().remove(path);
    indexLookupCache().remove(path);
    indexEntriesCache().remove(path);
}

QString keySignature(const QStringList &values)
{
    QString signature;
    for (const QString &value : values) {
        signature.append(QString::number(value.size()));
        signature.append(QLatin1Char(':'));
        signature.append(value);
        signature.append(QLatin1Char(';'));
    }
    return signature;
}

bool decodeKeySignature(const QString &signature, QStringList *values)
{
    if (values != nullptr) {
        values->clear();
    }

    int position = 0;
    while (position < signature.size()) {
        const int separatorIndex = signature.indexOf(QLatin1Char(':'), position);
        if (separatorIndex < 0) {
            return false;
        }

        bool ok = false;
        const int valueLength = signature.mid(position, separatorIndex - position).toInt(&ok);
        if (!ok || valueLength < 0) {
            return false;
        }

        position = separatorIndex + 1;
        if (position + valueLength > signature.size()) {
            return false;
        }

        if (values != nullptr) {
            values->append(signature.mid(position, valueLength));
        }
        position += valueLength;

        if (position >= signature.size() || signature.at(position) != QLatin1Char(';')) {
            return false;
        }
        ++position;
    }

    return true;
}

bool lessKeyValues(const QStringList &lhs, const QStringList &rhs)
{
    const int count = qMin(lhs.size(), rhs.size());
    for (int index = 0; index < count; ++index) {
        if (lhs.at(index) < rhs.at(index)) {
            return true;
        }
        if (lhs.at(index) > rhs.at(index)) {
            return false;
        }
    }
    return lhs.size() < rhs.size();
}

bool keyContainsEmptyValue(const QStringList &values)
{
    for (const QString &value : values) {
        if (value.isEmpty()) {
            return true;
        }
    }
    return false;
}

QJsonArray toJsonArray(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values) {
        array.append(value);
    }
    return array;
}

QStringList fromJsonArray(const QJsonArray &array)
{
    QStringList values;
    values.reserve(array.size());
    for (const QJsonValue &value : array) {
        values.append(value.toString());
    }
    return values;
}

QJsonObject serializeNode(const BPlusNode &node)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), node.id);
    object.insert(QStringLiteral("leaf"), node.leaf);
    object.insert(QStringLiteral("keys"), toJsonArray(node.keys));

    QJsonArray valuesArray;
    for (const QList<QStringList> &bucket : node.values) {
        QJsonArray bucketArray;
        for (const QStringList &rowLocators : bucket) {
            bucketArray.append(toJsonArray(rowLocators));
        }
        valuesArray.append(bucketArray);
    }
    object.insert(QStringLiteral("values"), valuesArray);

    QJsonArray childrenArray;
    for (int child : node.children) {
        childrenArray.append(child);
    }
    object.insert(QStringLiteral("children"), childrenArray);
    object.insert(QStringLiteral("next"), node.next);
    return object;
}

BPlusNode deserializeNode(const QJsonObject &object)
{
    BPlusNode node;
    node.id = object.value(QStringLiteral("id")).toInt(-1);
    node.leaf = object.value(QStringLiteral("leaf")).toBool(true);
    node.keys = fromJsonArray(object.value(QStringLiteral("keys")).toArray());

    const QJsonArray valuesArray = object.value(QStringLiteral("values")).toArray();
    node.values.reserve(valuesArray.size());
    for (const QJsonValue &bucketValue : valuesArray) {
        QList<QStringList> bucket;
        const QJsonArray bucketArray = bucketValue.toArray();
        bucket.reserve(bucketArray.size());
        for (const QJsonValue &locatorsValue : bucketArray) {
            bucket.append(fromJsonArray(locatorsValue.toArray()));
        }
        node.values.append(bucket);
    }

    const QJsonArray childrenArray = object.value(QStringLiteral("children")).toArray();
    node.children.reserve(childrenArray.size());
    for (const QJsonValue &childValue : childrenArray) {
        node.children.append(childValue.toInt());
    }
    node.next = object.value(QStringLiteral("next")).toInt(-1);
    return node;
}

QVector<IndexEntry> buildEntries(const repo::TableData &table,
                                 const QStringList &rowLocators,
                                 const QStringList &indexColumns,
                                 QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    QVector<IndexEntry> entries;
    entries.reserve(table.rows.size());
    for (int rowIndex = 0; rowIndex < table.rows.size(); ++rowIndex) {
        if (rowIndex >= rowLocators.size()) {
            if (error != nullptr) {
                *error = QStringLiteral("row locator count does not match row count");
            }
            return {};
        }
        const repo::TableRow &row = table.rows.at(rowIndex);
        IndexEntry entry;
        entry.rowLocators = {rowLocators.at(rowIndex)};
        entry.keyValues.reserve(indexColumns.size());
        for (const QString &columnName : indexColumns) {
            const int columnIndex = table.columns.indexOf(columnName);
            if (columnIndex < 0) {
                if (error != nullptr) {
                    *error = QStringLiteral("column '%1' does not exist").arg(columnName);
                }
                return {};
            }
            entry.keyValues.append(row.value(columnIndex));
        }
        entries.append(entry);
    }

    std::sort(entries.begin(), entries.end(), [](const IndexEntry &lhs, const IndexEntry &rhs) {
        if (lessKeyValues(lhs.keyValues, rhs.keyValues)) {
            return true;
        }
        if (lessKeyValues(rhs.keyValues, lhs.keyValues)) {
            return false;
        }
        return keySignature(lhs.rowLocators) < keySignature(rhs.rowLocators);
    });

    return entries;
}

QJsonObject buildTreeDocument(const tabledef::IndexMeta &index,
                              const QString &sourceTable,
                              const QVector<IndexEntry> &entries,
                              QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    constexpr int kOrder = 4;
    const int maxLeafKeys = qMax(1, kOrder - 1);

    QList<BPlusNode> allNodes;
    QList<int> currentLevelIds;
    int nextId = 0;
    int previousLeafId = -1;

    auto nodeById = [&allNodes](int id) -> BPlusNode * {
        for (BPlusNode &node : allNodes) {
            if (node.id == id) {
                return &node;
            }
        }
        return nullptr;
    };

    for (int offset = 0; offset < entries.size(); offset += maxLeafKeys) {
        BPlusNode leaf;
        leaf.id = nextId++;
        leaf.leaf = true;
        leaf.next = -1;
        const int upper = qMin(entries.size(), offset + maxLeafKeys);
        for (int index = offset; index < upper; ++index) {
            const IndexEntry &entry = entries.at(index);
            const QString key = keySignature(entry.keyValues);
            if (!leaf.keys.isEmpty() && leaf.keys.last() == key) {
                leaf.values.last().append(entry.rowLocators);
            } else {
                leaf.keys.append(key);
                QList<QStringList> bucket;
                bucket.append(entry.rowLocators);
                leaf.values.append(bucket);
            }
        }
        if (previousLeafId >= 0) {
            BPlusNode *previousLeaf = nodeById(previousLeafId);
            if (previousLeaf == nullptr) {
                if (error != nullptr) {
                    *error = QStringLiteral("failed to link index leaf chain");
                }
                return {};
            }
            previousLeaf->next = leaf.id;
        }
        previousLeafId = leaf.id;
        currentLevelIds.append(leaf.id);
        allNodes.append(leaf);
    }

    if (allNodes.isEmpty()) {
        BPlusNode root;
        root.id = nextId++;
        allNodes.append(root);
        currentLevelIds.append(root.id);
    }

    while (currentLevelIds.size() > 1) {
        QList<int> nextLevelIds;
        for (int offset = 0; offset < currentLevelIds.size(); offset += kOrder) {
            BPlusNode parent;
            parent.id = nextId++;
            parent.leaf = false;
            const int upper = qMin(currentLevelIds.size(), offset + kOrder);
            for (int index = offset; index < upper; ++index) {
                const int childId = currentLevelIds.at(index);
                parent.children.append(childId);
                BPlusNode *child = nodeById(childId);
                if (child == nullptr) {
                    if (error != nullptr) {
                        *error = QStringLiteral("failed to build index tree");
                    }
                    return {};
                }
                if (index > offset && !child->keys.isEmpty()) {
                    parent.keys.append(child->keys.first());
                }
            }
            nextLevelIds.append(parent.id);
            allNodes.append(parent);
        }
        currentLevelIds = nextLevelIds;
    }

    const int rootId = currentLevelIds.first();

    QJsonObject document;
    QJsonObject meta;
    meta.insert(QStringLiteral("indexName"), index.indexName);
    meta.insert(QStringLiteral("sourceTable"), sourceTable);
    meta.insert(QStringLiteral("columnNames"), toJsonArray(index.columnNames));
    meta.insert(QStringLiteral("isUnique"), index.isUnique);
    meta.insert(QStringLiteral("order"), kOrder);
    document.insert(QStringLiteral("meta"), meta);
    document.insert(QStringLiteral("rootId"), rootId);

    QJsonArray nodesArray;
    for (const BPlusNode &node : allNodes) {
        nodesArray.append(serializeNode(node));
    }
    document.insert(QStringLiteral("nodes"), nodesArray);

    return document;
}

bool writeIndexDocument(const QString &path, const QJsonObject &document, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (error != nullptr) {
            *error = QStringLiteral("failed to open '%1' for writing").arg(path);
        }
        return false;
    }

    const qint64 written = file.write(QJsonDocument(document).toJson(QJsonDocument::Indented));
    if (written < 0) {
        if (error != nullptr) {
            *error = QStringLiteral("failed to write '%1'").arg(path);
        }
        return false;
    }

    const QVector<IndexEntry> entries = loadEntriesFromDocument(document);
    {
        QWriteLocker locker(&indexCacheLock());
        indexDocumentCache().insert(path, document);
        indexLookupCache().remove(path);
        indexEntriesCache().insert(path, entries);
    }
    return true;
}

bool readIndexDocument(const QString &path, QJsonObject *document, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    if (document == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("index document output pointer cannot be null");
        }
        return false;
    }

    QFileInfo fileInfo(path);
    if (!fileInfo.exists()) {
        removeIndexCaches(path);
        if (error != nullptr) {
            *error = QStringLiteral("index file '%1' does not exist").arg(path);
        }
        return false;
    }

    {
        QReadLocker locker(&indexCacheLock());
        const auto cached = indexDocumentCache().constFind(path);
        if (cached != indexDocumentCache().constEnd()) {
            *document = cached.value();
            return true;
        }
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error != nullptr) {
            *error = QStringLiteral("failed to open '%1' for reading").arg(path);
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument parsed = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("failed to parse '%1': %2").arg(path, parseError.errorString());
        }
        return false;
    }

    *document = parsed.object();
    {
        QWriteLocker locker(&indexCacheLock());
        indexDocumentCache().insert(path, *document);
    }
    return true;
}

QVector<IndexEntry> loadEntriesFromDocument(const QJsonObject &document)
{
    QVector<IndexEntry> entries;
    const QJsonArray nodes = document.value(QStringLiteral("nodes")).toArray();
    QMap<int, BPlusNode> nodesById;
    for (const QJsonValue &nodeValue : nodes) {
        const BPlusNode node = deserializeNode(nodeValue.toObject());
        nodesById.insert(node.id, node);
    }

    int rootId = document.value(QStringLiteral("rootId")).toInt(-1);
    if (rootId < 0 && !nodesById.isEmpty()) {
        rootId = nodesById.firstKey();
    }

    if (!nodesById.contains(rootId)) {
        return entries;
    }

    BPlusNode node = nodesById.value(rootId);
    while (!node.leaf && !node.children.isEmpty()) {
        node = nodesById.value(node.children.first());
    }

    while (true) {
        for (int index = 0; index < node.keys.size(); ++index) {
            QStringList keyValues;
            if (!decodeKeySignature(node.keys.at(index), &keyValues)) {
                return entries;
            }
            const QList<QStringList> &bucket = node.values.at(index);
            for (const QStringList &locators : bucket) {
                entries.append(IndexEntry{keyValues, locators});
            }
        }
        if (node.next < 0 || !nodesById.contains(node.next)) {
            break;
        }
        node = nodesById.value(node.next);
    }

    return entries;
}

bool entryLessThan(const IndexEntry &lhs, const IndexEntry &rhs)
{
    if (lessKeyValues(lhs.keyValues, rhs.keyValues)) {
        return true;
    }
    if (lessKeyValues(rhs.keyValues, lhs.keyValues)) {
        return false;
    }
    return keySignature(lhs.rowLocators) < keySignature(rhs.rowLocators);
}

int findEntryIndex(const QVector<IndexEntry> &entries,
                   const QStringList &keyValues,
                   const QString &rowLocator)
{
    for (int index = 0; index < entries.size(); ++index) {
        const IndexEntry &entry = entries.at(index);
        if (entry.keyValues == keyValues && entry.rowLocators == QStringList{rowLocator}) {
            return index;
        }
    }
    return -1;
}

bool appendEntry(QVector<IndexEntry> *entries,
                const QStringList &keyValues,
                const QString &rowLocator,
                bool isUnique,
                QString *error)
{
    if (entries == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("entries output pointer cannot be null");
        }
        return false;
    }

    if (isUnique && keyContainsEmptyValue(keyValues)) {
        return true;
    }

    for (const IndexEntry &entry : *entries) {
        if (entry.keyValues != keyValues) {
            continue;
        }
        if (entry.rowLocators.contains(rowLocator)) {
            return true;
        }
        if (isUnique) {
            if (error != nullptr) {
                *error = QStringLiteral("unique index already contains duplicate key values");
            }
            return false;
        }
        break;
    }

    entries->append(IndexEntry{keyValues, {rowLocator}});
    std::sort(entries->begin(), entries->end(), entryLessThan);
    return true;
}

bool appendEntries(QVector<IndexEntry> *entries,
                   const QList<QStringList> &keyValuesList,
                   const QStringList &rowLocators,
                   bool isUnique,
                   QString *error)
{
    if (entries == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("entries output pointer cannot be null");
        }
        return false;
    }
    if (keyValuesList.size() != rowLocators.size()) {
        if (error != nullptr) {
            *error = QStringLiteral("index key count does not match row locator count");
        }
        return false;
    }

    QSet<QString> seenUniqueKeys;
    QSet<QString> seenLocatorKeys;
    seenLocatorKeys.reserve(entries->size() + keyValuesList.size());
    for (const IndexEntry &entry : *entries) {
        const QString entryKey = keySignature(entry.keyValues);
        for (const QString &rowLocator : entry.rowLocators) {
            seenLocatorKeys.insert(entryKey + QLatin1Char('|') + rowLocator);
        }
    }
    if (isUnique) {
        seenUniqueKeys.reserve(entries->size() + keyValuesList.size());
        for (const IndexEntry &entry : *entries) {
            if (!keyContainsEmptyValue(entry.keyValues)) {
                seenUniqueKeys.insert(keySignature(entry.keyValues));
            }
        }
    }

    bool changed = false;
    for (int index = 0; index < keyValuesList.size(); ++index) {
        const QStringList keyValues = keyValuesList.at(index);
        const QString rowLocator = rowLocators.at(index);
        if (isUnique && keyContainsEmptyValue(keyValues)) {
            continue;
        }

        const QString key = keySignature(keyValues);
        const QString locatorKey = key + QLatin1Char('|') + rowLocator;
        if (seenLocatorKeys.contains(locatorKey)) {
            continue;
        }
        seenLocatorKeys.insert(locatorKey);

        if (isUnique) {
            if (seenUniqueKeys.contains(key)) {
                if (error != nullptr) {
                    *error = QStringLiteral("unique index already contains duplicate key values");
                }
                return false;
            }
            seenUniqueKeys.insert(key);
        }

        entries->append(IndexEntry{keyValues, {rowLocator}});
        changed = true;
    }

    if (changed) {
        std::sort(entries->begin(), entries->end(), entryLessThan);
    }
    return true;
}

bool removeEntry(QVector<IndexEntry> *entries,
                const QStringList &keyValues,
                const QString &rowLocator,
                bool isUnique,
                QString *error)
{
    if (entries == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("entries output pointer cannot be null");
        }
        return false;
    }

    if (isUnique && keyContainsEmptyValue(keyValues)) {
        return true;
    }

    for (int index = 0; index < entries->size(); ++index) {
        IndexEntry &entry = (*entries)[index];
        if (entry.keyValues != keyValues || !entry.rowLocators.contains(rowLocator)) {
            continue;
        }

        entry.rowLocators.removeAll(rowLocator);
        if (entry.rowLocators.isEmpty()) {
            entries->removeAt(index);
        }
        return true;
    }

    if (error != nullptr) {
        *error = QStringLiteral("index entry for row locator '%1' was not found").arg(rowLocator);
    }
    return false;
}

QStringList rowLocatorsForIndexes(const QStringList &rowLocators, const QList<int> &rowIndexes)
{
    QStringList locators;
    locators.reserve(rowIndexes.size());
    for (int rowIndex : rowIndexes) {
        if (rowIndex < 0 || rowIndex >= rowLocators.size()) {
            continue;
        }
        locators.append(rowLocators.at(rowIndex));
    }
    return locators;
}

QStringList searchEntries(const QJsonObject &document, const QStringList &keyValues, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    const QJsonArray nodes = document.value(QStringLiteral("nodes")).toArray();
    QMap<int, BPlusNode> nodesById;
    for (const QJsonValue &nodeValue : nodes) {
        const BPlusNode node = deserializeNode(nodeValue.toObject());
        nodesById.insert(node.id, node);
    }

    const int rootId = document.value(QStringLiteral("rootId")).toInt(-1);
    if (!nodesById.contains(rootId)) {
        if (error != nullptr) {
            *error = QStringLiteral("index tree root is missing");
        }
        return {};
    }

    BPlusNode node = nodesById.value(rootId);
    while (!node.leaf) {
        int childIndex = 0;
        while (childIndex < node.keys.size()) {
            QStringList separator;
            if (!decodeKeySignature(node.keys.at(childIndex), &separator)) {
                if (error != nullptr) {
                    *error = QStringLiteral("index tree is malformed");
                }
                return {};
            }
            if (lessKeyValues(keyValues, separator)) {
                break;
            }
            ++childIndex;
        }
        if (childIndex >= node.children.size()) {
            childIndex = node.children.size() - 1;
        }
        if (childIndex < 0) {
            if (error != nullptr) {
                *error = QStringLiteral("index tree is malformed");
            }
            return {};
        }
        const int childId = node.children.at(childIndex);
        if (!nodesById.contains(childId)) {
            if (error != nullptr) {
                *error = QStringLiteral("index tree is malformed");
            }
            return {};
        }
        node = nodesById.value(childId);
    }

    QStringList matches;
    for (int index = 0; index < node.keys.size(); ++index) {
        QStringList storedKey;
        if (!decodeKeySignature(node.keys.at(index), &storedKey)) {
            if (error != nullptr) {
                *error = QStringLiteral("index tree is malformed");
            }
            return {};
        }
        if (storedKey == keyValues) {
            for (const QStringList &locators : node.values.at(index)) {
                matches.append(locators);
            }
            break;
        }
    }
    return matches;
}

QMap<QString, QStringList> buildLookupMap(const QJsonObject &document)
{
    QMap<QString, QStringList> lookup;
    const QVector<IndexEntry> entries = loadEntriesFromDocument(document);
    for (const IndexEntry &entry : entries) {
        QStringList &rowLocators = lookup[keySignature(entry.keyValues)];
        rowLocators.append(entry.rowLocators);
    }
    return lookup;
}

QVector<IndexEntry> orderedEntriesForPath(const QString &path, const QJsonObject &document)
{
    {
        QReadLocker locker(&indexCacheLock());
        const auto cached = indexEntriesCache().constFind(path);
        if (cached != indexEntriesCache().constEnd()) {
            return cached.value();
        }
    }
    QVector<IndexEntry> entries = loadEntriesFromDocument(document);
    {
        QWriteLocker locker(&indexCacheLock());
        indexEntriesCache().insert(path, entries);
    }
    return entries;
}

} // namespace

namespace repo {

SortIndexRepo::SortIndexRepo(QString databaseName,
                             QString indexName,
                             QString sourceTable,
                             QString dataRoot)
    : m_databaseName(std::move(databaseName))
    , m_indexName(std::move(indexName))
    , m_sourceTable(std::move(sourceTable))
    , m_store(std::move(dataRoot))
{
}

RepositoryResult SortIndexRepo::createIndex(const QStringList &columns) const
{
    if (m_databaseName.trimmed().isEmpty()) {
        return RepositoryResult::failure(QStringLiteral("database name cannot be empty"));
    }
    if (m_indexName.trimmed().isEmpty()) {
        return RepositoryResult::failure(QStringLiteral("index name cannot be empty"));
    }
    if (m_sourceTable.trimmed().isEmpty()) {
        return RepositoryResult::failure(QStringLiteral("source table cannot be empty"));
    }

    const RepositoryResult directoryReady =
        m_store.ensureDirectory(m_store.getSortIndexDirectory(m_databaseName, m_sourceTable));
    if (!directoryReady.ok) {
        return directoryReady;
    }

    if (m_store.exists(getIndexFilePath())) {
        return RepositoryResult::failure(
            QStringLiteral("sort index '%1' already exists").arg(m_indexName));
    }

    return m_store.createEmptyTable(getIndexFilePath(), columns);
}

RepositoryResult SortIndexRepo::createIndex(const tabledef::IndexMeta &index,
                                           const TableData &table,
                                           const QStringList &rowLocators) const
{
    if (index.indexName.trimmed().isEmpty()) {
        return RepositoryResult::failure(QStringLiteral("index name cannot be empty"));
    }

    const RepositoryResult directoryReady =
        m_store.ensureDirectory(m_store.getSortIndexDirectory(m_databaseName, m_sourceTable));
    if (!directoryReady.ok) {
        return directoryReady;
    }

    QString error;
    const QVector<IndexEntry> entries = buildEntries(table, rowLocators, index.columnNames, &error);
    if (!error.isEmpty()) {
        return RepositoryResult::failure(error);
    }

    QVector<IndexEntry> effectiveEntries = entries;
    if (index.isUnique) {
        effectiveEntries.erase(std::remove_if(effectiveEntries.begin(),
                                              effectiveEntries.end(),
                                              [](const IndexEntry &entry) {
                                                  return keyContainsEmptyValue(entry.keyValues);
                                              }),
                                    effectiveEntries.end());
    }

    if (index.isUnique) {
        QSet<QString> seen;
        for (const IndexEntry &entry : effectiveEntries) {
            const QString signature = keySignature(entry.keyValues);
            if (seen.contains(signature)) {
                return RepositoryResult::failure(
                    QStringLiteral("index '%1' contains duplicate key values").arg(index.indexName));
            }
            seen.insert(signature);
        }
    }

    const QString path = getIndexFilePath();
    if (m_store.exists(path)) {
        return RepositoryResult::failure(QStringLiteral("sort index '%1' already exists").arg(m_indexName));
    }

    const QJsonObject document = buildTreeDocument(index, m_sourceTable, effectiveEntries, &error);
    if (!error.isEmpty()) {
        return RepositoryResult::failure(error);
    }

    if (!writeIndexDocument(path, document, &error)) {
        return RepositoryResult::failure(error);
    }

    if (qEnvironmentVariableIsSet("DBMS_TEST_FAIL_SORT_INDEX_CREATE_AFTER_WRITE")) {
        return RepositoryResult::failure(QStringLiteral("failed to create sort index '%1' (test injected)").arg(m_indexName));
    }

    return RepositoryResult::success();
}

RepositoryResult SortIndexRepo::dropIndex() const
{
    removeIndexCaches(getIndexFilePath());
    return m_store.removeFile(getIndexFilePath());
}

TableData SortIndexRepo::readIndex(QString *error) const
{
    return m_store.readTable(getIndexFilePath(), error);
}

RepositoryResult SortIndexRepo::replaceIndex(const TableData &table) const
{
    return m_store.writeTable(getIndexFilePath(), table);
}

RepositoryResult SortIndexRepo::insertRow(const TableRow &row) const
{
    return m_store.appendRow(getIndexFilePath(), row);
}

RepositoryResult SortIndexRepo::updateRow(int rowIndex, const TableRow &row) const
{
    return m_store.updateRow(getIndexFilePath(), rowIndex, row);
}

RepositoryResult SortIndexRepo::deleteRow(int rowIndex) const
{
    return m_store.deleteRow(getIndexFilePath(), rowIndex);
}

RepositoryResult SortIndexRepo::rebuild(const tabledef::IndexMeta &index,
                                        const TableData &table,
                                        const QStringList &rowLocators) const
{
    if (index.indexName.trimmed().isEmpty()) {
        return RepositoryResult::failure(QStringLiteral("index name cannot be empty"));
    }

    QString error;
    const QVector<IndexEntry> entries = buildEntries(table, rowLocators, index.columnNames, &error);
    if (!error.isEmpty()) {
        return RepositoryResult::failure(error);
    }

    QVector<IndexEntry> effectiveEntries = entries;
    if (index.isUnique) {
        effectiveEntries.erase(std::remove_if(effectiveEntries.begin(),
                                              effectiveEntries.end(),
                                              [](const IndexEntry &entry) {
                                                  return keyContainsEmptyValue(entry.keyValues);
                                              }),
                                    effectiveEntries.end());
    }

    if (index.isUnique) {
        QSet<QString> seen;
        for (const IndexEntry &entry : effectiveEntries) {
            const QString signature = keySignature(entry.keyValues);
            if (seen.contains(signature)) {
                return RepositoryResult::failure(
                    QStringLiteral("index '%1' contains duplicate key values").arg(index.indexName));
            }
            seen.insert(signature);
        }
    }

    const QJsonObject rebuilt = buildTreeDocument(index, m_sourceTable, effectiveEntries, &error);
    if (!error.isEmpty()) {
        return RepositoryResult::failure(error);
    }

    if (!writeIndexDocument(getIndexFilePath(), rebuilt, &error)) {
        return RepositoryResult::failure(error);
    }

    return RepositoryResult::success();
}

RepositoryResult SortIndexRepo::rebuild(const TableData &table, const QStringList &rowLocators) const
{
    QString error;
    QJsonObject document;
    if (!readIndexDocument(getIndexFilePath(), &document, &error)) {
        return RepositoryResult::failure(error);
    }

    const QJsonObject metaObject = document.value(QStringLiteral("meta")).toObject();
    tabledef::IndexMeta index;
    index.indexName = metaObject.value(QStringLiteral("indexName")).toString();
    index.columnNames = fromJsonArray(metaObject.value(QStringLiteral("columnNames")).toArray());
    index.isUnique = metaObject.value(QStringLiteral("isUnique")).toBool(false);

    return rebuild(index, table, rowLocators);
}

RepositoryResult SortIndexRepo::insertIndexEntry(const QStringList &keyValues, const QString &rowLocator) const
{
    return insertIndexEntries({keyValues}, {rowLocator});
}

RepositoryResult SortIndexRepo::insertIndexEntries(const QList<QStringList> &keyValuesList,
                                                   const QStringList &rowLocators) const
{
    if (keyValuesList.size() != rowLocators.size()) {
        return RepositoryResult::failure(QStringLiteral("index key count does not match row locator count"));
    }
    if (keyValuesList.isEmpty()) {
        return RepositoryResult::success();
    }

    QString error;
    QJsonObject document;
    if (!readIndexDocument(getIndexFilePath(), &document, &error)) {
        return RepositoryResult::failure(error);
    }

    const QJsonObject metaObject = document.value(QStringLiteral("meta")).toObject();
    const bool isUnique = metaObject.value(QStringLiteral("isUnique")).toBool(false);
    QVector<IndexEntry> entries = loadEntriesFromDocument(document);
    if (!appendEntries(&entries, keyValuesList, rowLocators, isUnique, &error)) {
        return RepositoryResult::failure(error);
    }

    const tabledef::IndexMeta index{metaObject.value(QStringLiteral("indexName")).toString(),
                                    fromJsonArray(metaObject.value(QStringLiteral("columnNames")).toArray()),
                                    isUnique};
    const QJsonObject rebuilt = buildTreeDocument(index, m_sourceTable, entries, &error);
    if (!error.isEmpty()) {
        return RepositoryResult::failure(error);
    }
    if (!writeIndexDocument(getIndexFilePath(), rebuilt, &error)) {
        return RepositoryResult::failure(error);
    }
    return RepositoryResult::success();
}

RepositoryResult SortIndexRepo::deleteIndexEntry(const QStringList &keyValues, const QString &rowLocator) const
{
    QString error;
    QJsonObject document;
    if (!readIndexDocument(getIndexFilePath(), &document, &error)) {
        return RepositoryResult::failure(error);
    }

    const QJsonObject metaObject = document.value(QStringLiteral("meta")).toObject();
    const bool isUnique = metaObject.value(QStringLiteral("isUnique")).toBool(false);
    QVector<IndexEntry> entries = loadEntriesFromDocument(document);
    if (!removeEntry(&entries, keyValues, rowLocator, isUnique, &error)) {
        return RepositoryResult::failure(error);
    }

    const tabledef::IndexMeta index{metaObject.value(QStringLiteral("indexName")).toString(),
                                    fromJsonArray(metaObject.value(QStringLiteral("columnNames")).toArray()),
                                    isUnique};
    const QJsonObject rebuilt = buildTreeDocument(index, m_sourceTable, entries, &error);
    if (!error.isEmpty()) {
        return RepositoryResult::failure(error);
    }
    if (!writeIndexDocument(getIndexFilePath(), rebuilt, &error)) {
        return RepositoryResult::failure(error);
    }
    return RepositoryResult::success();
}

RepositoryResult SortIndexRepo::updateIndexEntry(const QStringList &oldKeyValues,
                                                 const QStringList &newKeyValues,
                                                 const QString &rowLocator) const
{
    if (oldKeyValues == newKeyValues) {
        return RepositoryResult::success();
    }

    QString error;
    QJsonObject document;
    if (!readIndexDocument(getIndexFilePath(), &document, &error)) {
        return RepositoryResult::failure(error);
    }

    const QJsonObject metaObject = document.value(QStringLiteral("meta")).toObject();
    const bool isUnique = metaObject.value(QStringLiteral("isUnique")).toBool(false);
    QVector<IndexEntry> entries = loadEntriesFromDocument(document);
    if (!removeEntry(&entries, oldKeyValues, rowLocator, isUnique, &error)) {
        return RepositoryResult::failure(error);
    }
    if (!appendEntry(&entries, newKeyValues, rowLocator, isUnique, &error)) {
        return RepositoryResult::failure(error);
    }

    const tabledef::IndexMeta index{metaObject.value(QStringLiteral("indexName")).toString(),
                                    fromJsonArray(metaObject.value(QStringLiteral("columnNames")).toArray()),
                                    isUnique};
    const QJsonObject rebuilt = buildTreeDocument(index, m_sourceTable, entries, &error);
    if (!error.isEmpty()) {
        return RepositoryResult::failure(error);
    }
    if (!writeIndexDocument(getIndexFilePath(), rebuilt, &error)) {
        return RepositoryResult::failure(error);
    }
    return RepositoryResult::success();
}

QStringList SortIndexRepo::search(const QStringList &keyValues, QString *error) const
{
    if (error != nullptr) {
        error->clear();
    }
    const QString path = getIndexFilePath();
    const QString key = keySignature(keyValues);
    {
        QReadLocker locker(&indexCacheLock());
        const auto lookupCacheIt = indexLookupCache().constFind(path);
        if (lookupCacheIt != indexLookupCache().constEnd()) {
            return lookupCacheIt.value().value(key);
        }
    }

    QJsonObject document;
    if (!readIndexDocument(path, &document, error)) {
        return {};
    }
    QMap<QString, QStringList> lookup = buildLookupMap(document);
    const QStringList matches = lookup.value(key);
    {
        QWriteLocker locker(&indexCacheLock());
        indexLookupCache().insert(path, std::move(lookup));
    }
    return matches;
}

QList<QStringList> SortIndexRepo::orderedKeyValues(bool descending, QString *error) const
{
    if (error != nullptr) {
        error->clear();
    }

    const QString path = getIndexFilePath();
    QJsonObject document;
    if (!readIndexDocument(path, &document, error)) {
        return {};
    }

    QVector<IndexEntry> entries = orderedEntriesForPath(path, document);
    if (descending) {
        std::reverse(entries.begin(), entries.end());
    }

    QList<QStringList> keyValuesList;
    for (const IndexEntry &entry : entries) {
        for (int rowIndex = 0; rowIndex < entry.rowLocators.size(); ++rowIndex) {
            keyValuesList.append(entry.keyValues);
        }
    }
    return keyValuesList;
}

QStringList SortIndexRepo::orderedRowLocators(bool descending, QString *error) const
{
    if (error != nullptr) {
        error->clear();
    }

    const QString path = getIndexFilePath();
    QJsonObject document;
    if (!readIndexDocument(path, &document, error)) {
        return {};
    }

    QVector<IndexEntry> entries = orderedEntriesForPath(path, document);
    if (descending) {
        std::reverse(entries.begin(), entries.end());
    }
    QStringList locators;
    for (const IndexEntry &entry : entries) {
        locators.append(entry.rowLocators);
    }
    return locators;
}

bool SortIndexRepo::validateUniqueKeys(QString *error) const
{
    if (error != nullptr) {
        error->clear();
    }

    QJsonObject document;
    QString readError;
    if (!readIndexDocument(getIndexFilePath(), &document, &readError)) {
        if (error != nullptr) {
            *error = readError;
        }
        return false;
    }

    const QJsonObject metaObject = document.value(QStringLiteral("meta")).toObject();
    if (!metaObject.value(QStringLiteral("isUnique")).toBool(false)) {
        return true;
    }

    const QVector<IndexEntry> entries = loadEntriesFromDocument(document);
    QStringList previousKey;
    bool hasPrevious = false;
    for (const IndexEntry &entry : entries) {
        bool hasEmptyValue = false;
        for (const QString &value : entry.keyValues) {
            if (value.isEmpty()) {
                hasEmptyValue = true;
                break;
            }
        }
        if (hasEmptyValue) {
            continue;
        }

        if (hasPrevious && entry.keyValues == previousKey) {
            if (error != nullptr) {
                *error = QStringLiteral("unique index '%1' contains duplicate key values").arg(m_indexName);
            }
            return false;
        }

        previousKey = entry.keyValues;
        hasPrevious = true;
    }

    return true;
}

QString SortIndexRepo::getIndexFilePath() const
{
    return m_store.getSortIndexFilePath(m_databaseName, m_sourceTable, m_indexName);
}

} // namespace repo
