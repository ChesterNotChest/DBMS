#include "constraint_dialog.h"

#include <QComboBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {

enum ConstraintCol {
    ColName = 0,
    ColType,
    ColColumns,
    ColCheck,
    ColRefTable,
    ColRefColumns,
    ColOnDelete,
    ColOnUpdate,
    ColCount
};

QString cleanConstraintLine(QString line)
{
    line = line.trimmed();
    if (line.endsWith(QLatin1Char(','))) {
        line.chop(1);
    }
    return line.trimmed();
}

QString normalizeCsv(QString value)
{
    QStringList parts = value.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (QString &part : parts) {
        part = part.trimmed();
    }
    return parts.join(QStringLiteral(", "));
}

QString normalizeAction(QString value)
{
    value = value.trimmed().toUpper();
    value.replace(QLatin1Char('_'), QLatin1Char(' '));
    value.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    if (value == QStringLiteral("NO ACTION")) {
        return QString();
    }
    if (value == QStringLiteral("CASCADE")
        || value == QStringLiteral("RESTRICT")
        || value == QStringLiteral("SET NULL")
        || value == QStringLiteral("SET DEFAULT")) {
        return value;
    }
    return QString();
}

QString actionFromTail(const QString &tail, const QString &verb)
{
    const QRegularExpression re(QStringLiteral("\\bON\\s+%1\\s+(CASCADE|RESTRICT|NO\\s+ACTION|SET\\s+NULL|SET\\s+DEFAULT)\\b")
                                    .arg(verb),
                                QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = re.match(tail);
    return match.hasMatch() ? normalizeAction(match.captured(1)) : QString();
}

QString itemText(const QTableWidget *table, int row, int col)
{
    const QTableWidgetItem *item = table->item(row, col);
    return item ? item->text().trimmed() : QString();
}

void setItem(QTableWidget *table, int row, int col, const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignCenter);
    table->setItem(row, col, item);
}

QComboBox *makeTypeCombo(const QString &type)
{
    auto *combo = new QComboBox;
    combo->addItems({QStringLiteral("PRIMARY KEY"),
                     QStringLiteral("UNIQUE"),
                     QStringLiteral("CHECK"),
                     QStringLiteral("FOREIGN KEY")});
    if (!type.trimmed().isEmpty()) {
        combo->setCurrentText(type.trimmed().toUpper());
    }
    return combo;
}

QComboBox *makeActionCombo(const QString &action)
{
    auto *combo = new QComboBox;
    combo->addItems({QString(), QStringLiteral("CASCADE"), QStringLiteral("RESTRICT"),
                     QStringLiteral("SET NULL"), QStringLiteral("SET DEFAULT")});
    const QString normalized = normalizeAction(action);
    if (!normalized.isEmpty()) {
        combo->setCurrentText(normalized);
    }
    return combo;
}

