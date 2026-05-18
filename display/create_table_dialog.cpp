#include "create_table_dialog.h"
#include <QListView>

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
            width: 10px;
            height: 6px;
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
            border: none;
        }
        QTableWidget::item:selected {
            background-color: #EEF0F3;
            color: #444;
        }
        QTableWidget QLineEdit {
            border: none;
            background-color: white;
            padding: 6px 8px;
            font-size: 13px;
            margin: 0;
            width: 100%;
        }
        QTableWidget QLineEdit:focus {
            border: none;
            outline: none;
            background-color: white;
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
            width: 10px;
            height: 6px;
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
            border: none;
        }
        QTableWidget::item:selected {
            background-color: #EEF0F3;
            color: #444;
        }
        QTableWidget QLineEdit {
            border: none;
            background-color: white;
            padding: 6px 8px;
            font-size: 13px;
            margin: 0;
            width: 100%;
        }
        QTableWidget QLineEdit:focus {
            border: none;
            outline: none;
            background-color: white;
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
    )");

    buildLayout(defaultDb);
    loadTableSchema(tableName, createTableText);
}

void CreateTableDialog::buildLayout(const QString &defaultDb)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(20);
    mainLayout->setContentsMargins(24, 24, 24, 24);

    auto *titleLabel = new QLabel("📋 创建新数据表");
    titleLabel->setStyleSheet("font-size: 22px; font-weight: 700; color: #374151; padding: 0; letter-spacing: 0.5px;");
    mainLayout->addWidget(titleLabel);

    auto *nameLayout = new QHBoxLayout;
    nameLayout->setSpacing(16);
    nameLayout->addWidget(new QLabel("数据表名称："));
    m_tableNameEdit = new QLineEdit;
    m_tableNameEdit->setPlaceholderText("请输入表名，例如：students");
    m_tableNameEdit->setStyleSheet("QLineEdit { min-width: 200px; }");
    nameLayout->addWidget(m_tableNameEdit, 1);
    mainLayout->addLayout(nameLayout);

    m_fieldTable = new QTableWidget(0, 13);
    m_fieldTable->setHorizontalHeaderLabels({
        "字段名称", "数据类型", "长度", "NOT NULL", "主键", "唯一", "自增", "默认值", "备注", "引用表", "引用字段", "ON DELETE", "ON UPDATE"
    });
    m_fieldTable->horizontalHeader()->setStretchLastSection(true);
    m_fieldTable->verticalHeader()->setDefaultSectionSize(28);
    m_fieldTable->verticalHeader()->setMinimumSectionSize(26);
    m_fieldTable->verticalHeader()->setVisible(false);
    m_fieldTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fieldTable->setAlternatingRowColors(true);
    m_fieldTable->setUpdatesEnabled(false);

    m_fieldTable->setColumnWidth(0, 110);
    m_fieldTable->setColumnWidth(1, 95);
    m_fieldTable->setColumnWidth(2, 70);
    m_fieldTable->setColumnWidth(3, 95);
    m_fieldTable->setColumnWidth(4, 55);
    m_fieldTable->setColumnWidth(5, 55);
    m_fieldTable->setColumnWidth(6, 55);
    m_fieldTable->setColumnWidth(7, 90);
    m_fieldTable->setColumnWidth(8, 90);
    m_fieldTable->setColumnWidth(9, 90);
    m_fieldTable->setColumnWidth(10, 90);
    m_fieldTable->setColumnWidth(11, 100);
    m_fieldTable->setColumnWidth(12, 100);

    m_fieldTable->setUpdatesEnabled(true);

    mainLayout->addWidget(m_fieldTable, 1);

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

    m_fieldTable->setUpdatesEnabled(false);
    for (int i = 0; i < 3; i++) {
        onAddColumn();
    }
    m_fieldTable->setUpdatesEnabled(true);
}

void CreateTableDialog::onAddColumnDialog()
{
    // 直接在表格中添加一行，不再弹出对话框
    onAddColumn();
    
    // 自动滚动到新添加的行并选中编辑
    int row = m_fieldTable->rowCount() - 1;
    m_fieldTable->scrollToItem(m_fieldTable->item(row, 0));
    m_fieldTable->selectRow(row);
    m_fieldTable->editItem(m_fieldTable->item(row, 0));
}

