#include "result_panel.h"
#include "add_column_dialog.h"
#include "utils/table_manu/table_manu.h"
#include <QMessageBox>
#include <QHeaderView>
#include <QCheckBox>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QComboBox>
#include <QDialog>
#include <QListView>

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
    auto* refreshBtn = new QPushButton("🔄 刷新", this);

    for (QPushButton* btn : {addRowBtn, delRowBtn, addColBtn, delColBtn, refreshBtn}) {
        btn->setStyleSheet("QPushButton { background:#F0F0F0; color:#333; border:1px solid #DDD; border-radius:3px; padding:4px 12px; font-size:12px; } "
                           "QPushButton:hover { background:#E0E0E0; }");
        topLayout->addWidget(btn);
    }
    rootLayout->addWidget(m_topBar);

    // 结果TabWidget
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setStyleSheet("QTabWidget::pane { border:1px solid #E0E0E0; border-top:none; } "
                               "QTabBar::tab { background:#F5F5F5; padding:6px 16px; border:1px solid #E0E0E0; border-bottom:none; margin-right:2px; font-size:12px; color:#424242; } "
                               "QTabBar::tab:selected { background:#FFFFFF; color:#212121; font-weight:bold; border-bottom:2px solid #FFFFFF; }");
    rootLayout->addWidget(m_tabWidget, 1);

    // 结果页面 + 表格
    auto* resultWidget = new QWidget(m_tabWidget);
    auto* resultLayout = new QVBoxLayout(resultWidget);
    resultLayout->setContentsMargins(0, 0, 0, 0);
    resultLayout->setSpacing(0);

    m_table = new QTableWidget(resultWidget);
    m_table->setStyleSheet(
        // 基础背景：纯白
        "QTableWidget { background:#FFFFFF; border:1px solid #E0E0E0; gridline-color:#E0E0E0; }"
        // 表头：浅灰背景 + 深灰文字 + 加粗
        "QHeaderView::section { background:#E0E0E0; font-weight:bold; color:#424242; "
        "padding:8px 12px; border:none; border-bottom:2px solid #BDBDBD; "
        "border-right:1px solid #EEEEEE; font-family:'Consolas','Courier New'; font-size:13px; "
        "min-width:50px; }"
        "QHeaderView::section:first { color:#616161; font-weight:normal; font-size:11px; "
        "border-right:1px solid #BDBDBD; border-bottom:2px solid #BDBDBD; "
        "min-width:40px; max-width:50px; }"
        // 内容行：白底黑字
        "QTableWidget::item { padding:8px 12px; border:none; color:#212121; "
        "font-family:'Consolas','Courier New'; font-size:13px; "
        "border-bottom:1px solid #E0E0E0; border-right:1px solid #E0E0E0; "
        "text-align:center; }"
        // 选中高亮：深灰
        "QTableWidget::item:selected { background:#757575; color:#FFFFFF; font-weight:600; }"
        // 悬停：浅灰
        "QTableWidget::item:hover { background:#F5F5F5; }"
        // 交替行：浅灰
        "QTableWidget::item:alternate { background:#FAFAFA; }"
        // 编辑器样式 - 确保编辑时能清楚看到内容
        "QTableWidget QLineEdit { background:#FFFFFF; color:#1A1A1A; border:2px solid #424242; font-weight:bold; font-size:14px; padding:4px 8px; selection-background-color:#90CAF9; min-height:26px; }"
        "QTableWidget QWidget { background:#FFFFFF; }"
        // 角落按钮：浅灰背景
        "QTableCornerButton::section { background:#E0E0E0; border:none; }"
        // 滚动条：细窄灰色
        "QScrollBar:vertical { background:#F5F5F5; width:10px; margin:0px; }"
        "QScrollBar::handle:vertical { background:#BDBDBD; border-radius:5px; min-height:30px; }"
        "QScrollBar::handle:vertical:hover { background:#9E9E9E; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0px; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }"
        "QScrollBar:horizontal { background:#F5F5F6; height:10px; margin:0px; }"
        "QScrollBar::handle:horizontal { background:#BDBDBD; border-radius:5px; min-width:30px; }"
        "QScrollBar::handle:horizontal:hover { background:#9E9E9E; }"
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
    connect(refreshBtn, &QPushButton::clicked, this, [this]() { emit refreshRequested(); });
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
    QString escaped = msg.toHtmlEscaped();
    escaped.replace(QLatin1Char('\t'), QStringLiteral("    "));
    escaped.replace(QLatin1Char(' '), QStringLiteral("&nbsp;"));
    escaped.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    QString colored = color.isEmpty()
                          ? escaped
                          : QString("<span style='color:%1'>%2</span>").arg(color, escaped);
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
    QSet<int> updatedNewRowIds;
    QSet<int> updatedDirtyRowIds;
    QList<int> ascendingDeletedRows = rowsToDelete;
    std::sort(ascendingDeletedRows.begin(), ascendingDeletedRows.end());

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

    auto adjustIndexSet = [&](const QSet<int> &oldSet) {
        QSet<int> adjusted;
        for (int id : oldSet) {
            if (rowsToDelete.contains(id)) {
                continue;
            }
            int shift = 0;
            for (int deleted : ascendingDeletedRows) {
                if (deleted < id) {
                    shift++;
                }
            }
            adjusted.insert(id - shift);
        }
        return adjusted;
    };

    updatedNewRowIds = adjustIndexSet(m_newRowIds);
    updatedDirtyRowIds = adjustIndexSet(m_dirtyRowIds);

    // 重新建立映射：从表格读取当前数据
    m_originalRows.clear();
    m_currentRows.clear();
    m_newRowIds = updatedNewRowIds;
    m_dirtyRowIds = updatedDirtyRowIds;
    
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
    if (!m_table) {
        QMessageBox::warning(this, "错误", "请先打开表！");
        return;
    }

    AddColumnDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    ColumnConfig cfg = dlg.getConfig();
    if (cfg.name.isEmpty()) {
        QMessageBox::warning(this, "错误", "列名不能为空！");
        return;
    }

    for (int c = 0; c < m_table->columnCount(); ++c) {
        QTableWidgetItem* headerItem = m_table->horizontalHeaderItem(c);
        if (headerItem && headerItem->text() == cfg.name) {
            QMessageBox::warning(this, "错误", "列名已存在！");
            return;
        }
    }

    QString typeStr = cfg.type;
    if ((cfg.type == "VARCHAR" || cfg.type == "CHAR") && cfg.length > 0) {
        typeStr = QString("%1(%2)").arg(cfg.type).arg(cfg.length);
    }

    int col = m_table->columnCount();
    
    // 先更新数据模型，再操作表格（避免 cellChanged 信号访问未更新的数据）
    for (auto it = m_currentRows.begin(); it != m_currentRows.end(); ++it) {
        it.value().append("");
    }
    for (auto it = m_originalRows.begin(); it != m_originalRows.end(); ++it) {
        it.value().append("");
    }
    
    // 临时断开 cellChanged 信号，防止 setItem 触发回调
    disconnect(m_table, &QTableWidget::itemChanged, this, &ResultPanel::onCellChanged);
    
    m_table->insertColumn(col);
    m_table->setHorizontalHeaderItem(col, new QTableWidgetItem(cfg.name));

    // 为现有行添加新列的单元格
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QTableWidgetItem* item = new QTableWidgetItem("");
        item->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(r, col, item);
    }
    
    // 重新连接 cellChanged 信号
    connect(m_table, &QTableWidget::itemChanged, this, &ResultPanel::onCellChanged);

    // 构建完整的列定义字符串，包含外键约束信息
    QString columnDef = cfg.name + ":" + typeStr;
    
    // 添加约束信息
    QString constraints;
    
    // 非空约束
    if (!cfg.allowNull) {
        constraints += "NOT NULL";
    }
    
    // 主键约束
    if (cfg.primaryKey) {
        if (!constraints.isEmpty()) constraints += ",";
        constraints += "PRIMARY KEY";
    }
    
    // 唯一约束
    if (cfg.unique) {
        if (!constraints.isEmpty()) constraints += ",";
        constraints += "UNIQUE";
    }
    
    // 默认值
    if (!cfg.defaultValue.isEmpty()) {
        if (!constraints.isEmpty()) constraints += ",";
        constraints += "DEFAULT " + cfg.defaultValue;
    }
    
    // CHECK约束
    if (!cfg.checkConstraint.isEmpty()) {
        if (!constraints.isEmpty()) constraints += ",";
        constraints += "CHECK(" + cfg.checkConstraint + ")";
    }
    
    // 外键约束
    if (!cfg.referencedTable.isEmpty() && !cfg.referencedColumns.isEmpty()) {
        if (!constraints.isEmpty()) constraints += ",";
        constraints += QString("FOREIGN KEY (%1) REFERENCES %2(%3)")
                        .arg(cfg.name)
                        .arg(cfg.referencedTable)
                        .arg(cfg.referencedColumns.join(","));
    }
    
    if (!constraints.isEmpty()) {
        columnDef += ":" + constraints;
    }
    
    m_addedColumns.append(columnDef);
    m_lastColumns.append(cfg.name);

    if (m_statsLabel) {
        int totalColChanges = m_addedColumns.size() + m_deletedColumnNames.size();
        m_statsLabel->setText(QString("列已变更（%1 列待保存）").arg(totalColChanges));
    }
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