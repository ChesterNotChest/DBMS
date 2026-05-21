#ifndef ADD_COLUMN_DIALOG_H
#define ADD_COLUMN_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QTableWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QHeaderView>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>

struct ColumnConfig {
    QString name;
    QString type;
    int length = 0;
    bool notNull = false;
    bool primaryKey = false;
    bool unique = false;
    QString referencedTable;
    QStringList referencedColumns;
    QString checkConstraint;
    QString defaultValue;
};

class AddColumnDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AddColumnDialog(const QString &currentDb,
                             QWidget *parent = nullptr);

    QList<ColumnConfig> getAllConfigs() const;
    QString getGeneratedSql() const;

private slots:
    void onAddRow();
    void onDeleteRow();
    void onClearAll();
    void onAccept();
    void onCellChanged(int row, int column);
    void onRefTableChanged(int row);

private:
    void buildUi();
    void updateSqlPreview();
    QString buildAlterSql() const;
    void populateRefTables(QComboBox *combo);
    void populateRefColumns(QComboBox *refTableCombo, QComboBox *refColumnCombo);
    static QString defaultDataRoot();

    QTableWidget *m_fieldTable = nullptr;
    QTextEdit    *m_sqlPreview = nullptr;
    QPushButton  *m_okBtn      = nullptr;

    QString m_currentDb;
    QString m_generatedSql;
};

#endif