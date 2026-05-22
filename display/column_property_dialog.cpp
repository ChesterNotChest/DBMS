#include "column_property_dialog.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace {

QString normalizeType(QString type)
{
    type = type.trimmed().toUpper();
    if (type == QStringLiteral("INTEGER")) {
        return QStringLiteral("INT");
    }
    if (type == QStringLiteral("DOUBLE") || type == QStringLiteral("REAL")) {
        return QStringLiteral("FLOAT");
    }
    if (type == QStringLiteral("VARCHAR2")) {
        return QStringLiteral("VARCHAR");
    }
    return type;
}

QString cleanColumnLine(QString line)
{
    line = line.trimmed();
    if (line.endsWith(QLatin1Char(','))) {
        line.chop(1);
    }
    return line.trimmed();
}

bool isQuotedLiteral(const QString &value)
{
    if (value.length() < 2) {
        return false;
    }
    const QChar first = value.front();
    const QChar last = value.back();
    return (first == QLatin1Char('\'') && last == QLatin1Char('\''))
           || (first == QLatin1Char('"') && last == QLatin1Char('"'));
}

bool isNumericLiteral(const QString &value)
{
    static const QRegularExpression re(QStringLiteral("^[+-]?(?:\\d+\\.\\d+|\\d+|\\.\\d+)$"));
    return re.match(value.trimmed()).hasMatch();
}

