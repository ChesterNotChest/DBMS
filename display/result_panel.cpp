#include "result_panel.h"
#include <QMessageBox>
#include <QHeaderView>
#include <QCheckBox>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QComboBox>

ResultPanel::ResultPanel(QWidget* parent) : QWidget(parent) {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(8, 0, 8, 0);
    rootLayout->setSpacing(0);

    // 顶部状态栏
    m_topBar = new QWidget(this);
    auto* topLayout = new QHBoxLayout(m_topBar);
    topLayout->setContentsMargins(0, 4, 0, 4);
    topLayout->setSpacing(8);

    m_statsLabel = new QLabel("Ready", this);
    m_statsLabel->setStyleSheet("QLabel { color:#666; font-family:Microsoft YaHei; font-size:11px; background:transparent; }");
    topLayout->addWidget(m_statsLabel);

    topLayout->addStretch();

    auto* addRowBtn = new QPushButton("+ 行", this);
    auto* delRowBtn = new QPushButton("- 行", this);
    auto* addColBtn = new QPushButton("+ 列", this);
    auto* delColBtn = new QPushButton("- 列", this);
    auto* saveBtn = new QPushButton("💾 保存", this);
    auto* undoBtn = new QPushButton("↩ 撤销", this);

    for (QPushButton* btn : {addRowBtn, delRowBtn, addColBtn, delColBtn, saveBtn, undoBtn}) {
        btn->setStyleSheet("QPushButton { background:#F0F0F0; color:#333; border:1px solid #DDD; border-radius:3px; padding:4px 12px; font-size:12px; } "
                           "QPushButton:hover { background:#E0E0E0; }");
        topLayout->addWidget(btn);
    }
    rootLayout->addWidget(m_topBar);

    // 结果TabWidget
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setStyleSheet("QTabWidget::pane { border:1px solid #CCC; border-top:none; } "
                               "QTabBar::tab { background:#F0F0F0; padding:6px 16px; border:1px solid #CCC; border-bottom:none; margin-right:2px; font-size:12px; color:#666; } "
                               "QTabBar::tab:selected { background:#E3F2FD; color:#1565C0; font-weight:bold; border-bottom:2px solid #E3F2FD; }");
    rootLayout->addWidget(m_tabWidget, 1);

    // 结果页面 + 表格
    auto* resultWidget = new QWidget(m_tabWidget);
    auto* resultLayout = new QVBoxLayout(resultWidget);
    resultLayout->setContentsMargins(0, 0, 0, 0);
    resultLayout->setSpacing(0);

    m_table = new QTableWidget(this);
    m_table->setStyleSheet(
        // 基础背景：浅蓝色
        "QTableWidget { background:#E3F2FD; border:none; gridline-color:#BBDEFB; }"
        // 表头：浅蓝背景 + 加粗
        "QHeaderView::section { background:#BBDEFB; font-weight:bold; color:#0D47A1; "
        "padding:6px 10px; border:none; border-bottom:2px solid #90CAF9; "
        "border-right:1px solid #BBDEFB; font-family:'Consolas','Courier New'; font-size:13px; "
        "min-width:50px; }"
        "QHeaderView::section:first { color:#1565C0; font-weight:normal; font-size:11px; "
        "border-right:1px solid #90CAF9; border-bottom:2px solid #90CAF9; "
        "min-width:40px; max-width:50px; }"
        // 内容行：交替背景色
        "QTableWidget::item { padding:6px 10px; border:none; color:#1A237E; "
        "font-family:'Consolas','Courier New'; font-size:13px; "
        "border-bottom:1px solid #E3F2FD; border-right:1px solid #E3F2FD; "
        "text-align:center; }"
        // 选中高亮：蓝色
        "QTableWidget::item:selected { background:#90CAF9; color:#0D47A1; font-weight:600; }"
        // 悬停：浅蓝
        "QTableWidget::item:hover { background:#BBDEFB; }"
        // 交替行：浅绿蓝/浅蓝
"QTableWidget::item:alternate { background:#E8F5E9; }"
        // 编辑器样式 - 确保编辑器能正常工作！
"QTableWidget QLineEdit { background:#FFFFFF; color:#0D47A1; border:2px solid #2196F3; font-weight:bold; font-size:15px; padding:6px; selection-background-color:#90CAF9; min-height:30px; }"
        // 角落按钮
        "QTableCornerButton::section { background:#BBDEFB; border:none; }"
        // 滚动条：细窄
        "QScrollBar:vertical { background:#E3F2FD; width:10px; margin:0px; }"
        "QScrollBar::handle:vertical { background:#90CAF9; border-radius:5px; min-height:30px; }"
        "QScrollBar::handle:vertical:hover { background:#64B5F6; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0px; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }"
        "QScrollBar:horizontal { background:#E3F2FD; height:10px; margin:0px; }"
        "QScrollBar::handle:horizontal { background:#90CAF9; border-radius:5px; min-width:30px; }"
        "QScrollBar::handle:horizontal:hover { background:#64B5F6; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0px; }"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background:transparent; }"
    );

    m_table->setSelectionMode(QAbstractItemView::ContiguousSelection);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setDefaultSectionSize(30);
    m_table->verticalHeader()->setMinimumSectionSize(30);
    m_table->verticalHeader()->setMaximumSectionSize(60);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->verticalHeader()->setHighlightSections(false);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);

    resultLayout->addWidget(m_table, 1);

    m_emptyLabel = new QLabel(resultWidget);
    m_emptyLabel->setFont(QFont("Microsoft YaHei", 12));
    m_emptyLabel->setStyleSheet("QLabel { color:#90CAF9; background:#E3F2FD; border:none; }");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setText("No data");
    m_emptyLabel->hide();
    resultLayout->addWidget(m_emptyLabel, 1);

    m_logFooter = new QWidget(resultWidget);
    m_logFooter->setFixedHeight(0);
    m_logFooter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* logLayout = new QVBoxLayout(m_logFooter);
    logLayout->setContentsMargins(0, 0, 0, 0);
    logLayout->setSpacing(0);

    m_logHeader = new QWidget(m_logFooter);
    auto* logHeaderLayout = new QHBoxLayout(m_logHeader);
    logHeaderLayout->setContentsMargins(4, 2, 4, 2);
    logHeaderLayout->setSpacing(4);
    m_logHeader->setStyleSheet("QWidget { background:#F0F0F0; border-top:1px solid #DDD; }");

    m_logCountLabel = new QLabel("0 条", m_logFooter);
    m_logCountLabel->setStyleSheet("QLabel { color:#666; font-size:11px; }");
    logHeaderLayout->addWidget(m_logCountLabel);
    logHeaderLayout->addStretch();
    m_logToggleBtn = new QPushButton("▼ 日志", m_logFooter);
    m_logToggleBtn->setStyleSheet("QPushButton { background:transparent; color:#666; border:none; font-size:11px; padding:2px 4px; } "
                                   "QPushButton:hover { color:#333; }");
    logHeaderLayout->addWidget(m_logToggleBtn);
    logLayout->addWidget(m_logHeader);

    m_logText = new QTextEdit(m_logFooter);
    m_logText->setReadOnly(true);
    m_logText->setMaximumHeight(150);
    m_logText->setStyleSheet("QTextEdit { background:#FFFFFF; color:#333333; border:none; border-top:1px solid #E0E0E0; font-family:Consolas,Courier New; font-size:13px; padding:6px; }");
    logLayout->addWidget(m_logText);

    resultLayout->addWidget(m_logFooter);

    m_tabWidget->addTab(resultWidget, "Result");

    connect(addRowBtn, &QPushButton::clicked, this, &ResultPanel::onAddRow);
    connect(delRowBtn, &QPushButton::clicked, this, &ResultPanel::onDeleteRow);
    connect(addColBtn, &QPushButton::clicked, this, &ResultPanel::onAddColumn);
    connect(delColBtn, &QPushButton::clicked, this, &ResultPanel::onDeleteColumn);
    connect(saveBtn, &QPushButton::clicked, this, &ResultPanel::onSave);
    connect(undoBtn, &QPushButton::clicked, this, &ResultPanel::undoLastChange);
    connect(m_logToggleBtn, &QPushButton::clicked, this, &ResultPanel::onToggleLogFooter);
    connect(m_table, &QTableWidget::itemChanged, this, &ResultPanel::onCellChanged);
}

