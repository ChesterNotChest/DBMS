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

class StructurePanel : public QWidget
{
    Q_OBJECT

public:
    explicit StructurePanel(QWidget *parent = nullptr);
    ~StructurePanel();

    void refresh();
    void selectDatabase(const QString &dbName);
    void selectTable(const QString &tableName);
    QString currentDatabase() const;
    QString currentTable() const;

signals:
    void databaseSelected(const QString &dbName);
    void tableSelected(const QString &dbName, const QString &tableName);
    void columnSelected(const QString &dbName, const QString &tableName, const QString &columnName);
    void newDatabaseRequested();
    void openDatabaseRequested();
    void deleteDatabaseRequested();

private slots:
    void onTreeItemDoubleClicked(QTreeWidgetItem *item, int column);
    void onTreeItemExpanded(QTreeWidgetItem *item);

private:
    void setupUI();
    void loadStructure();
    void addColumnsToTableItem(QTreeWidgetItem *tItem,
                               const QString &dbName,
                               const QString &tableName);
    void updateStatusLabel();

    QTreeWidget *m_treeWidget = nullptr;
    QLabel *m_statusLabel = nullptr;
    QString m_currentDatabase;
    QString m_currentTable;
};

#endif // STRUCTURE_PANEL_H
