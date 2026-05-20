#include "add_column_dialog.h"
#include <QFrame>
#include <QMessageBox>
#include <QGraphicsDropShadowEffect>

static QString findDataRoot()
{
    const QString envRoot = qEnvironmentVariable("DBMS_DATA_ROOT");
    if (!envRoot.trimmed().isEmpty())
        return QDir::cleanPath(envRoot);
#ifdef DBMS_REPO_ROOT
    return QDir::cleanPath(QDir(QString::fromUtf8(DBMS_REPO_ROOT)).absoluteFilePath("data"));
#else
    QString cwd = QDir::current().absolutePath();
    QStringList candidates = {
        cwd + "/data",
        QDir(cwd).absoluteFilePath("../data"),
        QDir(cwd).absoluteFilePath("../../data"),
        QDir::rootPath() + "DBMS/data"
    };
    for (const QString &p : candidates) {
        if (QDir(p).exists())
            return QDir::cleanPath(p);
    }
    return QDir::cleanPath(cwd + "/data");
#endif
}

QString AddColumnDialog::defaultDataRoot()
{
    static const QString root = findDataRoot();
    return root;
}

static QStringList listDbNames()
{
    QStringList dbs;
    QDir dataDir(findDataRoot());
    if (!dataDir.exists()) return dbs;
    for (const QFileInfo &fi : dataDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        dbs.append(fi.fileName());
    return dbs;
}

static QFrame *makeSeparator(QWidget *parent = nullptr)
{
    Q_UNUSED(parent);
    auto *sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("QFrame { background-color: #E5E7EB; max-height: 1px; border: none; }");
    return sep;
}

static QLabel *makeSectionTitle(const QString &text)
{
    auto *label = new QLabel(text);
    label->setStyleSheet(
        "font-size: 13px; font-weight: 700; "
        "color: #6B7280; letter-spacing: 0.5px; "
        "padding: 0; margin: 0;"
    );
    return label;
}

AddColumnDialog::AddColumnDialog(const QString &currentDb, QWidget *parent)
    : QDialog(parent)
    , m_currentDb(currentDb)
{
    setWindowTitle(QString::fromUtf8("新增列"));
    setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint);
    setMinimumSize(700, 780);
    setMaximumSize(760, 860);
    setSizeGripEnabled(true);
    buildUi();
}

