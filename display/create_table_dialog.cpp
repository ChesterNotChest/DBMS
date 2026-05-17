#include "create_table_dialog.h"

CreateTableDialog::CreateTableDialog(QWidget *parent, const QString &defaultDb)
    : QDialog(parent)
{
    setWindowTitle("可视化建表");
    setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint);
    setMinimumSize(980, 720);
    setStyleSheet(R"(
        QDialog {
            background-color: #F7F8FA;
        }
        QLabel {
            font-size: 14px;
            font-weight: 600;
            color: #4A5568;
            padding: 4px;
        }
        QLineEdit {
            border: 1px solid #DDE1E6;
            border-radius: 4px;
            padding: 6px 10px;
            background-color: white;
            font-size: 13px;
            color: #333;
            min-height: 30px;
        }
        QLineEdit:focus {
            border-color: #A0B0C0;
            outline: none;
        }
        QLineEdit:disabled {
            background-color: #F5F6F7;
            color: #B0B5BA;
        }
        QComboBox {
            border: 1px solid #DDE1E6;
            border-radius: 3px;
            padding: 2px 8px;
            background-color: white;
            font-size: 13px;
            color: #333;
            min-height: 22px;
            max-height: 26px;
        }
        QComboBox:focus {
            border-color: #A0B0C0;
            outline: none;
        }
        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: center right;
            width: 20px;
            border-left: none;
            border-top-right-radius: 3px;
            border-bottom-right-radius: 3px;
        }
        QComboBox::down-arrow {
            image: none;
            border-left: 4px solid transparent;
            border-right: 4px solid transparent;
            border-top: 5px solid #888;
            margin-right: 3px;
        }
        QComboBox::down-arrow:hover {
            border-top-color: #555;
        }
        QComboBox QAbstractItemView {
            background-color: white;
            border: 1px solid #DDE1E6;
            font-size: 13px;
            color: #333;
            selection-background-color: #EEF0F3;
            selection-color: #555;
        }
        QComboBox QAbstractItemView::item {
            padding: 6px 12px;
            color: #333;
        }
        QComboBox QAbstractItemView::item:hover {
            background-color: #EEF0F3;
            color: #444;
        }
        QComboBox QAbstractItemView::item:selected {
            background-color: #EEF0F3;
            color: #444;
        }
        QTableWidget {
            background-color: white;
            border: 1px solid #DDE1E6;
            border-radius: 6px;
            gridline-color: #EEE;
            font-size: 13px;
            color: #333;
            selection-background-color: #EEF0F3;
            selection-color: #444;
            alternate-background-color: #FAFBFC;
        }
        QTableWidget::item {
            padding: 6px 8px;
            text-align: center;
        }
        QTableWidget::item:selected {
            background-color: #EEF0F3;
            color: #444;
        }
        QHeaderView::section {
            background-color: #F0F1F3;
            padding: 8px 8px;
            border: none;
            border-right: 1px solid #E5E8EB;
            border-bottom: 2px solid #DDE1E6;
            font-weight: 600;
            font-size: 13px;
            color: #555;
            min-height: 36px;
        }
        QHeaderView::section:last {
            border-right: none;
        }
        QHeaderView::section:vertical {
            background-color: white;
            border: none;
            border-bottom: 1px solid #EEE;
            border-right: 1px solid #EEE;
            font-weight: 500;
            font-size: 12px;
            color: #999;
        }
        QTableCornerButton::section {
            background-color: #F0F1F3;
            border: none;
            border-right: 1px solid #E5E8EB;
            border-bottom: 2px solid #DDE1E6;
        }
        QPushButton {
            background-color: #F0F1F3;
            color: #555;
            border: 1px solid #DDE1E6;
            border-radius: 6px;
            padding: 9px 20px;
            font-size: 13px;
            font-weight: 500;
            min-height: 34px;
        }
        QPushButton:hover {
            background-color: #E5E7EA;
            border-color: #C8CCD1;
        }
        QPushButton:pressed {
            background-color: #DDDDE0;
        }
        QPushButton.primary {
            background-color: #607080;
            color: white;
            border-color: #607080;
        }
        QPushButton.primary:hover {
            background-color: #708090;
            border-color: #708090;
        }
        QPushButton.danger {
            background-color: #B85C50;
            color: white;
            border-color: #B85C50;
        }
        QPushButton.danger:hover {
            background-color: #C86B5E;
            border-color: #C86B5E;
        }
        QCheckBox {
            color: #555;
            font-size: 13px;
            font-weight: 400;
            spacing: 6px;
        }
        QCheckBox::indicator {
            width: 18px;
            height: 18px;
            border: 1.5px solid #BCC0C6;
            border-radius: 3px;
            background-color: white;
        }
        QCheckBox::indicator:hover {
            border-color: #909999;
        }
        QCheckBox::indicator:checked {
            background-color: white;
            border-color: #4A5568;
            border-width: 1.8px;
        }
        QCheckBox::indicator:checked::after {
            content: "✓";
            display: block;
            text-align: center;
            font-size: 12px;
            font-weight: bold;
            color: #2D3748;
            margin-top: -1px;
        }
    )");

    buildLayout(defaultDb);
}

