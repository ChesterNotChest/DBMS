#include "create_table_dialog.h"
#include <QTableWidgetItem>
#include <QIntValidator>

static const QStringList DATA_TYPES = {
    "INT", "VARCHAR", "CHAR", "DATE", "FLOAT", "DOUBLE", "TEXT", "BOOLEAN"
};

CreateTableDialog::CreateTableDialog(QWidget *parent,
                                 const QString &defaultDb)
    : QDialog(parent)
{
    buildLayout(defaultDb);
    onAddColumn();
    m_tableNameEdit->setFocus();
}

QString CreateTableDialog::getGeneratedSql() const
{
    return buildSql();
}

QString CreateTableDialog::buildSql() const
{
    QString tableName = m_tableNameEdit->text().trimmed();
    if (tableName.isEmpty()) return QString();

    if (m_fieldTable->rowCount() == 0) return QString();

    // 检查列名
    QSet<QString> colNames;
    for (int r = 0; r < m_fieldTable->rowCount(); ++r) {
        QTableWidgetItem *it = m_fieldTable->item(r, 0);
        QString colName = it ? it->text().trimmed() : "";
        if (colName.isEmpty()) return QString();
        if (colNames.contains(colName)) return QString();
        colNames.insert(colName);
    }

    // 检查主键
    int pkCount = 0;
    for (int r = 0; r < m_fieldTable->rowCount(); ++r) {
        QCheckBox *pk = qobject_cast<QCheckBox*>(m_fieldTable->cellWidget(r, 4));
        if (pk && pk->isChecked()) pkCount++;
    }
    if (pkCount > 1) return QString();

    // 生成 SQL
    QString sql = QString("CREATE TABLE %1 (\n").arg(tableName);
    QStringList colDefs;

    for (int r = 0; r < m_fieldTable->rowCount(); ++r) {
        QString colName = m_fieldTable->item(r, 0)->text().trimmed();
        QComboBox *typeCombo = qobject_cast<QComboBox*>(m_fieldTable->cellWidget(r, 1));
        QString dataType = typeCombo ? typeCombo->currentText() : "INT";

        QString colDef = "    " + colName + " " + dataType;

        if (dataType == "VARCHAR" || dataType == "CHAR") {
            QLineEdit *lenEdit = qobject_cast<QLineEdit*>(m_fieldTable->cellWidget(r, 2));
            QString len = lenEdit ? lenEdit->text().trimmed() : "";
            if (!len.isEmpty()) colDef += "(" + len + ")";
            else colDef += "(255)";
        }

        QCheckBox *nullBox = qobject_cast<QCheckBox*>(m_fieldTable->cellWidget(r, 3));
        if (nullBox && !nullBox->isChecked()) colDef += " NOT NULL";

        QCheckBox *pkBox = qobject_cast<QCheckBox*>(m_fieldTable->cellWidget(r, 4));
        if (pkBox && pkBox->isChecked()) colDef += " PRIMARY KEY";

        QCheckBox *uniqBox = qobject_cast<QCheckBox*>(m_fieldTable->cellWidget(r, 5));
        if (uniqBox && uniqBox->isChecked()) colDef += " UNIQUE";

        colDefs.append(colDef);
    }

    sql += colDefs.join(",\n");
    sql += "\n);";
    return sql;
}