ResultPanel::~ResultPanel() {}

void ResultPanel::clear() {
    m_table->setRowCount(0);
    m_table->setColumnCount(0);
    m_originalRows.clear();
    m_currentRows.clear();
    m_newRowIds.clear();
    m_deletedRows.clear();
    m_dirtyRowIds.clear();
    m_addedColumns.clear();
    m_deletedColumnNames.clear();
    m_deletedColumnsWithIndex.clear();
    m_originalColumns.clear();
    m_lastColumns.clear();
    m_logText->clear();
    m_logCount = 0;
    m_statsLabel->setText("Cleared");
    m_emptyLabel->show();
    m_table->hide();
}

void ResultPanel::showTable(const QStringList& cols, const QList<QStringList>& rows) {
    m_table->show();
    m_emptyLabel->hide();
    m_table->setRowCount(0);
    m_table->setColumnCount(cols.size());
    m_table->setHorizontalHeaderLabels(cols);

    m_originalColumns = cols;
    m_lastColumns = cols;
    m_addedColumns.clear();
    m_deletedColumnNames.clear();
    m_deletedColumnsWithIndex.clear();

    m_originalRows.clear();
    m_currentRows.clear();
    m_newRowIds.clear();
    m_deletedRows.clear();
    m_dirtyRowIds.clear();

    for (int r = 0; r < rows.size(); ++r) {
        m_table->insertRow(r);
        const auto& rowData = rows[r];
        for (int c = 0; c < qMin(cols.size(), rowData.size()); ++c) {
            auto* item = new QTableWidgetItem(rowData[c]);
            item->setTextAlignment(Qt::AlignCenter);
            m_table->setItem(r, c, item);
        }
        m_originalRows[r] = rowData;
        m_currentRows[r] = rowData;
    }

    m_statsLabel->setText(QString("Loaded %1 rows, %2 cols").arg(rows.size()).arg(cols.size()));
    m_startTime = QTime::currentTime();
}

