/**
 * structure_panel.cpp — 左侧数据库结构树（三层完整层级）
 *
 * 职责：纯 UI 展示 + 用户交互。
 * 不做 SQL 解析、不直接访问文件、不直接操作 repo。
 * 所有数据查询走 service 层（database_service / table_service）。
 *
 * 三层结构：database → table → column
 * - database 节点：当前库蓝色加粗高亮
 * - table 节点：展开时懒加载 column
 * - column 节点：🔑主键 / 📎普通列，自动识别
 */
#include "structure_panel.h"
#include "service/service.h"
#include <QHeaderView>
#include <QTextStream>
#include <QFile>

namespace {

QStringList firstColumnValues(const service::SelectRowsResult &result)
{
    QStringList values;
    if (!result.success) return values;
    for (const auto &row : result.resultTable.rows) {
        if (!row.isEmpty()) values.append(row.first());
    }
    return values;
}

QStringList listDatabaseNamesForTree()
{
    return firstColumnValues(service::database_service::showDatabases());
}

QStringList listTableNamesForTree(const QString &databaseName)
{
    if (databaseName.isEmpty()) return {};

    const QString savedDatabase = service::currentDatabase;
    service::currentDatabase = databaseName;
    const QStringList tableNames = firstColumnValues(service::table_service::showTables());
    service::currentDatabase = savedDatabase;
    return tableNames;
}

QStringList listColumnNamesForTree(const QString &databaseName, const QString &tableName)
{
    if (tableName.isEmpty()) return {};

    const QString savedDatabase = service::currentDatabase;
    if (!databaseName.isEmpty()) service::currentDatabase = databaseName;
    const service::TextResult result = service::table_service::describeTable(tableName);
    service::currentDatabase = savedDatabase;

    QStringList columnNames;
    if (!result.success) return columnNames;

    const QStringList lines = result.text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith(QStringLiteral("CONSTRAINT"), Qt::CaseInsensitive)) continue;
        columnNames.append(line.section(QLatin1Char(' '), 0, 0));
    }
    return columnNames;
}

} // namespace

StructurePanel::StructurePanel(QWidget *parent)
    : QWidget(parent)
    , m_currentDatabase("")
    , m_currentTable("")
{
    setupUI();
    loadStructure();
}

StructurePanel::~StructurePanel() {}

