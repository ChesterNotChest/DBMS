#include "add_column_dialog.h"
#include <QFrame>

AddColumnDialog::AddColumnDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("添加列");
    setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint);
    setFixedSize(420, 460);
    setModal(true);

    buildStyle();
    buildLayout();
}

void AddColumnDialog::buildStyle()
{
    setStyleSheet(R"(
        QDialog {
            background-color: #F0F1F3;
        }
        QLabel {
            font-size: 13px;
            font-weight: 500;
            color: #4A5568;
            padding: 0;
            min-width: 80px;
        }
        QLineEdit {
            border: 1px solid #C8CCD1;
            border-radius: 3px;
            padding: 0px 8px;
            background-color: white;
            font-size: 13px;
            color: #333;
            min-height: 34px;
            max-height: 34px;
        }
        QLineEdit:focus {
            border-color: #9099A0;
            outline: none;
        }
        QLineEdit::placeholder {
            color: #A0A5AA;
        }
        QComboBox {
            border: 1px solid #C8CCD1;
            border-radius: 3px;
            padding: 0px 8px;
            background-color: white;
            font-size: 13px;
            color: #333;
            min-height: 34px;
            max-height: 34px;
        }
        QComboBox:focus {
            border-color: #9099A0;
            outline: none;
        }
        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: center right;
            width: 22px;
            border-left: none;
        }
        QComboBox::down-arrow {
            image: none;
            border-left: 4px solid transparent;
            border-right: 4px solid transparent;
            border-top: 5px solid #888;
            margin-right: 4px;
        }
        QComboBox QAbstractItemView {
            background-color: white;
            border: 1px solid #C8CCD1;
            font-size: 13px;
            color: #333;
            selection-background-color: #E8EAED;
            selection-color: #333;
        }
        QComboBox QAbstractItemView::item {
            padding: 6px 12px;
        }
        QComboBox QAbstractItemView::item:hover {
            background-color: #E8EAED;
        }
        QComboBox QAbstractItemView::item:selected {
            background-color: #E8EAED;
        }
        QSpinBox {
            border: 1px solid #C8CCD1;
            border-radius: 3px;
            padding: 0px 8px;
            background-color: white;
            font-size: 13px;
            color: #333;
            min-height: 34px;
            max-height: 34px;
        }
        QSpinBox:focus {
            border-color: #9099A0;
            outline: none;
        }
        QSpinBox::up-button, QSpinBox::down-button {
            width: 18px;
            border: none;
            background-color: #F5F6F7;
        }
        QSpinBox::up-button:hover, QSpinBox::down-button:hover {
            background-color: #E8EAED;
        }
        QSpinBox::up-arrow {
            image: none;
            border-left: 3px solid transparent;
            border-right: 3px solid transparent;
            border-bottom: 4px solid #888;
        }
        QSpinBox::down-arrow {
            image: none;
            border-left: 3px solid transparent;
            border-right: 3px solid transparent;
            border-top: 4px solid #888;
        }
        QCheckBox {
            color: #4A5568;
            font-size: 13px;
            font-weight: 500;
            spacing: 8px;
        }
        QCheckBox::indicator {
            width: 16px;
            height: 16px;
            border: 1.5px solid #B0B5BA;
            border-radius: 2px;
            background-color: white;
        }
        QCheckBox::indicator:hover {
            border-color: #9099A0;
        }
        QCheckBox::indicator:checked {
            background-color: white;
            border-color: #607080;
            border-width: 1.8px;
        }
        QCheckBox::indicator:checked::after {
            content: "✓";
            display: block;
            text-align: center;
            font-size: 11px;
            font-weight: bold;
            color: #2D3748;
            margin-top: -1px;
        }
        QPushButton {
            background-color: #F0F1F3;
            color: #555;
            border: 1px solid #C8CCD1;
            border-radius: 4px;
            padding: 8px 20px;
            font-size: 13px;
            font-weight: 500;
            min-height: 32px;
        }
        QPushButton:hover {
            background-color: #E5E7EA;
            border-color: #B0B5BA;
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
    )");
}