void AddColumnDialog::buildUi()
{
    setStyleSheet(R"(
        QDialog {
            background-color: #FFFFFF;
        }
        QLabel {
            font-size: 14px;
            color: #374151;
        }
        QLineEdit {
            border: 1px solid #D1D5DB;
            border-radius: 6px;
            padding: 10px 14px;
            background: #FAFBFC;
            font-size: 14px;
            color: #111827;
            min-height: 22px;
        }
        QLineEdit:focus {
            border-color: #6366F1;
            background: #FFFFFF;
        }
        QLineEdit:hover:not(:focus) {
            border-color: #9CA3AF;
        }
        QComboBox {
            border: 1px solid #D1D5DB;
            border-radius: 6px;
            padding: 10px 14px;
            padding-right: 36px;
            background: #FAFBFC;
            font-size: 14px;
            color: #111827;
            min-height: 22px;
        }
        QComboBox:focus {
            border-color: #6366F1;
            background: #FFFFFF;
        }
        QComboBox:hover:not(:focus) {
            border-color: #9CA3AF;
        }
        QComboBox::drop-down {
            width: 32px;
            border: none;
            subcontrol-position: right center;
        }
        QComboBox::down-arrow {
            width: 10px;
            height: 10px;
        }
        QComboBox QAbstractItemView {
            background: #FFFFFF;
            border: 1px solid #D1D5DB;
            border-radius: 4px;
            font-size: 14px;
            color: #111827;
            selection-background-color: #EEF2FF;
            selection-color: #111827;
            padding: 4px 0;
            outline: none;
        }
        QComboBox QAbstractItemView::item {
            padding: 8px 16px;
            min-height: 28px;
        }
        QCheckBox {
            color: #374151;
            font-size: 14px;
            spacing: 10px;
        }
        QCheckBox::indicator {
            width: 20px;
            height: 20px;
            border: 2px solid #D1D5DB;
            border-radius: 4px;
            background: #FFFFFF;
        }
        QCheckBox::indicator:hover {
            border-color: #818CF8;
        }
        QCheckBox::indicator:checked {
            background: #6366F1;
            border-color: #6366F1;
        }
        QPushButton {
            background: #F3F4F6;
            color: #374151;
            border: 1px solid #D1D5DB;
            border-radius: 8px;
            padding: 10px 32px;
            font-size: 14px;
            font-weight: 600;
            min-height: 20px;
        }
        QPushButton:hover {
            background: #E5E7EB;
        }
        QPushButton:pressed {
            background: #D1D5DB;
        }
        QPushButton#okBtn {
            background: #6366F1;
            color: white;
            border: none;
        }
        QPushButton#okBtn:hover {
            background: #4F46E5;
        }
        QPushButton#okBtn:pressed {
            background: #4338CA;
        }
        QTextEdit {
            background: #F8FAFC;
            border: 1px solid #E2E8F0;
            border-radius: 6px;
            padding: 12px 14px;
            font-family: "Consolas", "Courier New", monospace;
            font-size: 13px;
            color: #1E293B;
        }
    )");

    auto *root = new QVBoxLayout(this);
    root->setSpacing(0);
    root->setContentsMargins(32, 28, 32, 24);

    // ══════ Title ══════
    auto *titleRow = new QHBoxLayout;
    auto *titleIcon = new QLabel(QString::fromUtf8("＋"));
    titleIcon->setStyleSheet("font-size: 24px; font-weight: 700; color: #6366F1; padding-right: 8px;");
    auto *titleText = new QLabel(QString::fromUtf8("新增列"));
    titleText->setStyleSheet("font-size: 24px; font-weight: 700; color: #111827;");
    titleRow->addWidget(titleIcon);
    titleRow->addWidget(titleText);
    titleRow->addStretch();
    root->addLayout(titleRow);

    auto *subtitle = new QLabel(QString::fromUtf8("在表中添加一个新字段，配置数据类型和约束"));
    subtitle->setStyleSheet("font-size: 13px; color: #9CA3AF; padding: 4px 0 12px 0;");
    root->addWidget(subtitle);
    root->addWidget(makeSeparator());
    root->addSpacing(20);

    // ══════ 基本信息 ══════
    auto *basicSection = new QVBoxLayout;
    basicSection->setSpacing(0);

    root->addWidget(makeSectionTitle(QString::fromUtf8("基本信息")));
    root->addSpacing(12);

    // 列名行
    auto *nameRow = new QHBoxLayout;
    nameRow->setSpacing(16);
    auto *nameLabel = new QLabel(QString::fromUtf8("列名"));
    nameLabel->setFixedWidth(80);
    nameLabel->setStyleSheet("font-size: 14px; font-weight: 600; color: #374151;");
    m_nameEdit = new QLineEdit;
    m_nameEdit->setPlaceholderText(QString::fromUtf8("输入字段名"));
    m_nameEdit->setMaxLength(64);
    nameRow->addWidget(nameLabel);
    nameRow->addWidget(m_nameEdit, 1);
    root->addLayout(nameRow);
    root->addSpacing(14);

    // 数据类型行
    auto *typeRow = new QHBoxLayout;
    typeRow->setSpacing(16);
    auto *typeLabel = new QLabel(QString::fromUtf8("数据类型"));
    typeLabel->setFixedWidth(80);
    typeLabel->setStyleSheet("font-size: 14px; font-weight: 600; color: #374151;");
    m_typeCombo = new QComboBox;
    m_typeCombo->addItems({"VARCHAR", "INT", "BIGINT", "FLOAT", "DOUBLE", "DECIMAL",
                           "CHAR", "TEXT", "DATE", "DATETIME", "TIME", "BOOLEAN", "BLOB"});
    m_typeCombo->setCurrentText("VARCHAR");
    auto *lenLabel = new QLabel(QString::fromUtf8("长度"));
    lenLabel->setStyleSheet("font-size: 14px; font-weight: 600; color: #374151;");
    m_lengthEdit = new QLineEdit("255");
    m_lengthEdit->setFixedWidth(100);
    m_lengthEdit->setAlignment(Qt::AlignCenter);
    typeRow->addWidget(typeLabel);
    typeRow->addWidget(m_typeCombo, 1);
    typeRow->addWidget(lenLabel);
    typeRow->addWidget(m_lengthEdit);
    root->addLayout(typeRow);
    root->addSpacing(24);

    // ══════ 约束 ══════
    root->addWidget(makeSeparator());
    root->addSpacing(16);
    root->addWidget(makeSectionTitle(QString::fromUtf8("约束选项")));
    root->addSpacing(12);

    auto *constrRow = new QHBoxLayout;
    constrRow->setSpacing(0);

    m_notNullCheck = new QCheckBox(QString::fromUtf8("NOT NULL"));
    m_pkCheck = new QCheckBox(QString::fromUtf8("主键 (PRIMARY KEY)"));
    m_uniqueCheck = new QCheckBox(QString::fromUtf8("唯一 (UNIQUE)"));

    constrRow->addWidget(m_notNullCheck);
    constrRow->addSpacing(40);
    constrRow->addWidget(m_pkCheck);
    constrRow->addSpacing(40);
    constrRow->addWidget(m_uniqueCheck);
    constrRow->addStretch();
    root->addLayout(constrRow);
    root->addSpacing(24);

    // ══════ 默认值 ══════
    root->addWidget(makeSeparator());
    root->addSpacing(16);
    root->addWidget(makeSectionTitle(QString::fromUtf8("默认值")));
    root->addSpacing(12);

    m_defaultEdit = new QLineEdit;
    m_defaultEdit->setPlaceholderText(QString::fromUtf8("0, CURRENT_DATE, '默认文本', CURRENT_TIMESTAMP"));
    root->addWidget(m_defaultEdit);
    root->addSpacing(24);

    // ══════ 外键 ══════
    root->addWidget(makeSeparator());
    root->addSpacing(16);
    root->addWidget(makeSectionTitle(QString::fromUtf8("外键配置")));
    root->addSpacing(12);

    // 外键表
    auto *fkTableRow = new QHBoxLayout;
    fkTableRow->setSpacing(16);
    auto *fkTableLabel = new QLabel(QString::fromUtf8("引用表"));
    fkTableLabel->setFixedWidth(80);
    fkTableLabel->setStyleSheet("font-size: 14px; font-weight: 600; color: #374151;");
    m_refTableCombo = new QComboBox;
    fkTableRow->addWidget(fkTableLabel);
    fkTableRow->addWidget(m_refTableCombo, 1);
    root->addLayout(fkTableRow);
    root->addSpacing(12);

    // 外键字段
    auto *fkColRow = new QHBoxLayout;
    fkColRow->setSpacing(16);
    auto *fkColLabel = new QLabel(QString::fromUtf8("引用字段"));
    fkColLabel->setFixedWidth(80);
    fkColLabel->setStyleSheet("font-size: 14px; font-weight: 600; color: #374151;");
    m_refColumnCombo = new QComboBox;
    fkColRow->addWidget(fkColLabel);
    fkColRow->addWidget(m_refColumnCombo, 1);
    root->addLayout(fkColRow);
    root->addSpacing(24);

    // ══════ CHECK ══════
    root->addWidget(makeSeparator());
    root->addSpacing(16);
    root->addWidget(makeSectionTitle(QString::fromUtf8("CHECK 约束（可选）")));
    root->addSpacing(12);

    m_checkEdit = new QLineEdit;
    m_checkEdit->setPlaceholderText(QString::fromUtf8("例如：age >= 0 AND age <= 150"));
    root->addWidget(m_checkEdit);
    root->addSpacing(24);

    // ══════ SQL 预览 ══════
    root->addWidget(makeSeparator());
    root->addSpacing(16);
    auto *sqlHeaderRow = new QHBoxLayout;
    auto *sqlLabel = new QLabel(QString::fromUtf8("SQL 预览"));
    sqlLabel->setStyleSheet("font-size: 13px; font-weight: 700; color: #6B7280; letter-spacing: 0.5px;");
    sqlHeaderRow->addWidget(sqlLabel);
    sqlHeaderRow->addStretch();
    root->addLayout(sqlHeaderRow);
    root->addSpacing(10);

    m_sqlPreview = new QTextEdit;
    m_sqlPreview->setReadOnly(true);
    m_sqlPreview->setMinimumHeight(85);
    m_sqlPreview->setMaximumHeight(120);
    m_sqlPreview->setPlaceholderText(QString::fromUtf8("自动生成的 ALTER TABLE 语句"));
    root->addWidget(m_sqlPreview);
    root->addSpacing(20);

    // ══════ 按钮 ══════
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(12);
    btnRow->addStretch();

    auto *cancelBtn = new QPushButton(QString::fromUtf8("取消"));
    cancelBtn->setCursor(Qt::PointingHandCursor);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    auto *okBtn = new QPushButton(QString::fromUtf8("确定添加"));
    okBtn->setObjectName("okBtn");
    okBtn->setCursor(Qt::PointingHandCursor);
    connect(okBtn, &QPushButton::clicked, this, [this]() {
        if (m_nameEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("请输入列名"));
            return;
        }
        accept();
    });

    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(okBtn);
    root->addLayout(btnRow);

    // ══════ 加载外键数据 ══════
    populateRefTables();

    // ══════ 信号 ══════
    connect(m_refTableCombo, &QComboBox::currentTextChanged,
            this, &AddColumnDialog::onReferenceTableChanged);
    connect(m_nameEdit, &QLineEdit::textChanged, this, &AddColumnDialog::updateSqlPreview);
    connect(m_typeCombo, &QComboBox::currentTextChanged, this, &AddColumnDialog::updateSqlPreview);
    connect(m_lengthEdit, &QLineEdit::textChanged, this, &AddColumnDialog::updateSqlPreview);
    connect(m_notNullCheck, &QCheckBox::toggled, this, &AddColumnDialog::updateSqlPreview);
    connect(m_pkCheck, &QCheckBox::toggled, this, &AddColumnDialog::updateSqlPreview);
    connect(m_uniqueCheck, &QCheckBox::toggled, this, &AddColumnDialog::updateSqlPreview);
    connect(m_defaultEdit, &QLineEdit::textChanged, this, &AddColumnDialog::updateSqlPreview);
    connect(m_refTableCombo, &QComboBox::currentTextChanged, this, &AddColumnDialog::updateSqlPreview);
    connect(m_refColumnCombo, &QComboBox::currentTextChanged, this, &AddColumnDialog::updateSqlPreview);
    connect(m_checkEdit, &QLineEdit::textChanged, this, &AddColumnDialog::updateSqlPreview);

    updateSqlPreview();
}