void applyColumnDialogStyle(QWidget *widget)
{
    widget->setStyleSheet(R"(
        QDialog {
            background-color: #F8F9FA;
        }
        QLabel {
            font-size: 13px;
            color: #374151;
        }
        QLineEdit {
            border: 1px solid #D1D5DB;
            border-radius: 4px;
            padding: 6px 10px;
            background: white;
            font-size: 13px;
            color: #1F2937;
        }
        QLineEdit:focus {
            border-color: #9CA3AF;
        }
        QLineEdit:disabled {
            background: #F3F4F6;
            color: #9CA3AF;
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
        QCheckBox {
            color: #374151;
            font-size: 12px;
            spacing: 4px;
        }
        QCheckBox::indicator {
            width: 16px;
            height: 16px;
            border: 1.5px solid #9CA3AF;
            border-radius: 2px;
            background: white;
        }
        QCheckBox::indicator:hover {
            border-color: #6B7280;
        }
        QCheckBox::indicator:checked {
            background: #4B5563;
            border-color: #4B5563;
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

ColumnPropertyDialog::ColumnPropertyDialog(const QString &tableName,
                                           const QString &columnName,
                                           const QString &createTableSql,
                                           QWidget *parent)
    : QDialog(parent)
    , m_tableName(tableName)
    , m_columnName(columnName)
{
    setWindowTitle(QStringLiteral("编辑列属性 - %1.%2").arg(tableName, columnName));
    setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint);
    setMinimumSize(560, 420);
    setSizeGripEnabled(true);
    applyColumnDialogStyle(this);

    m_loaded = loadColumn(createTableSql);
    buildUi();
    updatePreview();
}

QString ColumnPropertyDialog::generatedSql() const
{
    return m_generatedSql;
}

bool ColumnPropertyDialog::loadColumn(const QString &createTableSql)
{
    const QString escapedName = QRegularExpression::escape(m_columnName);
    const QRegularExpression lineRe(QStringLiteral("^%1\\s+(.+)$").arg(escapedName),
                                    QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression typeRe(
        QStringLiteral("^(INT|INTEGER|SMALLINT|FLOAT|DOUBLE|REAL|VARCHAR|VARCHAR2)(?:\\s*\\(\\s*(\\d+)\\s*\\))?\\b(.*)$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression notNullRe(QStringLiteral("\\bNOT\\s+NULL\\b"),
                                       QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression defaultRe(QStringLiteral("\\bDEFAULT\\s+(.+?)(?:\\s+AUTO_INCREMENT)?\\s*$"),
                                       QRegularExpression::CaseInsensitiveOption);

    const QStringList lines = createTableSql.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = cleanColumnLine(rawLine);
        if (line.isEmpty()
            || line.startsWith(QStringLiteral("CREATE TABLE"), Qt::CaseInsensitive)
            || line.startsWith(QStringLiteral("CONSTRAINT"), Qt::CaseInsensitive)
            || line == QStringLiteral(");")) {
            continue;
        }

        const QRegularExpressionMatch lineMatch = lineRe.match(line);
        if (!lineMatch.hasMatch()) {
            continue;
        }

        const QString definition = lineMatch.captured(1).trimmed();
        const QRegularExpressionMatch typeMatch = typeRe.match(definition);
        if (!typeMatch.hasMatch()) {
            return false;
        }

        m_original.type = normalizeType(typeMatch.captured(1));
        m_original.length = typeMatch.captured(2).toInt();
        if (m_original.type == QStringLiteral("VARCHAR") && m_original.length <= 0) {
            m_original.length = 255;
        }
        m_original.notNull = notNullRe.match(definition).hasMatch();

        const QRegularExpressionMatch defaultMatch = defaultRe.match(definition);
        if (defaultMatch.hasMatch()) {
            m_original.defaultValue = defaultMatch.captured(1).trimmed();
        }
        return true;
    }

    return false;
}

void ColumnPropertyDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("列属性：%1.%2").arg(m_tableName, m_columnName));
    title->setStyleSheet(QStringLiteral("font-size:20px;font-weight:700;color:#1F2937;"));
    root->addWidget(title);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignTop);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);

    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItems({QStringLiteral("INT"),
                           QStringLiteral("SMALLINT"),
                           QStringLiteral("FLOAT"),
                           QStringLiteral("VARCHAR")});
    if (m_loaded && !m_original.type.isEmpty()) {
        m_typeCombo->setCurrentText(m_original.type);
    }
    form->addRow(QStringLiteral("数据类型"), m_typeCombo);

    m_lengthEdit = new QLineEdit(this);
    m_lengthEdit->setPlaceholderText(QStringLiteral("255"));
    if (m_loaded && m_original.type == QStringLiteral("VARCHAR") && m_original.length > 0) {
        m_lengthEdit->setText(QString::number(m_original.length));
    }
    form->addRow(QStringLiteral("长度"), m_lengthEdit);

    m_notNullCheck = new QCheckBox(QStringLiteral("NOT NULL"), this);
    m_notNullCheck->setChecked(m_loaded && m_original.notNull);
    form->addRow(QStringLiteral("空值约束"), m_notNullCheck);

    m_defaultEdit = new QLineEdit(this);
    m_defaultEdit->setPlaceholderText(QStringLiteral("留空表示无默认值"));
    if (m_loaded && !m_original.defaultValue.isEmpty()) {
        m_defaultEdit->setText(m_original.defaultValue);
    }
    form->addRow(QStringLiteral("默认值"), m_defaultEdit);

    root->addLayout(form);

    auto *sqlLabel = new QLabel(QStringLiteral("SQL 预览"));
    sqlLabel->setStyleSheet(QStringLiteral("font-weight:600;font-size:13px;color:#4B5563;"));
    root->addWidget(sqlLabel);

    m_preview = new QTextEdit(this);
    m_preview->setReadOnly(true);
    m_preview->setMinimumHeight(110);
    m_preview->setPlaceholderText(QStringLiteral("生成的 ALTER TABLE 语句"));
    root->addWidget(m_preview, 1);

    auto *buttonRow = new QHBoxLayout;
    auto *cancelBtn = new QPushButton(QStringLiteral("取消"), this);
    auto *applyBtn = new QPushButton(QStringLiteral("应用"), this);
    applyBtn->setObjectName(QStringLiteral("applyBtn"));
    applyBtn->setDefault(true);
    buttonRow->addStretch();
    buttonRow->addWidget(cancelBtn);
    buttonRow->addWidget(applyBtn);
    root->addLayout(buttonRow);

    connect(m_typeCombo, &QComboBox::currentTextChanged, this, [this]() {
        updateLengthEnabled();
        updatePreview();
    });
    connect(m_lengthEdit, &QLineEdit::textChanged, this, &ColumnPropertyDialog::updatePreview);
    connect(m_notNullCheck, &QCheckBox::toggled, this, &ColumnPropertyDialog::updatePreview);
    connect(m_defaultEdit, &QLineEdit::textChanged, this, &ColumnPropertyDialog::updatePreview);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(applyBtn, &QPushButton::clicked, this, &ColumnPropertyDialog::onAccept);

    updateLengthEnabled();
}

void ColumnPropertyDialog::updateLengthEnabled()
{
    const bool varchar = m_typeCombo != nullptr
                         && m_typeCombo->currentText().compare(QStringLiteral("VARCHAR"),
                                                               Qt::CaseInsensitive) == 0;
    m_lengthEdit->setEnabled(varchar);
    if (!varchar) {
        m_lengthEdit->clear();
    } else if (m_lengthEdit->text().trimmed().isEmpty()) {
        m_lengthEdit->setText(QStringLiteral("255"));
    }
}

QString ColumnPropertyDialog::buildTypeSpec(const QString &type, int length) const
{
    if (type == QStringLiteral("VARCHAR")) {
        return QStringLiteral("VARCHAR(%1)").arg(length);
    }
    return type;
}

QString ColumnPropertyDialog::formatDefaultValue(const QString &value) const
{
    QString text = value.trimmed();
    if (text.isEmpty()) {
        return {};
    }

    const QString upper = text.toUpper();
    if (isQuotedLiteral(text)
        || isNumericLiteral(text)
        || upper == QStringLiteral("NULL")
        || upper.startsWith(QStringLiteral("CURRENT_"))) {
        return text;
    }

    if (m_typeCombo->currentText().compare(QStringLiteral("VARCHAR"), Qt::CaseInsensitive) == 0) {
        text.replace(QStringLiteral("'"), QStringLiteral("''"));
        return QStringLiteral("'%1'").arg(text);
    }

    return text;
}

QString ColumnPropertyDialog::buildAlterSql(QString *error) const
{
    if (error != nullptr) {
        error->clear();
    }
    if (!m_loaded) {
        if (error != nullptr) {
            *error = QStringLiteral("SHOW CREATE TABLE 输出中未找到列 '%1'").arg(m_columnName);
        }
        return {};
    }

    const QString type = m_typeCombo->currentText().trimmed().toUpper();
    int length = 0;
    if (type == QStringLiteral("VARCHAR")) {
        bool ok = false;
        length = m_lengthEdit->text().trimmed().toInt(&ok);
        if (!ok || length <= 0) {
            if (error != nullptr) {
                *error = QStringLiteral("VARCHAR 长度必须是正整数");
            }
            return {};
        }
    }

    QStringList statements;
    if (type != m_original.type || (type == QStringLiteral("VARCHAR") && length != m_original.length)) {
        statements.append(QStringLiteral("ALTER TABLE %1 ALTER COLUMN %2 TYPE %3;")
                              .arg(m_tableName, m_columnName, buildTypeSpec(type, length)));
    }

    const bool notNull = m_notNullCheck->isChecked();
    if (notNull != m_original.notNull) {
        statements.append(QStringLiteral("ALTER TABLE %1 ALTER COLUMN %2 %3 NOT NULL;")
                              .arg(m_tableName,
                                   m_columnName,
                                   notNull ? QStringLiteral("SET") : QStringLiteral("DROP")));
    }

    const QString originalDefault = m_original.defaultValue.trimmed();
    const QString defaultInput = m_defaultEdit->text().trimmed();
    if (defaultInput.isEmpty()) {
        if (!originalDefault.isEmpty()) {
            statements.append(QStringLiteral("ALTER TABLE %1 ALTER COLUMN %2 DROP DEFAULT;")
                                  .arg(m_tableName, m_columnName));
        }
    } else {
        const QString formattedDefault = formatDefaultValue(defaultInput);
        if (formattedDefault != originalDefault) {
            statements.append(QStringLiteral("ALTER TABLE %1 ALTER COLUMN %2 SET DEFAULT %3;")
                                  .arg(m_tableName, m_columnName, formattedDefault));
        }
    }

    return statements.join(QLatin1Char('\n'));
}

void ColumnPropertyDialog::updatePreview()
{
    QString error;
    const QString sql = buildAlterSql(&error);
    if (!error.isEmpty()) {
        m_preview->setText(QStringLiteral("-- %1").arg(error));
        return;
    }
    m_preview->setText(sql);
}

void ColumnPropertyDialog::onAccept()
{
    QString error;
    m_generatedSql = buildAlterSql(&error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("列属性无效"), error);
        return;
    }
    accept();
}
