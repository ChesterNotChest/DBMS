#include "create_table_dialog.h"

CreateTableDialog::CreateTableDialog(QWidget *parent, const QString &defaultDb)
    : QDialog(parent)
{
    setWindowTitle("可视化建表");
    setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint);
    setMinimumSize(980, 720);
    setStyleSheet(R"(
        QDialog {
            background-color: #E3F2FD;
        }
        QLabel {
            font-size: 14px;
            font-weight: 600;
            color: #1565C0;
            padding: 4px;
        }
        QLineEdit {
            border: 1px solid #E0E0E0;
            border-radius: 4px;
            padding: 6px 8px;
            background-color: white;
            font-size: 13px;
            color: #212121;
            min-height: 28px;
        }
        QLineEdit:focus {
            border-color: #1890ff;
            outline: none;
        }
        QLineEdit:disabled {
            background-color: #FAFAFA;
            color: #9E9E9E;
        }
        QComboBox {
            border: 1px solid #E0E0E0;
            border-radius: 4px;
            padding: 4px 8px;
            background-color: white;
            font-size: 13px;
            color: #212121;
            min-height: 28px;
            max-height: 28px;
        }
        QComboBox:focus {
            border-color: #1890ff;
            outline: none;
        }
        QComboBox::drop-down {
            border: none;
            width: 24px;
        }
        QComboBox::down-arrow {
            image: none;
            border-left: 4px solid transparent;
            border-right: 4px solid transparent;
            border-top: 4px solid #666;
        }
        QComboBox QAbstractItemView {
            background-color: white;
            border: 1px solid #E0E0E0;
            font-size: 13px;
            color: #212121;
            selection-background-color: #E6F7FF;
            selection-color: #1890ff;
        }
        QComboBox QAbstractItemView::item {
            padding: 6px 12px;
            color: #212121;
        }
        QComboBox QAbstractItemView::item:hover {
            background-color: #E6F7FF;
            color: #1890ff;
        }
        QComboBox QAbstractItemView::item:selected {
            background-color: #E6F7FF;
            color: #1890ff;
        }
        QTableWidget {
            background-color: white;
            border: 1px solid #E0E0E0;
            border-radius: 8px;
            gridline-color: #F0F0F0;
            font-size: 14px;
            color: #212121;
            selection-background-color: #E6F7FF;
            selection-color: #1890ff;
            alternate-background-color: #FAFAFA;
        }
        QTableWidget::item {
            padding: 8px 6px;
            text-align: center;
        }
        QTableWidget::item:selected {
            background-color: #E6F7FF;
            color: #1890ff;
        }
        QHeaderView::section {
            background-color: #F5F5F5;
            padding: 8px 6px;
            border: none;
            border-right: 1px solid #E0E0E0;
            border-bottom: 2px solid #E0E0E0;
            font-weight: bold;
            font-size: 13px;
            color: #555;
            min-height: 36px;
        }
        QHeaderView::section:last {
            border-right: none;
        }
        QHeaderView::section:vertical {
            background-color: #F5F5F5;
            border: none;
            border-bottom: 1px solid #E0E0E0;
            font-weight: bold;
            font-size: 12px;
            color: #555;
        }
        QTableCornerButton::section {
            background-color: #F5F5F5;
            border: none;
            border-right: 1px solid #E0E0E0;
            border-bottom: 2px solid #E0E0E0;
        }
        QPushButton {
            background-color: #F0F0F0;
            color: #666;
            border: 1px solid #E0E0E0;
            border-radius: 4px;
            padding: 10px 20px;
            font-size: 14px;
            font-weight: 600;
            min-height: 36px;
        }
        QPushButton:hover {
            background-color: #E8E8E8;
            border-color: #D0D0D0;
        }
        QPushButton:pressed {
            background-color: #D8D8D8;
        }
        QPushButton.primary {
            background-color: #1890ff;
            color: white;
            border-color: #1890ff;
        }
        QPushButton.primary:hover {
            background-color: #40A9ff;
            border-color: #40A9ff;
        }
        QPushButton.danger {
            background-color: #FF4D4F;
            color: white;
            border-color: #FF4D4F;
        }
        QPushButton.danger:hover {
            background-color: #FF7875;
            border-color: #FF7875;
        }
        QCheckBox {
            color: #555;
            font-size: 13px;
            font-weight: 500;
            spacing: 8px;
        }
        QCheckBox::indicator {
            width: 18px;
            height: 18px;
            border: 2px solid #D9D9D9;
            border-radius: 4px;
            background-color: white;
        }
        QCheckBox::indicator:hover {
            border-color: #1890ff;
        }
        QCheckBox::indicator:checked {
            background-color: #1890ff;
            border-color: #1890ff;
        }
        QCheckBox::indicator:checked::after {
            content: "✓";
            display: block;
            text-align: center;
            font-size: 12px;
            font-weight: bold;
            color: white;
        }
    )");

    buildLayout(defaultDb);
}

