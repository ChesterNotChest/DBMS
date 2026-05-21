#include "add_column_dialog.h"
#include <QMessageBox>
#include <QTimer>

// ── Column indices ──
enum FieldCol {
    ColName     = 0,
    ColType     = 1,
    ColLength   = 2,
    ColNotNull  = 3,
    ColPk       = 4,
    ColUnique   = 5,
    ColDefault  = 6,
    ColFkTable  = 7,
    ColFkColumn = 8,
    ColCheck    = 9,
    ColCount    = 10
};

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

static QWidget *makeCheckCell(QCheckBox *&cb)
{
    cb = new QCheckBox;
    cb->setFocusPolicy(Qt::StrongFocus);
    auto *w = new QWidget;
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setAlignment(Qt::AlignCenter);
    l->addWidget(cb);
    return w;
}

AddColumnDialog::AddColumnDialog(const QString &currentDb, QWidget *parent)
    : QDialog(parent)
    , m_currentDb(currentDb)
{
    setWindowTitle(QString::fromUtf8("新增列"));
    setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint);
    setMinimumSize(960, 640);
    setSizeGripEnabled(true);
    buildUi();
}

void AddColumnDialog::buildUi()
{
    setStyleSheet(R"(
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
        QPushButton#okBtn {
            background: #4B5563;
            color: white;
            border-color: #4B5563;
        }
        QPushButton#okBtn:hover {
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

    auto *root = new QVBoxLayout(this);
    root->setSpacing(12);
    root->setContentsMargins(20, 20, 20, 20);

    // ── Title ──
    auto *title = new QLabel(QString::fromUtf8("新增列"));
    title->setStyleSheet("font-size: 20px; font-weight: 700; color: #1F2937;");
    root->addWidget(title);

    // ── Field table ──
    m_fieldTable = new QTableWidget(0, ColCount);
    m_fieldTable->setHorizontalHeaderLabels({
        QString::fromUtf8("字段名"),
        QString::fromUtf8("数据类型"),
        QString::fromUtf8("长度"),
        QString::fromUtf8("NOT NULL"),
        QString::fromUtf8("主键"),
        QString::fromUtf8("唯一"),
        QString::fromUtf8("默认值"),
        QString::fromUtf8("外键表"),
        QString::fromUtf8("外键字段"),
        QString::fromUtf8("约束")
    });
    m_fieldTable->setColumnWidth(ColName,     130);
    m_fieldTable->setColumnWidth(ColType,     110);
    m_fieldTable->setColumnWidth(ColLength,   55);
    m_fieldTable->setColumnWidth(ColNotNull,  65);
    m_fieldTable->setColumnWidth(ColPk,       65);
    m_fieldTable->setColumnWidth(ColUnique,   65);
    m_fieldTable->setColumnWidth(ColDefault,  110);
    m_fieldTable->setColumnWidth(ColFkTable,  120);
    m_fieldTable->setColumnWidth(ColFkColumn, 120);
    m_fieldTable->horizontalHeader()->setStretchLastSection(true);

    m_fieldTable->verticalHeader()->setDefaultSectionSize(30);
    m_fieldTable->verticalHeader()->setMinimumSectionSize(26);
    m_fieldTable->verticalHeader()->setVisible(false);
    m_fieldTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fieldTable->setAlternatingRowColors(true);

    connect(m_fieldTable, &QTableWidget::cellChanged, this, &AddColumnDialog::onCellChanged);

    root->addWidget(m_fieldTable, 1);

    // ── Button row ──
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(10);

    auto *addBtn = new QPushButton(QString::fromUtf8("＋ 添加行"));
    addBtn->setToolTip(QString::fromUtf8("添加一个新字段行"));
    connect(addBtn, &QPushButton::clicked, this, &AddColumnDialog::onAddRow);

    auto *delBtn = new QPushButton(QString::fromUtf8("－ 删除行"));
    delBtn->setToolTip(QString::fromUtf8("删除当前选中的行"));
    connect(delBtn, &QPushButton::clicked, this, &AddColumnDialog::onDeleteRow);

    auto *clrBtn = new QPushButton(QString::fromUtf8("清空所有"));
    connect(clrBtn, &QPushButton::clicked, this, &AddColumnDialog::onClearAll);

    auto *cancelBtn = new QPushButton(QString::fromUtf8("取消"));
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    m_okBtn = new QPushButton(QString::fromUtf8("确定添加"));
    m_okBtn->setObjectName("okBtn");
    connect(m_okBtn, &QPushButton::clicked, this, &AddColumnDialog::onAccept);

    btnRow->addWidget(addBtn);
    btnRow->addWidget(delBtn);
    btnRow->addWidget(clrBtn);
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(m_okBtn);
    root->addLayout(btnRow);

    // ── SQL Preview ──
    auto *sqlLabel = new QLabel(QString::fromUtf8("SQL 预览"));
    sqlLabel->setStyleSheet("font-weight: 600; font-size: 13px; color: #4B5563;");
    root->addWidget(sqlLabel);

    m_sqlPreview = new QTextEdit;
    m_sqlPreview->setReadOnly(true);
    m_sqlPreview->setMinimumHeight(80);
    m_sqlPreview->setMaximumHeight(140);
    m_sqlPreview->setPlaceholderText(QString::fromUtf8("自动生成的 ALTER TABLE 语句将显示在这里..."));
    root->addWidget(m_sqlPreview);

    // ── Initial rows ──
    for (int i = 0; i < 2; ++i)
        onAddRow();

    // 延迟刷新外键下拉（确保对话框初始化完成后正确加载）
    QTimer::singleShot(0, this, [this]() {
        for (int row = 0; row < m_fieldTable->rowCount(); ++row) {
            auto *combo = qobject_cast<QComboBox*>(m_fieldTable->cellWidget(row, ColFkTable));
            if (combo)
                populateRefTables(combo);
        }
        updateSqlPreview();
    });
}

