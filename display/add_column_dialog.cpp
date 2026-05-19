#include "add_column_dialog.h"

#include <QFrame>
#include <QListView>
#include <QGroupBox>
#include <QFormLayout>







AddColumnDialog::AddColumnDialog(QWidget *parent)



    : QDialog(parent)



{



    setWindowTitle("添加列");



    setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint);



    setFixedSize(580, 720);



    setModal(true);







    buildStyle();



    buildLayout();



}







void AddColumnDialog::buildStyle()
{
    // 简化全局样式，控件样式在 buildLayout() 中单独设置
    setStyleSheet(R"(
        QDialog {
            background-color: #F8FAFC;
        }
        QComboBox QAbstractItemView {
            background-color: white;
            border: 1px solid #DCE0E5;
            font-size: 13px;
            color: #333;
            selection-background-color: #E8F4FD;
            selection-color: #409EFF;
        }
        QComboBox QAbstractItemView::item {
            padding: 6px 12px;
        }
    )");
}







void AddColumnDialog::buildLayout()
{
    // ========== 整体布局 ==========
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(24);
    mainLayout->setContentsMargins(28, 28, 28, 28);

    // ========== 标题区域 ==========
    auto *titleLabel = new QLabel("新增列");
    titleLabel->setStyleSheet(R"(
        font-size: 20px;
        font-weight: 600;
        color: #2C3E50;
    )");
    mainLayout->addWidget(titleLabel);

    // 标题分隔线
    auto *titleLine = new QFrame();
    titleLine->setFrameShape(QFrame::HLine);
    titleLine->setStyleSheet("background-color: #E8ECF0;");
    mainLayout->addWidget(titleLine);

    // ========== 表单区域 ==========
    auto *formLayout = new QVBoxLayout;
    formLayout->setSpacing(20);

    // --------------------------
    // 模块1：基础属性
    // --------------------------
    auto *basicGroup = new QGroupBox("基础属性");
    basicGroup->setStyleSheet(R"(
        QGroupBox {
            font-size: 14px;
            font-weight: 600;
            color: #34495E;
            border: 1px solid #E8ECF0;
            border-radius: 6px;
            margin-top: 6px;
            padding-top: 12px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 12px;
            padding: 0 8px;
            background-color: #F8FAFC;
        }
    )");

    auto *basicLayout = new QVBoxLayout(basicGroup);
    basicLayout->setSpacing(14);
    basicLayout->setContentsMargins(16, 8, 16, 16);

    // 列名
    auto *nameRow = new QHBoxLayout;
    nameRow->setSpacing(12);
    auto *nameLabel = new QLabel("列名");
    nameLabel->setFixedWidth(70);
    nameLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    nameLabel->setStyleSheet("font-size: 14px; color: #5A6C7D;");
    m_nameEdit = new QLineEdit();
    m_nameEdit->setPlaceholderText("请输入列名");
    m_nameEdit->setFixedHeight(36);
    m_nameEdit->setStyleSheet(R"(
        QLineEdit {
            border: 1px solid #DCE0E5;
            border-radius: 4px;
            padding: 0 12px;
            font-size: 14px;
            background-color: white;
            color: #333333;
        }
        QLineEdit:focus {
            border-color: #409EFF;
            outline: none;
        }
    )");
    nameRow->addWidget(nameLabel);
    nameRow->addWidget(m_nameEdit);
    basicLayout->addLayout(nameRow);

    // 数据类型 + 长度
    auto *typeLenRow = new QHBoxLayout;
    typeLenRow->setSpacing(12);

    auto *typeLabel = new QLabel("数据类型");
    typeLabel->setFixedWidth(70);
    typeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    typeLabel->setStyleSheet("font-size: 14px; color: #5A6C7D;");

    m_typeCombo = new QComboBox();
    m_typeCombo->addItems({"INT", "BIGINT", "FLOAT", "DOUBLE", "DECIMAL",
                           "VARCHAR", "CHAR", "TEXT", "DATE", "DATETIME", "TIME", "BOOLEAN"});
    m_typeCombo->setCurrentText("VARCHAR");
    m_typeCombo->setFixedHeight(36);
    m_typeCombo->setStyleSheet(R"(
        QComboBox {
            border: 1px solid #DCE0E5;
            border-radius: 4px;
            padding: 0 12px;
            font-size: 14px;
            background-color: white;
            min-width: 140px;
            color: #333333;
        }
        QComboBox:focus {
            border-color: #409EFF;
            outline: none;
        }
        QComboBox::drop-down {
            width: 24px;
            border-left: none;
        }
    )");

    auto *lenLabel = new QLabel("长度");
    lenLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lenLabel->setStyleSheet("font-size: 14px; color: #5A6C7D;");

    m_lengthEdit = new QLineEdit();
    m_lengthEdit->setText("255");
    m_lengthEdit->setFixedSize(80, 36);
    m_lengthEdit->setStyleSheet(R"(
        QLineEdit {
            border: 1px solid #DCE0E5;
            border-radius: 4px;
            padding: 0 8px;
            font-size: 14px;
            background-color: white;
            text-align: center;
            color: #333333;
        }
        QLineEdit:focus {
            border-color: #409EFF;
            outline: none;
        }
    )");

    typeLenRow->addWidget(typeLabel);
    typeLenRow->addWidget(m_typeCombo);
    typeLenRow->addWidget(lenLabel);
    typeLenRow->addWidget(m_lengthEdit);
    typeLenRow->addStretch();
    basicLayout->addLayout(typeLenRow);

    formLayout->addWidget(basicGroup);

    // --------------------------
    // 模块2：约束选项
    // --------------------------
    auto *constraintGroup = new QGroupBox("约束选项");
    constraintGroup->setStyleSheet(R"(
        QGroupBox {
            font-size: 14px;
            font-weight: 600;
            color: #34495E;
            border: 1px solid #E8ECF0;
            border-radius: 6px;
            margin-top: 6px;
            padding-top: 12px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 12px;
            padding: 0 8px;
            background-color: #F8FAFC;
        }
    )");

    auto *constraintLayout = new QHBoxLayout(constraintGroup);
    constraintLayout->setSpacing(24);
    constraintLayout->setContentsMargins(16, 8, 16, 16);

    m_nullCheck = new QCheckBox("允许空值");
    m_nullCheck->setChecked(true);
    m_nullCheck->setStyleSheet(R"(
        QCheckBox {
            font-size: 14px;
            color: #4A5568;
            spacing: 8px;
        }
        QCheckBox::indicator {
            width: 18px;
            height: 18px;
            border: 2px solid #DCE0E5;
            border-radius: 2px;
            background-color: white;
        }
        QCheckBox::indicator:hover {
            border-color: #409EFF;
        }
        QCheckBox::indicator:checked {
            background-color: #409EFF;
            border-color: #409EFF;
        }
    )");

    m_pkCheck = new QCheckBox("主键");
    m_pkCheck->setStyleSheet(R"(
        QCheckBox {
            font-size: 14px;
            color: #4A5568;
            spacing: 8px;
        }
        QCheckBox::indicator {
            width: 18px;
            height: 18px;
            border: 2px solid #DCE0E5;
            border-radius: 2px;
            background-color: white;
        }
        QCheckBox::indicator:hover {
            border-color: #409EFF;
        }
        QCheckBox::indicator:checked {
            background-color: #409EFF;
            border-color: #409EFF;
        }
    )");

    m_uniqueCheck = new QCheckBox("唯一");
    m_uniqueCheck->setStyleSheet(R"(
        QCheckBox {
            font-size: 14px;
            color: #4A5568;
            spacing: 8px;
        }
        QCheckBox::indicator {
            width: 18px;
            height: 18px;
            border: 2px solid #DCE0E5;
            border-radius: 2px;
            background-color: white;
        }
        QCheckBox::indicator:hover {
            border-color: #409EFF;
        }
        QCheckBox::indicator:checked {
            background-color: #409EFF;
            border-color: #409EFF;
        }
    )");

    constraintLayout->addWidget(m_nullCheck);
    constraintLayout->addWidget(m_pkCheck);
    constraintLayout->addWidget(m_uniqueCheck);
    constraintLayout->addStretch();

    formLayout->addWidget(constraintGroup);

    // --------------------------
    // 模块3：外键配置
    // --------------------------
    auto *fkGroup = new QGroupBox("外键配置");
    fkGroup->setStyleSheet(R"(
        QGroupBox {
            font-size: 14px;
            font-weight: 600;
            color: #34495E;
            border: 1px solid #E8ECF0;
            border-radius: 6px;
            margin-top: 6px;
            padding-top: 12px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 12px;
            padding: 0 8px;
            background-color: #F8FAFC;
        }
    )");

    auto *fkLayout = new QVBoxLayout(fkGroup);
    fkLayout->setSpacing(14);
    fkLayout->setContentsMargins(16, 8, 16, 16);

    // 引用表
    auto *refTableRow = new QHBoxLayout;
    refTableRow->setSpacing(12);
    auto *refTableLabel = new QLabel("引用表");
    refTableLabel->setFixedWidth(70);
    refTableLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    refTableLabel->setStyleSheet("font-size: 14px; color: #5A6C7D;");
    m_referenceTableCombo = new QComboBox();
    m_referenceTableCombo->setEditable(true);
    m_referenceTableCombo->setInsertPolicy(QComboBox::NoInsert);
    m_referenceTableCombo->setView(new QListView(this));
    m_referenceTableCombo->setMaxVisibleItems(8);
    m_referenceTableCombo->setPlaceholderText("选择引用表");
    m_referenceTableCombo->setFixedHeight(36);
    m_referenceTableCombo->setStyleSheet(R"(
        QComboBox {
            border: 1px solid #DCE0E5;
            border-radius: 4px;
            padding: 0 12px;
            font-size: 14px;
            background-color: white;
            color: #333333;
        }
        QComboBox:focus {
            border-color: #409EFF;
            outline: none;
        }
    )");
    populateReferenceTables();
    refTableRow->addWidget(refTableLabel);
    refTableRow->addWidget(m_referenceTableCombo);
    fkLayout->addLayout(refTableRow);

    // 引用字段
    auto *refColumnRow = new QHBoxLayout;
    refColumnRow->setSpacing(12);
    auto *refColumnLabel = new QLabel("引用字段");
    refColumnLabel->setFixedWidth(70);
    refColumnLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    refColumnLabel->setStyleSheet("font-size: 14px; color: #5A6C7D;");
    m_referenceColumnCombo = new QComboBox();
    m_referenceColumnCombo->setEditable(true);
    m_referenceColumnCombo->setInsertPolicy(QComboBox::NoInsert);
    m_referenceColumnCombo->setView(new QListView(this));
    m_referenceColumnCombo->setMaxVisibleItems(8);
    m_referenceColumnCombo->setPlaceholderText("选择引用字段");
    m_referenceColumnCombo->setFixedHeight(36);
    m_referenceColumnCombo->setStyleSheet(R"(
        QComboBox {
            border: 1px solid #DCE0E5;
            border-radius: 4px;
            padding: 0 12px;
            font-size: 14px;
            background-color: white;
            color: #333333;
        }
        QComboBox:focus {
            border-color: #409EFF;
            outline: none;
        }
    )");
    refColumnRow->addWidget(refColumnLabel);
    refColumnRow->addWidget(m_referenceColumnCombo);
    fkLayout->addLayout(refColumnRow);

    formLayout->addWidget(fkGroup);

    // --------------------------
    // 模块4：其他配置
    // --------------------------
    auto *otherGroup = new QGroupBox("其他配置");
    otherGroup->setStyleSheet(R"(
        QGroupBox {
            font-size: 14px;
            font-weight: 600;
            color: #34495E;
            border: 1px solid #E8ECF0;
            border-radius: 6px;
            margin-top: 6px;
            padding-top: 12px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 12px;
            padding: 0 8px;
            background-color: #F8FAFC;
        }
    )");

    auto *otherLayout = new QVBoxLayout(otherGroup);
    otherLayout->setSpacing(14);
    otherLayout->setContentsMargins(16, 8, 16, 16);

    // CHECK约束
    auto *checkRow = new QHBoxLayout;
    checkRow->setSpacing(12);
    auto *checkLabel = new QLabel("CHECK约束");
    checkLabel->setFixedWidth(70);
    checkLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    checkLabel->setStyleSheet("font-size: 14px; color: #5A6C7D;");
    m_checkCheck = new QCheckBox();
    m_checkCheck->setStyleSheet(R"(
        QCheckBox::indicator {
            width: 18px;
            height: 18px;
            border: 2px solid #DCE0E5;
            border-radius: 2px;
            background-color: white;
        }
        QCheckBox::indicator:hover {
            border-color: #409EFF;
        }
        QCheckBox::indicator:checked {
            background-color: #409EFF;
            border-color: #409EFF;
        }
    )");
    m_checkEdit = new QLineEdit();
    m_checkEdit->setPlaceholderText("例如：age > 0");
    m_checkEdit->setEnabled(false);
    m_checkEdit->setFixedHeight(36);
    m_checkEdit->setStyleSheet(R"(
        QLineEdit {
            border: 1px solid #DCE0E5;
            border-radius: 4px;
            padding: 0 12px;
            font-size: 14px;
            background-color: #F8FAFC;
            color: #333333;
        }
        QLineEdit:focus {
            border-color: #409EFF;
            outline: none;
        }
        QLineEdit:enabled {
            background-color: white;
        }
    )");
    checkRow->addWidget(checkLabel);
    checkRow->addWidget(m_checkCheck);
    checkRow->addWidget(m_checkEdit);
    otherLayout->addLayout(checkRow);

    // 默认值
    auto *defaultRow = new QHBoxLayout;
    defaultRow->setSpacing(12);
    auto *defaultLabel = new QLabel("默认值");
    defaultLabel->setFixedWidth(70);
    defaultLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    defaultLabel->setStyleSheet("font-size: 14px; color: #5A6C7D;");
    m_defaultEdit = new QLineEdit();
    m_defaultEdit->setPlaceholderText("输入默认值");
    m_defaultEdit->setFixedHeight(36);
    m_defaultEdit->setStyleSheet(R"(
        QLineEdit {
            border: 1px solid #DCE0E5;
            border-radius: 4px;
            padding: 0 12px;
            font-size: 14px;
            background-color: white;
            color: #333333;
        }
        QLineEdit:focus {
            border-color: #409EFF;
            outline: none;
        }
    )");
    defaultRow->addWidget(defaultLabel);
    defaultRow->addWidget(m_defaultEdit);
    otherLayout->addLayout(defaultRow);

    formLayout->addWidget(otherGroup);

    mainLayout->addLayout(formLayout);

    // ========== 底部按钮区域 ==========
    auto *btnLayout = new QHBoxLayout;
    btnLayout->setSpacing(12);
    btnLayout->setContentsMargins(0, 16, 0, 0);

    auto *cancelBtn = new QPushButton("取消");
    cancelBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #F5F7FA;
            color: #606266;
            border: 1px solid #DCDFE6;
            border-radius: 4px;
            padding: 8px 24px;
            font-size: 14px;
            min-height: 36px;
        }
        QPushButton:hover {
            background-color: #E8ECF0;
        }
    )");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    auto *okBtn = new QPushButton("确定");
    okBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #409EFF;
            color: white;
            border: none;
            border-radius: 4px;
            padding: 8px 24px;
            font-size: 14px;
            min-height: 36px;
        }
        QPushButton:hover {
            background-color: #66B1FF;
        }
        QPushButton:pressed {
            background-color: #3A8EE6;
        }
    )");
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);

    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(okBtn);
    mainLayout->addLayout(btnLayout);

    // ========== 信号连接 ==========
    connect(m_referenceTableCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AddColumnDialog::onReferenceTableChanged);

    connect(m_checkCheck, &QCheckBox::checkStateChanged, this, [this](int state) {
        m_checkEdit->setEnabled(state == Qt::Checked);
        if (state != Qt::Checked) m_checkEdit->clear();
    });
}







