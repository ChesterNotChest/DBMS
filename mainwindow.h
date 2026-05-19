#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSplitter>
#include <QMenuBar>
#include <QMenu>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QPushButton>
#include <QInputDialog>
#include <QMessageBox>
#include <QLineEdit>
#include <QApplication>
#include <QTime>
#include <QKeyEvent>
#include <QStringList>
#include <QTableWidget>

#include "display/structure_panel.h"
#include "display/editor_panel.h"
#include "display/result_panel.h"
#include "display/create_table_dialog.h"
#include "client/client_session_pool.h"
#include "client/sql_client_engine.h"
#include "controller/sql_dispatcher.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    QString guiClientId() const;
    const client::ClientSession *guiClientSession();
    const client::ClientSession *guiClientSession() const;
    service::SqlExecResult executeSqlForGui(const QString &sql);

private slots:
    void onNewDatabase();
    void onOpenDatabase();
    void onDeleteDatabase();
    void onExit();

    void onExecuteSql();
    void onNewQueryTab();
    void onCloseCurrentTab();

    void onToggleLeftPanel();
    void onToggleBottomPanel();
    void onRefreshData();

    void onDatabaseSelected(const QString &dbName);
    void onTableSelected(const QString &dbName, const QString &tableName);
    void onColumnSelected(const QString &dbName, const QString &tableName, const QString &columnName);

    void onExecuteRequested(const QString &sql);

    void onToolbarExecute();
    void onToolbarNewQuery();
    void onToolbarSelectTable();

    void onAbout();
    void onRefreshStructure();

    // ResultPanel save
    void onSaveRequested(const QString &tableName, const QList<QStringList> &rows);
    void onToolbarSave();
    void onToolbarNewTable();
    void onToolbarDropTable();

private:
    void setupMenuBar();
    void setupToolBar();
    void setupLayout();
    QString dataRoot() const;
    bool initializeClientSession();
    QStringList databaseNamesForDialog();
    bool applySqlResult(const service::SqlExecResult &r);
    bool switchGuiDatabase(const QString &dbName);
    void updateStatusDbLabel();

protected:
    void keyPressEvent(QKeyEvent *e) override;

    QSplitter *m_mainSplitter = nullptr;
    QSplitter *m_rightSplitter = nullptr;
    QWidget *m_rightPanel = nullptr;

    StructurePanel *m_structurePanel = nullptr;
    EditorPanel *m_editorPanel = nullptr;
    ResultPanel *m_resultPanel = nullptr;

    QToolBar *m_toolbar = nullptr;
    QStatusBar *m_statusBar = nullptr;
    QLabel *m_statusDbLabel = nullptr;
    QLabel *m_statusRowsLabel = nullptr;

    QPushButton *m_saveBtn = nullptr;
    QPushButton *m_selectTableBtn = nullptr;
    QPushButton *m_newTableBtn = nullptr;
    QPushButton *m_dropTableBtn = nullptr;

    QString m_currentDatabase;
    QString m_currentTable;
    client::ClientSessionPool m_clientSessionPool;
    client::SqlClientEngine m_clientEngine;
    QString m_guiClientId;
};

#endif // MAINWINDOW_H