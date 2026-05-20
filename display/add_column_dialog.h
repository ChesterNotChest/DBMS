#ifndef ADD_COLUMN_DIALOG_H
#define ADD_COLUMN_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include "../utils/table_manu/table_manu.h"

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

    ColumnConfig getConfig() const;

private slots:
    void onReferenceTableChanged();
    void updateSqlPreview();

private:
    void buildUi();
    static QString defaultDataRoot();
    void populateRefTables();
    void populateRefColumns();

    QLineEdit   *m_nameEdit             = nullptr;
    QComboBox   *m_typeCombo            = nullptr;
    QLineEdit   *m_lengthEdit           = nullptr;
    QCheckBox   *m_notNullCheck         = nullptr;
    QCheckBox   *m_pkCheck              = nullptr;
    QCheckBox   *m_uniqueCheck          = nullptr;
    QLineEdit   *m_defaultEdit          = nullptr;
    QComboBox   *m_refTableCombo        = nullptr;
    QComboBox   *m_refColumnCombo       = nullptr;
    QLineEdit   *m_checkEdit            = nullptr;
    QTextEdit   *m_sqlPreview           = nullptr;

    QString m_currentDb;
};

#endif