void AddColumnDialog::buildLayout()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(24, 20, 24, 20);

    // 标题行
    auto *titleLayout = new QHBoxLayout;
    auto *titleLabel = new QLabel("添加列");
    titleLabel->setStyleSheet("font-size: 16px; font-weight: 600; color: #374151;");
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();
    mainLayout->addLayout(titleLayout);

    // 分隔线
    auto *line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("color: #DDE1E6;");
    mainLayout->addWidget(line);

    // 表单区域
    auto *formLayout = new QVBoxLayout;
    formLayout->setSpacing(14);

    // 列名
    auto *nameLayout = new QHBoxLayout;
    nameLayout->setSpacing(12);
    auto *nameLabel = new QLabel("列名：");
    m_nameEdit = new QLineEdit();
    m_nameEdit->setPlaceholderText("请输入列名");
    nameLayout->addWidget(nameLabel);
    nameLayout->addWidget(m_nameEdit, 1);
    formLayout->addLayout(nameLayout);

    // 数据类型
    auto *typeLayout = new QHBoxLayout;
    typeLayout->setSpacing(12);
    auto *typeLabel = new QLabel("数据类型：");
    m_typeCombo = new QComboBox();
    m_typeCombo->addItems({
        "INT", "BIGINT", "FLOAT", "DOUBLE", "DECIMAL",
        "NUMBER", "VARCHAR", "CHAR", "TEXT", "DATE",
        "DATETIME", "TIME", "BLOB", "BOOLEAN"
    });
    m_typeCombo->setCurrentText("NUMBER");
    typeLayout->addWidget(typeLabel);
    typeLayout->addWidget(m_typeCombo, 1);
    formLayout->addLayout(typeLayout);

    // 长度/精度
    auto *lenLayout = new QHBoxLayout;
    lenLayout->setSpacing(12);
    auto *lenLabel = new QLabel("长度/精度：");
    m_lengthSpin = new QSpinBox();
    m_lengthSpin->setRange(0, 99999);
    m_lengthSpin->setValue(255);
    m_lengthSpin->setFixedWidth(100);
    lenLayout->addWidget(lenLabel);
    lenLayout->addWidget(m_lengthSpin);
    lenLayout->addStretch();
    formLayout->addLayout(lenLayout);

    // 约束区域 - 复选框水平排列
    auto *constraintLayout = new QHBoxLayout;
    constraintLayout->setSpacing(20);

    m_nullCheck = new QCheckBox("允许空值");
    m_nullCheck->setChecked(true);
    constraintLayout->addWidget(m_nullCheck);

    m_pkCheck = new QCheckBox("主键");
    constraintLayout->addWidget(m_pkCheck);

    m_uniqueCheck = new QCheckBox("唯一");
    constraintLayout->addWidget(m_uniqueCheck);

    constraintLayout->addStretch();
    formLayout->addLayout(constraintLayout);

    // CHECK 约束
    // CHECK 约束 - 复选框+输入框组合
    auto *checkLayout = new QHBoxLayout;
    checkLayout->setSpacing(12);
    auto *checkLabel = new QLabel("CHECK约束：");
    m_checkCheck = new QCheckBox("启用");
    m_checkCheck->setChecked(false);
    m_checkEdit = new QLineEdit();
    m_checkEdit->setPlaceholderText("age > 0 AND age < 150");
    m_checkEdit->setEnabled(false);
    connect(m_checkCheck, &QCheckBox::checkStateChanged, this, [this](int state) {
        m_checkEdit->setEnabled(state == Qt::Checked);
        if (state != Qt::Checked) m_checkEdit->clear();
    });
    checkLayout->addWidget(checkLabel);
    checkLayout->addWidget(m_checkCheck);
    checkLayout->addWidget(m_checkEdit, 1);
    formLayout->addLayout(checkLayout);

    // 默认值
    auto *defaultLayout = new QHBoxLayout;
    defaultLayout->setSpacing(12);
    auto *defaultLabel = new QLabel("默认值：");
    m_defaultEdit = new QLineEdit();
    m_defaultEdit->setPlaceholderText("可选");
    defaultLayout->addWidget(defaultLabel);
    defaultLayout->addWidget(m_defaultEdit, 1);
    formLayout->addLayout(defaultLayout);

    mainLayout->addLayout(formLayout, 1);

    // 底部按钮
    auto *btnLayout = new QHBoxLayout;
    btnLayout->setSpacing(12);

    auto *cancelBtn = new QPushButton("取消");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    auto *okBtn = new QPushButton("确定");
    okBtn->setStyleSheet("QPushButton { background-color: #607080; color: white; border: 1px solid #607080; border-radius: 4px; padding: 8px 20px; font-size: 13px; font-weight: 500; min-height: 32px; } "
                         "QPushButton:hover { background-color: #708090; border-color: #708090; }");
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);

    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(okBtn);
    mainLayout->addLayout(btnLayout);
}

ColumnConfig AddColumnDialog::getConfig() const
{
    ColumnConfig cfg;
    cfg.name = m_nameEdit->text().trimmed();
    cfg.type = m_typeCombo->currentText();
    cfg.length = m_lengthSpin->value();
    cfg.allowNull = m_nullCheck->isChecked();
    cfg.primaryKey = m_pkCheck->isChecked();
    cfg.unique = m_uniqueCheck->isChecked();
    cfg.checkConstraint = m_checkCheck->isChecked() ? m_checkEdit->text().trimmed() : QString();
    cfg.defaultValue = m_defaultEdit->text().trimmed();
    return cfg;
}