void ResultPanel::appendLog(const QString& msg, const QString& color) {
    QString ts = QTime::currentTime().toString("HH:mm:ss");
    QString colored = color.isEmpty() ? msg : QString("<span style='color:%1'>%2</span>").arg(color, msg);
    m_logText->append(QString("<span style='color:#999'>[%1]</span> %2").arg(ts, colored));
    m_logCount++;
    m_logCountLabel->setText(QString("%1 条").arg(m_logCount));
    expandLogFooter();
}

void ResultPanel::expandLogFooter() {
    if (!m_logExpanded) onToggleLogFooter();
}

void ResultPanel::collapseLogFooter() {
    if (m_logExpanded) onToggleLogFooter();
}

void ResultPanel::onToggleLogFooter() {
    m_logExpanded = !m_logExpanded;
    m_logToggleBtn->setText(m_logExpanded ? "▲ 收起" : "▼ 日志");
    m_logFooter->setFixedHeight(m_logExpanded ? 160 : 0);
}

void ResultPanel::onAddRow() {
    int r = m_table->rowCount();
    m_table->insertRow(r);
    for (int c = 0; c < m_table->columnCount(); ++c) {
        auto* item = new QTableWidgetItem("");
        item->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(r, c, item);
    }
    m_newRowIds.insert(r);
    QStringList emptyRow(m_table->columnCount(), "");
    m_currentRows[r] = emptyRow;
    appendLog(QString("➕ 新增行 #%1").arg(r), "#4CAF50");
    int totalChanges = m_deletedRows.size() + m_dirtyRowIds.size() + m_newRowIds.size();
    m_statsLabel->setText(QString("<span style='color:#4CAF50'>行已新增，%1 处待保存</span>").arg(totalChanges));
}