void CreateTableDialog::buildLayout(const QString &defaultDb)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(20);
    mainLayout->setContentsMargins(24, 24, 24, 24);

    // 标题区域
    auto *titleLabel = new QLabel("📋 创建新数据表");
    titleLabel->setStyleSheet("font-size: 24px; font-weight: bold; color: #1565C0; padding: 0;");
    mainLayout->addWidget(titleLabel);

    // 数据库与表名区域
    auto *nameLayout = new QHBoxLayout;
    nameLayout->setSpacing(16);
    nameLayout->addWidget(new QLabel("数据表名称："));
    
    m_tableNameEdit = new QLineEdit;
    m_tableNameEdit->setPlaceholderText("请输入表名，例如：students");
    m_tableNameEdit->setStyleSheet("QLineEdit { min-width: 200px; }");
    nameLayout->addWidget(m_tableNameEdit, 1);
    mainLayout->addLayout(nameLayout);

    // 字段表格
    m_fieldTable = new QTableWidget(0, 9);
    m_fieldTable->setHorizontalHeaderLabels({
        "字段名称", "数据类型", "长度", "NOT NULL", "主键", "唯一", "自增", "默认值", "备注"
    });
    m_fieldTable->horizontalHeader()->setStretchLastSection(true);
    m_fieldTable->verticalHeader()->setDefaultSectionSize(38);
    m_fieldTable->verticalHeader()->setMinimumSectionSize(36);
    m_fieldTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fieldTable->setAlternatingRowColors(true);

    // 设置列宽
    m_fieldTable->setColumnWidth(0, 130);
    m_fieldTable->setColumnWidth(1, 110);
    m_fieldTable->setColumnWidth(2, 75);
    m_fieldTable->setColumnWidth(3, 75);
    m_fieldTable->setColumnWidth(4, 60);
    m_fieldTable->setColumnWidth(5, 60);
    m_fieldTable->setColumnWidth(6, 60);
    m_fieldTable->setColumnWidth(7, 100);

    mainLayout->addWidget(m_fieldTable, 1);

    // 操作按钮区域
    auto *btnLayout = new QHBoxLayout;
    btnLayout->setSpacing(12);

    auto *addBtn = new QPushButton("➕ 添加字段");
    connect(addBtn, &QPushButton::clicked, this, &CreateTableDialog::onAddColumn);

    auto *delBtn = new QPushButton("➖ 删除选中");
    connect(delBtn, &QPushButton::clicked, this, &CreateTableDialog::onDeleteColumn);

    auto *clearBtn = new QPushButton("🗑️ 清空所有");
    clearBtn->setObjectName("danger");
    connect(clearBtn, &QPushButton::clicked, this, &CreateTableDialog::onClearAll);

    btnLayout->addWidget(addBtn);
    btnLayout->addWidget(delBtn);
    btnLayout->addWidget(clearBtn);
    btnLayout->addStretch();
    mainLayout->addLayout(btnLayout);

    // 底部按钮区域
    auto *bottomBtnLayout = new QHBoxLayout;
    bottomBtnLayout->setSpacing(16);

    m_cancelBtn = new QPushButton("取消");
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    m_execBtn = new QPushButton("执行创建");
    m_execBtn->setObjectName("primary");
    connect(m_execBtn, &QPushButton::clicked, this, &CreateTableDialog::onExecuteCreate);

    bottomBtnLayout->addStretch();
    bottomBtnLayout->addWidget(m_cancelBtn);
    bottomBtnLayout->addWidget(m_execBtn);
    mainLayout->addLayout(bottomBtnLayout);

    // 默认添加几个示例行
    for (int i = 0; i < 3; i++) {
        onAddColumn();
    }
}