void applyConstraintDialogStyle(QWidget *widget)
{
    widget->setStyleSheet(R"(
        QDialog {
            background-color: #F8F9FA;
        }
        QLabel {
            font-size: 13px;
            color: #374151;
        }
        QComboBox {
            border: 1px solid #D1D5DB;
            border-radius: 3px;
            padding: 2px 6px;
            background: white;
            font-size: 12px;
            color: #1F2937;
            min-height: 22px;
            max-height: 24px;
        }
        QComboBox:focus {
            border-color: #9CA3AF;
        }
        QComboBox::drop-down {
            width: 16px;
            border: none;
        }
        QComboBox QAbstractItemView {
            background: white;
            border: 1px solid #D1D5DB;
            font-size: 12px;
            color: #1F2937;
            selection-background-color: #E5E7EB;
            selection-color: #1F2937;
        }
        QComboBox QAbstractItemView::item {
            padding: 4px 8px;
        }
        QTableWidget {
            background: white;
            border: 1px solid #D1D5DB;
            border-radius: 4px;
            gridline-color: #E5E7EB;
            font-size: 12px;
            color: #1F2937;
            selection-background-color: #E5E7EB;
            selection-color: #1F2937;
            alternate-background-color: #FAFBFC;
        }
        QTableWidget::item {
            padding: 2px 6px;
            border: none;
        }
        QTableWidget::item:selected {
            background: #E5E7EB;
            color: #1F2937;
        }
        QHeaderView::section {
            background: #F3F4F6;
            padding: 6px 4px;
            border: none;
            border-right: 1px solid #E5E7EB;
            border-bottom: 1px solid #D1D5DB;
            font-weight: 600;
            font-size: 12px;
            color: #4B5563;
        }
        QHeaderView::section:last {
            border-right: none;
        }
        QPushButton {
            background: #F3F4F6;
            color: #374151;
            border: 1px solid #D1D5DB;
            border-radius: 4px;
            padding: 7px 16px;
            font-size: 12px;
            font-weight: 500;
        }
        QPushButton:hover {
            background: #E5E7EB;
        }
        QPushButton:pressed {
            background: #D1D5DB;
        }
        QPushButton#applyBtn {
            background: #4B5563;
            color: white;
            border-color: #4B5563;
        }
        QPushButton#applyBtn:hover {
            background: #6B7280;
            border-color: #6B7280;
        }
        QTextEdit {
            background: #F9FAFB;
            border: 1px solid #D1D5DB;
            border-radius: 4px;
            padding: 8px;
            font-family: "Consolas", "Courier New", monospace;
            font-size: 12px;
            color: #1F2937;
        }
    )");
}

} // namespace

ConstraintDialog::ConstraintDialog(const QString &tableName,
                                   const QString &createTableSql,
                                   QWidget *parent)
    : QDialog(parent)
    , m_tableName(tableName)
{
    setWindowTitle(QStringLiteral("编辑表约束 - %1").arg(tableName));
    setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint);
    setMinimumSize(980, 600);
    setSizeGripEnabled(true);
    applyConstraintDialogStyle(this);
    buildUi();
    loadConstraints(createTableSql);
    updatePreview();
}

QString ConstraintDialog::generatedSql() const
{
    return m_generatedSql;
}

void ConstraintDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("表约束：%1").arg(m_tableName));
    title->setStyleSheet(QStringLiteral("font-size:20px;font-weight:700;color:#1F2937;"));
    root->addWidget(title);

    buildTableConstraintUi(root);

    auto *sqlLabel = new QLabel(QStringLiteral("SQL 预览"));
    sqlLabel->setStyleSheet(QStringLiteral("font-weight:600;font-size:13px;color:#4B5563;"));
    root->addWidget(sqlLabel);

    m_preview = new QTextEdit(this);
    m_preview->setReadOnly(true);
    m_preview->setMaximumHeight(140);
    m_preview->setMinimumHeight(90);
    m_preview->setPlaceholderText(QStringLiteral("生成的 ALTER TABLE 语句"));
    root->addWidget(m_preview);
}