void CreateTableDialog::buildLayout(const QString &defaultDb)
{
    setWindowTitle("可视化建表");
    setMinimumSize(720, 520);
    setStyleSheet(
        "QDialog { background:#E3F2FD; color:#000000; }"
        "QLabel { color:#000000; }"
        "QLineEdit { color:#000000; }"
        "QComboBox { color:#000000; }"
        "QCheckBox { color:#000000; }"
        "QTableWidget { color:#000000; }"
        "QHeaderView::section { color:#000000; }"
    );

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(10);

    // ── 表名 ──
    QHBoxLayout *nameLayout = new QHBoxLayout();
    QLabel *nameLabel = new QLabel("表名：", this);
    nameLabel->setFixedWidth(48);
    nameLayout->addWidget(nameLabel);
    m_tableNameEdit = new QLineEdit(this);
    m_tableNameEdit->setPlaceholderText("请输入表名，如：student");
    m_tableNameEdit->setStyleSheet(
        "QLineEdit { border:1px solid #90CAF9; border-radius:4px; "
        "padding:4px 8px; font-size:12px; font-family:'Microsoft YaHei'; "
        "background:#FFFFFF; color:#000000; }"
        "QLineEdit:focus { border:1px solid #1976D2; }");
    nameLayout->addWidget(m_tableNameEdit);
    root->addLayout(nameLayout);

    // ── 字段列表标签 ──
    QLabel *fieldLabel = new QLabel("字段列表：", this);
    fieldLabel->setStyleSheet("QLabel { font-weight:bold; font-size:12px; }");
    root->addWidget(fieldLabel);

    // ── 字段表格 ──
    m_fieldTable = new QTableWidget(this);
    m_fieldTable->setColumnCount(6);
    m_fieldTable->setHorizontalHeaderLabels({
        "列名", "数据类型", "长度", "允许空", "主键", "唯一"
    });
    m_fieldTable->horizontalHeader()->setStretchLastSection(false);
    m_fieldTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_fieldTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_fieldTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_fieldTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_fieldTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_fieldTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    m_fieldTable->setColumnWidth(2, 56);
    m_fieldTable->setColumnWidth(3, 56);
    m_fieldTable->setColumnWidth(4, 44);
    m_fieldTable->setColumnWidth(5, 44);
    m_fieldTable->setAlternatingRowColors(true);
    m_fieldTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fieldTable->setStyleSheet(
        "QTableWidget { border:1px solid #90CAF9; gridline-color:#BBDEFB; "
        "background:#FFFFFF; color:#000000; selection-background-color:#BBDEFB; }"
        "QHeaderView { background:transparent; }"
        "QHeaderView::section { background:#BBDEFB; padding:4px 6px; "
        "border:none; border-bottom:2px solid #1976D2; font-weight:bold; color:#000000; }"
        "QTableWidget::item { padding:3px 6px; color:#000000; background:#FFFFFF; }"
        "QTableWidget::item:selected { background:#BBDEFB; color:#000000; }"
        "QTableWidget::item:focus { background:#BBDEFB; color:#000000; }"
        "QLineEdit { background:#FFFFFF; color:#000000; border:1px solid #90CAF9; }"
        "QLineEdit:focus { background:#FFFFFF; color:#000000; border:1px solid #1976D2; }"
        "QComboBox { background:#FFFFFF; color:#000000; border:1px solid #90CAF9; }"
        "QComboBox:focus { background:#FFFFFF; color:#000000; border:1px solid #1976D2; }"
        "QComboBox QAbstractItemView { background:#FFFFFF; color:#000000; selection-background-color:#BBDEFB; }"
        "QTableWidget QTableCornerButton { background:#BBDEFB; border:1px solid #90CAF9; }");
    root->addWidget(m_fieldTable, 1);

    // ── 字段操作按钮 ──
    QHBoxLayout *fieldBtnLayout = new QHBoxLayout();
    QPushButton *addColBtn = new QPushButton("添加列", this);
    addColBtn->setCursor(Qt::PointingHandCursor);
    addColBtn->setStyleSheet(
        "QPushButton { background:#43A047; color:white; border:none; "
        "border-radius:4px; padding:5px 14px; font-size:12px; }"
        "QPushButton:hover { background:#2E7D32; }");
    connect(addColBtn, &QPushButton::clicked, this, &CreateTableDialog::onAddColumn);
    fieldBtnLayout->addWidget(addColBtn);

    QPushButton *delColBtn = new QPushButton("删除列", this);
    delColBtn->setCursor(Qt::PointingHandCursor);
    delColBtn->setStyleSheet(
        "QPushButton { background:#E53935; color:white; border:none; "
        "border-radius:4px; padding:5px 14px; font-size:12px; }"
        "QPushButton:hover { background:#C62828; }");
    connect(delColBtn, &QPushButton::clicked, this, &CreateTableDialog::onDeleteColumn);
    fieldBtnLayout->addWidget(delColBtn);

    QPushButton *clearBtn = new QPushButton("清空", this);
    clearBtn->setCursor(Qt::PointingHandCursor);
    clearBtn->setStyleSheet(
        "QPushButton { background:#757575; color:white; border:none; "
        "border-radius:4px; padding:5px 14px; font-size:12px; }"
        "QPushButton:hover { background:#424242; }");
    connect(clearBtn, &QPushButton::clicked, this, &CreateTableDialog::onClearAll);
    fieldBtnLayout->addWidget(clearBtn);
    fieldBtnLayout->addStretch();
    root->addLayout(fieldBtnLayout);

    // ── 底部按钮 ──
    QHBoxLayout *bottomLayout = new QHBoxLayout();
    bottomLayout->addStretch();

    m_execBtn = new QPushButton("执行建表", this);
    m_execBtn->setCursor(Qt::PointingHandCursor);
    m_execBtn->setStyleSheet(
        "QPushButton { background:#FF8F00; color:white; border:none; "
        "border-radius:4px; padding:6px 18px; font-size:12px; }"
        "QPushButton:hover { background:#E65100; }");
    connect(m_execBtn, &QPushButton::clicked, this, &CreateTableDialog::onExecuteCreate);
    bottomLayout->addWidget(m_execBtn);

    QPushButton *closeBtn = new QPushButton("关闭", this);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(
        "QPushButton { background:#E0E0E0; color:#333333; border:none; "
        "border-radius:4px; padding:6px 18px; font-size:12px; }"
        "QPushButton:hover { background:#BDBDBD; }");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    bottomLayout->addWidget(closeBtn);
    root->addLayout(bottomLayout);
}