void CreateTableDialog::onAddColumn()
{
    int row = m_fieldTable->rowCount();
    m_fieldTable->insertRow(row);

    // 字段名称
    auto *nameItem = new QTableWidgetItem(QString("column%1").arg(row + 1));
    nameItem->setTextAlignment(Qt::AlignCenter);
    m_fieldTable->setItem(row, 0, nameItem);

    // 数据类型
    auto *typeCombo = new QComboBox();
    typeCombo->setEditable(false);
    typeCombo->addItems({
        "INT", "BIGINT", "FLOAT", "DOUBLE", "DECIMAL",
        "VARCHAR", "CHAR", "TEXT", "DATE", "DATETIME",
        "TIME", "BLOB", "BOOLEAN"
    });
    typeCombo->setCurrentText("VARCHAR");
    m_fieldTable->setCellWidget(row, 1, typeCombo);

    // 长度
    auto *lenEdit = new QLineEdit("255");
    lenEdit->setAlignment(Qt::AlignCenter);
    lenEdit->setPlaceholderText("长度");
    lenEdit->setStyleSheet("QLineEdit { min-height: 24px; padding: 4px 6px; font-size: 12px; }");
    m_fieldTable->setCellWidget(row, 2, lenEdit);

    // NOT NULL
    auto *chkNotNull = new QCheckBox();
    chkNotNull->setChecked(false);
    auto *widgetNn = new QWidget();
    auto *layoutNn = new QHBoxLayout(widgetNn);
    layoutNn->setContentsMargins(0, 0, 0, 0);
    layoutNn->setAlignment(Qt::AlignCenter);
    layoutNn->addWidget(chkNotNull);
    m_fieldTable->setCellWidget(row, 3, widgetNn);

    // 主键
    auto *chkPk = new QCheckBox();
    chkPk->setChecked(false);
    auto *widgetPk = new QWidget();
    auto *layoutPk = new QHBoxLayout(widgetPk);
    layoutPk->setContentsMargins(0, 0, 0, 0);
    layoutPk->setAlignment(Qt::AlignCenter);
    layoutPk->addWidget(chkPk);
    m_fieldTable->setCellWidget(row, 4, widgetPk);

    // 唯一
    auto *chkUnique = new QCheckBox();
    chkUnique->setChecked(false);
    auto *widgetUniq = new QWidget();
    auto *layoutUniq = new QHBoxLayout(widgetUniq);
    layoutUniq->setContentsMargins(0, 0, 0, 0);
    layoutUniq->setAlignment(Qt::AlignCenter);
    layoutUniq->addWidget(chkUnique);
    m_fieldTable->setCellWidget(row, 5, widgetUniq);

    // 自增
    auto *chkAutoInc = new QCheckBox();
    chkAutoInc->setChecked(false);
    auto *widgetAi = new QWidget();
    auto *layoutAi = new QHBoxLayout(widgetAi);
    layoutAi->setContentsMargins(0, 0, 0, 0);
    layoutAi->setAlignment(Qt::AlignCenter);
    layoutAi->addWidget(chkAutoInc);
    m_fieldTable->setCellWidget(row, 6, widgetAi);

    // 默认值
    auto *defItem = new QTableWidgetItem("");
    defItem->setTextAlignment(Qt::AlignCenter);
    defItem->setData(Qt::UserRole, "placeholder");
    m_fieldTable->setItem(row, 7, defItem);

    // 备注
    auto *commItem = new QTableWidgetItem("");
    commItem->setTextAlignment(Qt::AlignCenter);
    commItem->setData(Qt::UserRole, "placeholder");
    m_fieldTable->setItem(row, 8, commItem);

    connect(chkPk, &QCheckBox::stateChanged, this, [this, row](int state) {
        if (state == Qt::Checked) {
            updatePkRadio(row);
        }
    });

    connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this, row]() {
        refreshLengthEnable(row);
    });

    refreshLengthEnable(row);
}

void CreateTableDialog::updatePkRadio(int row)
{
    for (int r = 0; r < m_fieldTable->rowCount(); r++) {
        if (r != row) {
            auto *widget = m_fieldTable->cellWidget(r, 4);
            if (widget) {
                auto *layout = widget->layout();
                if (layout && layout->count() > 0) {
                    auto *chk = qobject_cast<QCheckBox*>(layout->itemAt(0)->widget());
                    if (chk) {
                        chk->blockSignals(true);
                        chk->setChecked(false);
                        chk->blockSignals(false);
                    }
                }
            }
        }
    }
}

void CreateTableDialog::refreshLengthEnable(int row)
{
    auto *typeCombo = qobject_cast<QComboBox*>(m_fieldTable->cellWidget(row, 1));
    auto *lenEdit = qobject_cast<QLineEdit*>(m_fieldTable->cellWidget(row, 2));
    if (!typeCombo || !lenEdit) return;

    QString type = typeCombo->currentText().toUpper();
    bool needsLen = (type == "VARCHAR" || type == "CHAR");
    lenEdit->setEnabled(needsLen);
    lenEdit->setStyleSheet(needsLen ? "QLineEdit { min-height: 32px; padding: 6px; }" : "QLineEdit { min-height: 32px; padding: 6px; background-color: #FAFAFA; color: #9E9E9E; }");
}

