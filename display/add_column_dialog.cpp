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



    setFixedSize(420, 520);



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



            width: 10px;



            height: 6px;



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
    mainLayout->setContentsMargins(20, 20, 20, 20);

    // 标题行
    auto *titleLabel = new QLabel("添加列");
    titleLabel->setStyleSheet("font-size: 16px; font-weight: 600; color: #333;");
    mainLayout->addWidget(titleLabel);

    // 分隔线
    auto *line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("color: #E0E0E0;");
    mainLayout->addWidget(line);

    // 表单区域 - 使用简单的垂直布局
    auto *formLayout = new QVBoxLayout;
    formLayout->setSpacing(14);

    // === 列名 ===
    auto *nameLayout = new QHBoxLayout;
    nameLayout->setSpacing(10);
    auto *nameLabel = new QLabel("列名：");
    nameLabel->setFixedWidth(70);
    nameLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_nameEdit = new QLineEdit();
    m_nameEdit->setPlaceholderText("请输入列名");
    m_nameEdit->setFixedHeight(32);
    nameLayout->addWidget(nameLabel);
    nameLayout->addWidget(m_nameEdit);
    formLayout->addLayout(nameLayout);

    // === 数据类型 ===
    auto *typeLayout = new QHBoxLayout;
    typeLayout->setSpacing(10);
    auto *typeLabel = new QLabel("数据类型：");
    typeLabel->setFixedWidth(70);
    typeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_typeCombo = new QComboBox();
    m_typeCombo->addItems({
        "INT", "BIGINT", "FLOAT", "DOUBLE", "DECIMAL",
        "NUMBER", "VARCHAR", "CHAR", "TEXT", "DATE",
        "DATETIME", "TIME", "BLOB", "BOOLEAN"
    });
    m_typeCombo->setCurrentText("VARCHAR");
    m_typeCombo->setFixedHeight(32);
    typeLayout->addWidget(typeLabel);
    typeLayout->addWidget(m_typeCombo);
    formLayout->addLayout(typeLayout);

    // === 长度/精度 ===
    auto *lenLayout = new QHBoxLayout;
    lenLayout->setSpacing(10);
    auto *lenLabel = new QLabel("长度：");
    lenLabel->setFixedWidth(70);
    lenLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_lengthSpin = new QSpinBox();
    m_lengthSpin->setRange(0, 99999);
    m_lengthSpin->setValue(255);
    m_lengthSpin->setFixedSize(100, 32);
    lenLayout->addWidget(lenLabel);
    lenLayout->addWidget(m_lengthSpin);
    lenLayout->addStretch();
    formLayout->addLayout(lenLayout);

    // === 约束选项 ===
    auto *constraintLayout = new QHBoxLayout;
    constraintLayout->setSpacing(20);
    constraintLayout->setContentsMargins(70, 0, 0, 0);

    m_nullCheck = new QCheckBox("允许空值");
    m_nullCheck->setChecked(true);
    constraintLayout->addWidget(m_nullCheck);

    m_pkCheck = new QCheckBox("主键");
    constraintLayout->addWidget(m_pkCheck);

    m_uniqueCheck = new QCheckBox("唯一");
    constraintLayout->addWidget(m_uniqueCheck);

    constraintLayout->addStretch();
    formLayout->addLayout(constraintLayout);

    // === 外键配置分隔线 ===
    auto *fkLine = new QFrame();
    fkLine->setFrameShape(QFrame::HLine);
    fkLine->setStyleSheet("color: #F0F0F0;");
    formLayout->addWidget(fkLine);

    // === 引用表 ===
    auto *refTableLayout = new QHBoxLayout;
    refTableLayout->setSpacing(10);
    auto *refTableLabel = new QLabel("引用表：");
    refTableLabel->setFixedWidth(70);
    refTableLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_referenceTableCombo = new QComboBox();
    m_referenceTableCombo->setEditable(true);
    m_referenceTableCombo->setInsertPolicy(QComboBox::NoInsert);
    m_referenceTableCombo->setView(new QListView(this));
    m_referenceTableCombo->setMaxVisibleItems(8);
    m_referenceTableCombo->setPlaceholderText("选择引用表（可选）");
    m_referenceTableCombo->setFixedHeight(32);
    populateReferenceTables();
    refTableLayout->addWidget(refTableLabel);
    refTableLayout->addWidget(m_referenceTableCombo);
    formLayout->addLayout(refTableLayout);

    // === 引用字段 ===
    auto *refColumnLayout = new QHBoxLayout;
    refColumnLayout->setSpacing(10);
    auto *refColumnLabel = new QLabel("引用字段：");
    refColumnLabel->setFixedWidth(70);
    refColumnLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_referenceColumnCombo = new QComboBox();
    m_referenceColumnCombo->setEditable(true);
    m_referenceColumnCombo->setInsertPolicy(QComboBox::NoInsert);
    m_referenceColumnCombo->setView(new QListView(this));
    m_referenceColumnCombo->setMaxVisibleItems(8);
    m_referenceColumnCombo->setPlaceholderText("选择引用字段");
    m_referenceColumnCombo->setFixedHeight(32);
    refColumnLayout->addWidget(refColumnLabel);
    refColumnLayout->addWidget(m_referenceColumnCombo);
    formLayout->addLayout(refColumnLayout);

    // === ON DELETE / ON UPDATE ===
    auto *fkActionLayout = new QHBoxLayout;
    fkActionLayout->setSpacing(10);
    fkActionLayout->setContentsMargins(70, 0, 0, 0);

    auto *deleteLabel = new QLabel("ON DELETE：");
    m_deleteActionCombo = new QComboBox();
    m_deleteActionCombo->addItems({"NO ACTION", "RESTRICT", "CASCADE", "SET NULL", "SET DEFAULT"});
    m_deleteActionCombo->setCurrentText("NO ACTION");
    m_deleteActionCombo->setFixedHeight(32);
    m_deleteActionCombo->setFixedWidth(120);

    auto *updateLabel = new QLabel("ON UPDATE：");
    m_updateActionCombo = new QComboBox();
    m_updateActionCombo->addItems({"NO ACTION", "RESTRICT", "CASCADE", "SET NULL", "SET DEFAULT"});
    m_updateActionCombo->setCurrentText("NO ACTION");
    m_updateActionCombo->setFixedHeight(32);
    m_updateActionCombo->setFixedWidth(120);

    fkActionLayout->addWidget(deleteLabel);
    fkActionLayout->addWidget(m_deleteActionCombo);
    fkActionLayout->addWidget(updateLabel);
    fkActionLayout->addWidget(m_updateActionCombo);
    fkActionLayout->addStretch();
    formLayout->addLayout(fkActionLayout);

    // 连接引用表变化信号
    connect(m_referenceTableCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AddColumnDialog::onReferenceTableChanged);

    // === CHECK约束分隔线 ===
    auto *checkLine = new QFrame();
    checkLine->setFrameShape(QFrame::HLine);
    checkLine->setStyleSheet("color: #F0F0F0;");
    formLayout->addWidget(checkLine);

    // === CHECK 约束 ===
    auto *checkLayout = new QHBoxLayout;
    checkLayout->setSpacing(10);
    checkLayout->setContentsMargins(70, 0, 0, 0);

    m_checkCheck = new QCheckBox("CHECK约束");
    m_checkCheck->setChecked(false);
    checkLayout->addWidget(m_checkCheck);

    m_checkEdit = new QLineEdit();
    m_checkEdit->setPlaceholderText("例如：age > 0");
    m_checkEdit->setEnabled(false);
    m_checkEdit->setFixedHeight(32);
    checkLayout->addWidget(m_checkEdit);

    connect(m_checkCheck, &QCheckBox::checkStateChanged, this, [this](int state) {
        m_checkEdit->setEnabled(state == Qt::Checked);
        if (state != Qt::Checked) m_checkEdit->clear();
    });

    formLayout->addLayout(checkLayout);

    // === 默认值 ===
    auto *defaultLayout = new QHBoxLayout;
    defaultLayout->setSpacing(10);
    auto *defaultLabel = new QLabel("默认值：");
    defaultLabel->setFixedWidth(70);
    defaultLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_defaultEdit = new QLineEdit();
    m_defaultEdit->setPlaceholderText("输入默认值（可选）");
    m_defaultEdit->setFixedHeight(32);
    defaultLayout->addWidget(defaultLabel);
    defaultLayout->addWidget(m_defaultEdit);
    formLayout->addLayout(defaultLayout);

    mainLayout->addLayout(formLayout);

    // === 底部按钮 ===
    auto *btnLayout = new QHBoxLayout;
    btnLayout->setSpacing(12);
    btnLayout->setContentsMargins(0, 10, 0, 0);

    auto *cancelBtn = new QPushButton("取消");
    cancelBtn->setStyleSheet("QPushButton { background-color: #F0F0F0; color: #555; border: 1px solid #DDD; border-radius: 4px; padding: 8px 20px; font-size: 13px; min-height: 32px; } "
                            "QPushButton:hover { background-color: #E5E5E5; }");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    auto *okBtn = new QPushButton("确定");
    okBtn->setStyleSheet("QPushButton { background-color: #555; color: white; border: 1px solid #555; border-radius: 4px; padding: 8px 20px; font-size: 13px; min-height: 32px; } "
                         "QPushButton:hover { background-color: #333; }");
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



    cfg.referencedTable = m_referenceTableCombo->currentText().trimmed();



    QString referencedColumn = m_referenceColumnCombo->currentText().trimmed();



    if (!referencedColumn.isEmpty()) {



        cfg.referencedColumns = { referencedColumn };



    }



    tabledef::tryParseForeignKeyAction(m_deleteActionCombo->currentText().trimmed(), &cfg.onDeleteAction);



    tabledef::tryParseForeignKeyAction(m_updateActionCombo->currentText().trimmed(), &cfg.onUpdateAction);



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