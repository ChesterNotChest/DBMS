/**
 * mainwindow.cpp 鈥?DBMS 涓荤獥鍙?
 *
 * 鑱岃矗锛?
 *  - 鏁翠綋甯冨眬鍜岀粍浠舵寕杞?
 *  - 淇″彿涓浆锛坉isplay 灞?鈫?service 灞傦級
 *  - 缁熶竴鍏ュ彛锛氭墍鏈?SQL 鎵ц璧?SqlDispatcher
 *  - 鐘舵€佸悓姝ワ細currentDatabase / currentTable
 *
 * 绂佹锛?
 *  - 鍦ㄦ鏂囦欢鍋?SQL 瑙ｆ瀽
 *  - 鍦ㄦ鏂囦欢鐩存帴鎿嶄綔 repo
 *  - 鍦ㄦ鏂囦欢鐩存帴璁块棶鏂囦欢
 */
#include "mainwindow.h"
#include "controller/sql_dispatcher.h"
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

QStringList firstColumnValues(const service::SelectRowsResult &result)
{
    QStringList values;
    if (!result.success) return values;
    for (const auto &row : result.resultTable.rows) {
        if (!row.isEmpty()) values.append(row.first());
    }
    return values;
}

QStringList listDatabaseNamesForDialog()
{
    return firstColumnValues(service::database_service::showDatabases());
}

} // namespace

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

    QMenu *menuQuery = mb->addMenu("鏌ヨ(&Q)");
    QAction *actExec = menuQuery->addAction("鎵ц褰撳墠鏌ヨ", this, &MainWindow::onExecuteSql);
    actExec->setShortcut(QKeySequence("F5"));
    menuQuery->addAction("鏂板缓鏌ヨ鏍囩", this, &MainWindow::onNewQueryTab)
        ->setShortcut(QKeySequence("Ctrl+N"));
    menuQuery->addAction("鍏抽棴褰撳墠鏍囩", this, &MainWindow::onCloseCurrentTab)
        ->setShortcut(QKeySequence("Ctrl+W"));

    QMenu *menuView = mb->addMenu("瑙嗗浘(&V)");
    menuView->addAction("鍒囨崲宸︿晶闈㈡澘", this, &MainWindow::onToggleLeftPanel)
        ->setCheckable(true);
    menuView->addAction("鍒囨崲缁撴灉闈㈡澘", this, &MainWindow::onToggleBottomPanel)
        ->setCheckable(true);

    QMenu *menuHelp = mb->addMenu("甯姪(&H)");
    menuHelp->addAction("鍏充簬 DBMS", this, &MainWindow::onAbout);
}

void MainWindow::setupToolBar()
{
    m_toolbar = addToolBar("涓诲伐鍏锋爮");
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
    btnExecute->setToolTip("杩愯 (F5)");
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

    m_statusDbLabel = new QLabel("鏁版嵁搴擄細鏈€夋嫨");
    m_statusDbLabel->setStyleSheet("color:#333333; font-size:11px; padding:0 4px;");
    m_statusRowsLabel = new QLabel("");
    m_statusRowsLabel->setStyleSheet("color:#666666; font-size:11px;");

    QLabel *verLabel = new QLabel("DBMS v1.0  路  Qt6 + CSV");
    verLabel->setStyleSheet("color:#999999; font-size:11px;");
    m_statusBar->addPermanentWidget(verLabel);
    m_statusBar->addWidget(m_statusDbLabel);
    m_statusBar->addWidget(m_statusRowsLabel);

    // 鈹€鈹€ 淇″彿杩炴帴 鈹€鈹€
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

    // editor 鎵ц璇锋眰 鈫?涓荤獥鍙ｇ粺涓€鎵ц
    connect(m_editorPanel, &EditorPanel::executeRequested,
            this, &MainWindow::onExecuteRequested);
}

// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
//  鎵ц鍏ュ彛锛氭墍鏈?SQL 缁熶竴璧拌繖閲?
// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
void MainWindow::onExecuteRequested(const QString &sql)
{
    const QStringList statements = service::SqlDispatcher::splitStatements(sql);
    if (statements.isEmpty()) return;

    m_resultPanel->addHistory(sql);

    service::setDataRoot(dataRoot());
    if (!m_currentDatabase.isEmpty())
        service::currentDatabase = m_currentDatabase;

    service::SqlDispatcher disp;
    QElapsedTimer timer;
    timer.start();

    auto applyResult = [&](const service::SqlExecResult &r) -> bool {
        if (!r.success) {
            const QString ts = "[" + QTime::currentTime().toString("hh:mm:ss") + "] ";
            m_resultPanel->showError(ts + r.errorMessage);
            m_statusBar->showMessage("错误", 5000);
            return false;
        }

        if (!r.text.isEmpty())
            m_resultPanel->showLog(r.text);
        else
            m_resultPanel->showLog("执行成功");

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
            || r.commandType.startsWith("ALTER_")) {
            m_structurePanel->refresh();
        }

        if (r.commandType == "USE_DATABASE") {
            const QString dbName = r.payload["databaseName"].toString();
            if (!dbName.isEmpty()) {
                m_currentDatabase = dbName;
                m_currentTable.clear();
                updateStatusDbLabel();
                m_structurePanel->selectDatabase(m_currentDatabase);
            }
        }

        return true;
    };

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

        const service::SqlExecResult r = disp.execute(statement);
        if (!applyResult(r)) {
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

// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
//  缁撴瀯鏍戜俊鍙峰鐞?
// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
void MainWindow::onDatabaseSelected(const QString &dbName)
{
    m_currentDatabase = dbName;
    m_currentTable.clear();
    service::currentDatabase = dbName;
    updateStatusDbLabel();
    m_resultPanel->showLog("鍒囨崲鏁版嵁搴? " + dbName);
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
    m_resultPanel->showLog("閫変腑琛? " + tableName);
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
    QString db = m_currentDatabase.isEmpty() ? "鏈€夋嫨" : m_currentDatabase;
    QString tbl = m_currentTable.isEmpty() ? "" : "  |  琛細" + m_currentTable;
    m_statusDbLabel->setText("鏁版嵁搴擄細" + db + tbl);
}

// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
//  鑿滃崟/宸ュ叿鏍忔搷浣?
// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
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
        m_resultPanel->showLog("鏁版嵁搴?'" + name + "' 鍒涘缓鎴愬姛");
        m_statusBar->showMessage("鏁版嵁搴?'" + name + "' 鍒涘缓鎴愬姛", 5000);
    } else {
        m_resultPanel->showError("鍒涘缓澶辫触: " + r.errorMessage);
    }
}

void MainWindow::onOpenDatabase()
{
    service::setDataRoot(dataRoot());
    QStringList dbs = listDatabaseNamesForDialog();
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
    m_resultPanel->showLog("鎵撳紑鏁版嵁搴? " + sel);
    m_structurePanel->refresh();
}

void MainWindow::onDeleteDatabase()
{
    service::setDataRoot(dataRoot());
    QStringList dbs = listDatabaseNamesForDialog();
    if (dbs.isEmpty()) {
        m_resultPanel->showError("娌℃湁鍙垹闄ょ殑鏁版嵁搴擄紒");
        return;
    }
    bool ok;
    QString sel = QInputDialog::getItem(this, "删除数据库", "选择要删除的数据库：", dbs, 0, false, &ok);
    if (!ok) return;

    int ret = QMessageBox::warning(this, "纭鍒犻櫎",
        "纭畾瑕佸垹闄ゆ暟鎹簱 '" + sel + "' 鍚楋紵\n姝ゆ搷浣滀笉鍙仮澶嶏紒",
        QMessageBox::Yes | QMessageBox::Cancel);
    if (ret != QMessageBox::Yes) return;

    auto r = service::database_service::dropDatabase(sel);
    if (r.success) {
        m_resultPanel->showLog("鍒犻櫎鏁版嵁搴? " + sel);
        if (m_currentDatabase == sel) {
            m_currentDatabase.clear();
            m_currentTable.clear();
            service::currentDatabase.clear();
            updateStatusDbLabel();
        }
        m_structurePanel->refresh();
    } else {
        m_resultPanel->showError("鍒犻櫎澶辫触: " + r.errorMessage);
    }
}

void MainWindow::onRefreshStructure()
{
    m_structurePanel->refresh();
    m_resultPanel->showLog("缁撴瀯宸插埛鏂?" + QTime::currentTime().toString("hh:mm:ss"));
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
    QMessageBox::about(this, "鍏充簬 DBMS",
        "<b>DBMS 鏁版嵁搴撶鐞嗙郴缁?/b><br><br>"
        "鐗堟湰锛?.0<br>"
        "鍩轰簬 Qt6 + CSV 鏂囦欢瀛樺偍<br><br>"
        "鏀寔锛?br>"
        "CREATE DATABASE / USE / DROP / SHOW DATABASES<br>"
        "CREATE TABLE / DROP TABLE / ALTER TABLE / DESC<br>"
        "INSERT / SELECT / UPDATE / DELETE<br><br>"
        "蹇嵎閿細F5鎵ц 路 Ctrl+N鏂板缓 路 Ctrl+W鍏抽棴");
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

