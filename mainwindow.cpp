/**
 * mainwindow.cpp - DBMS 主窗口
 *
 * 职责：
 *  - 整体布局和组件挂载
 *  - 信号中转：display 层 -> controller/service 层
 *  - 统一入口：所有 SQL 执行走 SqlDispatcher
 *  - 状态同步：currentDatabase / currentTable
 *
 * 禁止：
 *  - 在此文件做 SQL 解析
 *  - 在此文件直接操作 repo
 *  - 在此文件直接访问文件
 */
#include "mainwindow.h"
#include "constants/cli_client_def.h"
#include "controller/sql_dispatcher.h"
#include "client/sql_result_formatter.h"
#include "repo/repo.h"
#include <QInputDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QDateTime>
#include <QElapsedTimer>
#include <QMenu>
#include <QToolBar>
#include <QKeyEvent>
#include <QVBoxLayout>

namespace {
QStringList firstColumnValues(const service::SqlExecResult &result)
{
    QStringList values;
    if (!result.success || !result.selectResult.success) return values;
    for (const auto &row : result.selectResult.resultTable.rows) {
        if (!row.isEmpty()) values.append(row.first());
    }
    return values;
}
} // namespace

QString MainWindow::dataRoot() const
{
    const QString configuredRoot = qEnvironmentVariable("DBMS_GUI_DATA_ROOT");
    if (!configuredRoot.trimmed().isEmpty()) {
        return configuredRoot;
    }
    return repo::FlatFileTableStore::defaultDataRoot();
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_clientEngine(&m_clientSessionPool)
{
    setWindowTitle("DBMS - 数据库管理系统");
    setMinimumSize(1000, 650);
    resize(1380, 820);

    QFont appFont("Microsoft YaHei", 9);
    QApplication::setFont(appFont);

    setStyleSheet("QMainWindow { background:#FFFFFF; }");

    setupMenuBar();
    setupToolBar();

    setupLayout();
    initializeClientSession();

    m_resultPanel->showLog("DBMS 启动成功 " + QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
}

MainWindow::~MainWindow() {}

QString MainWindow::guiClientId() const
{
    return m_guiClientId;
}

const client::ClientSession *MainWindow::guiClientSession() const
{
    return m_clientSessionPool.session(m_guiClientId);
}

const client::ClientSession *MainWindow::guiClientSession()
{
    const client::ClientSession *session = m_clientSessionPool.session(m_guiClientId);
    if (session != nullptr) {
        return session;
    }

    if (initializeClientSession()) {
        return m_clientSessionPool.session(m_guiClientId);
    }
    return nullptr;
}

bool MainWindow::initializeClientSession()
{
    m_guiClientId = m_clientSessionPool.createSession(dataRoot(), QString::fromLatin1(cliclient::kRootUserName));
    if (m_structurePanel != nullptr) {
        m_structurePanel->setClientRuntime(&m_clientEngine, m_guiClientId);
    }

    if constexpr (cliclient::kEnableGuiAutoRootLogin) {
        const service::SqlExecResult result =
            m_clientEngine.login(m_guiClientId,
                                 QString::fromLatin1(cliclient::kRootUserName),
                                 QString::fromLatin1(cliclient::kRootInitialPassword));
        if (!result.success) {
            if (m_resultPanel != nullptr) {
                m_resultPanel->showError(QStringLiteral("GUI 登录失败: ") + result.errorMessage);
            }
            return false;
        }
    }

    return true;
}

service::SqlExecResult MainWindow::executeSqlForGui(const QString &sql)
{
    return m_clientEngine.executeSql(m_guiClientId, sql);
}

QStringList MainWindow::databaseNamesForDialog()
{
    return firstColumnValues(m_clientEngine.executeSql(m_guiClientId, QStringLiteral("SHOW DATABASES;")));
}

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

    // 新增：保存按钮
    m_saveBtn = new QPushButton(u8"\U0001F4BE");
    m_saveBtn->setFont(QFont("Segoe UI Emoji", 12));
    m_saveBtn->setCursor(Qt::PointingHandCursor);
    m_saveBtn->setFocusPolicy(Qt::NoFocus);
    m_saveBtn->setStyleSheet(
        "QPushButton { background:#E3F2FD; color:#1565C0; border:1px solid #BBDEFB; "
        "border-radius:4px; padding:4px 12px; font-size:12px; }"
        "QPushButton:hover { background:#BBDEFB; border-color:#64B5F6; }"
        "QPushButton:pressed { background:#90CAF9; }");
    m_saveBtn->setToolTip("保存当前表格数据到数据库");
    connect(m_saveBtn, &QPushButton::clicked, this, &MainWindow::onToolbarSave);
    m_toolbar->addWidget(m_saveBtn);

    // 新增：选择表按钮
    m_selectTableBtn = new QPushButton(u8"📑", this);
    m_selectTableBtn->setFont(QFont("Segoe UI Emoji", 12));
    m_selectTableBtn->setCursor(Qt::PointingHandCursor);
    m_selectTableBtn->setFocusPolicy(Qt::NoFocus);
    m_selectTableBtn->setStyleSheet(
        "QPushButton { background:#E8F0FE; color:#1565C0; border:1px solid #BBDEFB; "
        "border-radius:4px; padding:4px 12px; font-size:12px; }"
        "QPushButton:hover { background:#D7E9FC; border-color:#90CAF9; }"
        "QPushButton:pressed { background:#B3D4F6; }"
    );
    m_selectTableBtn->setToolTip("选择要编辑或删除的表");
    connect(m_selectTableBtn, &QPushButton::clicked, this, &MainWindow::onToolbarSelectTable);
    m_toolbar->addWidget(m_selectTableBtn);

    // 新增：可视化建表按钮
    m_newTableBtn = new QPushButton(u8"\U0001F4C4", this);
    m_newTableBtn->setFont(QFont("Segoe UI Emoji", 12));
    m_newTableBtn->setCursor(Qt::PointingHandCursor);
    m_newTableBtn->setFocusPolicy(Qt::NoFocus);
    m_newTableBtn->setStyleSheet(
        "QPushButton { background:#E8F5E9; color:#2E7D32; border:1px solid #C8E6C9; "
        "border-radius:4px; padding:4px 12px; font-size:12px; }"
        "QPushButton:hover { background:#C8E6C9; }"
        "QPushButton:pressed { background:#A5D6A7; }");
    m_newTableBtn->setToolTip("可视化建表 / 编辑当前表");
    connect(m_newTableBtn, SIGNAL(clicked()), this, SLOT(onToolbarNewTable()));
    m_toolbar->addWidget(m_newTableBtn);

    // 新增：删除表按钮
    m_dropTableBtn = new QPushButton(u8"\U0001F5D1", this);
    m_dropTableBtn->setFont(QFont("Segoe UI Emoji", 12));
    m_dropTableBtn->setCursor(Qt::PointingHandCursor);
    m_dropTableBtn->setFocusPolicy(Qt::NoFocus);
    m_dropTableBtn->setStyleSheet(
        "QPushButton { background:#FFEBEE; color:#C62828; border:1px solid #FFCDD2; "
        "border-radius:4px; padding:4px 12px; font-size:12px; }"
        "QPushButton:hover { background:#FFCDD2; }"
        "QPushButton:pressed { background:#EF9A9A; }");
    m_dropTableBtn->setToolTip("删除当前选中表");
    connect(m_dropTableBtn, &QPushButton::clicked, this, &MainWindow::onToolbarDropTable);
    m_toolbar->addWidget(m_dropTableBtn);

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

    QWidget *leftContainer = new QWidget(this);
    leftContainer->setMinimumWidth(220);
    leftContainer->setMaximumWidth(320);
    QVBoxLayout *leftLayout = new QVBoxLayout(leftContainer);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    m_structurePanel = new StructurePanel(this);
    leftLayout->addWidget(m_structurePanel);
    m_mainSplitter->addWidget(leftContainer);

    m_rightSplitter = new QSplitter(Qt::Vertical, this);
    m_rightSplitter->setHandleWidth(2);
    m_rightSplitter->setStyleSheet(
        "QSplitter::handle { background:#E0E0E0; height:2px; }"
        "QSplitter::handle:hover { background:#CCCCCC; }");

    m_editorPanel = new EditorPanel(this);
    m_editorPanel->setMinimumHeight(150);
    m_rightSplitter->addWidget(m_editorPanel);

    m_resultPanel = new ResultPanel(this);
    m_resultPanel->setMinimumHeight(200);
    m_resultPanel->setMaximumHeight(500);
    m_rightSplitter->addWidget(m_resultPanel);

    // Set initial sizes: editor takes 35%, result panel takes 65%
    m_rightSplitter->setStretchFactor(0, 1);
    m_rightSplitter->setStretchFactor(1, 2);

    m_mainSplitter->addWidget(m_rightSplitter);
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

    QLabel *verLabel = new QLabel("DBMS v1.0 · Qt6 + CSV");
    verLabel->setStyleSheet("color:#999999; font-size:11px;");
    m_statusBar->addPermanentWidget(verLabel);
    m_statusBar->addWidget(m_statusDbLabel);
    m_statusBar->addWidget(m_statusRowsLabel);


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

    // editor 执行请求 -> 主窗口统一执行
    connect(m_editorPanel, &EditorPanel::executeRequested,
            this, &MainWindow::onExecuteRequested);

    // result panel refresh request
    connect(m_resultPanel, &ResultPanel::refreshRequested,
            this, &MainWindow::onRefreshData);

}


// SQL 执行入口：所有 SQL 统一走这里

void MainWindow::onExecuteRequested(const QString &sql)
{
    const QStringList statements = service::SqlDispatcher::splitStatements(sql);
    if (statements.isEmpty()) return;

    m_resultPanel->addHistory(sql);

    QElapsedTimer timer;
    timer.start();

    for (int i = 0; i < statements.size(); ++i) {
        const QString &statement = statements.at(i);
        if (statements.size() == 1) {
            m_resultPanel->showLog("▶ " + statement);
        } else {
            m_resultPanel->showLog(QString("▶ [%1/%2] %3")
                                       .arg(i + 1)
                                       .arg(statements.size())
                                       .arg(statement));
        }

        const service::SqlExecResult r = executeSqlForGui(statement);
        if (!applySqlResult(r)) {
            if (statements.size() > 1) {
                m_resultPanel->showLog(
                    QString("批量执行已停止：完成 %1/%2 条").arg(i).arg(statements.size()));
            }
            return;
        }
    }

    if (statements.size() > 1) {
        m_resultPanel->showLog(
            QString("批量执行完成：%1 条，耗时 %2 ms")
                .arg(statements.size())
                .arg(timer.elapsed()));
    }
}

bool MainWindow::applySqlResult(const service::SqlExecResult &r)
{
    if (!r.success) {
        const QString ts = "[" + QTime::currentTime().toString("hh:mm:ss") + "] ";
        m_resultPanel->showError(ts + r.errorMessage);
        m_statusBar->showMessage("错误", 5000);
        return false;
    }

    m_resultPanel->showLog(client::formatSqlExecResultForText(r));

    if (r.commandType == "SHOW_CREATE_TABLE" && !r.text.isEmpty()) {
        const QString tableName = r.payload["tableName"].toString();
        const QStringList headers = {"Table", "Create Table"};
        QList<QStringList> rows;
        rows.append({tableName, r.text});
        m_resultPanel->showTable(headers, rows);
        m_statusRowsLabel->setText(" 1 行 × 2 列");
    } else if (r.selectResult.success && !r.selectResult.resultTable.columns.isEmpty()) {
        const auto &tbl = r.selectResult.resultTable;
        QStringList headers;
        for (const QString &c : tbl.columns) headers.append(c);
        QList<QStringList> rows;
        for (const auto &row : tbl.rows) rows.append(row);
        m_resultPanel->showTable(headers, rows);

        const int cols = tbl.columns.size();
        const int rowsCnt = tbl.rows.size();
        m_statusRowsLabel->setText(QString("  %1 行 × %2 列").arg(rowsCnt).arg(cols));
    } else {
        m_statusRowsLabel->setText(
            r.affectedRows >= 0 ? QString("  %1 行受影响").arg(r.affectedRows) : "");
    }

    if (r.commandType.startsWith("CREATE_")
        || r.commandType.startsWith("DROP_")
        || r.commandType.startsWith("ALTER_")
        || r.commandType == "GRANT_ALL"
        || r.commandType == "REVOKE_ALL") {
        m_structurePanel->refresh();
    }

    if (r.commandType == "USE_DATABASE") {
        const QString dbName = r.payload["databaseName"].toString();
        if (!dbName.isEmpty()) {
            m_currentDatabase = dbName;
            m_currentTable.clear();
            updateStatusDbLabel();
            m_structurePanel->selectDatabase(m_currentDatabase);
            m_resultPanel->setCurrentDb(m_currentDatabase);
            m_resultPanel->setCurrentTable(QString());
        }
    }

    return true;
}


// 结构树信号处理

void MainWindow::onDatabaseSelected(const QString &dbName)
{
    if (!switchGuiDatabase(dbName)) {
        return;
    }
    m_resultPanel->showLog("切换数据库: " + dbName);
}

void MainWindow::onTableSelected(const QString &dbName, const QString &tableName)
{
    if (m_currentDatabase != dbName && !switchGuiDatabase(dbName)) {
        return;
    }
    m_currentTable = tableName;
    updateStatusDbLabel();
    m_resultPanel->setCurrentDb(m_currentDatabase);
    m_resultPanel->setCurrentTable(m_currentTable);
    m_resultPanel->showLog("选中表: " + tableName);

    // 自动执行查询：双击表名 → 显示全部数据
    QString sql = QString("SELECT * FROM %1;").arg(tableName);
    onExecuteRequested(sql);
}

void MainWindow::onColumnSelected(const QString &dbName,
                                   const QString &tableName,
    const QString &columnName)
{
    if (m_currentDatabase != dbName && !switchGuiDatabase(dbName)) {
        return;
    }
    m_currentTable = tableName;
    updateStatusDbLabel();
    m_resultPanel->setCurrentDb(m_currentDatabase);
    m_resultPanel->setCurrentTable(m_currentTable);
    m_resultPanel->showLog("选中列: " + tableName + "." + columnName);

    // 自动执行查询：SELECT <column> FROM <table>
    QString sql = QString("SELECT %1 FROM %2;").arg(columnName).arg(tableName);
    onExecuteRequested(sql);
}

bool MainWindow::switchGuiDatabase(const QString &dbName)
{
    const service::SqlExecResult result = executeSqlForGui(QStringLiteral("USE %1;").arg(dbName));
    if (!result.success) {
        applySqlResult(result);
        return false;
    }

    m_currentDatabase = dbName;
    m_currentTable.clear();
    updateStatusDbLabel();
    m_structurePanel->selectDatabase(dbName);
    m_resultPanel->setCurrentDb(m_currentDatabase);
    m_resultPanel->setCurrentTable(QString());
    return true;
}

void MainWindow::updateStatusDbLabel()
{
    QString db = m_currentDatabase.isEmpty() ? "未选择" : m_currentDatabase;
    QString tbl = m_currentTable.isEmpty() ? "" : "  |  表：" + m_currentTable;
    m_statusDbLabel->setText("数据库：" + db + tbl);
}


// 菜单和工具栏操作

void MainWindow::onToolbarExecute()   { m_editorPanel->execute(); }
void MainWindow::onToolbarNewQuery()   { m_editorPanel->newQuery(); }
void MainWindow::onToolbarSelectTable() { 
    if (m_currentDatabase.isEmpty()) {
        m_resultPanel->showError("请先选择或打开一个数据库");
        return;
    }
    const service::SqlExecResult result = executeSqlForGui(QStringLiteral("SHOW TABLES;"));
    QStringList tables = firstColumnValues(result);
    if (tables.isEmpty()) {
        m_resultPanel->showError("当前数据库暂无可选表");
        return;
    }
    bool ok;
    QString sel = QInputDialog::getItem(this, "选择表", "表名：", tables, 0, false, &ok);
    if (!ok || sel.isEmpty()) return;
    m_currentTable = sel;
    updateStatusDbLabel();
    m_resultPanel->showLog("选中表: " + sel);
}
void MainWindow::onExecuteSql()        { m_editorPanel->execute(); }
void MainWindow::onNewQueryTab()       { m_editorPanel->newQuery(); }
void MainWindow::onCloseCurrentTab()   { m_editorPanel->closeCurrentTab(); }

void MainWindow::onNewDatabase()
{
    bool ok;
    QString name = QInputDialog::getText(this, "新建数据库", "数据库名称：",
        QLineEdit::Normal, "", &ok);
    if (!ok || name.isEmpty()) return;

    auto r = executeSqlForGui(QStringLiteral("CREATE DATABASE %1;").arg(name));
    if (r.success) {
    m_currentDatabase = name;
    executeSqlForGui(QStringLiteral("USE %1;").arg(name));
    updateStatusDbLabel();
    m_resultPanel->setCurrentDb(m_currentDatabase);
    m_resultPanel->setCurrentTable(QString());
    m_structurePanel->refresh();
        m_resultPanel->showLog("数据库 '" + name + "' 创建成功");
        m_statusBar->showMessage("数据库 '" + name + "' 创建成功", 5000);
    } else {
        m_resultPanel->showError("创建失败: " + r.errorMessage);
    }
}

void MainWindow::onOpenDatabase()
{
    QStringList dbs = databaseNamesForDialog();
    if (dbs.isEmpty()) {
        m_resultPanel->showError("暂无可用数据库，请先新建！");
        return;
    }
    bool ok;
    QString sel = QInputDialog::getItem(this, "打开数据库", "选择数据库：", dbs, 0, false, &ok);
    if (!ok) return;

    m_currentDatabase = sel;
    executeSqlForGui(QStringLiteral("USE %1;").arg(sel));
    updateStatusDbLabel();
    m_resultPanel->setCurrentDb(m_currentDatabase);
    m_resultPanel->setCurrentTable(QString());
    m_resultPanel->showLog("打开数据库: " + sel);
    m_structurePanel->refresh();
}

void MainWindow::onDeleteDatabase()
{
    QStringList dbs = databaseNamesForDialog();
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

    auto r = executeSqlForGui(QStringLiteral("DROP DATABASE %1;").arg(sel));
    if (r.success) {
        m_resultPanel->showLog("删除数据库: " + sel);
        if (m_currentDatabase == sel) {
            m_currentDatabase.clear();
            m_currentTable.clear();
            updateStatusDbLabel();
            m_resultPanel->setCurrentDb(QString());
            m_resultPanel->setCurrentTable(QString());
        }
        m_structurePanel->refresh();
    } else {
        m_resultPanel->showError("删除失败: " + r.errorMessage);
    }
}

void MainWindow::onToolbarDropTable()
{
    if (m_currentDatabase.isEmpty()) {
        m_resultPanel->showError("请先选择或打开一个数据库");
        return;
    }

    QString tableName = m_currentTable;
    if (tableName.isEmpty()) {
        const service::SqlExecResult result = executeSqlForGui(QStringLiteral("SHOW TABLES;"));
        QStringList tables = firstColumnValues(result);
        if (tables.isEmpty()) {
            m_resultPanel->showError("当前数据库暂无可删除表");
            return;
        }
        bool ok;
        tableName = QInputDialog::getItem(this, "删除表", "选择要删除的表：", tables, 0, false, &ok);
        if (!ok || tableName.isEmpty()) return;
    }

    int ret = QMessageBox::warning(this, "确认删除",
        QString("确定要删除表 '%1' 吗？\n此操作不可恢复！").arg(tableName),
        QMessageBox::Yes | QMessageBox::Cancel);
    if (ret != QMessageBox::Yes) return;

    const QString sql = QStringLiteral("DROP TABLE %1;").arg(tableName);
    auto r = executeSqlForGui(sql);
    if (r.success) {
        m_resultPanel->showLog("删除表: " + tableName);
        if (m_currentTable == tableName) {
            m_currentTable.clear();
            updateStatusDbLabel();
            m_resultPanel->setCurrentTable(QString());
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

void MainWindow::onRefreshData()
{
    if (!m_currentDatabase.isEmpty() && !m_currentTable.isEmpty()) {
        QString sql = QString("SELECT * FROM %1").arg(m_currentTable);
        executeSqlForGui(sql);
        m_resultPanel->showLog("数据已刷新 " + QTime::currentTime().toString("hh:mm:ss"));
    } else {
        m_resultPanel->showLog("请先选择一个表");
    }
}

void MainWindow::onToggleLeftPanel()
{
    QWidget *leftContainer = m_structurePanel->parentWidget();
    if (leftContainer) leftContainer->setVisible(!leftContainer->isVisible());
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
        "INSERT / SELECT / UPDATE / DELETE<br><br>"
        "快捷键：F5 执行 · Ctrl+N 新建 · Ctrl+W 关闭");
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

// 处理 ResultPanel 信号（兼容）
void MainWindow::onSaveRequested(const QString &tableName, const QList<QStringList> &rows)
{
    Q_UNUSED(tableName);
    // 直接用当前已选表和表格数据保存
    onToolbarSave();
}

// 工具栏保存按钮：精准生成 UPDATE/INSERT/DELETE SQL 并执行
void MainWindow::onToolbarSave()
{
    if (m_currentTable.isEmpty()) {
        m_resultPanel->showError("请先在左侧选择一张表，再点击保存");
        return;
    }

    if (!m_resultPanel->hasUnsavedChanges()) {
        m_resultPanel->showLog("数据未变更，无需保存");
        return;
    }

    QStringList columns = m_resultPanel->getLastColumns();
    if (columns.isEmpty()) {
        m_resultPanel->showError("无法确定列名，请先 SELECT * FROM 该表");
        return;
    }

    ResultPanel::ChangeInfo info = m_resultPanel->diffWithOriginal();
    // ── 首先统计所有变更类型 ──
    int addCols = info.addedColumns.size();
    int dropCols = info.deletedColumns.size();
    int upd = info.updatedCells.size();
    int ins = info.newRowIds.size();
    int del = info.deletedRows.size();
    bool hasColChanges = (addCols > 0 || dropCols > 0);
    bool hasRowChanges = (upd > 0 || ins > 0 || del > 0);

    // ── ALTER TABLE：处理新增列 ──
    for (auto cit = info.addedColumns.constBegin(); cit != info.addedColumns.constEnd(); ++cit) {
        int colIdx = cit.key();
        QString colDef = cit.value();
        
        // 解析列定义：格式为 "列名:类型:约束:外键约束"
        QStringList parts = colDef.split(':');
        QString colName = parts.value(0);
        QString colType = parts.value(1, "VARCHAR(255)");
        QString colConstraints = parts.value(2);
        QString fkConstraint = parts.value(3);
        
        // 第一步：添加列（只包含基本约束：NOT NULL、PRIMARY KEY、UNIQUE、DEFAULT、CHECK）
        QString sql;
        if (colConstraints.isEmpty()) {
            sql = QString("ALTER TABLE %1 ADD COLUMN %2 %3;")
                    .arg(m_currentTable)
                    .arg(colName)
                    .arg(colType);
        } else {
            // 从约束中移除 FOREIGN KEY 部分，因为外键需要单独添加
            QString basicConstraints = colConstraints;
            int fkIndex = basicConstraints.indexOf("FOREIGN KEY", 0, Qt::CaseInsensitive);
            if (fkIndex != -1) {
                basicConstraints = basicConstraints.left(fkIndex).trimmed();
                // 移除末尾的逗号
                if (basicConstraints.endsWith(',')) {
                    basicConstraints.chop(1);
                    basicConstraints = basicConstraints.trimmed();
                }
            }
            
            if (basicConstraints.isEmpty()) {
                sql = QString("ALTER TABLE %1 ADD COLUMN %2 %3;")
                        .arg(m_currentTable)
                        .arg(colName)
                        .arg(colType);
            } else {
                sql = QString("ALTER TABLE %1 ADD COLUMN %2 %3 %4;")
                        .arg(m_currentTable)
                        .arg(colName)
                        .arg(colType)
                        .arg(basicConstraints);
            }
        }
        m_resultPanel->showLog(sql);
        auto r = executeSqlForGui(sql);
        if (!r.success) {
            m_resultPanel->showError(QString("❌ ADD COLUMN 失败: %1\n%2").arg(r.errorMessage).arg(sql));
            return;
        }
        
        // 第二步：如果有外键约束，单独添加
        int fkStart = colConstraints.indexOf("FOREIGN KEY", 0, Qt::CaseInsensitive);
        if (fkStart != -1) {
            QString fkSql = QString("ALTER TABLE %1 ADD CONSTRAINT fk_%2_%3 %4;")
                            .arg(m_currentTable)
                            .arg(m_currentTable)
                            .arg(colName)
                            .arg(colConstraints.mid(fkStart));
            m_resultPanel->showLog(fkSql);
            auto fkResult = executeSqlForGui(fkSql);
            if (!fkResult.success) {
                m_resultPanel->showError(QString("❌ ADD FOREIGN KEY 失败: %1\n%2").arg(fkResult.errorMessage).arg(fkSql));
                return;
            }
        }
    }

    // ── ALTER TABLE：处理删除列 ──
    for (auto it = info.deletedColumns.constBegin(); it != info.deletedColumns.constEnd(); ++it) {
        QString colName = it.value();
        QString sql = QString("ALTER TABLE %1 DROP COLUMN %2;")
                        .arg(m_currentTable)
                        .arg(colName);
        m_resultPanel->showLog(sql);
        auto r = executeSqlForGui(sql);
        if (!r.success) {
            m_resultPanel->showError(QString("❌ DROP COLUMN 失败: %1\n%2").arg(r.errorMessage).arg(sql));
            return;
        }
    }

    int affectedTotal = 0;

    // ── 如果只有列变更，直接标记完成并刷新 ──
    if (hasColChanges && !hasRowChanges) {
        m_resultPanel->markAllCommitted();
        QString colMsg = (addCols > 0 ? QString("➕ ADD COLUMN %1 列").arg(addCols) : "") +
                        ((addCols > 0 && dropCols > 0) ? " " : "") +
                        (dropCols > 0 ? QString("➖ DROP COLUMN %1 列").arg(dropCols) : "");
        m_resultPanel->showLog(QString("✅ 保存成功！%1").arg(colMsg));

        // 重新查询刷新表
        QString sql = QString("SELECT * FROM %1;").arg(m_currentTable);
        onExecuteRequested(sql);
        return;
    }

    // ── 只有行变更，显示确认对话框 ──
    // 确认对话框
    int ret = QMessageBox::question(this, u8"确认保存",
        QString(u8"即将对表 '%1' 执行以下变更：\n\n"
                u8"✏️ UPDATE %2 行\n"
                u8"➕ INSERT %3 行\n"
                u8"➖ DELETE %4 行\n\n"
                u8"是否继续？").arg(m_currentTable).arg(upd).arg(ins).arg(del),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    m_resultPanel->showLog(u8"=== 开始保存 ===");

    // ── 辅助：格式化字段值 ──
    auto fmtVal = [](const QString &raw) -> QString {
        if (raw.isEmpty()) return "''";
        bool isNum;
        raw.toDouble(&isNum);
        if (isNum) return raw;
        if (raw.compare("NULL", Qt::CaseInsensitive) == 0) return "NULL";
        QString escaped = raw;
        escaped.replace("'", "''");
        return "'" + escaped + "'";
    };

    // ── UPDATE：只更新脏单元格 ──
    for (auto cit = info.updatedCells.constBegin(); cit != info.updatedCells.constEnd(); ++cit) {
        int rowIdx = cit.key();
        const QMap<int, QString> &cells = cit.value();
        if (cells.isEmpty()) continue;

        // WHERE 用原始 PK 值（第0列）
        QString pkOrig = info.originalRows[rowIdx].value(0, "");
        if (pkOrig.isEmpty()) {
            m_resultPanel->showLog(QString(u8"⚠️ 第 %1 行无法 UPDATE：PK 值为空").arg(rowIdx + 1));
            continue;
        }

        QString pkColName = columns.value(0, "");
        QString setClause, whereClause;
        QList<QPair<QString, QString>> setPairs;
        for (auto mit = cells.constBegin(); mit != cells.constEnd(); ++mit) {
            int ci = mit.key();
            QString colName = columns.value(ci, QString("col%1").arg(ci + 1));
            setPairs.append(qMakePair(colName, fmtVal(mit.value())));
        }
        for (int i = 0; i < setPairs.size(); ++i) {
            setClause += setPairs[i].first + "=" + setPairs[i].second;
            if (i < setPairs.size() - 1) setClause += ", ";
        }
        whereClause = pkColName + "=" + fmtVal(pkOrig);

        QString sql = QString("UPDATE %1 SET %2 WHERE %3;")
                        .arg(m_currentTable).arg(setClause).arg(whereClause);
        m_resultPanel->showLog(sql);
        auto r = executeSqlForGui(sql);
        if (!r.success) {
            m_resultPanel->showError(QString(u8"❌ UPDATE 失败: %1\n%2").arg(r.errorMessage).arg(sql));
            return;
        }
        affectedTotal += qMax(0, r.affectedRows);
    }

    // ── INSERT：处理新行 ──
    for (int rowIdx : info.newRowIds) {
        QStringList currVals = info.currentRows.value(rowIdx, QStringList());
        if (currVals.isEmpty()) {
            // 从表格重新取数（防止空数据）
            QTableWidget *tbl = m_resultPanel->getTable();
            currVals.clear();
            for (int ci = 0; ci < tbl->columnCount(); ++ci) {
                QTableWidgetItem *it = tbl->item(rowIdx, ci);
                currVals.append(it ? it->text() : "");
            }
        }

        QStringList vals;
        for (int ci = 0; ci < qMin(currVals.size(), columns.size()); ++ci)
            vals.append(fmtVal(currVals[ci]));

        QString sql = QString("INSERT INTO %1 (%2) VALUES (%3);")
                        .arg(m_currentTable)
                        .arg(columns.join(", "))
                        .arg(vals.join(", "));
        m_resultPanel->showLog(sql);
        auto r = executeSqlForGui(sql);
        if (!r.success) {
            m_resultPanel->showError(QString(u8"❌ INSERT 失败: %1\n%2").arg(r.errorMessage).arg(sql));
            return;
        }
        affectedTotal += qMax(0, r.affectedRows);
    }

    // ── DELETE：恢复已删除行 ──
    for (auto dit = info.deletedRows.constBegin(); dit != info.deletedRows.constEnd(); ++dit) {
        QString pkVal = dit.key();
        QString pkColName = columns.value(0, "");
        QString sql = QString("DELETE FROM %1 WHERE %2=%3;")
                        .arg(m_currentTable)
                        .arg(pkColName)
                        .arg(fmtVal(pkVal));
        m_resultPanel->showLog(sql);
        auto r = executeSqlForGui(sql);
        if (!r.success) {
            m_resultPanel->showError(QString(u8"❌ DELETE 失败: %1\n%2").arg(r.errorMessage).arg(sql));
            return;
        }
        affectedTotal += qMax(0, r.affectedRows);
    }

    // 全部成功：标记已提交，重新刷新数据
    m_resultPanel->markAllCommitted();
    m_resultPanel->showLog(QString(u8"✅ 保存成功！受影响 %1 行").arg(affectedTotal));

    // Re-select to refresh
    QString sql = QString("SELECT * FROM %1;").arg(m_currentTable);
    onExecuteRequested(sql);
}

// Visual table creation
void MainWindow::onToolbarNewTable()
{
    CreateTableDialog dlg(this, m_currentDatabase);
    if (dlg.exec() != QDialog::Accepted) return;
    QString sql = dlg.getGeneratedSql();
    if (sql.isEmpty()) return;
    onExecuteRequested(sql);
}