// ── 删除选中行 ──
void ResultPanel::onDeleteRow() {
    QList<QTableWidgetSelectionRange> ranges = m_table->selectedRanges();
    if (ranges.isEmpty()) {
        m_statsLabel->setText("<span style='color:#FF8F00'>请先选中要删除的行</span>");
        return;
    }

    // 获取要删除的行号（从表格获取，确保正确）
    QList<int> rowsToDelete;
    for (const QTableWidgetSelectionRange &range : ranges) {
        for (int r = range.topRow(); r <= range.bottomRow(); ++r)
            rowsToDelete.append(r);
    }
    std::sort(rowsToDelete.begin(), rowsToDelete.end(), std::greater<int>());

    int realDeleted = 0;
    for (int r : rowsToDelete) {
        // 从原始数据中获取PK（删除前获取）
        QString pk = m_currentRows.value(r, QStringList()).value(0, "");
        
        // 如果是新增的行，直接删除
        if (m_newRowIds.contains(r)) {
            appendLog(QString("➖ 移除新增行 #%1").arg(r), "#FF8A65");
            m_newRowIds.remove(r);
        } else {
            // 如果是原有行，记录到待删除集合
            if (!pk.isEmpty()) {
                m_deletedRows[pk] = m_originalRows.value(r, m_currentRows.value(r));
                m_originalRows.remove(r);
                realDeleted++;
                appendLog(QString("➖ 标记删除行 #%1（PK=%2）").arg(r).arg(pk), "#FF8A65");
            }
        }
        
        // 移除当前数据和脏标记
        m_currentRows.remove(r);
        m_dirtyRowIds.remove(r);
        
        // 从表格中删除行
        m_table->removeRow(r);
    }

    // 重新建立映射：从表格读取当前数据
    m_originalRows.clear();
    m_currentRows.clear();
    m_newRowIds.clear();
    
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QStringList rowData;
        for (int c = 0; c < m_table->columnCount(); ++c) {
            QTableWidgetItem* item = m_table->item(r, c);
            rowData.append(item ? item->text() : "");
        }
        m_currentRows[r] = rowData;
        // 如果不在deletedRows中，就是原始行
        QString pk = rowData.value(0, "");
        if (!pk.isEmpty() && !m_deletedRows.contains(pk)) {
            m_originalRows[r] = rowData;
        }
    }

    int totalChanges = m_deletedRows.size() + m_dirtyRowIds.size() + m_newRowIds.size();
    m_statsLabel->setText(QString("<span style='color:#E65100'>已删除 %1 行，%2 处待保存变更</span>").arg(realDeleted).arg(totalChanges));
}

void ResultPanel::onCellChanged(QTableWidgetItem* item) {
    if (!item) return;
    int r = item->row();
    int c = item->column();
    QString newValue = item->text();
    if (m_currentRows.contains(r)) {
        m_currentRows[r][c] = newValue;
        if (!m_newRowIds.contains(r)) {
            QString oldVal = m_originalRows.value(r, QStringList()).value(c, "");
            if (oldVal != newValue) {
                m_dirtyRowIds.insert(r);
            } else {
                m_dirtyRowIds.remove(r);
            }
        }
    }
    int totalChanges = m_deletedRows.size() + m_dirtyRowIds.size() + m_newRowIds.size();
    if (totalChanges > 0)
        m_statsLabel->setText(QString("<span style='color:#E65100'>%1 处待保存变更</span>").arg(totalChanges));
    else
        m_statsLabel->setText(QString("No changes"));
}

QStringList ResultPanel::currentRowData(int r) const {
    QStringList data;
    for (int c = 0; c < m_table->columnCount(); ++c) {
        auto* item = m_table->item(r, c);
        data.append(item ? item->text() : "");
    }
    return data;
}

bool ResultPanel::isNewRow(int r) const { return m_newRowIds.contains(r); }
bool ResultPanel::isRowDirty(int r) const { return m_dirtyRowIds.contains(r); }

