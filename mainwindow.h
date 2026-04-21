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
#include <QRegularExpression>
#include <QKeyEvent>

#include "display/structure_panel.h"
#include "display/editor_panel.h"
#include "display/result_panel.h"
#include "service/sql_dispatcher.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

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

    void onDatabaseSelected(const QString &dbName);
    void onTableSelected(const QString &dbName, const QString &tableName);
    void onColumnSelected(const QString &dbName, const QString &tableName, const QString &columnName);

    void onExecuteRequested(const QString &sql);

    void onToolbarExecute();
    void onToolbarNewQuery();

    void onAbout();
    void onRefreshStructure();

private:
    void setupMenuBar();
    void setupToolBar();
    void setupLayout();
    QString dataRoot() const;
    void updateStatusDbLabel();

protected:
    void keyPressEvent(QKeyEvent *e) override;

    QSplitter *m_mainSplitter = nullptr;
    QWidget *m_leftPanel = nullptr;
    QWidget *m_rightPanel = nullptr;

    StructurePanel *m_structurePanel = nullptr;
    EditorPanel *m_editorPanel = nullptr;
    ResultPanel *m_resultPanel = nullptr;

    QToolBar *m_toolbar = nullptr;
    QStatusBar *m_statusBar = nullptr;
    QLabel *m_statusDbLabel = nullptr;
    QLabel *m_statusRowsLabel = nullptr;

    QString m_currentDatabase;
    QString m_currentTable;
};

#endif // MAINWINDOW_H