void AddColumnDialog::populateRefTables()
{
    m_refTableCombo->clear();
    m_refTableCombo->addItem(QString());

    QString dbName = m_currentDb;
    if (dbName.isEmpty()) {
        QStringList dbs = listDbNames();
        if (!dbs.isEmpty())
            dbName = dbs.first();
    }
    if (dbName.isEmpty()) return;

    const QString dataRoot = defaultDataRoot();
    const QString tabPath = QDir(dataRoot).absoluteFilePath(
        dbName + "/" + dbName + ".tab");

    QFile f(tabPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QDir dbDir(QDir(dataRoot).absoluteFilePath(dbName));
        QStringList tables;
        for (const QFileInfo &fi : dbDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (QFileInfo::exists(fi.absoluteFilePath() + "/table.meta"))
                tables.append(fi.fileName());
        }
        if (!tables.isEmpty()) {
            tables.sort();
            for (const QString &t : tables)
                m_refTableCombo->addItem(t);
        }
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) return;

    const QJsonArray rows = doc.object().value("rows").toArray();
    for (const QJsonValue &rv : rows) {
        const QJsonArray cols = rv.toArray();
        if (!cols.isEmpty()) {
            const QString name = cols[0].toString().trimmed();
            if (!name.isEmpty())
                m_refTableCombo->addItem(name);
        }
    }
}