void CreateTableDialog::onDeleteColumn()
{
    int currentRow = m_fieldTable->currentRow();
    if (currentRow >= 0) {
        m_fieldTable->removeRow(currentRow);
    } else {
        QMessageBox::information(this, "提示", "请先选择一行！");
    }
}

void CreateTableDialog::onClearAll()
{
    auto reply = QMessageBox::question(this, "确认", "确定要清空所有字段吗？",
                                       QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        while (m_fieldTable->rowCount() > 0) {
            m_fieldTable->removeRow(0);
        }
    }
}

QString CreateTableDialog::buildSql() const
{
    QString table = m_tableNameEdit->text().trimmed();
    if (table.isEmpty()) return QString();

    QString sql = QString("CREATE TABLE %1 (\n").arg(table);
    QStringList colDefs;
    QString primaryKey;

    for (int row = 0; row < m_fieldTable->rowCount(); ++row) {
        QString name = m_fieldTable->item(row, 0)->text().trimmed();
        if (name.isEmpty()) continue;

        auto *typeCombo = qobject_cast<QComboBox*>(m_fieldTable->cellWidget(row, 1));
        QString type = typeCombo ? typeCombo->currentText().toUpper() : "VARCHAR";

        auto *lenEdit = qobject_cast<QLineEdit*>(m_fieldTable->cellWidget(row, 2));
        QString len = lenEdit ? lenEdit->text().trimmed() : "";

        QString fullType = type;
        if ((type == "VARCHAR" || type == "CHAR") && !len.isEmpty()) {
            fullType = QString("%1(%2)").arg(type).arg(len);
        }

        QStringList constraints;

        auto *nnWidget = m_fieldTable->cellWidget(row, 3);
        if (nnWidget) {
            auto *nnLayout = nnWidget->layout();
            if (nnLayout && nnLayout->count() > 0) {
                auto *chk = qobject_cast<QCheckBox*>(nnLayout->itemAt(0)->widget());
                if (chk && chk->isChecked()) {
                    constraints.append("NOT NULL");
                }
            }
        }

        auto *pkWidget = m_fieldTable->cellWidget(row, 4);
        if (pkWidget) {
            auto *pkLayout = pkWidget->layout();
            if (pkLayout && pkLayout->count() > 0) {
                auto *chk = qobject_cast<QCheckBox*>(pkLayout->itemAt(0)->widget());
                if (chk && chk->isChecked()) {
                    constraints.append("PRIMARY KEY");
                }
            }
        }

        auto *uniqueWidget = m_fieldTable->cellWidget(row, 5);
        if (uniqueWidget) {
            auto *uniqueLayout = uniqueWidget->layout();
            if (uniqueLayout && uniqueLayout->count() > 0) {
                auto *chk = qobject_cast<QCheckBox*>(uniqueLayout->itemAt(0)->widget());
                if (chk && chk->isChecked()) {
                    constraints.append("UNIQUE");
                }
            }
        }

        auto *aiWidget = m_fieldTable->cellWidget(row, 6);
        if (aiWidget) {
            auto *aiLayout = aiWidget->layout();
            if (aiLayout && aiLayout->count() > 0) {
                auto *chk = qobject_cast<QCheckBox*>(aiLayout->itemAt(0)->widget());
                if (chk && chk->isChecked()) {
                    constraints.append("AUTO_INCREMENT");
                }
            }
        }

        QString defaultValue;
        auto *defItem = m_fieldTable->item(row, 7);
        if (defItem) {
            QString def = defItem->text().trimmed();
            if (!def.isEmpty()) {
                bool isNum;
                def.toDouble(&isNum);
                if (isNum) {
                    defaultValue = QString("DEFAULT %1").arg(def);
                } else {
                    defaultValue = QString("DEFAULT '%1'").arg(def);
                }
            }
        }

        QString colDef = QString("    %1 %2").arg(name).arg(fullType);
        if (!defaultValue.isEmpty()) colDef.append(" ").append(defaultValue);
        if (!constraints.isEmpty()) colDef.append(" ").append(constraints.join(" "));
        colDefs.append(colDef);
    }

    sql.append(colDefs.join(",\n"));
    sql.append("\n);");

    return sql;
}

QString CreateTableDialog::getGeneratedSql() const
{
    return m_generatedSql;
}

void CreateTableDialog::onExecuteCreate()
{
    m_generatedSql = buildSql();
    if (m_generatedSql.isEmpty()) {
        QMessageBox::warning(this, "错误", "请输入表名！");
        return;
    }

    accept();
}