// ── Populate foreign key table dropdown ──
void AddColumnDialog::populateRefTables(QComboBox *combo)
{
    if (!combo) return;
    combo->clear();
    combo->addItem(QString());

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
        if (!dbDir.exists()) return;
        QStringList tables;
        for (const QFileInfo &fi : dbDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (QFileInfo::exists(fi.absoluteFilePath() + "/table.meta"))
                tables.append(fi.fileName());
        }
        if (!tables.isEmpty()) {
            tables.sort();
            for (const QString &t : tables)
                combo->addItem(t);
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
                combo->addItem(name);
        }
    }
}

// ── Populate foreign key column dropdown ──
void AddColumnDialog::populateRefColumns(QComboBox *refTableCombo, QComboBox *refColumnCombo)
{
    if (!refTableCombo || !refColumnCombo) return;
    refColumnCombo->clear();
    const QString tableName = refTableCombo->currentText().trimmed();
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
    refColumnCombo->addItem(QString());
    for (const QJsonValue &rv : rows) {
        const QJsonArray row = rv.toArray();
        if (!row.isEmpty()) {
            const QString colName = row[0].toString().trimmed();
            if (!colName.isEmpty())
                refColumnCombo->addItem(colName);
        }
    }
}

// ── Add a new row ──
void AddColumnDialog::onAddRow()
{
    const int row = m_fieldTable->rowCount();
    m_fieldTable->insertRow(row);
    m_fieldTable->setRowHeight(row, 30);
    m_fieldTable->blockSignals(true);

    // Col 0: field name
    auto *nameItem = new QTableWidgetItem;
    nameItem->setTextAlignment(Qt::AlignCenter);
    m_fieldTable->setItem(row, ColName, nameItem);

    // Col 1: type combo
    auto *typeCombo = new QComboBox;
    typeCombo->addItems({"INT", "BIGINT", "FLOAT", "DOUBLE", "DECIMAL",
                         "VARCHAR", "CHAR", "TEXT", "DATE", "DATETIME",
                         "TIME", "BOOLEAN", "BLOB"});
    typeCombo->setCurrentText("VARCHAR");
    m_fieldTable->setCellWidget(row, ColType, typeCombo);
    connect(typeCombo, &QComboBox::currentTextChanged, this, [this, row]() {
        onCellChanged(row, ColLength);
    });

    // Col 2: length
    auto *lenItem = new QTableWidgetItem(QStringLiteral("255"));
    lenItem->setTextAlignment(Qt::AlignCenter);
    m_fieldTable->setItem(row, ColLength, lenItem);

    // Col 3: NOT NULL checkbox
    QCheckBox *nnCb = nullptr;
    m_fieldTable->setCellWidget(row, ColNotNull, makeCheckCell(nnCb));
    connect(nnCb, &QCheckBox::toggled, this, [this, row]() { onCellChanged(row, ColNotNull); });

    // Col 4: PK checkbox
    QCheckBox *pkCb = nullptr;
    m_fieldTable->setCellWidget(row, ColPk, makeCheckCell(pkCb));
    connect(pkCb, &QCheckBox::toggled, this, [this, row](bool checked) {
        if (checked) {
            for (int r = 0; r < m_fieldTable->rowCount(); ++r) {
                if (r == row) continue;
                auto *w = m_fieldTable->cellWidget(r, ColPk);
                if (w && w->layout() && w->layout()->count() > 0) {
                    auto *cb = qobject_cast<QCheckBox*>(w->layout()->itemAt(0)->widget());
                    if (cb) {
                        cb->blockSignals(true);
                        cb->setChecked(false);
                        cb->blockSignals(false);
                    }
                }
            }
        }
        onCellChanged(row, ColPk);
    });

    // Col 5: UNIQUE checkbox
    QCheckBox *uqCb = nullptr;
    m_fieldTable->setCellWidget(row, ColUnique, makeCheckCell(uqCb));
    connect(uqCb, &QCheckBox::toggled, this, [this, row]() { onCellChanged(row, ColUnique); });

    // Col 6: default value
    auto *defItem = new QTableWidgetItem;
    defItem->setTextAlignment(Qt::AlignCenter);
    m_fieldTable->setItem(row, ColDefault, defItem);

    // Col 7: FK table combo
    auto *fkTableCombo = new QComboBox;
    populateRefTables(fkTableCombo);
    m_fieldTable->setCellWidget(row, ColFkTable, fkTableCombo);
    connect(fkTableCombo, &QComboBox::currentTextChanged, this, [this, row]() {
        onRefTableChanged(row);
    });

    // Col 8: FK column combo
    auto *fkColCombo = new QComboBox;
    m_fieldTable->setCellWidget(row, ColFkColumn, fkColCombo);

    // Col 9: CHECK constraint
    auto *checkItem = new QTableWidgetItem;
    checkItem->setTextAlignment(Qt::AlignCenter);
    m_fieldTable->setItem(row, ColCheck, checkItem);

    m_fieldTable->blockSignals(false);

    m_fieldTable->scrollToBottom();
    m_fieldTable->editItem(m_fieldTable->item(row, ColName));
    updateSqlPreview();
}

