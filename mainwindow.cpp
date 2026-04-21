/**
 * mainwindow.cpp — DBMS 主窗口
 *
 * 职责：
 *  - 整体布局和组件挂载
 *  - 信号中转（display 层 ↔ service 层）
 *  - 统一入口：所有 SQL 执行走 SqlDispatcher
 *  - 状态同步：currentDatabase / currentTable
 *
 * 禁止：
 *  - 在此文件做 SQL 解析
 *  - 在此文件直接操作 repo
 *  - 在此文件直接访问文件
 */
#include "mainwindow.h"
#include "service/sql_dispatcher.h"
#include <QInputDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QDateTime>
#include <QElapsedTimer>
#include <QMenu>
#include <QToolBar>
#include <QKeyEvent>
#include <QVBoxLayout>

QString MainWindow::dataRoot() const
{
    return QApplication::applicationDirPath() + "/data";
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("DBMS - 数据库管理系统");
    setMinimumSize(1000, 650);
    resize(1380, 820);

    QFont appFont("Microsoft YaHei", 9);
    QApplication::setFont(appFont);

    setStyleSheet("QMainWindow { background:#FFFFFF; }");

    setupMenuBar();
    setupToolBar();

    service::setDataRoot(dataRoot());
    setupLayout();

    m_resultPanel->showLog("DBMS 启动成功 " + QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
}

MainWindow::~MainWindow() {}

void MainWindow::setupMenuBar()
{
    QMenuBar *mb = menuBar();
    mb->setStyleSheet(
        "QMenuBar { background:#FFFFFF; border-bottom:1px solid #E0E0E0; padding:2px; }"
        "QMenuBar::item { background:transparent; padding:4px 12px; border-radius:3px; color:#333333; }"
        "QMenuBar::item:selected { background:#E8F0FE; color:#0066CC; }"
        "QMenu { background:#FFFFFF; border:1px solid #E0E0E0; }"
        "QMenu::item { padding:6px 24px; color:#333333; }"
        "QMenu::item:selected { background:#E8F0FE; color:#0066CC; }");

    QMenu *menuFile = mb->addMenu("文件(&F)");
    menuFile->addAction("新建数据库", this, &MainWindow::onNewDatabase);
    menuFile->addAction("打开数据库", this, &MainWindow::onOpenDatabase);
    menuFile->addAction("删除数据库", this, &MainWindow::onDeleteDatabase);
    menuFile->addSeparator();
    QAction *actExit = menuFile->addAction("退出", this, &MainWindow::onExit);
    actExit->setShortcut(QKeySequence("Alt+F4"));

    QMenu *menuQuery = mb->addMenu("查询(&Q)");
    QAction *actExec = menuQuery->addAction("执行当前查询", this, &MainWindow::onExecuteSql);
    actExec->setShortcut(QKeySequence("F5"));
    menuQuery->addAction("新建查询标签", this, &MainWindow::onNewQueryTab)
        ->setShortcut(QKeySequence("Ctrl+N"));
    menuQuery->addAction("关闭当前标签", this, &MainWindow::onCloseCurrentTab)
        ->setShortcut(QKeySequence("Ctrl+W"));

    QMenu *menuView = mb->addMenu("视图(&V)");
    menuView->addAction("切换左侧面板", this, &MainWindow::onToggleLeftPanel)
        ->setCheckable(true);
    menuView->addAction("切换结果面板", this, &MainWindow::onToggleBottomPanel)
        ->setCheckable(true);

    QMenu *menuHelp = mb->addMenu("帮助(&H)");
    menuHelp->addAction("关于 DBMS", this, &MainWindow::onAbout);
}

void MainWindow::setupToolBar()
{
    m_toolbar = addToolBar("主工具栏");
    m_toolbar->setMovable(false);
    m_toolbar->setIconSize(QSize(16, 16));
    m_toolbar->setFixedHeight(36);
    m_toolbar->setStyleSheet(
        "QToolBar { background:#FFFFFF; border:none; border-bottom:1px solid #E0E0E0; "
        "padding:0 8px; spacing:4px; }"
        "QToolButton { background:transparent; border:none; border-radius:4px; "
        "padding:4px 12px; color:#555555; font-family:'Microsoft YaHei'; font-size:12px; }"
        "QToolButton:hover { background:#F0F0F0; }"
        "QToolButton:pressed { background:#E0E0E0; }");

    QPushButton *btnExecute = new QPushButton("▶");
    btnExecute->setFont(QFont("Microsoft YaHei", 14));
    btnExecute->setCursor(Qt::PointingHandCursor);
    btnExecute->setFocusPolicy(Qt::NoFocus);
    btnExecute->setStyleSheet(
        "QPushButton { background:#FFFFFF; color:#555555; border:1px solid #CCCCCC; "
        "border-radius:4px; padding:4px 10px; }"
        "QPushButton:hover { background:#F5F5F5; border-color:#BBBBBB; color:#333333; }"
        "QPushButton:pressed { background:#E8E8E8; }");
    btnExecute->setToolTip("运行 (F5)");
    connect(btnExecute, &QPushButton::clicked, this, &MainWindow::onToolbarExecute);
    m_toolbar->addWidget(btnExecute);

    QWidget *spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolbar->addWidget(spacer);
}

void MainWindow::setupLayout()
{
    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_mainSplitter = new QSplitter(Qt::Horizontal, this);
    m_mainSplitter->setHandleWidth(1);
    m_mainSplitter->setStyleSheet(
        "QSplitter::handle { background:#E0E0E0; width:1px; }"
        "QSplitter::handle:hover { background:#CCCCCC; }");
    rootLayout->addWidget(m_mainSplitter);

    m_leftPanel = new QWidget(this);
    m_leftPanel->setMinimumWidth(220);
    m_leftPanel->setMaximumWidth(320);
    QVBoxLayout *leftLayout = new QVBoxLayout(m_leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    m_structurePanel = new StructurePanel(this);
    leftLayout->addWidget(m_structurePanel);
    m_mainSplitter->addWidget(m_leftPanel);

    m_rightPanel = new QWidget(this);
    m_rightPanel->setStyleSheet("QWidget { background:#FFFFFF; }");
    QVBoxLayout *rightLayout = new QVBoxLayout(m_rightPanel);
    rightLayout->setContentsMargins(4, 4, 4, 4);
    rightLayout->setSpacing(4);

    m_editorPanel = new EditorPanel(this);
    rightLayout->addWidget(m_editorPanel, 1);

    m_resultPanel = new ResultPanel(this);
    m_resultPanel->setMaximumHeight(360);
    rightLayout->addWidget(m_resultPanel);

    m_mainSplitter->addWidget(m_rightPanel);
    m_mainSplitter->setStretchFactor(0, 0);
    m_mainSplitter->setStretchFactor(1, 1);
    m_mainSplitter->setSizes({220, 1180});

    m_statusBar = statusBar();
    m_statusBar->setStyleSheet(
        "QStatusBar { background:#F5F5F5; border-top:1px solid #E0E0E0; "
        "color:#666666; font-size:11px; padding:2px 8px; }");

    m_statusDbLabel = new QLabel("数据库：未选择");
    m_statusDbLabel->setStyleSheet("color:#333333; font-size:11px; padding:0 4px;");
    m_statusRowsLabel = new QLabel("");
    m_statusRowsLabel->setStyleSheet("color:#666666; font-size:11px;");

    QLabel *verLabel = new QLabel("DBMS v1.0  ·  Qt6 + CSV");
    verLabel->setStyleSheet("color:#999999; font-size:11px;");
    m_statusBar->addPermanentWidget(verLabel);
    m_statusBar->addWidget(m_statusDbLabel);
    m_statusBar->addWidget(m_statusRowsLabel);

    // ── 信号连接 ──
    connect(m_structurePanel, &StructurePanel::databaseSelected,
            this, &MainWindow::onDatabaseSelected);
    connect(m_structurePanel, &StructurePanel::tableSelected,
            this, &MainWindow::onTableSelected);
    connect(m_structurePanel, &StructurePanel::columnSelected,
            this, &MainWindow::onColumnSelected);
    connect(m_structurePanel, &StructurePanel::newDatabaseRequested,
            this, &MainWindow::onNewDatabase);
    connect(m_structurePanel, &StructurePanel::openDatabaseRequested,
            this, &MainWindow::onOpenDatabase);
    connect(m_structurePanel, &StructurePanel::deleteDatabaseRequested,
            this, &MainWindow::onDeleteDatabase);

    // editor 执行请求 → 主窗口统一执行
    connect(m_editorPanel, &EditorPanel::executeRequested,
            this, &MainWindow::onExecuteRequested);
}

// ─────────────────────────────────────────────────────────────────
//  执行入口：所有 SQL 统一走这里
// ─────────────────────────────────────────────────────────────────
void MainWindow::onExecuteRequested(const QString &sql)
{
    m_resultPanel->showLog("▶ " + sql);
    m_resultPanel->addHistory(sql);

    service::setDataRoot(dataRoot());
    if (!m_currentDatabase.isEmpty())
        service::currentDatabase = m_currentDatabase;

    QElapsedTimer timer;
    timer.start();

    service::SqlDispatcher disp;
    service::SqlExecResult r = disp.execute(sql);

    qint64 execTime = timer.elapsed();

    if (r.success) {
        // 成功日志
        if (!r.text.isEmpty())
            m_resultPanel->showLog(r.text);
        else
            m_resultPanel->showLog("执行成功");

        // SELECT 结果：展示为表格
        if (r.selectResult.success && !r.selectResult.resultTable.columns.isEmpty()) {
            const auto &tbl = r.selectResult.resultTable;
            QStringList headers;
            for (const QString &c : tbl.columns) headers.append(c);
            QList<QStringList> rows;
            for (const auto &row : tbl.rows) rows.append(row);
            m_resultPanel->showTable(headers, rows);

            int cols = tbl.columns.size();
            int rowsCnt = tbl.rows.size();
            m_statusRowsLabel->setText(QString("  %1 行 × %2 列").arg(rowsCnt).arg(cols));
        } else {
            m_statusRowsLabel->setText(
                r.affectedRows >= 0 ? QString("  %1 行受影响").arg(r.affectedRows) : "");
        }

        // 检测 DDL：CREATE / DROP / ALTER → 刷新结构树
        if (r.commandType.startsWith("CREATE_") ||
            r.commandType.startsWith("DROP_") ||
            r.commandType.startsWith("ALTER_")) {
            m_structurePanel->refresh();
        }

        // 检测 USE：更新当前数据库状态
        if (r.commandType == "USE_DATABASE") {
            QString dbName = r.payload["databaseName"].toString();
            if (!dbName.isEmpty()) {
                m_currentDatabase = dbName;
                m_currentTable.clear();
                updateStatusDbLabel();
                m_structurePanel->selectDatabase(m_currentDatabase);
            }
        }

    } else {
        // 错误展示
        QString ts = "[" + QTime::currentTime().toString("hh:mm:ss") + "] ";
        m_resultPanel->showError(ts + r.errorMessage);
        m_statusBar->showMessage("错误", 5000);
    }
}

// ─────────────────────────────────────────────────────────────────
//  结构树信号处理
// ─────────────────────────────────────────────────────────────────
void MainWindow::onDatabaseSelected(const QString &dbName)
{
    m_currentDatabase = dbName;
    m_currentTable.clear();
    service::currentDatabase = dbName;
    updateStatusDbLabel();
    m_resultPanel->showLog("切换数据库: " + dbName);
    m_editorPanel->insertSql("\nUSE " + dbName + ";\n");
}

void MainWindow::onTableSelected(const QString &dbName, const QString &tableName)
{
    if (m_currentDatabase != dbName) {
        m_currentDatabase = dbName;
        service::currentDatabase = dbName;
    }
    m_currentTable = tableName;
    updateStatusDbLabel();
    m_resultPanel->showLog("选中表: " + tableName);
    m_editorPanel->insertSql("\nSELECT * FROM " + tableName + " LIMIT 100;\n");
}

void MainWindow::onColumnSelected(const QString &dbName,
                                   const QString &tableName,
                                   const QString &columnName)
{
    if (m_currentDatabase != dbName) {
        m_currentDatabase = dbName;
        service::currentDatabase = dbName;
    }
    m_currentTable = tableName;
    updateStatusDbLabel();
    m_editorPanel->insertSql("\nSELECT " + columnName + " FROM " + tableName + ";\n");
}

void MainWindow::updateStatusDbLabel()
{
    QString db = m_currentDatabase.isEmpty() ? "未选择" : m_currentDatabase;
    QString tbl = m_currentTable.isEmpty() ? "" : "  |  表：" + m_currentTable;
    m_statusDbLabel->setText("数据库：" + db + tbl);
}

// ─────────────────────────────────────────────────────────────────
//  菜单/工具栏操作
// ─────────────────────────────────────────────────────────────────
void MainWindow::onToolbarExecute()   { m_editorPanel->execute(); }
void MainWindow::onToolbarNewQuery()   { m_editorPanel->newQuery(); }
void MainWindow::onExecuteSql()        { m_editorPanel->execute(); }
void MainWindow::onNewQueryTab()       { m_editorPanel->newQuery(); }
void MainWindow::onCloseCurrentTab()   { m_editorPanel->closeCurrentTab(); }

void MainWindow::onNewDatabase()
{
    bool ok;
    QString name = QInputDialog::getText(this, "新建数据库", "数据库名称：",
        QLineEdit::Normal, "", &ok);
    if (!ok || name.isEmpty()) return;

    service::setDataRoot(dataRoot());
    auto r = service::database_service::createDatabase(name);
    if (r.success) {
        m_currentDatabase = name;
        service::currentDatabase = name;
        updateStatusDbLabel();
        m_structurePanel->refresh();
        m_resultPanel->showLog("数据库 '" + name + "' 创建成功");
        m_statusBar->showMessage("数据库 '" + name + "' 创建成功", 5000);
    } else {
        m_resultPanel->showError("创建失败: " + r.errorMessage);
    }
}

void MainWindow::onOpenDatabase()
{
    service::setDataRoot(dataRoot());
    QStringList dbs = service::database_service::listAllDatabases();
    if (dbs.isEmpty()) {
        m_resultPanel->showError("暂无可用数据库，请先新建！");
        return;
    }
    bool ok;
    QString sel = QInputDialog::getItem(this, "打开数据库", "选择数据库：", dbs, 0, false, &ok);
    if (!ok) return;

    m_currentDatabase = sel;
    service::currentDatabase = sel;
    updateStatusDbLabel();
    m_resultPanel->showLog("打开数据库: " + sel);
    m_structurePanel->refresh();
}

void MainWindow::onDeleteDatabase()
{
    service::setDataRoot(dataRoot());
    QStringList dbs = service::database_service::listAllDatabases();
    if (dbs.isEmpty()) {
        m_resultPanel->showError("没有可删除的数据库！");
        return;
    }
    bool ok;
    QString sel = QInputDialog::getItem(this, "删除数据库", "选择要删除的数据库：", dbs, 0, false, &ok);
    if (!ok) return;

    int ret = QMessageBox::warning(this, "确认删除",
        "确定要删除数据库 '" + sel + "' 吗？\n此操作不可恢复！",
        QMessageBox::Yes | QMessageBox::Cancel);
    if (ret != QMessageBox::Yes) return;

    auto r = service::database_service::dropDatabase(sel);
    if (r.success) {
        m_resultPanel->showLog("删除数据库: " + sel);
        if (m_currentDatabase == sel) {
            m_currentDatabase.clear();
            m_currentTable.clear();
            service::currentDatabase.clear();
            updateStatusDbLabel();
        }
        m_structurePanel->refresh();
    } else {
        m_resultPanel->showError("删除失败: " + r.errorMessage);
    }
}

void MainWindow::onRefreshStructure()
{
    m_structurePanel->refresh();
    m_resultPanel->showLog("结构已刷新 " + QTime::currentTime().toString("hh:mm:ss"));
}

void MainWindow::onToggleLeftPanel()
{
    m_leftPanel->setVisible(!m_leftPanel->isVisible());
}

void MainWindow::onToggleBottomPanel()
{
    m_resultPanel->setVisible(!m_resultPanel->isVisible());
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, "关于 DBMS",
        "<b>DBMS 数据库管理系统</b><br><br>"
        "版本：1.0<br>"
        "基于 Qt6 + CSV 文件存储<br><br>"
        "支持：<br>"
        "CREATE DATABASE / USE / DROP / SHOW DATABASES<br>"
        "CREATE TABLE / DROP TABLE / ALTER TABLE / DESC<br>"
        "INSERT / SELECT / UPDATE / DELETE<br>"
        "WHERE 简单条件<br><br>"
        "快捷键：F5执行 · Ctrl+N新建 · Ctrl+W关闭");
}

void MainWindow::onExit()
{
    close();
}

void MainWindow::keyPressEvent(QKeyEvent *e)
{
    if (e->modifiers() == Qt::NoModifier && e->key() == Qt::Key_F5) {
        m_editorPanel->execute();
        e->accept();
    } else {
        QMainWindow::keyPressEvent(e);
    }
}
