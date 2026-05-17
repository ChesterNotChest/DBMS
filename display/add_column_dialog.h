#ifndef ADD_COLUMN_DIALOG_H
#define ADD_COLUMN_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

struct ColumnConfig {
    QString name;
    QString type;
    int length;
    bool allowNull;
    bool primaryKey;
    bool unique;
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
    QSpinBox  *m_lengthSpin = nullptr;
    QCheckBox *m_nullCheck = nullptr;
    QCheckBox *m_pkCheck = nullptr;
    QCheckBox *m_uniqueCheck = nullptr;
    QCheckBox *m_checkCheck = nullptr;
    QLineEdit *m_checkEdit = nullptr;
    QLineEdit *m_defaultEdit = nullptr;
};

#endif // ADD_COLUMN_DIALOG_H
