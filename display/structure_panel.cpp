/**
 * structure_panel.cpp 鈥?宸︿晶鏁版嵁搴撶粨鏋勬爲锛堜笁灞傚畬鏁村眰绾э級
 *
 * 鑱岃矗锛氱函 UI 灞曠ず + 鐢ㄦ埛浜や簰銆? * 涓嶅仛 SQL 瑙ｆ瀽銆佷笉鐩存帴璁块棶鏂囦欢銆佷笉鐩存帴鎿嶄綔 repo銆? * 鎵€鏈夋暟鎹煡璇㈣蛋 service 灞傦紙database_service / table_service锛夈€? *
 * 涓夊眰缁撴瀯锛歞atabase 鈫?table 鈫?column
 * - database 鑺傜偣锛氬綋鍓嶅簱钃濊壊鍔犵矖楂樹寒
 * - table 鑺傜偣锛氬睍寮€鏃舵噿鍔犺浇 column
 * - column 鑺傜偣锛氿煍戜富閿?/ 馃搸鏅€氬垪锛岃嚜鍔ㄨ瘑鍒? */
#include "structure_panel.h"
#include "client/sql_client_engine.h"
#include "service/service.h"
#include <QHeaderView>
#include <QTextStream>
#include <QFile>

namespace {

QStringList columnNamesFromDescribeText(const QString &text)
{
    QStringList columnNames;
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
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

    // VS Code Light 鈥?渚ц竟鏍?#F3F3F3
    setStyleSheet("QWidget { background:#F3F3F3; }");

    QLabel *title = new QLabel(QStringLiteral("Objects"));
    title->setFont(QFont("Microsoft YaHei", 12, QFont::Bold));
    title->setStyleSheet(
        "QLabel { color:#333333; padding:10px 14px 6px; background:#F3F3F3; font-weight:600; }");
    layout->addWidget(title);

    m_treeWidget = new QTreeWidget(this);
    m_treeWidget->setHeaderLabel(QStringLiteral("Object"));
    m_treeWidget->setFont(QFont("Microsoft YaHei", 12));
    m_treeWidget->setAnimated(true);
    m_treeWidget->setIndentation(16);
    m_treeWidget->setRootIsDecorated(true);
    m_treeWidget->setUniformRowHeights(true);
    // VS Code Light 鈥?鏍戝舰鎺т欢
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

    m_statusLabel = new QLabel(QStringLiteral("Database: none\nTable: none"));
    m_statusLabel->setFont(QFont("Microsoft YaHei", 11));
    m_statusLabel->setStyleSheet(
        "QLabel { color:#616161; font-size:11px; padding:6px 12px; background:#F3F3F3; border-top:1px solid #E0E0E0; }");
    layout->addWidget(m_statusLabel);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setContentsMargins(8, 4, 8, 8);
    btnLayout->setSpacing(4);

    QPushButton *btnNew = new QPushButton(QStringLiteral("+ New"));
    btnNew->setFont(QFont("Microsoft YaHei", 11));
    btnNew->setCursor(Qt::PointingHandCursor);
    btnNew->setFocusPolicy(Qt::NoFocus);
    btnNew->setStyleSheet(
        "QPushButton { background:#4CAF50; color:#FFFFFF; border:none; border-radius:3px; "
        "padding:4px 10px; font-size:11px; }"
        "QPushButton:hover { background:#43A047; }");
    connect(btnNew, &QPushButton::clicked, this, &StructurePanel::newDatabaseRequested);
    btnLayout->addWidget(btnNew);

    QPushButton *btnOpen = new QPushButton(QStringLiteral("Open"));
    btnOpen->setFont(QFont("Microsoft YaHei", 11));
    btnOpen->setCursor(Qt::PointingHandCursor);
    btnOpen->setFocusPolicy(Qt::NoFocus);
    btnOpen->setStyleSheet(
        "QPushButton { background:#FFFFFF; color:#555555; border:1px solid #CCCCCC; "
        "border-radius:3px; padding:4px 10px; font-size:11px; }"
        "QPushButton:hover { background:#F0F0F0; border-color:#BBBBBB; }");
    connect(btnOpen, &QPushButton::clicked, this, &StructurePanel::openDatabaseRequested);
    btnLayout->addWidget(btnOpen);

    QPushButton *btnDelete = new QPushButton(QStringLiteral("Delete"));
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
    // 闃绘淇″彿锛岄伩鍏嶅姞杞芥椂瑙﹀彂浜嬩欢
    m_treeWidget->blockSignals(true);
    m_treeWidget->clear();

    // 璋冪敤 service 灞傝幏鍙栨墍鏈夋暟鎹簱鍚嶇О
    QStringList dbNames = firstColumnValuesFromSql(QStringLiteral("SHOW DATABASES;"));
    dbNames.sort();

    for (const QString &dbName : dbNames) {
        // 鈹€鈹€ database 鑺傜偣 鈹€鈹€
        QTreeWidgetItem *dbItem = new QTreeWidgetItem(m_treeWidget);
        dbItem->setText(0, "馃梽  " + dbName);
        dbItem->setData(0, Qt::UserRole, "database:" + dbName);

        if (dbName == m_currentDatabase) {
            dbItem->setFont(0, QFont("Microsoft YaHei", 9, QFont::Bold));
            dbItem->setForeground(0, QColor("#0066cc"));
            dbItem->setExpanded(true);
        } else {
            dbItem->setFont(0, QFont("Microsoft YaHei", 9));
        }

        QStringList tblNames = firstColumnValuesFromSql(QStringLiteral("USE %1; SHOW TABLES;").arg(dbName));
        tblNames.sort();

        for (const QString &tblName : tblNames) {
            // 鈹€鈹€ table 鑺傜偣 鈹€鈹€
            QTreeWidgetItem *tItem = new QTreeWidgetItem(dbItem);
            tItem->setText(0, "馃搵 " + tblName);
            tItem->setData(0, Qt::UserRole, "table:" + dbName + ":" + tblName);
            tItem->setFont(0, QFont("Consolas", 9));

            // 褰撳墠鏁版嵁搴?褰撳墠琛ㄦ椂棰勫姞杞?column 骞跺睍寮€
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

    // 璋冪敤 service 灞傝鍙栧垪淇℃伅
    QStringList columns;
    if (m_clientEngine != nullptr && !m_clientId.isEmpty() && !tableName.isEmpty()) {
        const service::SqlExecResult result =
            m_clientEngine->executeSql(m_clientId, QStringLiteral("USE %1; DESC %2;").arg(dbName, tableName));
        if (result.success) {
            columns = columnNamesFromDescribeText(result.text);
        }
    }

    for (const QString &colName : columns) {
        // 涓婚敭璇嗗埆锛歩d / pk / _id 鍚庣紑 / tableNameId
        bool isPk = (colName.toLower() == "id" ||
                     colName.toLower() == "pk" ||
                     colName.toLower().endsWith("_id") ||
                     colName.toLower() == (tableName.toLower() + "id"));

        QString icon = isPk ? "馃攽" : "馃搸";
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

    // 灞曞紑 table 鑺傜偣鏃舵噿鍔犺浇 column
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
    QString db = m_currentDatabase.isEmpty() ? "鏈€夋嫨" : m_currentDatabase;
    QString tbl = m_currentTable.isEmpty() ? "鏈€夋嫨" : m_currentTable;
    m_statusLabel->setText(QString("褰撳墠鏁版嵁搴擄細%1\n褰撳墠琛細%2").arg(db, tbl));
}

void StructurePanel::refresh()
{
    loadStructure();
}

void StructurePanel::setClientRuntime(client::SqlClientEngine *clientEngine, const QString &clientId)
{
    m_clientEngine = clientEngine;
    m_clientId = clientId;
    loadStructure();
}

QStringList StructurePanel::firstColumnValuesFromSql(const QString &sql) const
{
    QStringList values;
    if (m_clientEngine == nullptr || m_clientId.isEmpty()) {
        return values;
    }

    const service::SqlExecResult result = m_clientEngine->executeSql(m_clientId, sql);
    if (!result.success || !result.selectResult.success) {
        return values;
    }

    for (const QStringList &row : result.selectResult.resultTable.rows) {
        if (!row.isEmpty()) {
            values.append(row.first());
        }
    }
    return values;
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