void ResultPanel::onAddColumn() {
    QDialog dlg(this);
    dlg.setWindowTitle("➕ 新增列");
    dlg.setMinimumWidth(480);
    dlg.setStyleSheet(R"(
        QDialog { background-color:#E3F2FD; border:none; }
        QLabel { font-size:14px; font-weight:bold; color:#1565C0; padding:4px; }
        QLineEdit { border:2px solid #90CAF9; border-radius:6px; padding:10px; background:#FFFFFF; font-size:14px; color:#1A237E; min-height:32px; }
        QLineEdit:focus { border-color:#1E88E5; }
        QComboBox { border:2px solid #90CAF9; border-radius:6px; padding:8px; background:#FFFFFF; font-size:14px; color:#1A237E; min-height:32px; }
        QComboBox::drop-down { border:none; width:28px; }
        QComboBox::down-arrow { border-left:6px solid transparent; border-right:6px solid transparent; border-top:7px solid #1565C0; }
        QComboBox QAbstractItemView { background:#FFFFFF; border:1px solid #E0E0E0; }
        QPushButton { background-color:#BBDEFB; color:#1565C0; border:2px solid #90CAF9; border-radius:6px; padding:10px 24px; font-size:14px; font-weight:bold; min-height:36px; }
        QPushButton:hover { background-color:#90CAF9; }
        QCheckBox { color:#1565C0; font-size:14px; font-weight:600; spacing:10px; }
        QCheckBox::indicator { width:22px; height:22px; border:2px solid #90CAF9; border-radius:4px; background:#FFFFFF; }
        QCheckBox::indicator:hover { border-color:#2196F3; }
        QCheckBox::indicator:checked { background-color:#90CAF9; border-color:#2196F3; }
    )");

    static const QStringList TYPES = {"INT", "BIGINT", "FLOAT", "DOUBLE", "DECIMAL", "VARCHAR", "CHAR", "TEXT", "DATE", "DATETIME", "TIME", "BLOB", "BOOLEAN"};

    QVBoxLayout* root = new QVBoxLayout(&dlg);
    root->setContentsMargins(24,24,24,24);
    root->setSpacing(16);

    QLabel* titleLabel = new QLabel("📝 请填写新列信息");
    titleLabel->setStyleSheet("font-size:20px; font-weight:bold; color:#0D47A1; padding:0;");
    root->addWidget(titleLabel);

    QHBoxLayout* row1 = new QHBoxLayout();
    row1->setSpacing(12);
    QLabel* lblName = new QLabel("列名：", &dlg);
    QLineEdit* nameEdit = new QLineEdit(&dlg);
    nameEdit->setPlaceholderText("请输入列名，如：student_name");
    row1->addWidget(lblName);
    row1->addWidget(nameEdit, 1);
    root->addLayout(row1);

    QHBoxLayout* row2 = new QHBoxLayout();
    row2->setSpacing(12);
    QLabel* lblType = new QLabel("数据类型：", &dlg);
    QComboBox* typeCombo = new QComboBox(&dlg);
    typeCombo->addItems(TYPES);
    typeCombo->setCurrentText("VARCHAR");
    QLabel* lblLen = new QLabel("长度：", &dlg);
    QLineEdit* lenEdit = new QLineEdit("255", &dlg);
    lenEdit->setAlignment(Qt::AlignCenter);
    lenEdit->setMaximumWidth(100);
    row2->addWidget(lblType);
    row2->addWidget(typeCombo, 1);
    row2->addWidget(lblLen);
    row2->addWidget(lenEdit);
    root->addLayout(row2);

    QLabel* constraintLabel = new QLabel("设置约束：", &dlg);
    root->addWidget(constraintLabel);

    QGridLayout* chkGrid = new QGridLayout();
    chkGrid->setSpacing(12);
    chkGrid->setContentsMargins(8,4,8,4);
    QCheckBox* chkNotNull = new QCheckBox("📌 非空 NOT NULL", &dlg);
    QCheckBox* chkPK = new QCheckBox("🔑 主键 PRIMARY KEY", &dlg);
    QCheckBox* chkUnique = new QCheckBox("🔒 唯一 UNIQUE", &dlg);
    QCheckBox* chkAutoInc = new QCheckBox("⚡ 自增 AUTO_INCREMENT", &dlg);
    chkGrid->addWidget(chkNotNull, 0, 0);
    chkGrid->addWidget(chkPK, 0, 1);
    chkGrid->addWidget(chkUnique, 1, 0);
    chkGrid->addWidget(chkAutoInc, 1, 1);
    root->addLayout(chkGrid);

    QLabel* hintLabel = new QLabel("<i style='color:#757575; font-size:12px'>💡 提示：同一表只能有一个主键；VARCHAR/CHAR 需要设置长度</i>", &dlg);
    root->addWidget(hintLabel);

    root->addStretch();

    QHBoxLayout* btnRow = new QHBoxLayout();
    btnRow->setSpacing(12);
    btnRow->addStretch();
    QPushButton* cancelBtn = new QPushButton("取消", &dlg);
    cancelBtn->setStyleSheet("QPushButton { background-color:#E0E0E0; color:#666666; border:2px solid #BDBDBD; border-radius:6px; padding:10px 24px; font-size:14px; font-weight:bold; min-height:36px; } QPushButton:hover { background-color:#BDBDBD; }");
    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    btnRow->addWidget(cancelBtn);
    QPushButton* okBtn = new QPushButton("确定", &dlg);
    okBtn->setStyleSheet("QPushButton { background-color:#90CAF9; color:#1565C0; border:2px solid #2196F3; border-radius:6px; padding:10px 24px; font-size:14px; font-weight:bold; min-height:36px; } QPushButton:hover { background-color:#64B5F6; }");
    connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    btnRow->addWidget(okBtn);
    root->addLayout(btnRow);

    nameEdit->setFocus();

    connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [&](int) {
        QString type = typeCombo->currentText().toUpper();
        bool needsLen = (type == "VARCHAR" || type == "CHAR");
        lenEdit->setEnabled(needsLen);
        lenEdit->setStyleSheet(needsLen ? "" : "background-color:#ECEFF1; color:#9E9E9E;");
    });
    typeCombo->currentIndexChanged(0);

    if (dlg.exec() != QDialog::Accepted) return;

    QString colName = nameEdit->text().trimmed();
    if (colName.isEmpty()) {
        QMessageBox::warning(this, "错误", "列名不能为空！");
        return;
    }

    for (int c = 0; c < m_table->columnCount(); ++c) {
        if (m_table->horizontalHeaderItem(c)->text() == colName) {
            QMessageBox::warning(this, "错误", "列名已存在！");
            return;
        }
    }

    QString type = typeCombo->currentText().toUpper();
    QString typeStr;
    if (type == "VARCHAR" || type == "CHAR") {
        QString len = lenEdit->text().trimmed();
        if (len.isEmpty()) len = "255";
        typeStr = QString("%1(%2)").arg(type).arg(len);
    } else {
        typeStr = type;
    }

    QStringList constraints;
    if (chkNotNull->isChecked()) constraints << "NOT NULL";
    if (chkPK->isChecked()) constraints << "PRIMARY KEY";
    if (chkUnique->isChecked()) constraints << "UNIQUE";
    if (chkAutoInc->isChecked()) constraints << "AUTO_INCREMENT";
    QString constrStr = constraints.join(" ");

    QString stored = colName + ":" + typeStr + (constrStr.isEmpty() ? "" : ":" + constrStr);

    int col = m_table->columnCount();
    m_table->insertColumn(col);
    m_table->setHorizontalHeaderItem(col, new QTableWidgetItem(colName));

    for (int r = 0; r < m_table->rowCount(); ++r) {
        QTableWidgetItem* item = new QTableWidgetItem("");
        item->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(r, col, item);
    }

    for (auto it = m_currentRows.begin(); it != m_currentRows.end(); ++it) {
        it.value().append("");
    }

    m_addedColumns.append(stored);
    m_lastColumns.append(colName);

    expandLogFooter();
    appendLog(QString("➕ 新增列：%1 %2（待保存）").arg(colName).arg(typeStr), "#00695C");
    int totalColChanges = m_addedColumns.size() + m_deletedColumnNames.size();
    m_statsLabel->setText(QString("列已变更（%1 列待保存）").arg(totalColChanges));
}

// ── 删除选中列 ──
void ResultPanel::onDeleteColumn() {
    // 获取所有选中的列，但要过滤整行选中的情况
    QList<QTableWidgetItem*> selected = m_table->selectedItems();
    if (selected.isEmpty()) {
        m_statsLabel->setText(QStringLiteral("<span style='color:#FF8F00'>请先选中要删除的列（点击列头或单元格）</span>"));
        return;
    }

    QSet<int> colsToDelete;
    for (QTableWidgetItem* item : selected) {
        colsToDelete.insert(item->column());
    }

    // 如果选中了所有列，可能是整行选中，弹出提示
    if (colsToDelete.size() == m_table->columnCount() && m_table->selectedRanges().size() > 0) {
        auto reply = QMessageBox::question(this, "确认",
            "您选中了所有列，您是想：\n\n"
            "• 取消选择，单独选中列\n"
            "• 继续删除所有列？",
            QMessageBox::Cancel | QMessageBox::Yes, QMessageBox::Cancel);
        if (reply != QMessageBox::Yes) {
            return;
        }
    } else if (colsToDelete.size() > 1) {
        // 删除多列时确认
        auto reply = QMessageBox::question(this, "确认删除",
            QString("确定要删除选中的 %1 列吗？\n\n此操作可以撤销。").arg(colsToDelete.size()),
            QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) return;
    }

    QList<int> sortedCols = colsToDelete.values();
    std::sort(sortedCols.begin(), sortedCols.end(), std::greater<int>());

    int realDeleted = 0;
    for (int c : sortedCols) {
        QString colName = m_table->horizontalHeaderItem(c)->text();
        // 保存原始列索引（从m_originalColumns中查找）
        int origIndex = m_originalColumns.indexOf(colName);
        if (origIndex >= 0) {
            m_deletedColumnsWithIndex[origIndex] = colName;
        }
        m_deletedColumnNames.insert(colName);

        // 从 m_currentRows 中移除该列
        for (auto it = m_currentRows.begin(); it != m_currentRows.end(); ++it) {
            if (it.value().size() > c)
                it.value().removeAt(c);
        }
        // 从 m_originalRows 中移除该列
        for (auto it = m_originalRows.begin(); it != m_originalRows.end(); ++it) {
            if (it.value().size() > c)
                it.value().removeAt(c);
        }

        m_table->removeColumn(c);
        realDeleted++;
    }

    // 更新 m_lastColumns
    m_lastColumns.clear();
    for (int c = 0; c < m_table->columnCount(); ++c)
        m_lastColumns.append(m_table->horizontalHeaderItem(c)->text());

    expandLogFooter();
    appendLog(QString("➖ 删除 %1 列（待保存）").arg(realDeleted), "#AD1457");
    int totalColChanges = m_addedColumns.size() + m_deletedColumnNames.size();
    m_statsLabel->setText(QString("列已变更（%1 列待保存）").arg(totalColChanges));
}

bool ResultPanel::hasUnsavedChanges() const {
    return !m_dirtyRowIds.isEmpty() || !m_newRowIds.isEmpty() || !m_deletedRows.isEmpty()
           || !m_addedColumns.isEmpty() || !m_deletedColumnNames.isEmpty();
}

ResultPanel::ChangeInfo ResultPanel::diffWithOriginal() const {
    ChangeInfo info;
    info.originalRows = m_originalRows;
    info.currentRows = m_currentRows;

    // 直接使用 m_deletedRows 的内容
    info.deletedRows = m_deletedRows;

    for (auto it = m_originalRows.constBegin(); it != m_originalRows.constEnd(); ++it) {
        int row = it.key();
        const QStringList& orig = it.value();

        if (!m_currentRows.contains(row)) {
            continue;
        }

        const QStringList& curr = m_currentRows[row];
        QMap<int, QString> cellChanges;

        for (int c = 0; c < qMin(orig.size(), curr.size()); ++c) {
            QString o = orig[c];
            QString n = curr[c];
            if (qstrcmp(o.toUtf8().constData(), n.toUtf8().constData()) != 0) {
                cellChanges[c] = n;
            }
        }

        if (!cellChanges.isEmpty())
            info.updatedCells[row] = cellChanges;
    }

    info.newRowIds = m_newRowIds;

    // ── 列变更：新增列 ──
    for (int i = 0; i < m_addedColumns.size(); ++i) {
        info.addedColumns[i + m_originalColumns.size()] = m_addedColumns[i];
    }
    // ── 列变更：删除列 ──
    // 使用保存的原始索引
    for (auto it = m_deletedColumnsWithIndex.constBegin(); it != m_deletedColumnsWithIndex.constEnd(); ++it) {
        info.deletedColumns[it.key()] = it.value();
    }

    return info;
}

void ResultPanel::onSave() {
    emit saveRequested();
}

void ResultPanel::markAllCommitted() {
    m_originalRows.clear();
    for (auto it = m_currentRows.begin(); it != m_currentRows.end(); ++it) {
        m_originalRows[it.key()] = it.value();
    }
    m_newRowIds.clear();
    m_deletedRows.clear();
    m_dirtyRowIds.clear();
    m_addedColumns.clear();
    m_deletedColumnNames.clear();
    m_deletedColumnsWithIndex.clear();
    m_originalColumns = m_lastColumns;
    m_statsLabel->setText("All saved");
    appendLog("✅ 变更已保存", "#4CAF50");
}

void ResultPanel::undoLastChange() {
    if (!hasUnsavedChanges()) {
        m_statsLabel->setText(QStringLiteral("<span style='color:#757575'>没有可撤销的操作</span>"));
        return;
    }

    // 1. 优先撤销新增的列
    if (!m_addedColumns.isEmpty()) {
        QString lastCol = m_addedColumns.last();
        for (int c = 0; c < m_table->columnCount(); ++c) {
            if (m_table->horizontalHeaderItem(c)->text() == lastCol) {
                for (auto it = m_currentRows.begin(); it != m_currentRows.end(); ++it) {
                    if (it.value().size() > c) it.value().removeAt(c);
                }
                for (auto it = m_originalRows.begin(); it != m_originalRows.end(); ++it) {
                    if (it.value().size() > c) it.value().removeAt(c);
                }
                m_table->removeColumn(c);
                break;
            }
        }
        m_addedColumns.removeLast();
        m_lastColumns.removeLast();

        expandLogFooter();
        appendLog(QString("↩ 撤销新增列：%1").arg(lastCol), "#FF6F00");
        m_statsLabel->setText(QStringLiteral("已撤销新增列"));
        return;
    }

    // 2. 撤销删除的列
    if (!m_deletedColumnsWithIndex.isEmpty()) {
        QList<int> sortedIndexes = m_deletedColumnsWithIndex.keys();
        std::sort(sortedIndexes.begin(), sortedIndexes.end(), std::greater<int>());
        int lastDeletedIndex = sortedIndexes.first();
        QString lastDeletedColName = m_deletedColumnsWithIndex[lastDeletedIndex];

        for (auto it = m_originalRows.begin(); it != m_originalRows.end(); ++it) {
            int r = it.key();
            if (r < m_originalRows.size()) {
                QStringList origRowData;
                for (int c = 0; c < m_table->columnCount(); ++c) {
                    QTableWidgetItem* item = m_table->item(r, c);
                    origRowData.append(item ? item->text() : "");
                }
                if (lastDeletedIndex <= origRowData.size()) {
                    origRowData.insert(lastDeletedIndex, "");
                    m_originalRows[r] = origRowData;
                }
            }
        }

        for (auto it = m_currentRows.begin(); it != m_currentRows.end(); ++it) {
            QStringList currRowData = it.value();
            if (lastDeletedIndex <= currRowData.size()) {
                currRowData.insert(lastDeletedIndex, "");
                it.value() = currRowData;
            }
        }

        int insertPos = qMin(lastDeletedIndex, m_table->columnCount());
        m_table->insertColumn(insertPos);
        m_table->setHorizontalHeaderItem(insertPos, new QTableWidgetItem(lastDeletedColName));

        for (int r = 0; r < m_table->rowCount(); ++r) {
            QTableWidgetItem* item = new QTableWidgetItem("");
            item->setTextAlignment(Qt::AlignCenter);
            m_table->setItem(r, insertPos, item);
        }

        m_deletedColumnsWithIndex.remove(lastDeletedIndex);
        m_deletedColumnNames.remove(lastDeletedColName);

        m_lastColumns.clear();
        for (int c = 0; c < m_table->columnCount(); ++c)
            m_lastColumns.append(m_table->horizontalHeaderItem(c)->text());

        expandLogFooter();
        appendLog(QString("↩ 撤销删除列：%1").arg(lastDeletedColName), "#FF6F00");
        int totalColChanges = m_addedColumns.size() + m_deletedColumnNames.size();
        m_statsLabel->setText(QString("列已变更（%1 处待保存）").arg(totalColChanges));
        return;
    }

    // 3. 撤销删除的行
    if (!m_deletedRows.isEmpty()) {
        expandLogFooter();
        appendLog(QStringLiteral("↩️ 撤销：已清除待删除行标记（请重新加载数据以完全恢复）"), "#FF6F00");
        m_deletedRows.clear();
        m_statsLabel->setText(QStringLiteral("已撤销删除行标记"));
        return;
    }

    // 4. 撤销新增的行
    if (!m_newRowIds.isEmpty()) {
        QList<int> newRows = m_newRowIds.values();
        std::sort(newRows.begin(), newRows.end(), std::greater<int>());
        for (int r : newRows) {
            m_table->removeRow(r);
            m_newRowIds.remove(r);
            m_currentRows.remove(r);
        }
        expandLogFooter();
        appendLog(QStringLiteral("↩️ 撤销新增行"), "#FF6F00");
        m_statsLabel->setText(QStringLiteral("已撤销新增行"));
        return;
    }

    m_statsLabel->setText(QStringLiteral("没有可撤销的操作"));
}

void ResultPanel::onExport() {
    // TODO: Implement CSV export
    QMessageBox::information(this, "Export", "Export feature coming soon");
}

void ResultPanel::showLog(const QString& message) {
    appendLog(message, "");
}

void ResultPanel::showError(const QString& message) {
    appendLog(message, "#F44336");
}

void ResultPanel::addHistory(const QString& sql) {
    // TODO: Implement history tab
    Q_UNUSED(sql);
}