void AddColumnDialog::populateRefColumns()
{
    m_refColumnCombo->clear();
    const QString tableName = m_refTableCombo->currentText().trimmed();
    if (tableName.isEmpty()) return;

    QString dbName = m_currentDb;
    if (dbName.isEmpty()) {
        QStringList dbs = listDbNames();
        if (!dbs.isEmpty())
            dbName = dbs.first();
        else return;
    }

    const QString dataRoot = defaultDataRoot();
    const QString metaPath = QDir(dataRoot).absoluteFilePath(
        dbName + "/" + tableName + "/table.meta");

    QFile f(metaPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) return;

    const QJsonArray rows = doc.object().value("rows").toArray();
    m_refColumnCombo->addItem(QString());
    for (const QJsonValue &rv : rows) {
        const QJsonArray row = rv.toArray();
        if (!row.isEmpty()) {
            const QString colName = row[0].toString().trimmed();
            if (!colName.isEmpty())
                m_refColumnCombo->addItem(colName);
        }
    }
}

void AddColumnDialog::onReferenceTableChanged()
{
    populateRefColumns();
    updateSqlPreview();
}

void AddColumnDialog::updateSqlPreview()
{
    const QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty()) {
        m_sqlPreview->clear();
        return;
    }

    const QString type = m_typeCombo->currentText();
    const QString len = m_lengthEdit->text().trimmed();

    QString fullType = type;
    if (!len.isEmpty() && (type == "VARCHAR" || type == "CHAR" || type == "DECIMAL"))
        fullType = QString("%1(%2)").arg(type, len);

    QStringList parts;
    parts.append(QString("ALTER TABLE <表名> ADD %1 %2").arg(name, fullType));

    if (m_notNullCheck->isChecked())
        parts.append("NOT NULL");
    if (m_pkCheck->isChecked())
        parts.append("PRIMARY KEY");
    if (m_uniqueCheck->isChecked() && !m_pkCheck->isChecked())
        parts.append("UNIQUE");

    const QString dv = m_defaultEdit->text().trimmed();
    if (!dv.isEmpty()) {
        if (dv.compare("CURRENT_DATE", Qt::CaseInsensitive) == 0 ||
            dv.compare("CURRENT_TIME", Qt::CaseInsensitive) == 0 ||
            dv.compare("CURRENT_TIMESTAMP", Qt::CaseInsensitive) == 0) {
            parts.append(QString("DEFAULT %1").arg(dv.toUpper()));
        } else {
            bool isNum;
            dv.toDouble(&isNum);
            if (isNum || dv.compare("NULL", Qt::CaseInsensitive) == 0)
                parts.append(QString("DEFAULT %1").arg(dv));
            else
                parts.append(QString("DEFAULT '%1'").arg(dv));
        }
    }

    const QString refTable = m_refTableCombo->currentText().trimmed();
    const QString refCol = m_refColumnCombo->currentText().trimmed();
    if (!refTable.isEmpty() && !refCol.isEmpty())
        parts.append(QString("REFERENCES %1(%2)").arg(refTable, refCol));

    const QString checkStr = m_checkEdit->text().trimmed();
    if (!checkStr.isEmpty())
        parts.append(QString("CHECK (%1)").arg(checkStr));

    m_sqlPreview->setText(parts.join(" "));
}

ColumnConfig AddColumnDialog::getConfig() const
{
    ColumnConfig cfg;
    cfg.name = m_nameEdit->text().trimmed();
    cfg.type = m_typeCombo->currentText();
    const QString lenText = m_lengthEdit->text().trimmed();
    cfg.length = lenText.isEmpty() ? 0 : lenText.toInt();
    cfg.notNull = m_notNullCheck->isChecked();
    cfg.primaryKey = m_pkCheck->isChecked();
    cfg.unique = m_uniqueCheck->isChecked();
    cfg.referencedTable = m_refTableCombo->currentText().trimmed();
    const QString refCol = m_refColumnCombo->currentText().trimmed();
    if (!refCol.isEmpty())
        cfg.referencedColumns = { refCol };
    cfg.checkConstraint = m_checkEdit->text().trimmed();
    cfg.defaultValue = m_defaultEdit->text().trimmed();
    return cfg;
}