void CreateTableDialog::onAddColumn()
{
    int row = m_fieldTable->rowCount();
    m_fieldTable->insertRow(row);
    m_fieldTable->setRowHeight(row, 22);

    auto *nameItem = new QTableWidgetItem(QString("column%1").arg(row + 1));
    nameItem->setTextAlignment(Qt::AlignCenter);
    m_fieldTable->setItem(row, 0, nameItem);

    auto *typeCombo = new QComboBox();
    typeCombo->setEditable(false);
    typeCombo->setFixedHeight(20);
    typeCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    typeCombo->addItems({
        "INT", "BIGINT", "FLOAT", "DOUBLE", "DECIMAL",
        "VARCHAR", "CHAR", "TEXT", "DATE", "DATETIME",
        "TIME", "BLOB", "BOOLEAN"
    });
    auto *typeView = new QListView(typeCombo);
    typeView->setTextElideMode(Qt::ElideNone);
    typeView->setMinimumWidth(90);
    typeCombo->setView(typeView);
    typeCombo->setStyleSheet(
        "QComboBox { margin: 1px; padding: 0px 3px; font-size: 11px; border: 1px solid #E8E8E8; border-radius: 2px; }"
        "QComboBox::drop-down { width: 14px; }"
        "QComboBox::down-arrow { width: 6px; height: 4px; }"
    );
    typeCombo->setCurrentText("VARCHAR");
    m_fieldTable->setCellWidget(row, 1, typeCombo);

    auto *lenItem = new QTableWidgetItem("255");
    lenItem->setTextAlignment(Qt::AlignCenter);
    m_fieldTable->setItem(row, 2, lenItem);

    auto *chkNotNull = new QCheckBox();
    chkNotNull->setChecked(false);
    chkNotNull->setStyleSheet(
        "QCheckBox { spacing: 0px; padding: 3px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border-radius: 1px; }"
        "QCheckBox::indicator::unchecked { border: 1.5px solid #333; background-color: white; }"
        "QCheckBox::indicator::checked { border: 1.5px solid #333; background-color: #333; }"
        "QCheckBox::indicator::checked::text { color: white; }"
    );
    m_fieldTable->setCellWidget(row, 3, chkNotNull);

    auto *chkPk = new QCheckBox();
    chkPk->setChecked(false);
    chkPk->setStyleSheet(
        "QCheckBox { spacing: 0px; padding: 3px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border-radius: 1px; }"
        "QCheckBox::indicator::unchecked { border: 1.5px solid #333; background-color: white; }"
        "QCheckBox::indicator::checked { border: 1.5px solid #333; background-color: #333; }"
        "QCheckBox::indicator::checked::text { color: white; }"
    );
    m_fieldTable->setCellWidget(row, 4, chkPk);

    auto *chkUnique = new QCheckBox();
    chkUnique->setChecked(false);
    chkUnique->setStyleSheet(
        "QCheckBox { spacing: 0px; padding: 3px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border-radius: 1px; }"
        "QCheckBox::indicator::unchecked { border: 1.5px solid #333; background-color: white; }"
        "QCheckBox::indicator::checked { border: 1.5px solid #333; background-color: #333; }"
        "QCheckBox::indicator::checked::text { color: white; }"
    );
    m_fieldTable->setCellWidget(row, 5, chkUnique);

    auto *chkAutoInc = new QCheckBox();
    chkAutoInc->setChecked(false);
    chkAutoInc->setStyleSheet(
        "QCheckBox { spacing: 0px; padding: 3px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border-radius: 1px; }"
        "QCheckBox::indicator::unchecked { border: 1.5px solid #333; background-color: white; }"
        "QCheckBox::indicator::checked { border: 1.5px solid #333; background-color: #333; }"
        "QCheckBox::indicator::checked::text { color: white; }"
    );
    m_fieldTable->setCellWidget(row, 6, chkAutoInc);

    auto *defItem = new QTableWidgetItem("");
    defItem->setTextAlignment(Qt::AlignCenter);
    defItem->setData(Qt::UserRole, "placeholder");
    m_fieldTable->setItem(row, 7, defItem);

    auto *commItem = new QTableWidgetItem("");
    commItem->setTextAlignment(Qt::AlignCenter);
    commItem->setData(Qt::UserRole, "placeholder");
    m_fieldTable->setItem(row, 8, commItem);

    auto *refTableItem = new QTableWidgetItem("");
    refTableItem->setTextAlignment(Qt::AlignCenter);
    refTableItem->setData(Qt::UserRole, "placeholder");
    m_fieldTable->setItem(row, 9, refTableItem);

    auto *refColumnItem = new QTableWidgetItem("");
    refColumnItem->setTextAlignment(Qt::AlignCenter);
    refColumnItem->setData(Qt::UserRole, "placeholder");
    m_fieldTable->setItem(row, 10, refColumnItem);

    auto *onDeleteCombo = new QComboBox();
    onDeleteCombo->addItems({"NO ACTION", "RESTRICT", "CASCADE", "SET NULL", "SET DEFAULT"});
    onDeleteCombo->setCurrentText("NO ACTION");
    onDeleteCombo->setFixedHeight(22);
    onDeleteCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *deleteView = new QListView(onDeleteCombo);
    deleteView->setTextElideMode(Qt::ElideNone);
    deleteView->setMinimumWidth(100);
    onDeleteCombo->setView(deleteView);
    onDeleteCombo->setStyleSheet(
        "QComboBox { margin: 2px; padding: 1px 4px; font-size: 10px; border: 1px solid #E0E0E0; border-radius: 2px; }"
        "QComboBox::drop-down { width: 16px; }"
        "QComboBox::down-arrow { width: 8px; height: 5px; }"
    );
    m_fieldTable->setCellWidget(row, 11, onDeleteCombo);

    auto *onUpdateCombo = new QComboBox();
    onUpdateCombo->addItems({"NO ACTION", "RESTRICT", "CASCADE", "SET NULL", "SET DEFAULT"});
    onUpdateCombo->setCurrentText("NO ACTION");
    onUpdateCombo->setFixedHeight(22);
    onUpdateCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *updateView = new QListView(onUpdateCombo);
    updateView->setTextElideMode(Qt::ElideNone);
    updateView->setMinimumWidth(100);
    onUpdateCombo->setView(updateView);
    onUpdateCombo->setStyleSheet(
        "QComboBox { margin: 2px; padding: 1px 4px; font-size: 10px; border: 1px solid #E0E0E0; border-radius: 2px; }"
        "QComboBox::drop-down { width: 16px; }"
        "QComboBox::down-arrow { width: 8px; height: 5px; }"
    );
    m_fieldTable->setCellWidget(row, 12, onUpdateCombo);

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
    auto *typeView = new QListView(typeCombo);
    typeView->setTextElideMode(Qt::ElideNone);
    typeView->setMinimumWidth(180);
    typeCombo->setView(typeView);
    typeCombo->setStyleSheet("QComboBox { padding: 2px 6px; font-size: 13px; min-width: 100px; }");
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

    auto *refTableCombo = new QComboBox();
    refTableCombo->setEditable(true);
    refTableCombo->setInsertPolicy(QComboBox::NoInsert);
    refTableCombo->setFixedHeight(24);
    refTableCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    populateReferenceTables(refTableCombo);
    if (!cfg.referencedTable.isEmpty()) {
        refTableCombo->setCurrentText(cfg.referencedTable);
    }
    refTableCombo->setStyleSheet("QComboBox { padding: 2px 6px; font-size: 12px; }");
    m_fieldTable->setCellWidget(row, 9, refTableCombo);

    auto *refColumnCombo = new QComboBox();
    refColumnCombo->setEditable(true);
    refColumnCombo->setInsertPolicy(QComboBox::NoInsert);
    refColumnCombo->setFixedHeight(24);
    refColumnCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    if (!cfg.referencedColumns.isEmpty()) {
        refColumnCombo->addItem(cfg.referencedColumns.first());
    }
    refColumnCombo->setStyleSheet("QComboBox { padding: 2px 6px; font-size: 12px; }");
    m_fieldTable->setCellWidget(row, 10, refColumnCombo);

    auto *onDeleteCombo = new QComboBox();
    onDeleteCombo->addItems({"NO ACTION", "RESTRICT", "CASCADE", "SET NULL", "SET DEFAULT"});
    QString deleteActionStr = tabledef::foreignKeyActionToString(cfg.onDeleteAction);
    onDeleteCombo->setCurrentText(deleteActionStr);
    onDeleteCombo->setFixedHeight(24);
    onDeleteCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    onDeleteCombo->setStyleSheet("QComboBox { padding: 2px 6px; font-size: 12px; }");
    m_fieldTable->setCellWidget(row, 11, onDeleteCombo);

    auto *onUpdateCombo = new QComboBox();
    onUpdateCombo->addItems({"NO ACTION", "RESTRICT", "CASCADE", "SET NULL", "SET DEFAULT"});
    QString updateActionStr = tabledef::foreignKeyActionToString(cfg.onUpdateAction);
    onUpdateCombo->setCurrentText(updateActionStr);
    onUpdateCombo->setFixedHeight(24);
    onUpdateCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    onUpdateCombo->setStyleSheet("QComboBox { padding: 2px 6px; font-size: 12px; }");
    m_fieldTable->setCellWidget(row, 12, onUpdateCombo);

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
            auto *chk = qobject_cast<QCheckBox*>(m_fieldTable->cellWidget(r, 4));
            if (chk) {
                chk->blockSignals(true);
                chk->setChecked(false);
                chk->blockSignals(false);
            }
        }
    }
}