void AddColumnDialog::onDeleteRow()
{
    const int row = m_fieldTable->currentRow();
    if (row < 0) return;
    m_fieldTable->removeRow(row);
    updateSqlPreview();
}

void AddColumnDialog::onClearAll()
{
    if (m_fieldTable->rowCount() == 0) return;
    m_fieldTable->setRowCount(0);
    updateSqlPreview();
}

void AddColumnDialog::onAccept()
{
    if (m_fieldTable->rowCount() == 0) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("请至少添加一个字段"));
        return;
    }
    bool hasName = false;
    for (int row = 0; row < m_fieldTable->rowCount(); ++row) {
        auto *item = m_fieldTable->item(row, ColName);
        if (item && !item->text().trimmed().isEmpty()) {
            hasName = true;
            break;
        }
    }
    if (!hasName) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("请至少填写一个字段名"));
        return;
    }
    m_generatedSql = buildAlterSql();
    accept();
}

void AddColumnDialog::onCellChanged(int, int)
{
    updateSqlPreview();
}

void AddColumnDialog::onRefTableChanged(int row)
{
    if (row < 0 || row >= m_fieldTable->rowCount()) return;
    auto *refTableCombo = qobject_cast<QComboBox*>(m_fieldTable->cellWidget(row, ColFkTable));
    auto *refColCombo   = qobject_cast<QComboBox*>(m_fieldTable->cellWidget(row, ColFkColumn));
    if (refTableCombo && refColCombo)
        populateRefColumns(refTableCombo, refColCombo);
    updateSqlPreview();
}

