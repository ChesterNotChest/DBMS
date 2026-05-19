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
#include <QRegularExpression>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include "add_column_dialog.h"

class CreateTableDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CreateTableDialog(QWidget *parent = nullptr,
                               const QString &defaultDb = QString());
    CreateTableDialog(QWidget *parent,
                      const QString &defaultDb,
                      const QString &tableName,
                      const QString &createTableText);

    QString getGeneratedSql() const;

private slots:
    void onAddColumn();
    void onAddColumnDialog();
    void onDeleteColumn();
    void onClearAll();
    void onExecuteCreate();

private:
    QString buildSql() const;
    QString buildAlterSql() const;
    void buildLayout(const QString &defaultDb);
    void updatePkRadio(int row);
    void refreshLengthEnable(int row);

    void loadTableSchema(const QString &tableName, const QString &createTableText);
    ColumnConfig parseColumnDefinition(const QString &text) const;
    ColumnConfig parseForeignKeyConstraint(const QString &text) const;
    ColumnConfig rowColumnConfig(int row) const;
    void onAddColumnWithConfig(const ColumnConfig &cfg);
    QString columnDefinitionText(const ColumnConfig &cfg) const;

    QLineEdit   *m_tableNameEdit = nullptr;
    QTableWidget *m_fieldTable   = nullptr;
    QPushButton  *m_execBtn      = nullptr;
    QPushButton  *m_cancelBtn    = nullptr;

    QString m_generatedSql;
    QString m_sourceTableName;
    bool m_isEditMode = false;
    QList<ColumnConfig> m_originalColumns;

    void setRowForeignKeyData(int row, const ColumnConfig &cfg);
    ColumnConfig getRowForeignKeyData(int row) const;
    void populateReferenceTables(QComboBox *combo);
    void populateReferenceColumns(QComboBox *refTableCombo, QComboBox *refColumnCombo);
};

#endif // CREATE_TABLE_DIALOG_H