#ifndef ADD_COLUMN_DIALOG_H
#define ADD_COLUMN_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QDir>
#include <QFileInfo>
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
    bool allowNull = true;
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
    explicit AddColumnDialog(QWidget *parent = nullptr);

    ColumnConfig getConfig() const;

private:
    void buildLayout();
    void buildStyle();

    QLineEdit *m_nameEdit = nullptr;
    QComboBox *m_typeCombo = nullptr;
    QLineEdit *m_lengthEdit = nullptr;
    QCheckBox *m_nullCheck = nullptr;
    QCheckBox *m_pkCheck = nullptr;
    QCheckBox *m_uniqueCheck = nullptr;
    QComboBox *m_referenceTableCombo = nullptr;
    QComboBox *m_referenceColumnCombo = nullptr;

    void populateReferenceTables();
    void onReferenceTableChanged(int index);
    QCheckBox *m_checkCheck = nullptr;
    QLineEdit *m_checkEdit = nullptr;
    QLineEdit *m_defaultEdit = nullptr;
};

#endif // ADD_COLUMN_DIALOG_H