void StructurePanel::setupUI()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // VS Code Light — 侧边栏 #F3F3F3
    setStyleSheet("QWidget { background:#F3F3F3; }");

    QLabel *title = new QLabel("对象浏览器");
    title->setFont(QFont("Microsoft YaHei", 12, QFont::Bold));
    title->setStyleSheet(
        "QLabel { color:#333333; padding:10px 14px 6px; background:#F3F3F3; font-weight:600; }");
    layout->addWidget(title);

    m_treeWidget = new QTreeWidget(this);
    m_treeWidget->setHeaderLabel("对象");
    m_treeWidget->setFont(QFont("Microsoft YaHei", 12));
    m_treeWidget->setAnimated(true);
    m_treeWidget->setIndentation(16);
    m_treeWidget->setRootIsDecorated(true);
    m_treeWidget->setUniformRowHeights(true);
    // VS Code Light — 树形控件
    m_treeWidget->setStyleSheet(
        "QTreeWidget { border:none; background:#F3F3F3; padding:4px 0; outline:none; }"
        "QTreeWidget::item { padding:3px 12px 3px 8px; color:#333333; border-radius:3px; }"
        "QTreeWidget::item:hover { background:#E8E8E8; }"
        "QTreeWidget::item:selected { background:#ADD6FF; color:#000000; }"
        "QTreeWidget::item:selected:active { background:#ADD6FF; color:#000000; }"
        "QHeaderView::section { background:#EEEEEE; font-weight:600; color:#333333; "
        "padding:4px 12px; border:none; border-bottom:1px solid #E0E0E0; font-size:11px; }"
        "QTreeWidget::branch { background:transparent; }"
        "QScrollBar:vertical { background:#F3F3F3; width:8px; }"
        "QScrollBar::handle:vertical { background:#C4C4C4; border-radius:4px; min-height:30px; }"
        "QScrollBar::handle:vertical:hover { background:#A0A0A0; }");

    connect(m_treeWidget, &QTreeWidget::itemDoubleClicked,
            this, &StructurePanel::onTreeItemDoubleClicked);
    connect(m_treeWidget, &QTreeWidget::itemExpanded,
            this, &StructurePanel::onTreeItemExpanded);

    layout->addWidget(m_treeWidget, 1);

    m_statusLabel = new QLabel("当前数据库：未选择\n当前表：未选择");
    m_statusLabel->setFont(QFont("Microsoft YaHei", 11));
    m_statusLabel->setStyleSheet(
        "QLabel { color:#616161; font-size:11px; padding:6px 12px; background:#F3F3F3; border-top:1px solid #E0E0E0; }");
    layout->addWidget(m_statusLabel);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setContentsMargins(8, 4, 8, 8);
    btnLayout->setSpacing(4);

    QPushButton *btnNew = new QPushButton("+ 新建库");
    btnNew->setFont(QFont("Microsoft YaHei", 11));
    btnNew->setCursor(Qt::PointingHandCursor);
    btnNew->setFocusPolicy(Qt::NoFocus);
    btnNew->setStyleSheet(
        "QPushButton { background:#4CAF50; color:#FFFFFF; border:none; border-radius:3px; "
        "padding:4px 10px; font-size:11px; }"
        "QPushButton:hover { background:#43A047; }");
    connect(btnNew, &QPushButton::clicked, this, &StructurePanel::newDatabaseRequested);
    btnLayout->addWidget(btnNew);

    QPushButton *btnOpen = new QPushButton("打开库");
    btnOpen->setFont(QFont("Microsoft YaHei", 11));
    btnOpen->setCursor(Qt::PointingHandCursor);
    btnOpen->setFocusPolicy(Qt::NoFocus);
    btnOpen->setStyleSheet(
        "QPushButton { background:#FFFFFF; color:#555555; border:1px solid #CCCCCC; "
        "border-radius:3px; padding:4px 10px; font-size:11px; }"
        "QPushButton:hover { background:#F0F0F0; border-color:#BBBBBB; }");
    connect(btnOpen, &QPushButton::clicked, this, &StructurePanel::openDatabaseRequested);
    btnLayout->addWidget(btnOpen);

    QPushButton *btnDelete = new QPushButton("删除库");
    btnDelete->setFont(QFont("Microsoft YaHei", 11));
    btnDelete->setCursor(Qt::PointingHandCursor);
    btnDelete->setFocusPolicy(Qt::NoFocus);
    btnDelete->setStyleSheet(
        "QPushButton { background:#FFFFFF; color:#666666; border:1px solid #CCCCCC; "
        "border-radius:3px; padding:4px 10px; font-size:11px; }"
        "QPushButton:hover { background:#FFF0F0; border-color:#FFAAAA; color:#CC3333; }");
    connect(btnDelete, &QPushButton::clicked, this, &StructurePanel::deleteDatabaseRequested);
    btnLayout->addWidget(btnDelete);

    layout->addLayout(btnLayout);
}

void StructurePanel::loadStructure()
{
    // 阻止信号，避免加载时触发事件
    m_treeWidget->blockSignals(true);
    m_treeWidget->clear();

    // 调用 service 层获取所有数据库名称
    QStringList dbNames = listDatabaseNamesForTree();
    dbNames.sort();

    for (const QString &dbName : dbNames) {
        // ── database 节点 ──
        QTreeWidgetItem *dbItem = new QTreeWidgetItem(m_treeWidget);
        dbItem->setText(0, "🗄  " + dbName);
        dbItem->setData(0, Qt::UserRole, "database:" + dbName);

        if (dbName == m_currentDatabase) {
            dbItem->setFont(0, QFont("Microsoft YaHei", 9, QFont::Bold));
            dbItem->setForeground(0, QColor("#0066cc"));
            dbItem->setExpanded(true);
        } else {
            dbItem->setFont(0, QFont("Microsoft YaHei", 9));
        }

        // 调用 service 层获取该库下的所有表名
        QStringList tblNames = listTableNamesForTree(dbName);
        tblNames.sort();

        for (const QString &tblName : tblNames) {
            // ── table 节点 ──
            QTreeWidgetItem *tItem = new QTreeWidgetItem(dbItem);
            tItem->setText(0, "📋 " + tblName);
            tItem->setData(0, Qt::UserRole, "table:" + dbName + ":" + tblName);
            tItem->setFont(0, QFont("Consolas", 9));

            // 当前数据库+当前表时预加载 column 并展开
            if (dbName == m_currentDatabase && tblName == m_currentTable) {
                addColumnsToTableItem(tItem, dbName, tblName);
                tItem->setExpanded(true);
            }
        }
    }

    m_treeWidget->blockSignals(false);
    updateStatusLabel();
}