void ConstraintDialog::buildTableConstraintUi(QVBoxLayout *root)
{
    m_table = new QTableWidget(0, ColCount, this);
    m_table->setHorizontalHeaderLabels({QStringLiteral("约束名"),
                                        QStringLiteral("类型"),
                                        QStringLiteral("作用列"),
                                        QStringLiteral("CHECK 表达式"),
                                        QStringLiteral("引用表"),
                                        QStringLiteral("引用列"),
                                        QStringLiteral("删除动作"),
                                        QStringLiteral("更新动作")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(ColName, 150);
    m_table->setColumnWidth(ColType, 125);
    m_table->setColumnWidth(ColColumns, 140);
    m_table->setColumnWidth(ColCheck, 180);
    m_table->setColumnWidth(ColRefTable, 130);
    m_table->setColumnWidth(ColRefColumns, 140);
    m_table->setColumnWidth(ColOnDelete, 115);
    m_table->setColumnWidth(ColOnUpdate, 115);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(30);
    m_table->verticalHeader()->setMinimumSectionSize(26);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    root->addWidget(m_table, 1);

    connect(m_table, &QTableWidget::cellChanged, this, &ConstraintDialog::updatePreview);

    auto *buttonRow = new QHBoxLayout;
    auto *addBtn = new QPushButton(QStringLiteral("新增"));
    auto *deleteBtn = new QPushButton(QStringLiteral("删除"));
    auto *cancelBtn = new QPushButton(QStringLiteral("取消"));
    auto *okBtn = new QPushButton(QStringLiteral("应用"));
    okBtn->setObjectName(QStringLiteral("applyBtn"));
    okBtn->setDefault(true);

    connect(addBtn, &QPushButton::clicked, this, &ConstraintDialog::onAddConstraint);
    connect(deleteBtn, &QPushButton::clicked, this, &ConstraintDialog::onDeleteConstraint);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(okBtn, &QPushButton::clicked, this, &ConstraintDialog::onAccept);

    buttonRow->addWidget(addBtn);
    buttonRow->addWidget(deleteBtn);
    buttonRow->addStretch();
    buttonRow->addWidget(cancelBtn);
    buttonRow->addWidget(okBtn);
    root->addLayout(buttonRow);
}

void ConstraintDialog::loadConstraints(const QString &createTableSql)
{
    const QStringList lines = createTableSql.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = cleanConstraintLine(rawLine);
        if (!line.startsWith(QStringLiteral("CONSTRAINT "), Qt::CaseInsensitive)) {
            continue;
        }

        ConstraintConfig config;
        QRegularExpressionMatch match;

        match = QRegularExpression(QStringLiteral("^CONSTRAINT\\s+(\\S+)\\s+PRIMARY\\s+KEY\\s*\\(([^)]*)\\)$"),
                                   QRegularExpression::CaseInsensitiveOption)
                    .match(line);
        if (match.hasMatch()) {
            config.name = match.captured(1);
            config.type = QStringLiteral("PRIMARY KEY");
            config.columns = normalizeCsv(match.captured(2));
        } else {
            match = QRegularExpression(QStringLiteral("^CONSTRAINT\\s+(\\S+)\\s+UNIQUE\\s*\\(([^)]*)\\)$"),
                                       QRegularExpression::CaseInsensitiveOption)
                        .match(line);
            if (match.hasMatch()) {
                config.name = match.captured(1);
                config.type = QStringLiteral("UNIQUE");
                config.columns = normalizeCsv(match.captured(2));
            }
        }

        if (config.name.isEmpty()) {
            match = QRegularExpression(QStringLiteral("^CONSTRAINT\\s+(\\S+)\\s+CHECK\\s*\\((.*)\\)$"),
                                       QRegularExpression::CaseInsensitiveOption)
                        .match(line);
            if (match.hasMatch()) {
                config.name = match.captured(1);
                config.type = QStringLiteral("CHECK");
                config.checkClause = match.captured(2).trimmed();
            }
        }

        if (config.name.isEmpty()) {
            match = QRegularExpression(QStringLiteral("^CONSTRAINT\\s+(\\S+)\\s+FOREIGN\\s+KEY\\s*\\(([^)]*)\\)\\s+REFERENCES\\s+(\\S+)\\s*\\(([^)]*)\\)(.*)$"),
                                       QRegularExpression::CaseInsensitiveOption)
                        .match(line);
            if (match.hasMatch()) {
                config.name = match.captured(1);
                config.type = QStringLiteral("FOREIGN KEY");
                config.columns = normalizeCsv(match.captured(2));
                config.referencedTable = match.captured(3).trimmed();
                config.referencedColumns = normalizeCsv(match.captured(4));
                config.onDeleteAction = actionFromTail(match.captured(5), QStringLiteral("DELETE"));
                config.onUpdateAction = actionFromTail(match.captured(5), QStringLiteral("UPDATE"));
            }
        }

        if (!config.name.isEmpty()) {
            config.originalName = config.name;
            m_originalConstraints.append(config);
            appendRow(config);
        }
    }
}

void ConstraintDialog::appendRow(const ConstraintConfig &config)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->blockSignals(true);

    setItem(m_table, row, ColName, config.name);
    m_table->item(row, ColName)->setData(Qt::UserRole, config.originalName);
    m_table->setCellWidget(row, ColType, makeTypeCombo(config.type.isEmpty() ? QStringLiteral("CHECK") : config.type));
    setItem(m_table, row, ColColumns, config.columns);
    setItem(m_table, row, ColCheck, config.checkClause);
    setItem(m_table, row, ColRefTable, config.referencedTable);
    setItem(m_table, row, ColRefColumns, config.referencedColumns);
    m_table->setCellWidget(row, ColOnDelete, makeActionCombo(config.onDeleteAction));
    m_table->setCellWidget(row, ColOnUpdate, makeActionCombo(config.onUpdateAction));

    auto *typeCombo = qobject_cast<QComboBox*>(m_table->cellWidget(row, ColType));
    auto *deleteCombo = qobject_cast<QComboBox*>(m_table->cellWidget(row, ColOnDelete));
    auto *updateCombo = qobject_cast<QComboBox*>(m_table->cellWidget(row, ColOnUpdate));
    if (typeCombo) connect(typeCombo, &QComboBox::currentTextChanged, this, &ConstraintDialog::updatePreview);
    if (deleteCombo) connect(deleteCombo, &QComboBox::currentTextChanged, this, &ConstraintDialog::updatePreview);
    if (updateCombo) connect(updateCombo, &QComboBox::currentTextChanged, this, &ConstraintDialog::updatePreview);

    m_table->blockSignals(false);
}