void CreateTableDialog::onAddColumn()
{
    int row = m_fieldTable->rowCount();
    m_fieldTable->insertRow(row);

    m_fieldTable->setItem(row, 0, new QTableWidgetItem(""));

    QComboBox *typeCombo = new QComboBox(this);
    typeCombo->addItems(DATA_TYPES);
    typeCombo->setCurrentIndex(0);
    m_fieldTable->setCellWidget(row, 1, typeCombo);

    QLineEdit *lenEdit = new QLineEdit(this);
    lenEdit->setPlaceholderText(u8"可选");
    lenEdit->setMaximumWidth(56);
    m_fieldTable->setCellWidget(row, 2, lenEdit);

    connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, row](int) { refreshLengthEnable(row); });
    refreshLengthEnable(row);

    QCheckBox *nullBox = new QCheckBox(this);
    nullBox->setChecked(true);
    m_fieldTable->setCellWidget(row, 3, nullBox);

    QCheckBox *pkBox = new QCheckBox(this);
    pkBox->setStyleSheet("QCheckBox { margin-left:8px; }");
    m_fieldTable->setCellWidget(row, 4, pkBox);
    connect(pkBox, &QCheckBox::toggled, this, [this, row](bool) {
        updatePkRadio(row);
    });

    QCheckBox *uniqBox = new QCheckBox(this);
    uniqBox->setStyleSheet("QCheckBox { margin-left:8px; }");
    m_fieldTable->setCellWidget(row, 5, uniqBox);
}

void CreateTableDialog::onDeleteColumn()
{
    QList<QTableWidgetItem*> sel = m_fieldTable->selectedItems();
    if (sel.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先选中要删除的字段行！");
        return;
    }
    QSet<int> rows;
    for (auto *it : sel) rows.insert(it->row());
    QList<int> rl = rows.values();
    std::sort(rl.begin(), rl.end(), std::greater<int>());
    for (int r : rl) m_fieldTable->removeRow(r);
}

void CreateTableDialog::onClearAll()
{
    if (m_fieldTable->rowCount() == 0) return;
    auto ret = QMessageBox::question(this, "确认",
                                    "确定要清空所有字段吗？",
                                    QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;
    m_fieldTable->setRowCount(0);
}

void CreateTableDialog::updatePkRadio(int row)
{
    for (int r = 0; r < m_fieldTable->rowCount(); ++r) {
        if (r == row) continue;
        QCheckBox *cb = qobject_cast<QCheckBox*>(m_fieldTable->cellWidget(r, 4));
        if (cb) cb->setChecked(false);
    }
}

void CreateTableDialog::refreshLengthEnable(int row)
{
    QComboBox *cb = qobject_cast<QComboBox*>(m_fieldTable->cellWidget(row, 1));
    QLineEdit *le = qobject_cast<QLineEdit*>(m_fieldTable->cellWidget(row, 2));
    if (!cb || !le) return;
    QString t = cb->currentText();
    bool needLen = (t == "VARCHAR" || t == "CHAR");
    le->setEnabled(needLen);
    if (!needLen) le->clear();
}

void CreateTableDialog::onExecuteCreate()
{
    QString tableName = m_tableNameEdit->text().trimmed();
    if (tableName.isEmpty()) {
        QMessageBox::warning(this, "错误", "表名不能为空！");
        m_tableNameEdit->setFocus();
        return;
    }

    if (m_fieldTable->rowCount() == 0) {
        QMessageBox::warning(this, "错误", "至少需要一个字段！");
        return;
    }

    // 检查列名重复
    QSet<QString> colNames;
    for (int r = 0; r < m_fieldTable->rowCount(); ++r) {
        QTableWidgetItem *it = m_fieldTable->item(r, 0);
        QString colName = it ? it->text().trimmed() : "";
        if (colName.isEmpty()) {
            QMessageBox::warning(this, "错误",
                                   QString("第 %1 行的列名不能为空！").arg(r + 1));
            return;
        }
        if (colNames.contains(colName)) {
            QMessageBox::warning(this, "错误",
                                   QString("列名 '%1' 重复（第 %2 行）！")
                                   .arg(colName).arg(r + 1));
            return;
        }
        colNames.insert(colName);
    }

    // 检查主键唯一
    int pkCount = 0;
    for (int r = 0; r < m_fieldTable->rowCount(); ++r) {
        QCheckBox *pk = qobject_cast<QCheckBox*>(m_fieldTable->cellWidget(r, 4));
        if (pk && pk->isChecked()) pkCount++;
    }
    if (pkCount > 1) {
        QMessageBox::warning(this, "错误", "只能设置一个主键！");
        return;
    }

    accept();
}