#ifndef CREATE_TABLE_DIALOG_H
#define CREATE_TABLE_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QTableWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QHeaderView>
#include "add_column_dialog.h"

class CreateTableDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CreateTableDialog(QWidget *parent = nullptr,
                               const QString &defaultDb = QString());

    QString getGeneratedSql() const;

private slots:
    void onAddColumn();
    void onAddColumnDialog();
    void onDeleteColumn();
    void onClearAll();
    void onExecuteCreate();

private:
    QString buildSql() const;
    void buildLayout(const QString &defaultDb);
    void updatePkRadio(int row);
    void refreshLengthEnable(int row);

    QLineEdit   *m_tableNameEdit = nullptr;
    QTableWidget *m_fieldTable   = nullptr;
    QPushButton  *m_execBtn      = nullptr;
    QPushButton  *m_cancelBtn    = nullptr;

    QString m_generatedSql;
};

#endif // CREATE_TABLE_DIALOG_H
