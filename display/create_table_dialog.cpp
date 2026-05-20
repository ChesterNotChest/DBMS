#include "create_table_dialog.h"
#include <QApplication>
#include <QClipboard>

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
    ColFkAction = 9,
    ColCount    = 10
};

static QString defaultDataRootImpl()
{
    const QString envRoot = qEnvironmentVariable("DBMS_DATA_ROOT");
    if (!envRoot.trimmed().isEmpty())
        return QDir::cleanPath(envRoot);
#ifdef DBMS_REPO_ROOT
    return QDir::cleanPath(QDir(QString::fromUtf8(DBMS_REPO_ROOT)).absoluteFilePath("data"));
#else
    return QDir::cleanPath(QDir::current().absoluteFilePath("data"));
#endif
}

static void applyCommonStyle(QWidget *w)
{
    w->setStyleSheet(R"(
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
        QPushButton#saveBtn {
            background: #4B5563;
            color: white;
            border-color: #4B5563;
        }
        QPushButton#saveBtn:hover {
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

CreateTableDialog::CreateTableDialog(QWidget *parent, const QString &defaultDb)
    : QDialog(parent)
    , m_currentDb(defaultDb)
{
    setWindowTitle(QString::fromUtf8("可视化建表"));
    setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint);
    setMinimumSize(960, 720);
    setSizeGripEnabled(true);
    applyCommonStyle(this);
    buildUi(defaultDb);
}

QString CreateTableDialog::defaultDataRoot()
{
    return defaultDataRootImpl();
}

void CreateTableDialog::buildUi(const QString &defaultDb)
{
    Q_UNUSED(defaultDb);
    auto *root = new QVBoxLayout(this);
    root->setSpacing(12);
    root->setContentsMargins(20, 20, 20, 20);

    // ── Title ──
    auto *title = new QLabel(QString::fromUtf8("创建新数据表"));
    title->setStyleSheet("font-size: 20px; font-weight: 700; color: #1F2937;");
    root->addWidget(title);

    // ── Table name ──
    auto *nameRow = new QHBoxLayout;
    auto *nameLabel = new QLabel(QString::fromUtf8("表名："));
    nameLabel->setStyleSheet("font-weight: 600;");
    m_tableNameEdit = new QLineEdit;
    m_tableNameEdit->setPlaceholderText(QString::fromUtf8("输入表名，例如：student"));
    m_tableNameEdit->setMaxLength(64);
    nameRow->addWidget(nameLabel);
    nameRow->addWidget(m_tableNameEdit, 1);
    root->addLayout(nameRow);

    connect(m_tableNameEdit, &QLineEdit::textChanged, this, &CreateTableDialog::onTableNameChanged);

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
    m_fieldTable->setColumnWidth(ColPk,       50);
    m_fieldTable->setColumnWidth(ColUnique,   50);
    m_fieldTable->setColumnWidth(ColDefault,  110);
    m_fieldTable->setColumnWidth(ColFkTable,  120);
    m_fieldTable->setColumnWidth(ColFkColumn, 120);
    m_fieldTable->horizontalHeader()->setStretchLastSection(true);

    m_fieldTable->verticalHeader()->setDefaultSectionSize(30);
    m_fieldTable->verticalHeader()->setMinimumSectionSize(26);
    m_fieldTable->verticalHeader()->setVisible(false);
    m_fieldTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fieldTable->setAlternatingRowColors(true);

    connect(m_fieldTable, &QTableWidget::cellChanged, this, &CreateTableDialog::onCellChanged);

    root->addWidget(m_fieldTable, 1);

    // ── Button row ──
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(10);

    auto *addBtn = new QPushButton(QString::fromUtf8("＋ 添加行"));
    addBtn->setToolTip(QString::fromUtf8("添加一个新字段行"));
    connect(addBtn, &QPushButton::clicked, this, &CreateTableDialog::onAddRow);

    auto *delBtn = new QPushButton(QString::fromUtf8("－ 删除行"));
    delBtn->setToolTip(QString::fromUtf8("删除当前选中的行"));
    connect(delBtn, &QPushButton::clicked, this, &CreateTableDialog::onDeleteRow);

    auto *clrBtn = new QPushButton(QString::fromUtf8("清空所有"));
    connect(clrBtn, &QPushButton::clicked, this, &CreateTableDialog::onClearAll);

    m_cancelBtn = new QPushButton(QString::fromUtf8("取消"));
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    m_saveBtn = new QPushButton(QString::fromUtf8("保存创建"));
    m_saveBtn->setObjectName("saveBtn");
    connect(m_saveBtn, &QPushButton::clicked, this, &CreateTableDialog::onSave);

    btnRow->addWidget(addBtn);
    btnRow->addWidget(delBtn);
    btnRow->addWidget(clrBtn);
    btnRow->addStretch();
    btnRow->addWidget(m_cancelBtn);
    btnRow->addWidget(m_saveBtn);
    root->addLayout(btnRow);

    // ── SQL Preview ──
    auto *sqlLabel = new QLabel(QString::fromUtf8("SQL 预览"));
    sqlLabel->setStyleSheet("font-weight: 600; font-size: 13px; color: #4B5563;");
    root->addWidget(sqlLabel);

    m_sqlPreview = new QTextEdit;
    m_sqlPreview->setReadOnly(true);
    m_sqlPreview->setMinimumHeight(100);
    m_sqlPreview->setMaximumHeight(160);
    m_sqlPreview->setPlaceholderText(QString::fromUtf8("自动生成的 CREATE TABLE 语句将显示在这里..."));
    root->addWidget(m_sqlPreview);

    // ── Initial rows ──
    for (int i = 0; i < 3; ++i)
        onAddRow();
}