// ── Build ALTER TABLE SQL ──
QString AddColumnDialog::buildAlterSql() const
{
    QStringList statements;
    for (int row = 0; row < m_fieldTable->rowCount(); ++row) {
        auto *nameItem = m_fieldTable->item(row, ColName);
        if (!nameItem) continue;
        const QString name = nameItem->text().trimmed();
        if (name.isEmpty()) continue;

        // Type
        auto *typeCombo = qobject_cast<QComboBox*>(m_fieldTable->cellWidget(row, ColType));
        const QString type = typeCombo ? typeCombo->currentText().toUpper() : "VARCHAR";

        // Length
        auto *lenItem = m_fieldTable->item(row, ColLength);
        const QString len = lenItem ? lenItem->text().trimmed() : QString();
        QString fullType = type;
        if (!len.isEmpty() && (type == "VARCHAR" || type == "CHAR" || type == "DECIMAL"))
            fullType = QString("%1(%2)").arg(type, len);

        // Build column definition
        QString colDef = QString("%1 %2").arg(name, fullType);

        // NOT NULL
        auto *nnW = m_fieldTable->cellWidget(row, ColNotNull);
        if (nnW && nnW->layout() && nnW->layout()->count() > 0) {
            auto *cb = qobject_cast<QCheckBox*>(nnW->layout()->itemAt(0)->widget());
            if (cb && cb->isChecked()) colDef += " NOT NULL";
        }

        // PK
        auto *pkW = m_fieldTable->cellWidget(row, ColPk);
        if (pkW && pkW->layout() && pkW->layout()->count() > 0) {
            auto *cb = qobject_cast<QCheckBox*>(pkW->layout()->itemAt(0)->widget());
            if (cb && cb->isChecked()) colDef += " PRIMARY KEY";
        }

        // UNIQUE
        auto *uqW = m_fieldTable->cellWidget(row, ColUnique);
        if (uqW && uqW->layout() && uqW->layout()->count() > 0) {
            auto *cb = qobject_cast<QCheckBox*>(uqW->layout()->itemAt(0)->widget());
            if (cb && cb->isChecked() && !colDef.contains("PRIMARY KEY")) colDef += " UNIQUE";
        }

        // Default
        auto *defItem = m_fieldTable->item(row, ColDefault);
        if (defItem) {
            const QString dv = defItem->text().trimmed();
            if (!dv.isEmpty()) {
                if (dv.compare("CURRENT_DATE", Qt::CaseInsensitive) == 0 ||
                    dv.compare("CURRENT_TIME", Qt::CaseInsensitive) == 0 ||
                    dv.compare("CURRENT_TIMESTAMP", Qt::CaseInsensitive) == 0) {
                    colDef += QString(" DEFAULT %1").arg(dv.toUpper());
                } else {
                    bool isNum;
                    dv.toDouble(&isNum);
                    if (isNum || dv.compare("NULL", Qt::CaseInsensitive) == 0)
                        colDef += QString(" DEFAULT %1").arg(dv);
                    else
                        colDef += QString(" DEFAULT '%1'").arg(QString(dv).replace("'", "''"));
                }
            }
        }

        // FK reference
        auto *fkTableCombo = qobject_cast<QComboBox*>(m_fieldTable->cellWidget(row, ColFkTable));
        auto *fkColCombo = qobject_cast<QComboBox*>(m_fieldTable->cellWidget(row, ColFkColumn));
        const QString refTable = fkTableCombo ? fkTableCombo->currentText().trimmed() : QString();
        const QString refCol = fkColCombo ? fkColCombo->currentText().trimmed() : QString();
        if (!refTable.isEmpty() && !refCol.isEmpty())
            colDef += QString(" REFERENCES %1(%2)").arg(refTable, refCol);

        // CHECK
        auto *checkItem = m_fieldTable->item(row, ColCheck);
        if (checkItem) {
            const QString checkStr = checkItem->text().trimmed();
            if (!checkStr.isEmpty())
                colDef += QString(" CHECK (%1)").arg(checkStr);
        }

        statements.append(QString("ALTER TABLE <表名> ADD %1;").arg(colDef));
    }
    return statements.join("\n");
}