ColumnConfig AddColumnDialog::getConfig() const



{



    ColumnConfig cfg;



    cfg.name = m_nameEdit->text().trimmed();



    cfg.type = m_typeCombo->currentText();



    QString lengthText = m_lengthEdit->text().trimmed();
    cfg.length = lengthText.isEmpty() ? 0 : lengthText.toInt();



    cfg.allowNull = m_nullCheck->isChecked();



    cfg.primaryKey = m_pkCheck->isChecked();



    cfg.unique = m_uniqueCheck->isChecked();



    cfg.referencedTable = m_referenceTableCombo->currentText().trimmed();



    QString referencedColumn = m_referenceColumnCombo->currentText().trimmed();



    if (!referencedColumn.isEmpty()) {



        cfg.referencedColumns = { referencedColumn };



    }









    cfg.checkConstraint = m_checkCheck->isChecked() ? m_checkEdit->text().trimmed() : QString();



    cfg.defaultValue = m_defaultEdit->text().trimmed();



    return cfg;



}





void AddColumnDialog::populateReferenceTables()
{
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
            m_referenceTableCombo->addItem(tableName);
        }
    }
}

void AddColumnDialog::onReferenceTableChanged(int index)
{
    Q_UNUSED(index)
    QString tableName = m_referenceTableCombo->currentText().trimmed();
    m_referenceColumnCombo->clear();
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
        m_referenceColumnCombo->addItem(colName);
    }
}