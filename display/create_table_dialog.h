#ifndef CREATE_TABLE_DIALOG_H
#define CREATE_TABLE_DIALOG_H

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

class CreateTableDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CreateTableDialog(QWidget *parent = nullptr,
                               const QString &defaultDb = QString());

    QString getGeneratedSql() const;

private slots:
    void onAddRow();
    void onDeleteRow();
    void onClearAll();
    void onSave();
    void onTableNameChanged(const QString &text);
    void onCellChanged(int row, int column);
    void onRefTableChanged(int row);

private:
    void buildUi(const QString &defaultDb);
    void updateSqlPreview();
    QString buildCreateSql() const;
    void populateRefTables(QComboBox *combo);
    void populateRefColumns(QComboBox *refTableCombo, QComboBox *refColumnCombo);
    static QString defaultDataRoot();

    QLineEdit    *m_tableNameEdit = nullptr;
    QTableWidget *m_fieldTable    = nullptr;
    QTextEdit    *m_sqlPreview    = nullptr;
    QPushButton  *m_saveBtn       = nullptr;
    QPushButton  *m_cancelBtn     = nullptr;

    QString m_currentDb;
    QString m_generatedSql;
};

#endif