void AddColumnDialog::updateSqlPreview()
{
    const QString sql = buildAlterSql();
    if (sql.isEmpty()) {
        m_sqlPreview->clear();
        return;
    }
    m_sqlPreview->setText(sql);
}

QString AddColumnDialog::getGeneratedSql() const
{
    return m_generatedSql;
}

QList<ColumnConfig> AddColumnDialog::getAllConfigs() const
{
    QList<ColumnConfig> configs;
    for (int row = 0; row < m_fieldTable->rowCount(); ++row) {
        auto *nameItem = m_fieldTable->item(row, ColName);
        if (!nameItem) continue;
        const QString name = nameItem->text().trimmed();
        if (name.isEmpty()) continue;

        ColumnConfig cfg;
        cfg.name = name;

        auto *typeCombo = qobject_cast<QComboBox*>(m_fieldTable->cellWidget(row, ColType));
        cfg.type = typeCombo ? typeCombo->currentText().toUpper() : "VARCHAR";

        auto *lenItem = m_fieldTable->item(row, ColLength);
        const QString lenText = lenItem ? lenItem->text().trimmed() : QString();
        cfg.length = lenText.isEmpty() ? 0 : lenText.toInt();

        // NOT NULL
        auto *nnW = m_fieldTable->cellWidget(row, ColNotNull);
        if (nnW && nnW->layout() && nnW->layout()->count() > 0) {
            auto *cb = qobject_cast<QCheckBox*>(nnW->layout()->itemAt(0)->widget());
            cfg.notNull = cb && cb->isChecked();
        }

        // PK
        auto *pkW = m_fieldTable->cellWidget(row, ColPk);
        if (pkW && pkW->layout() && pkW->layout()->count() > 0) {
            auto *cb = qobject_cast<QCheckBox*>(pkW->layout()->itemAt(0)->widget());
            cfg.primaryKey = cb && cb->isChecked();
        }

        // UNIQUE
        auto *uqW = m_fieldTable->cellWidget(row, ColUnique);
        if (uqW && uqW->layout() && uqW->layout()->count() > 0) {
            auto *cb = qobject_cast<QCheckBox*>(uqW->layout()->itemAt(0)->widget());
            cfg.unique = cb && cb->isChecked();
        }

        // Default
        auto *defItem = m_fieldTable->item(row, ColDefault);
        cfg.defaultValue = defItem ? defItem->text().trimmed() : QString();

        // FK table
        auto *fkTableCombo = qobject_cast<QComboBox*>(m_fieldTable->cellWidget(row, ColFkTable));
        cfg.referencedTable = fkTableCombo ? fkTableCombo->currentText().trimmed() : QString();

        // FK column
        auto *fkColCombo = qobject_cast<QComboBox*>(m_fieldTable->cellWidget(row, ColFkColumn));
        const QString refCol = fkColCombo ? fkColCombo->currentText().trimmed() : QString();
        if (!refCol.isEmpty())
            cfg.referencedColumns = { refCol };

        // CHECK
        auto *checkItem = m_fieldTable->item(row, ColCheck);
        cfg.checkConstraint = checkItem ? checkItem->text().trimmed() : QString();

        configs.append(cfg);
    }
    return configs;
}