CreateTableDialog::CreateTableDialog(QWidget *parent,
                                     const QString &defaultDb,
                                     const QString &tableName,
                                     const QString &createTableText)
    : QDialog(parent)
    , m_sourceTableName(tableName)
    , m_isEditMode(true)
{
    setWindowTitle("可视化建表");
    setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint);
    setMinimumSize(980, 720);
    setStyleSheet(R"(
        QDialog {
            background-color: #F7F8FA;
        }
        QLabel {
            font-size: 14px;
            font-weight: 600;
            color: #4A5568;
            padding: 4px;
        }
        QLineEdit {
            border: 1px solid #DDE1E6;
            border-radius: 4px;
            padding: 6px 10px;
            background-color: white;
            font-size: 13px;
            color: #333;
            min-height: 30px;
        }
        QLineEdit:focus {
            border-color: #A0B0C0;
            outline: none;
        }
        QLineEdit:disabled {
            background-color: #F5F6F7;
            color: #B0B5BA;
        }
        QComboBox {
            border: 1px solid #DDE1E6;
            border-radius: 3px;
            padding: 2px 8px;
            background-color: white;
            font-size: 13px;
            color: #333;
            min-height: 22px;
            max-height: 26px;
        }
        QComboBox:focus {
            border-color: #A0B0C0;
            outline: none;
        }
        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: center right;
            width: 20px;
            border-left: none;
            border-top-right-radius: 3px;
            border-bottom-right-radius: 3px;
        }
        QComboBox::down-arrow {
            image: none;
            border-left: 4px solid transparent;
            border-right: 4px solid transparent;
            border-top: 5px solid #888;
            margin-right: 3px;
        }
        QComboBox::down-arrow:hover {
            border-top-color: #555;
        }
        QComboBox QAbstractItemView {
            background-color: white;
            border: 1px solid #DDE1E6;
            font-size: 13px;
            color: #333;
            selection-background-color: #EEF0F3;
            selection-color: #555;
        }
        QComboBox QAbstractItemView::item {
            padding: 6px 12px;
            color: #333;
        }
        QComboBox QAbstractItemView::item:hover {
            background-color: #EEF0F3;
            color: #444;
        }
        QComboBox QAbstractItemView::item:selected {
            background-color: #EEF0F3;
            color: #444;
        }
        QTableWidget {
            background-color: white;
            border: 1px solid #DDE1E6;
            border-radius: 6px;
            gridline-color: #EEE;
            font-size: 13px;
            color: #333;
            selection-background-color: #EEF0F3;
            selection-color: #444;
            alternate-background-color: #FAFBFC;
        }
        QTableWidget::item {
            padding: 6px 8px;
            text-align: center;
        }
        QTableWidget::item:selected {
            background-color: #EEF0F3;
            color: #444;
        }
        QHeaderView::section {
            background-color: #F0F1F3;
            padding: 8px 8px;
            border: none;
            border-right: 1px solid #E5E8EB;
            border-bottom: 2px solid #DDE1E6;
            font-weight: 600;
            font-size: 13px;
            color: #555;
            min-height: 36px;
        }
        QHeaderView::section:last {
            border-right: none;
        }
        QHeaderView::section:vertical {
            background-color: white;
            border: none;
            border-bottom: 1px solid #EEE;
            border-right: 1px solid #EEE;
            font-weight: 500;
            font-size: 12px;
            color: #999;
        }
        QTableCornerButton::section {
            background-color: #F0F1F3;
            border: none;
            border-right: 1px solid #E5E8EB;
            border-bottom: 2px solid #DDE1E6;
        }
        QPushButton {
            background-color: #F0F1F3;
            color: #555;
            border: 1px solid #DDE1E6;
            border-radius: 6px;
            padding: 9px 20px;
            font-size: 13px;
            font-weight: 500;
            min-height: 34px;
        }
        QPushButton:hover {
            background-color: #E5E7EA;
            border-color: #C8CCD1;
        }
        QPushButton:pressed {
            background-color: #DDDDE0;
        }
        QPushButton.primary {
            background-color: #607080;
            color: white;
            border-color: #607080;
        }
        QPushButton.primary:hover {
            background-color: #708090;
            border-color: #708090;
        }
        QPushButton.danger {
            background-color: #B85C50;
            color: white;
            border-color: #B85C50;
        }
        QPushButton.danger:hover {
            background-color: #C86B5E;
            border-color: #C86B5E;
        }
        QCheckBox {
            color: #555;
            font-size: 13px;
            font-weight: 400;
            spacing: 6px;
        }
        QCheckBox::indicator {
            width: 18px;
            height: 18px;
            border: 1.5px solid #BCC0C6;
            border-radius: 3px;
            background-color: white;
        }
        QCheckBox::indicator:hover {
            border-color: #909999;
        }
        QCheckBox::indicator:checked {
            background-color: white;
            border-color: #4A5568;
            border-width: 1.8px;
        }
        QCheckBox::indicator:checked::after {
            content: "✓";
            display: block;
            text-align: center;
            font-size: 12px;
            font-weight: bold;
            color: #2D3748;
            margin-top: -1px;
        }
    )");

    buildLayout(defaultDb);
    loadTableSchema(tableName, createTableText);
}