// ── Populate foreign key table dropdown ──
void CreateTableDialog::populateRefTables(QComboBox *combo)
{
    if (!combo) return;
    combo->clear();
    combo->addItem(QString());
    if (m_currentDb.isEmpty()) return;

    const QString dataRoot = defaultDataRoot();
    const QString tabPath = QDir(dataRoot).absoluteFilePath(
        m_currentDb + "/" + m_currentDb + ".tab");

    QFile f(tabPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
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
void CreateTableDialog::populateRefColumns(QComboBox *refTableCombo, QComboBox *refColumnCombo)
{
    if (!refTableCombo || !refColumnCombo) return;
    refColumnCombo->clear();
    const QString tableName = refTableCombo->currentText().trimmed();
    if (tableName.isEmpty() || m_currentDb.isEmpty()) return;

    const QString dataRoot = defaultDataRoot();
    const QString metaPath = QDir(dataRoot).absoluteFilePath(
        m_currentDb + "/" + tableName + "/table.meta");

    QFile f(metaPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
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
void CreateTableDialog::onAddRow()
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
    auto *nnCb = new QCheckBox;
    auto *nnW = new QWidget;
    auto *nnL = new QHBoxLayout(nnW);
    nnL->setContentsMargins(0, 0, 0, 0);
    nnL->setAlignment(Qt::AlignCenter);
    nnL->addWidget(nnCb);
    m_fieldTable->setCellWidget(row, ColNotNull, nnW);
    connect(nnCb, &QCheckBox::toggled, this, [this, row]() { onCellChanged(row, ColNotNull); });

    // Col 4: PK checkbox
    auto *pkCb = new QCheckBox;
    auto *pkW = new QWidget;
    auto *pkL = new QHBoxLayout(pkW);
    pkL->setContentsMargins(0, 0, 0, 0);
    pkL->setAlignment(Qt::AlignCenter);
    pkL->addWidget(pkCb);
    m_fieldTable->setCellWidget(row, ColPk, pkW);
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
    auto *uqCb = new QCheckBox;
    auto *uqW = new QWidget;
    auto *uqL = new QHBoxLayout(uqW);
    uqL->setContentsMargins(0, 0, 0, 0);
    uqL->setAlignment(Qt::AlignCenter);
    uqL->addWidget(uqCb);
    m_fieldTable->setCellWidget(row, ColUnique, uqW);
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

    // Col 9: constraint (ON DELETE / ON UPDATE / CHECK)
    auto *actionItem = new QTableWidgetItem;
    actionItem->setTextAlignment(Qt::AlignCenter);
    m_fieldTable->setItem(row, ColFkAction, actionItem);

    m_fieldTable->blockSignals(false);

    // Scroll to new row
    m_fieldTable->scrollToBottom();
    m_fieldTable->editItem(m_fieldTable->item(row, ColName));
    updateSqlPreview();
}

void CreateTableDialog::onDeleteRow()
{
    const int row = m_fieldTable->currentRow();
    if (row < 0) return;
    m_fieldTable->removeRow(row);
    updateSqlPreview();
}

void CreateTableDialog::onClearAll()
{
    if (m_fieldTable->rowCount() == 0) return;
    m_fieldTable->setRowCount(0);
    updateSqlPreview();
}

void CreateTableDialog::onTableNameChanged(const QString &)
{
    updateSqlPreview();
}

void CreateTableDialog::onCellChanged(int, int)
{
    updateSqlPreview();
}

void CreateTableDialog::onRefTableChanged(int row)
{
    if (row < 0 || row >= m_fieldTable->rowCount()) return;
    auto *refTableCombo = qobject_cast<QComboBox*>(m_fieldTable->cellWidget(row, ColFkTable));
    auto *refColCombo   = qobject_cast<QComboBox*>(m_fieldTable->cellWidget(row, ColFkColumn));
    if (refTableCombo && refColCombo)
        populateRefColumns(refTableCombo, refColCombo);
    updateSqlPreview();
}

// ── Build CREATE TABLE SQL ──
QString CreateTableDialog::buildCreateSql() const
{
    const QString tableName = m_tableNameEdit->text().trimmed();
    if (tableName.isEmpty()) return QString();

    QStringList lines;
    lines.append(QString("CREATE TABLE %1 (").arg(tableName));

    QStringList colDefs;
    bool hasCompositePk = false;
    int pkCount = 0;

    for (int row = 0; row < m_fieldTable->rowCount(); ++row) {
        auto *nameItem = m_fieldTable->item(row, ColName);
        if (!nameItem) continue;
        const QString name = nameItem->text().trimmed();
        if (name.isEmpty()) continue;

        // Type
        auto *typeCombo = qobject_cast<QComboBox*>(m_fieldTable->cellWidget(row, ColType));
        const QString type = typeCombo ? typeCombo->currentText().toUpper() : "VARCHAR";

        // Length
        QString fullType = type;
        auto *lenItem = m_fieldTable->item(row, ColLength);
        const QString len = lenItem ? lenItem->text().trimmed() : QString();
        if (!len.isEmpty() && (type == "VARCHAR" || type == "CHAR" || type == "DECIMAL"))
            fullType = QString("%1(%2)").arg(type, len);

        // NOT NULL
        bool notNull = false;
        auto *nnW = m_fieldTable->cellWidget(row, ColNotNull);
        if (nnW && nnW->layout() && nnW->layout()->count() > 0) {
            auto *cb = qobject_cast<QCheckBox*>(nnW->layout()->itemAt(0)->widget());
            notNull = cb && cb->isChecked();
        }

        // PK
        bool isPk = false;
        auto *pkW = m_fieldTable->cellWidget(row, ColPk);
        if (pkW && pkW->layout() && pkW->layout()->count() > 0) {
            auto *cb = qobject_cast<QCheckBox*>(pkW->layout()->itemAt(0)->widget());
            isPk = cb && cb->isChecked();
        }
        if (isPk) ++pkCount;

        // UNIQUE
        bool isUq = false;
        auto *uqW = m_fieldTable->cellWidget(row, ColUnique);
        if (uqW && uqW->layout() && uqW->layout()->count() > 0) {
            auto *cb = qobject_cast<QCheckBox*>(uqW->layout()->itemAt(0)->widget());
            isUq = cb && cb->isChecked();
        }

        // Default
        QString defaultStr;
        auto *defItem = m_fieldTable->item(row, ColDefault);
        if (defItem) {
            const QString dv = defItem->text().trimmed();
            if (!dv.isEmpty()) {
                if (dv.compare("CURRENT_DATE", Qt::CaseInsensitive) == 0 ||
                    dv.compare("CURRENT_TIME", Qt::CaseInsensitive) == 0 ||
                    dv.compare("CURRENT_TIMESTAMP", Qt::CaseInsensitive) == 0) {
                    defaultStr = QString("DEFAULT %1").arg(dv.toUpper());
                } else {
                    bool isNum;
                    dv.toDouble(&isNum);
                    if (isNum || dv.compare("NULL", Qt::CaseInsensitive) == 0)
                        defaultStr = QString("DEFAULT %1").arg(dv);
                    else
                        defaultStr = QString("DEFAULT '%1'").arg(QString(dv).replace("'", "''"));
                }
            }
        }

        // FK table
        auto *fkTableCombo = qobject_cast<QComboBox*>(m_fieldTable->cellWidget(row, ColFkTable));
        const QString refTable = fkTableCombo ? fkTableCombo->currentText().trimmed() : QString();

        // FK column
        auto *fkColCombo = qobject_cast<QComboBox*>(m_fieldTable->cellWidget(row, ColFkColumn));
        const QString refCol = fkColCombo ? fkColCombo->currentText().trimmed() : QString();

        // FK action
        const QString action = m_fieldTable->item(row, ColFkAction)
                               ? m_fieldTable->item(row, ColFkAction)->text().trimmed()
                               : QString();

        // Build column definition
        QString colDef = QString("  %1 %2").arg(name, fullType);

        if (notNull)   colDef += " NOT NULL";
        if (isPk && pkCount <= 1) colDef += " PRIMARY KEY";
        if (isUq && !isPk) colDef += " UNIQUE";
        if (!defaultStr.isEmpty()) colDef += " " + defaultStr;

        // FK reference (inline)
        if (!refTable.isEmpty() && !refCol.isEmpty()) {
            colDef += QString(" REFERENCES %1(%2)").arg(refTable, refCol);
            if (!action.isEmpty())
                colDef += " " + action;
        }

        colDefs.append(colDef);
    }

    lines.append(colDefs.join(",\n"));

    // Composite PK if multiple PKs
    if (pkCount > 1) {
        QStringList pkCols;
        for (int row = 0; row < m_fieldTable->rowCount(); ++row) {
            auto *nameItem = m_fieldTable->item(row, ColName);
            if (!nameItem) continue;
            auto *pkW = m_fieldTable->cellWidget(row, ColPk);
            if (pkW && pkW->layout() && pkW->layout()->count() > 0) {
                auto *cb = qobject_cast<QCheckBox*>(pkW->layout()->itemAt(0)->widget());
                if (cb && cb->isChecked()) {
                    const QString n = nameItem->text().trimmed();
                    if (!n.isEmpty()) pkCols.append(n);
                }
            }
        }
        if (!pkCols.isEmpty()) {
            // Remove inline PK from individual columns
            QStringList adjusted;
            for (const QString &cd : colDefs) {
                QString c = cd;
                c.replace(" PRIMARY KEY", "");
                adjusted.append(c);
            }
            lines.clear();
            lines.append(QString("CREATE TABLE %1 (").arg(tableName));
            lines.append(adjusted.join(",\n"));
            lines.append(QString("  PRIMARY KEY (%1)").arg(pkCols.join(", ")));
        }
    }

    lines.append(");");
    return lines.join("\n");
}

void CreateTableDialog::updateSqlPreview()
{
    const QString sql = buildCreateSql();
    if (sql.isEmpty()) {
        m_sqlPreview->clear();
        return;
    }
    m_sqlPreview->setText(sql);
}

void CreateTableDialog::onSave()
{
    const QString sql = buildCreateSql();
    if (sql.isEmpty()) {
        return;
    }
    m_generatedSql = sql;
    accept();
}

QString CreateTableDialog::getGeneratedSql() const
{
    return m_generatedSql;
}