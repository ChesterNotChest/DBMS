#ifndef STRUCTURE_PANEL_H
#define STRUCTURE_PANEL_H

#include <QWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QFont>
#include <QColor>
#include <QPoint>

namespace client {
class SqlClientEngine;
}

class StructurePanel : public QWidget
{
    Q_OBJECT

public:
    explicit StructurePanel(QWidget *parent = nullptr);
    ~StructurePanel();

    void refresh();
    void setClientRuntime(client::SqlClientEngine *clientEngine, const QString &clientId);
    void selectDatabase(const QString &dbName);
    void selectTable(const QString &tableName);
    QString currentDatabase() const;
    QString currentTable() const;

signals:
    void databaseSelected(const QString &dbName);
    void tableSelected(const QString &dbName, const QString &tableName);
    void columnSelected(const QString &dbName, const QString &tableName, const QString &columnName);
    void editConstraintsRequested(const QString &dbName, const QString &tableName);
    void editColumnRequested(const QString &dbName, const QString &tableName, const QString &columnName);
    void newDatabaseRequested();
    void openDatabaseRequested();
    void deleteDatabaseRequested();

private slots:
    void onTreeItemDoubleClicked(QTreeWidgetItem *item, int column);
    void onTreeItemExpanded(QTreeWidgetItem *item);
    void onTreeContextMenuRequested(const QPoint &pos);

private:
    void setupUI();
    void loadStructure();
    void loadTablesForDatabase(QTreeWidgetItem *dbItem, const QString &dbName);
    QStringList firstColumnValuesFromSql(const QString &sql) const;
    void addColumnsToTableItem(QTreeWidgetItem *tItem,
                               const QString &dbName,
                               const QString &tableName);
    void addConstraintsToTableItem(QTreeWidgetItem *tItem,
                                   const QString &dbName,
                                   const QString &tableName);
    void updateStatusLabel();

    QTreeWidget *m_treeWidget = nullptr;
    QLabel *m_statusLabel = nullptr;
    QString m_currentDatabase;
    QString m_currentTable;
    client::SqlClientEngine *m_clientEngine = nullptr;
    QString m_clientId;
};

#endif // STRUCTURE_PANEL_H