void ConstraintDialog::onAddConstraint()
{
    ConstraintConfig config;
    config.type = QStringLiteral("CHECK");
    config.name = QStringLiteral("ck_%1_new").arg(m_tableName);
    appendRow(config);
    updatePreview();
}

void ConstraintDialog::onDeleteConstraint()
{
    const int row = m_table->currentRow();
    if (row < 0) {
        return;
    }
    m_table->removeRow(row);
    updatePreview();
}

QList<ConstraintDialog::ConstraintConfig> ConstraintDialog::currentConfigs(QString *error) const
{
    if (error != nullptr) {
        error->clear();
    }

    QList<ConstraintConfig> configs;
    QSet<QString> names;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        ConstraintConfig config;
        config.name = itemText(m_table, row, ColName);
        config.originalName = m_table->item(row, ColName)
                                  ? m_table->item(row, ColName)->data(Qt::UserRole).toString()
                                  : QString();

        auto *typeCombo = qobject_cast<QComboBox*>(m_table->cellWidget(row, ColType));
        config.type = typeCombo ? typeCombo->currentText().trimmed().toUpper() : QStringLiteral("CHECK");
        config.columns = normalizeCsv(itemText(m_table, row, ColColumns));
        config.checkClause = itemText(m_table, row, ColCheck);
        config.referencedTable = itemText(m_table, row, ColRefTable);
        config.referencedColumns = normalizeCsv(itemText(m_table, row, ColRefColumns));

        auto *deleteCombo = qobject_cast<QComboBox*>(m_table->cellWidget(row, ColOnDelete));
        auto *updateCombo = qobject_cast<QComboBox*>(m_table->cellWidget(row, ColOnUpdate));
        config.onDeleteAction = normalizeAction(deleteCombo ? deleteCombo->currentText() : QString());
        config.onUpdateAction = normalizeAction(updateCombo ? updateCombo->currentText() : QString());

        if (config.name.isEmpty()) {
            if (error != nullptr) *error = QStringLiteral("约束名不能为空");
            return {};
        }
        if (names.contains(config.name)) {
            if (error != nullptr) *error = QStringLiteral("约束名 '%1' 重复").arg(config.name);
            return {};
        }
        names.insert(config.name);

        configs.append(config);
    }
    return configs;
}

