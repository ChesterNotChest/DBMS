#ifndef CONSTRAINT_DIALOG_H
#define CONSTRAINT_DIALOG_H

#include <QDialog>
#include <QList>
#include <QString>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

class ConstraintDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConstraintDialog(const QString &tableName,
                              const QString &createTableSql,
                              QWidget *parent = nullptr);

    QString generatedSql() const;

private slots:
    void onAddConstraint();
    void onDeleteConstraint();
    void onAccept();
    void updatePreview();

private:
    struct ConstraintConfig
    {
        QString originalName;
        QString name;
        QString type;
        QString columns;
        QString checkClause;
        QString referencedTable;
        QString referencedColumns;
        QString onDeleteAction;
        QString onUpdateAction;
    };

    void buildUi();
    void buildTableConstraintUi(QVBoxLayout *root);
    void loadConstraints(const QString &createTableSql);
    void appendRow(const ConstraintConfig &config);
    QList<ConstraintConfig> currentConfigs(QString *error = nullptr) const;
    QString constraintDefinition(const ConstraintConfig &config) const;
    QString buildAlterSql(QString *error = nullptr) const;

    QString m_tableName;
    QList<ConstraintConfig> m_originalConstraints;
    QTableWidget *m_table = nullptr;
    QTextEdit *m_preview = nullptr;
    QString m_generatedSql;
};

#endif // CONSTRAINT_DIALOG_H