void CreateTableDialog::refreshLengthEnable(int row)
{
    auto *typeCombo = qobject_cast<QComboBox*>(m_fieldTable->cellWidget(row, 1));
    auto *lenItem = m_fieldTable->item(row, 2);
    if (!typeCombo || !lenItem) return;

    QString type = typeCombo->currentText().toUpper();
    bool needsLen = (type == "VARCHAR" || type == "CHAR" || type == "DECIMAL");
    
    if (!needsLen && lenItem->text().trimmed() == "255") {
        lenItem->setText("");
    } else if (needsLen && lenItem->text().trimmed().isEmpty()) {
        lenItem->setText("255");
    }
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

    for (int row = 0; row < m_fieldTable->rowCount(); ++row) {
        QString name = m_fieldTable->item(row, 0)->text().trimmed();
        if (name.isEmpty()) continue;

        auto *typeCombo = qobject_cast<QComboBox*>(m_fieldTable->cellWidget(row, 1));
        QString type = typeCombo ? typeCombo->currentText().toUpper() : "VARCHAR";

        QString len;
        auto *lenItem = m_fieldTable->item(row, 2);
        if (lenItem) {
            len = lenItem->text().trimmed();
        }

        QString fullType = type;
        if ((type == "VARCHAR" || type == "CHAR" || type == "DECIMAL") && !len.isEmpty()) {
            fullType = QString("%1(%2)").arg(type).arg(len);
        }

        QStringList constraints;

        auto *chkNotNull = qobject_cast<QCheckBox*>(m_fieldTable->cellWidget(row, 3));
        if (chkNotNull && chkNotNull->isChecked()) {
            constraints.append("NOT NULL");
        }

        auto *chkPk = qobject_cast<QCheckBox*>(m_fieldTable->cellWidget(row, 4));
        if (chkPk && chkPk->isChecked()) {
            constraints.append("PRIMARY KEY");
        }

        auto *chkUnique = qobject_cast<QCheckBox*>(m_fieldTable->cellWidget(row, 5));
        if (chkUnique && chkUnique->isChecked()) {
            constraints.append("UNIQUE");
        }

        auto *chkAutoInc = qobject_cast<QCheckBox*>(m_fieldTable->cellWidget(row, 6));
        if (chkAutoInc && chkAutoInc->isChecked()) {
            constraints.append("AUTO_INCREMENT");
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

        auto *checkItem = m_fieldTable->item(row, 8);
        if (checkItem) {
            QString check = checkItem->text().trimmed();
            if (!check.isEmpty()) {
                constraints.append(QString("CHECK (%1)").arg(check));
            }
        }

        auto *refTableItem = m_fieldTable->item(row, 9);
        auto *refColumnItem = m_fieldTable->item(row, 10);
        auto *onDeleteCombo = qobject_cast<QComboBox*>(m_fieldTable->cellWidget(row, 11));
        auto *onUpdateCombo = qobject_cast<QComboBox*>(m_fieldTable->cellWidget(row, 12));

        QString refTable = refTableItem ? refTableItem->text().trimmed() : "";
        QString refColumn = refColumnItem ? refColumnItem->text().trimmed() : "";
        QString onDelete = onDeleteCombo ? onDeleteCombo->currentText().trimmed() : "";
        QString onUpdate = onUpdateCombo ? onUpdateCombo->currentText().trimmed() : "";

        if (!refTable.isEmpty() && !refColumn.isEmpty()) {
            QStringList fkParts;
            fkParts.append(QString("REFERENCES %1(%2)").arg(refTable).arg(refColumn));
            if (onDelete != "NO ACTION") {
                fkParts.append(QString("ON DELETE %1").arg(onDelete));
            }
            if (onUpdate != "NO ACTION") {
                fkParts.append(QString("ON UPDATE %1").arg(onUpdate));
            }
            constraints.append(fkParts.join(" "));
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

    auto *refTableCombo = qobject_cast<QComboBox*>(m_fieldTable->cellWidget(row, 9));
    auto *refColumnCombo = qobject_cast<QComboBox*>(m_fieldTable->cellWidget(row, 10));
    auto *onDeleteCombo = qobject_cast<QComboBox*>(m_fieldTable->cellWidget(row, 11));
    auto *onUpdateCombo = qobject_cast<QComboBox*>(m_fieldTable->cellWidget(row, 12));

    if (refTableCombo) {
        cfg.referencedTable = refTableCombo->currentText().trimmed();
    }
    if (refColumnCombo) {
        QString refColumn = refColumnCombo->currentText().trimmed();
        if (!refColumn.isEmpty()) {
            cfg.referencedColumns = { refColumn };
        }
    }
    if (onDeleteCombo) {
        QString deleteAction = onDeleteCombo->currentText().trimmed();
        tabledef::tryParseForeignKeyAction(deleteAction, &cfg.onDeleteAction);
    }
    if (onUpdateCombo) {
        QString updateAction = onUpdateCombo->currentText().trimmed();
        tabledef::tryParseForeignKeyAction(updateAction, &cfg.onUpdateAction);
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

    static const QRegularExpression fkRegex(R"((?i)REFERENCES\s+([^\s(]+)\s*\(([^)]+)\))");
    auto fkMatch = fkRegex.match(rest);
    if (fkMatch.hasMatch()) {
        cfg.referencedTable = fkMatch.captured(1).trimmed();
        QString refColumns = fkMatch.captured(2).trimmed();
        if (!refColumns.isEmpty()) {
            cfg.referencedColumns = { refColumns };
        }
    }

    static const QRegularExpression onDeleteRegex(R"((?i)ON\s+DELETE\s+(NO\s+ACTION|RESTRICT|CASCADE|SET\s+NULL|SET\s+DEFAULT))");
    auto onDeleteMatch = onDeleteRegex.match(rest);
    if (onDeleteMatch.hasMatch()) {
        QString deleteAction = onDeleteMatch.captured(1).trimmed().toUpper();
        deleteAction = deleteAction.replace(" ", "_");
        tabledef::tryParseForeignKeyAction(deleteAction, &cfg.onDeleteAction);
    }

    static const QRegularExpression onUpdateRegex(R"((?i)ON\s+UPDATE\s+(NO\s+ACTION|RESTRICT|CASCADE|SET\s+NULL|SET\s+DEFAULT))");
    auto onUpdateMatch = onUpdateRegex.match(rest);
    if (onUpdateMatch.hasMatch()) {
        QString updateAction = onUpdateMatch.captured(1).trimmed().toUpper();
        updateAction = updateAction.replace(" ", "_");
        tabledef::tryParseForeignKeyAction(updateAction, &cfg.onUpdateAction);
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

void CreateTableDialog::populateReferenceTables(QComboBox *combo)
{
    if (!combo) return;
    combo->clear();
    combo->addItem("");

    QDir dbDir(QStringLiteral("database"));
    if (!dbDir.exists()) {
        dbDir = QDir::current();
    }
    QStringList filters;
    filters << QStringLiteral("*.tab");
    QFileInfoList files = dbDir.entryInfoList(filters, QDir::Files);
    for (const QFileInfo &fi : files) {
        QString tableName = fi.baseName();
        if (!tableName.isEmpty()) {
            combo->addItem(tableName);
        }
    }
}

void CreateTableDialog::populateReferenceColumns(QComboBox *refTableCombo, QComboBox *refColumnCombo)
{
    if (!refTableCombo || !refColumnCombo) return;

    refColumnCombo->clear();
    QString tableName = refTableCombo->currentText().trimmed();
    if (tableName.isEmpty()) return;

    QString metaPath = QStringLiteral("database/%1.meta").arg(tableName);
    QFile metaFile(metaPath);
    if (!metaFile.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QByteArray data = metaFile.readAll();
    metaFile.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return;
    QJsonObject obj = doc.object();
    QJsonArray columns = obj.value(QStringLiteral("columns")).toArray();
    QJsonArray constraints = obj.value(QStringLiteral("constraints")).toArray();

    QSet<QString> keyColumns;
    for (const QJsonValue &cv : constraints) {
        QJsonObject cobj = cv.toObject();
        QString cType = cobj.value(QStringLiteral("type")).toString();
        if (cType == QStringLiteral("PRIMARY_KEY") || cType == QStringLiteral("UNIQUE")) {
            QJsonArray cols = cobj.value(QStringLiteral("columns")).toArray();
            for (const QJsonValue &col : cols) {
                keyColumns.insert(col.toString());
            }
        }
    }

    if (keyColumns.isEmpty() && !columns.isEmpty()) {
        keyColumns.insert(columns.first().toObject().value(QStringLiteral("name")).toString());
    }

    for (const QString &colName : keyColumns) {
        refColumnCombo->addItem(colName);
    }
}