QString ConstraintDialog::constraintDefinition(const ConstraintConfig &config) const
{
    if (config.type == QStringLiteral("PRIMARY KEY")) {
        if (config.columns.isEmpty()) return {};
        return QStringLiteral("CONSTRAINT %1 PRIMARY KEY (%2)").arg(config.name, config.columns);
    }
    if (config.type == QStringLiteral("UNIQUE")) {
        if (config.columns.isEmpty()) return {};
        return QStringLiteral("CONSTRAINT %1 UNIQUE (%2)").arg(config.name, config.columns);
    }
    if (config.type == QStringLiteral("CHECK")) {
        if (config.checkClause.isEmpty()) return {};
        return QStringLiteral("CONSTRAINT %1 CHECK (%2)").arg(config.name, config.checkClause);
    }
    if (config.type == QStringLiteral("FOREIGN KEY")) {
        if (config.columns.isEmpty() || config.referencedTable.isEmpty() || config.referencedColumns.isEmpty()) {
            return {};
        }
        QString definition = QStringLiteral("CONSTRAINT %1 FOREIGN KEY (%2) REFERENCES %3(%4)")
                                 .arg(config.name,
                                      config.columns,
                                      config.referencedTable,
                                      config.referencedColumns);
        if (!config.onDeleteAction.isEmpty()) {
            definition += QStringLiteral(" ON DELETE %1").arg(config.onDeleteAction);
        }
        if (!config.onUpdateAction.isEmpty()) {
            definition += QStringLiteral(" ON UPDATE %1").arg(config.onUpdateAction);
        }
        return definition;
    }
    return {};
}

QString ConstraintDialog::buildAlterSql(QString *error) const
{
    if (error != nullptr) {
        error->clear();
    }

    QString configError;
    const QList<ConstraintConfig> current = currentConfigs(&configError);
    if (!configError.isEmpty()) {
        if (error != nullptr) *error = configError;
        return {};
    }

    QMap<QString, QString> originalDefinitions;
    for (const ConstraintConfig &config : m_originalConstraints) {
        originalDefinitions.insert(config.originalName, constraintDefinition(config));
    }

    QSet<QString> seenOriginalNames;
    QStringList statements;
    for (const ConstraintConfig &config : current) {
        const QString definition = constraintDefinition(config);
        if (definition.isEmpty()) {
            if (error != nullptr) {
                *error = QStringLiteral("约束 '%1' 信息不完整").arg(config.name);
            }
            return {};
        }

        if (config.originalName.isEmpty()) {
            statements.append(QStringLiteral("ALTER TABLE %1 ADD %2;").arg(m_tableName, definition));
            continue;
        }

        seenOriginalNames.insert(config.originalName);
        if (definition != originalDefinitions.value(config.originalName)) {
            statements.append(QStringLiteral("ALTER TABLE %1 MODIFY CONSTRAINT %2 %3;")
                                  .arg(m_tableName, config.originalName, definition));
        }
    }

    for (const ConstraintConfig &config : m_originalConstraints) {
        if (!seenOriginalNames.contains(config.originalName)) {
            statements.append(QStringLiteral("ALTER TABLE %1 DROP CONSTRAINT %2;")
                                  .arg(m_tableName, config.originalName));
        }
    }

    return statements.join(QLatin1Char('\n'));
}

void ConstraintDialog::updatePreview()
{
    QString error;
    const QString sql = buildAlterSql(&error);
    if (!error.isEmpty()) {
        m_preview->setText(QStringLiteral("-- %1").arg(error));
        return;
    }
    m_preview->setText(sql);
}

void ConstraintDialog::onAccept()
{
    QString error;
    m_generatedSql = buildAlterSql(&error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("约束无效"), error);
        return;
    }
    accept();
}