void CreateTableDialog::buildLayout(const QString &defaultDb)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(20);
    mainLayout->setContentsMargins(24, 24, 24, 24);

    // 标题区域
    auto *titleLabel = new QLabel("📋 创建新数据表");
    titleLabel->setStyleSheet("font-size: 22px; font-weight: 700; color: #374151; padding: 0; letter-spacing: 0.5px;");
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
    m_fieldTable->verticalHeader()->setDefaultSectionSize(34);
    m_fieldTable->verticalHeader()->setMinimumSectionSize(32);
    m_fieldTable->verticalHeader()->setVisible(false);
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
    connect(addBtn, &QPushButton::clicked, this, &CreateTableDialog::onAddColumnDialog);

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

void CreateTableDialog::onAddColumnDialog()
{
    AddColumnDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    ColumnConfig cfg = dlg.getConfig();
    if (cfg.name.isEmpty()) {
        QMessageBox::warning(this, "警告", "列名不能为空！");
        return;
    }

    int row = m_fieldTable->rowCount();
    m_fieldTable->insertRow(row);

    // 字段名称
    auto *nameItem = new QTableWidgetItem(cfg.name);
    nameItem->setTextAlignment(Qt::AlignCenter);
    m_fieldTable->setItem(row, 0, nameItem);

    // 数据类型
    auto *typeCombo = new QComboBox();
    typeCombo->setEditable(false);
    typeCombo->setFixedHeight(24);
    typeCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    typeCombo->addItems({
        "INT", "BIGINT", "FLOAT", "DOUBLE", "DECIMAL",
        "NUMBER", "VARCHAR", "CHAR", "TEXT", "DATE",
        "DATETIME", "TIME", "BLOB", "BOOLEAN"
    });
    typeCombo->setCurrentText(cfg.type);
    m_fieldTable->setCellWidget(row, 1, typeCombo);

    // 长度
    auto *lenEdit = new QLineEdit(cfg.length > 0 ? QString::number(cfg.length) : "");
    lenEdit->setAlignment(Qt::AlignCenter);
    lenEdit->setPlaceholderText("长度");
    lenEdit->setFixedHeight(24);
    lenEdit->setStyleSheet("QLineEdit { padding: 2px 6px; font-size: 13px; }");
    m_fieldTable->setCellWidget(row, 2, lenEdit);

    // NOT NULL
    auto *chkNotNull = new QCheckBox();
    chkNotNull->setChecked(!cfg.allowNull);
    auto *widgetNn = new QWidget();
    auto *layoutNn = new QHBoxLayout(widgetNn);
    layoutNn->setContentsMargins(0, 0, 0, 0);
    layoutNn->setAlignment(Qt::AlignCenter);
    layoutNn->addWidget(chkNotNull);
    m_fieldTable->setCellWidget(row, 3, widgetNn);

    // 主键
    auto *chkPk = new QCheckBox();
    chkPk->setChecked(cfg.primaryKey);
    auto *widgetPk = new QWidget();
    auto *layoutPk = new QHBoxLayout(widgetPk);
    layoutPk->setContentsMargins(0, 0, 0, 0);
    layoutPk->setAlignment(Qt::AlignCenter);
    layoutPk->addWidget(chkPk);
    m_fieldTable->setCellWidget(row, 4, widgetPk);

    // 唯一
    auto *chkUnique = new QCheckBox();
    chkUnique->setChecked(cfg.unique);
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
    auto *defItem = new QTableWidgetItem(cfg.defaultValue);
    defItem->setTextAlignment(Qt::AlignCenter);
    m_fieldTable->setItem(row, 7, defItem);

    // 备注（CHECK约束存入备注列）
    auto *commItem = new QTableWidgetItem(cfg.checkConstraint);
    commItem->setTextAlignment(Qt::AlignCenter);
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
    typeCombo->setFixedHeight(24);
    typeCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
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
    lenEdit->setFixedHeight(24);
    lenEdit->setStyleSheet("QLineEdit { padding: 2px 6px; font-size: 13px; }");
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

void CreateTableDialog::onAddColumnWithConfig(const ColumnConfig &cfg)
{
    int row = m_fieldTable->rowCount();
    m_fieldTable->insertRow(row);

    auto *nameItem = new QTableWidgetItem(cfg.name);
    nameItem->setTextAlignment(Qt::AlignCenter);
    m_fieldTable->setItem(row, 0, nameItem);

    auto *typeCombo = new QComboBox();
    typeCombo->setEditable(false);
    typeCombo->setFixedHeight(24);
    typeCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    typeCombo->addItems({
        "INT", "BIGINT", "FLOAT", "DOUBLE", "DECIMAL",
        "VARCHAR", "CHAR", "TEXT", "DATE", "DATETIME",
        "TIME", "BLOB", "BOOLEAN"
    });
    typeCombo->setCurrentText(cfg.type.isEmpty() ? "VARCHAR" : cfg.type.toUpper());
    m_fieldTable->setCellWidget(row, 1, typeCombo);

    auto *lenEdit = new QLineEdit(cfg.length > 0 ? QString::number(cfg.length) : "");
    lenEdit->setAlignment(Qt::AlignCenter);
    lenEdit->setPlaceholderText("长度");
    lenEdit->setFixedHeight(24);
    lenEdit->setStyleSheet("QLineEdit { padding: 2px 6px; font-size: 13px; }");
    m_fieldTable->setCellWidget(row, 2, lenEdit);

    auto *chkNotNull = new QCheckBox();
    chkNotNull->setChecked(!cfg.allowNull);
    auto *widgetNn = new QWidget();
    auto *layoutNn = new QHBoxLayout(widgetNn);
    layoutNn->setContentsMargins(0, 0, 0, 0);
    layoutNn->setAlignment(Qt::AlignCenter);
    layoutNn->addWidget(chkNotNull);
    m_fieldTable->setCellWidget(row, 3, widgetNn);

    auto *chkPk = new QCheckBox();
    chkPk->setChecked(cfg.primaryKey);
    auto *widgetPk = new QWidget();
    auto *layoutPk = new QHBoxLayout(widgetPk);
    layoutPk->setContentsMargins(0, 0, 0, 0);
    layoutPk->setAlignment(Qt::AlignCenter);
    layoutPk->addWidget(chkPk);
    m_fieldTable->setCellWidget(row, 4, widgetPk);

    auto *chkUnique = new QCheckBox();
    chkUnique->setChecked(cfg.unique);
    auto *widgetUniq = new QWidget();
    auto *layoutUniq = new QHBoxLayout(widgetUniq);
    layoutUniq->setContentsMargins(0, 0, 0, 0);
    layoutUniq->setAlignment(Qt::AlignCenter);
    layoutUniq->addWidget(chkUnique);
    m_fieldTable->setCellWidget(row, 5, widgetUniq);

    auto *chkAutoInc = new QCheckBox();
    chkAutoInc->setChecked(false);
    auto *widgetAi = new QWidget();
    auto *layoutAi = new QHBoxLayout(widgetAi);
    layoutAi->setContentsMargins(0, 0, 0, 0);
    layoutAi->setAlignment(Qt::AlignCenter);
    layoutAi->addWidget(chkAutoInc);
    m_fieldTable->setCellWidget(row, 6, widgetAi);

    auto *defItem = new QTableWidgetItem(cfg.defaultValue);
    defItem->setTextAlignment(Qt::AlignCenter);
    m_fieldTable->setItem(row, 7, defItem);

    auto *commItem = new QTableWidgetItem(cfg.checkConstraint);
    commItem->setTextAlignment(Qt::AlignCenter);
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
    lenEdit->setStyleSheet(needsLen ? "QLineEdit { padding: 2px 6px; font-size: 13px; }" : "QLineEdit { padding: 2px 6px; background-color: #F5F6F7; color: #B0B5BA; }");
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

        // CHECK约束（从备注列读取）
        auto *commItem = m_fieldTable->item(row, 8);
        if (commItem) {
            QString check = commItem->text().trimmed();
            if (!check.isEmpty()) {
                constraints.append(QString("CHECK (%1)").arg(check));
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

void CreateTableDialog::loadTableSchema(const QString &tableName, const QString &createTableText)
{
    if (tableName.isEmpty() || createTableText.trimmed().isEmpty()) return;
    setWindowTitle("可视化建表 / 编辑表");
    m_tableNameEdit->setText(tableName);
    m_tableNameEdit->setEnabled(false);

    m_originalColumns.clear();
    while (m_fieldTable->rowCount() > 0) {
        m_fieldTable->removeRow(0);
    }

    int start = createTableText.indexOf('(');
    int end = createTableText.lastIndexOf(')');
    QString body = createTableText;
    if (start >= 0 && end > start) {
        body = createTableText.mid(start + 1, end - start - 1);
    }
    const QStringList lines = body.split('\n', Qt::SkipEmptyParts);

    for (const QString &rawLine : lines) {
        QString line = rawLine.trimmed();
        if (line.endsWith(',')) line.chop(1);
        if (line.isEmpty()) continue;
        if (line.startsWith("PRIMARY KEY", Qt::CaseInsensitive)
            || line.startsWith("UNIQUE", Qt::CaseInsensitive)
            || line.startsWith("CONSTRAINT", Qt::CaseInsensitive)
            || line.startsWith("CHECK", Qt::CaseInsensitive)) {
            continue;
        }

        ColumnConfig cfg = parseColumnDefinition(line);
        if (cfg.name.isEmpty()) continue;
        m_originalColumns.append(cfg);
        onAddColumnWithConfig(cfg);
    }
}

QString CreateTableDialog::columnDefinitionText(const ColumnConfig &cfg) const
{
    QString fullType = cfg.type.toUpper();
    if ((fullType == "VARCHAR" || fullType == "CHAR") && cfg.length > 0) {
        fullType = QString("%1(%2)").arg(fullType).arg(cfg.length);
    }

    QStringList constraints;
    if (!cfg.allowNull) {
        constraints.append("NOT NULL");
    }
    if (cfg.primaryKey) {
        constraints.append("PRIMARY KEY");
    }
    if (cfg.unique) {
        constraints.append("UNIQUE");
    }
    if (!cfg.defaultValue.isEmpty()) {
        bool isNum;
        cfg.defaultValue.toDouble(&isNum);
        if (isNum || cfg.defaultValue.compare("NULL", Qt::CaseInsensitive) == 0) {
            constraints.append(QString("DEFAULT %1").arg(cfg.defaultValue));
        } else {
            QString escapedDefault = cfg.defaultValue;
            constraints.append(QString("DEFAULT '%1'").arg(escapedDefault.replace("'", "''")));
        }
    }
    if (!cfg.checkConstraint.isEmpty()) {
        constraints.append(QString("CHECK (%1)").arg(cfg.checkConstraint));
    }

    QString colDef = QString("    %1 %2").arg(cfg.name).arg(fullType);
    if (!constraints.isEmpty()) {
        colDef.append(" ").append(constraints.join(" "));
    }
    return colDef;
}

ColumnConfig CreateTableDialog::rowColumnConfig(int row) const
{
    ColumnConfig cfg;
    if (row < 0 || row >= m_fieldTable->rowCount()) return cfg;

    QTableWidgetItem *nameItem = m_fieldTable->item(row, 0);
    if (!nameItem) return cfg;
    cfg.name = nameItem->text().trimmed();
    if (cfg.name.isEmpty()) return cfg;

    auto *typeCombo = qobject_cast<QComboBox *>(m_fieldTable->cellWidget(row, 1));
    cfg.type = typeCombo ? typeCombo->currentText().toUpper() : QString("VARCHAR");

    auto *lenEdit = qobject_cast<QLineEdit *>(m_fieldTable->cellWidget(row, 2));
    cfg.length = lenEdit ? lenEdit->text().trimmed().toInt() : 0;

    auto *nnWidget = m_fieldTable->cellWidget(row, 3);
    if (nnWidget) {
        auto *nnLayout = nnWidget->layout();
        if (nnLayout && nnLayout->count() > 0) {
            auto *chk = qobject_cast<QCheckBox *>(nnLayout->itemAt(0)->widget());
            cfg.allowNull = !(chk && chk->isChecked());
        }
    }

    auto *pkWidget = m_fieldTable->cellWidget(row, 4);
    if (pkWidget) {
        auto *pkLayout = pkWidget->layout();
        if (pkLayout && pkLayout->count() > 0) {
            auto *chk = qobject_cast<QCheckBox *>(pkLayout->itemAt(0)->widget());
            cfg.primaryKey = chk && chk->isChecked();
        }
    }

    auto *uniqueWidget = m_fieldTable->cellWidget(row, 5);
    if (uniqueWidget) {
        auto *uniqueLayout = uniqueWidget->layout();
        if (uniqueLayout && uniqueLayout->count() > 0) {
            auto *chk = qobject_cast<QCheckBox *>(uniqueLayout->itemAt(0)->widget());
            cfg.unique = chk && chk->isChecked();
        }
    }

    auto *defItem = m_fieldTable->item(row, 7);
    if (defItem) {
        cfg.defaultValue = defItem->text().trimmed();
    }

    auto *commItem = m_fieldTable->item(row, 8);
    if (commItem) {
        cfg.checkConstraint = commItem->text().trimmed();
    }

    return cfg;
}

QString CreateTableDialog::buildAlterSql() const
{
    if (m_sourceTableName.isEmpty()) return QString();
    QStringList statements;
    QMap<QString, ColumnConfig> originalMap;
    for (const ColumnConfig &cfg : std::as_const(m_originalColumns)) {
        originalMap.insert(cfg.name, cfg);
    }

    QSet<QString> currentNames;
    for (int row = 0; row < m_fieldTable->rowCount(); ++row) {
        ColumnConfig current = rowColumnConfig(row);
        if (current.name.isEmpty()) continue;
        currentNames.insert(current.name);

        if (!originalMap.contains(current.name)) {
            statements.append(QString("ALTER TABLE %1 ADD COLUMN %2;").arg(m_sourceTableName, columnDefinitionText(current)));
            continue;
        }

        const ColumnConfig original = originalMap.value(current.name);
        if (current.type != original.type
            || current.length != original.length
            || current.allowNull != original.allowNull
            || current.primaryKey != original.primaryKey
            || current.unique != original.unique
            || current.defaultValue != original.defaultValue
            || current.checkConstraint != original.checkConstraint) {
            statements.append(QString("ALTER TABLE %1 MODIFY COLUMN %2;").arg(m_sourceTableName, columnDefinitionText(current)));
        }
    }

    for (const ColumnConfig &orig : qAsConst(m_originalColumns)) {
        if (!currentNames.contains(orig.name)) {
            statements.append(QStringLiteral("ALTER TABLE %1 DROP COLUMN %2;").arg(m_sourceTableName, orig.name));
        }
    }

    return statements.join("\n");
}

ColumnConfig CreateTableDialog::parseColumnDefinition(const QString &text) const
{
    ColumnConfig cfg;
    QString line = text.trimmed();
    if (line.endsWith(",")) line.chop(1);
    if (line.isEmpty()) return cfg;

    static const QRegularExpression columnRegex(R"(^\s*([`\"]?\w+[`\"]?)\s+([A-Za-z]+)(\([^\)]*\))?(.*)$)");
    auto match = columnRegex.match(line);
    if (!match.hasMatch()) return cfg;

    cfg.name = match.captured(1).remove('`').remove('"');
    cfg.type = match.captured(2).toUpper();
    QString length = match.captured(3);
    if (!length.isEmpty()) {
        length.remove('(').remove(')');
        cfg.length = length.toInt();
    }

    QString rest = match.captured(4).trimmed();
    QString lower = rest.toLower();
    if (lower.contains("not null")) cfg.allowNull = false;
    if (lower.contains("primary key")) cfg.primaryKey = true;
    if (lower.contains("unique")) cfg.unique = true;
    if (lower.contains("auto_increment")) {
        // no direct UI support for auto increment in edit mode, leave default
    }

    static const QRegularExpression defaultRegex(R"((?i)DEFAULT\s+((?:'[^']*'|\S+)))");
    auto defMatch = defaultRegex.match(rest);
    if (defMatch.hasMatch()) {
        QString token = defMatch.captured(1).trimmed();
        if (token.startsWith("'") && token.endsWith("'")) {
            token = token.mid(1, token.length() - 2).replace("''", "'");
        }
        cfg.defaultValue = token;
    }

    static const QRegularExpression checkRegex(R"((?i)CHECK\s*\((.*)\))");
    auto checkMatch = checkRegex.match(rest);
    if (checkMatch.hasMatch()) {
        cfg.checkConstraint = checkMatch.captured(1).trimmed();
    }

    return cfg;
}

QString CreateTableDialog::getGeneratedSql() const
{
    return m_generatedSql;
}

void CreateTableDialog::onExecuteCreate()
{
    if (m_isEditMode) {
        m_generatedSql = buildAlterSql();
        if (m_generatedSql.isEmpty()) {
            QMessageBox::information(this, "提示", "表结构未发生变化，无法提交任何修改。请调整字段或新增列。");
            return;
        }
    } else {
        m_generatedSql = buildSql();
        if (m_generatedSql.isEmpty()) {
            QMessageBox::warning(this, "错误", "请输入表名！");
            return;
        }
    }

    accept();
}