void StructurePanel::addColumnsToTableItem(QTreeWidgetItem *tItem,
                                             const QString &dbName,
                                             const QString &tableName)
{
    if (tItem->childCount() > 0) return;

    // 调用 service 层读取列信息
    QStringList columns = listColumnNamesForTree(dbName, tableName);

    for (const QString &colName : columns) {
        // 主键识别：id / pk / _id 后缀 / tableNameId
        bool isPk = (colName.toLower() == "id" ||
                     colName.toLower() == "pk" ||
                     colName.toLower().endsWith("_id") ||
                     colName.toLower() == (tableName.toLower() + "id"));

        QString icon = isPk ? "🔑" : "📎";
        QTreeWidgetItem *cItem = new QTreeWidgetItem(tItem);
        cItem->setText(0, icon + " " + colName);
        cItem->setData(0, Qt::UserRole, "column:" + dbName + ":" + tableName + ":" + colName);
        cItem->setFont(0, QFont("Consolas", 8));
        cItem->setForeground(0, QColor("#888888"));
    }
}

void StructurePanel::onTreeItemExpanded(QTreeWidgetItem *item)
{
    QString data = item->data(0, Qt::UserRole).toString();

    // 展开 table 节点时懒加载 column
    if (data.startsWith("table:")) {
        QStringList parts = data.mid(6).split(":");
        if (parts.size() == 2) {
            addColumnsToTableItem(item, parts[0], parts[1]);
        }
    }
}

void StructurePanel::onTreeItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);
    QString data = item->data(0, Qt::UserRole).toString();

    if (data.startsWith("database:")) {
        QString dbName = data.mid(9);
        m_currentDatabase = dbName;
        m_currentTable.clear();
        updateStatusLabel();
        loadStructure();
        emit databaseSelected(dbName);

    } else if (data.startsWith("table:")) {
        QStringList parts = data.mid(6).split(":");
        if (parts.size() == 2) {
            m_currentDatabase = parts[0];
            m_currentTable = parts[1];
            updateStatusLabel();
            loadStructure();
            emit tableSelected(parts[0], parts[1]);
        }

    } else if (data.startsWith("column:")) {
        QStringList parts = data.mid(7).split(":");
        if (parts.size() == 3) {
            m_currentDatabase = parts[0];
            m_currentTable = parts[1];
            updateStatusLabel();
            emit columnSelected(parts[0], parts[1], parts[2]);
        }
    }
}

void StructurePanel::updateStatusLabel()
{
    QString db = m_currentDatabase.isEmpty() ? "未选择" : m_currentDatabase;
    QString tbl = m_currentTable.isEmpty() ? "未选择" : m_currentTable;
    m_statusLabel->setText(QString("当前数据库：%1\n当前表：%2").arg(db, tbl));
}

void StructurePanel::refresh()
{
    loadStructure();
}

void StructurePanel::selectDatabase(const QString &dbName)
{
    m_currentDatabase = dbName;
    m_currentTable.clear();
    loadStructure();
}

void StructurePanel::selectTable(const QString &tableName)
{
    m_currentTable = tableName;
    loadStructure();
}

QString StructurePanel::currentDatabase() const
{
    return m_currentDatabase;
}

QString StructurePanel::currentTable() const
{
    return m_currentTable;
}
