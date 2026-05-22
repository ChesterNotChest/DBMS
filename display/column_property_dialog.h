#ifndef COLUMN_PROPERTY_DIALOG_H
#define COLUMN_PROPERTY_DIALOG_H

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QString>
#include <QTextEdit>

class ColumnPropertyDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ColumnPropertyDialog(const QString &tableName,
                                  const QString &columnName,
                                  const QString &createTableSql,
                                  QWidget *parent = nullptr);

    QString generatedSql() const;

private slots:
    void updatePreview();
    void onAccept();

private:
    struct ColumnProperties
    {
        QString type;
        int length = 0;
        bool notNull = false;
        QString defaultValue;
    };

    void buildUi();
    bool loadColumn(const QString &createTableSql);
    QString buildTypeSpec(const QString &type, int length) const;
    QString formatDefaultValue(const QString &value) const;
    QString buildAlterSql(QString *error = nullptr) const;
    void updateLengthEnabled();

    QString m_tableName;
    QString m_columnName;
    ColumnProperties m_original;
    bool m_loaded = false;

    QComboBox *m_typeCombo = nullptr;
    QLineEdit *m_lengthEdit = nullptr;
    QCheckBox *m_notNullCheck = nullptr;
    QLineEdit *m_defaultEdit = nullptr;
    QTextEdit *m_preview = nullptr;
    QString m_generatedSql;
};

#endif // COLUMN_PROPERTY_DIALOG_H
