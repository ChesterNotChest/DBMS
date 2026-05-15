#include "result_panel.h"
#include <QFont>
#include <QHeaderView>
#include <QTime>
#include <QVBoxLayout>
#include <QDateTime>
#include <QLabel>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QScrollBar>

ResultPanel::ResultPanel(QWidget *parent) : QWidget(parent)
{
    QVBoxLayout *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ── 顶部统计栏 ──
    m_topBar = new QWidget(this);
    m_topBar->setFixedHeight(30);
    m_topBar->setStyleSheet("QWidget { background:#F7F7F7; border-bottom:1px solid #E0E0E0; }");
    QHBoxLayout *topLayout = new QHBoxLayout(m_topBar);
    topLayout->setContentsMargins(10, 0, 8, 0);
    topLayout->setSpacing(8);

    m_statsLabel = new QLabel(this);
    m_statsLabel->setFont(QFont("Microsoft YaHei", 10));
    m_statsLabel->setStyleSheet("QLabel { color:#666666; background:transparent; }");
    m_statsLabel->setText("Ready");
    topLayout->addWidget(m_statsLabel);

    QWidget *topSpacer = new QWidget(this);
    topSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    topLayout->addWidget(topSpacer);

    // ── 编辑按钮 ──
    QPushButton *addBtn = new QPushButton("+", this);
    addBtn->setFixedSize(24, 24);
    addBtn->setCursor(Qt::PointingHandCursor);
    addBtn->setFocusPolicy(Qt::NoFocus);
    addBtn->setStyleSheet(
        "QPushButton { background:#E8F5E9; color:#2E7D32; border:1px solid #C8E6C9; "
        "border-radius:3px; font-weight:bold; font-size:14px; line-height:20px; }"
        "QPushButton:hover { background:#C8E6C9; }"
        "QPushButton:pressed { background:#A5D6A7; }");
    addBtn->setToolTip("新增行");
    connect(addBtn, &QPushButton::clicked, this, &ResultPanel::onAddRow);
    topLayout->addWidget(addBtn);

    QPushButton *delBtn = new QPushButton("-", this);
    delBtn->setFixedSize(24, 24);
    delBtn->setCursor(Qt::PointingHandCursor);
    delBtn->setFocusPolicy(Qt::NoFocus);
    delBtn->setStyleSheet(
        "QPushButton { background:#FFEBEE; color:#C62828; border:1px solid #FFCDD2; "
        "border-radius:3px; font-weight:bold; font-size:14px; line-height:20px; }"
        "QPushButton:hover { background:#FFCDD2; }"
        "QPushButton:pressed { background:#EF9A9A; }");
    delBtn->setToolTip("删除选中行");
    connect(delBtn, &QPushButton::clicked, this, &ResultPanel::onDeleteRow);
    topLayout->addWidget(delBtn);

    QPushButton *saveBtn = new QPushButton(u8"\U0001F4BE", this);
    saveBtn->setFixedSize(24, 24);
    saveBtn->setCursor(Qt::PointingHandCursor);
    saveBtn->setFocusPolicy(Qt::NoFocus);
    saveBtn->setStyleSheet(
        "QPushButton { background:#E3F2FD; color:#1565C0; border:1px solid #BBDEFB; "
        "border-radius:3px; font-size:12px; }"
        "QPushButton:hover { background:#BBDEFB; }"
        "QPushButton:pressed { background:#90CAF9; }");
    saveBtn->setToolTip("保存更改到数据库");
    connect(saveBtn, &QPushButton::clicked, this, &ResultPanel::onSave);
    topLayout->addWidget(saveBtn);

    m_infoBtn = new QPushButton(this);
    m_infoBtn->setFixedSize(24, 24);
    m_infoBtn->setCursor(Qt::PointingHandCursor);
    m_infoBtn->setFocusPolicy(Qt::NoFocus);
    m_infoBtn->setStyleSheet(
        "QPushButton { background:transparent; border:none; border-radius:3px; }"
        "QPushButton:hover { background:#E0E0E0; }"
        "QPushButton:pressed { background:#D0D0D0; }");
    m_infoBtn->setToolTip("信息");
    topLayout->addWidget(m_infoBtn);

    m_exportBtn = new QPushButton(this);
    m_exportBtn->setFixedSize(24, 24);
    m_exportBtn->setCursor(Qt::PointingHandCursor);
    m_exportBtn->setFocusPolicy(Qt::NoFocus);
    m_exportBtn->setStyleSheet(
        "QPushButton { background:transparent; border:none; border-radius:3px; }"
        "QPushButton:hover { background:#E0E0E0; }"
        "QPushButton:pressed { background:#D0D0D0; }");
    m_exportBtn->setToolTip("导出 CSV");
    connect(m_exportBtn, &QPushButton::clicked, this, &ResultPanel::onExport);
    topLayout->addWidget(m_exportBtn);

    rootLayout->addWidget(m_topBar);

    // ── Tab 标签栏 ──
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setDocumentMode(true);
    m_tabWidget->setStyleSheet(
        "QTabWidget::pane { border:none; background:#FFFFFF; }"
        "QTabBar { background:#F5F5F5; border-bottom:1px solid #E0E0E0; }"
        "QTabBar::tab { background:transparent; color:#757575; padding:4px 14px 3px; "
        "margin-right:2px; border:none; font-family:'Microsoft YaHei'; font-size:11px; }"
        "QTabBar::tab:selected { background:#FFFFFF; color:#006BB3; font-weight:500; "
        "border-top:2px solid #006BB3; }"
        "QTabBar::tab:hover:!selected { background:#ECECEC; color:#333333; }");

    // ══════════════════════════════════════════════════════
    //  统一查询结果 Tab（含表格 + 可折叠日志抽屉）
    // ══════════════════════════════════════════════════════
    QWidget *resultWidget = new QWidget(this);
    QVBoxLayout *resultLayout = new QVBoxLayout(resultWidget);
    resultLayout->setContentsMargins(0, 0, 0, 0);
    resultLayout->setSpacing(0);

    // 表格区域（占据主要空间）
    m_table = new QTableWidget(this);
    m_table->setFont(QFont("Microsoft YaHei", 11));
    m_table->setAlternatingRowColors(true);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setGridStyle(Qt::SolidLine);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->verticalHeader()->setVisible(true);
    m_table->verticalHeader()->setFont(QFont("Microsoft YaHei", 10));
    m_table->setShowGrid(true);
    m_table->setFrameShape(QFrame::NoFrame);
    connect(m_table, &QTableWidget::itemChanged, this, &ResultPanel::onCellChanged);

    QFont headerFont("Microsoft YaHei", 11);
    headerFont.setBold(true);
    m_table->horizontalHeader()->setFont(headerFont);
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    m_table->horizontalHeader()->setMinimumSectionSize(40);
    m_table->horizontalHeader()->setMaximumSectionSize(400);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->verticalHeader()->setDefaultSectionSize(24);
    m_table->verticalHeader()->setMinimumSectionSize(24);
    m_table->verticalHeader()->setMaximumSectionSize(60);

    m_table->setStyleSheet(
        "QTableWidget { background:#FFFFFF; border:none; gridline-color:#E8E8E8; }"
        "QHeaderView::section { background:#FFFFFF; font-weight:700; color:#1A1A1A; "
        "padding:4px 8px; border:none; border-bottom:2px solid #006BB3; "
        "border-right:1px solid #EFEFEF; font-family:'Microsoft YaHei'; font-size:11px; "
        "min-width:40px; }"
        "QHeaderView::section:first { color:#999999; font-weight:500; "
        "border-right:1px solid #EFEFEF; border-bottom:2px solid #006BB3; "
        "min-width:36px; max-width:48px; }"
        "QTableWidget::item { padding:3px 8px; border:none; color:#1A1A1A; "
        "font-family:'Microsoft YaHei'; font-size:12px; "
        "border-bottom:1px solid #F0F0F0; border-right:1px solid #F5F5F5; "
        "text-align:center; }"
        "QTableWidget::item:selected { background:#EEF5FF; color:#000000; }"
        "QTableWidget::item:hover { background:#F5F9FF; }"
        "QTableWidget::item:alternate { background:#FAFBFC; }"
        "QTableWidget::item:editing { background:#FFFDE7; border:1px solid #FFC107; }"
        "QTableCornerButton::section { background:#FFFFFF; border:none; }"
        "QScrollBar:vertical { background:#FFFFFF; width:7px; margin:0px; }"
        "QScrollBar::handle:vertical { background:#CCCCCC; border-radius:3px; min-height:30px; }"
        "QScrollBar::handle:vertical:hover { background:#BBBBBB; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0px; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }"
        "QScrollBar:horizontal { background:#FFFFFF; height:7px; margin:0px; }"
        "QScrollBar::handle:horizontal { background:#CCCCCC; border-radius:3px; min-width:30px; }"
        "QScrollBar::handle:horizontal:hover { background:#BBBBBB; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0px; }"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background:transparent; }"
    );

    m_table->setAlternatingRowColors(true);
    resultLayout->addWidget(m_table, 1);  // stretch=1，占据主要空间

    // 空状态占位
    m_emptyLabel = new QLabel(resultWidget);
    m_emptyLabel->setFont(QFont("Microsoft YaHei", 11));
    m_emptyLabel->setStyleSheet("QLabel { color:#AAAAAA; background:#FFFFFF; border:none; }");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setText("No data");
    m_emptyLabel->hide();
    resultLayout->addWidget(m_emptyLabel, 1);

    // ══ 可折叠日志抽屉（嵌在查询结果 Tab 底部） ══
    m_logFooter = new QWidget(resultWidget);
    m_logFooter->setFixedHeight(0);  // 默认折叠，高度为0
    m_logFooter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QVBoxLayout *logFooterLayout = new QVBoxLayout(m_logFooter);
    logFooterLayout->setContentsMargins(0, 0, 0, 0);
    logFooterLayout->setSpacing(0);

    // 抽屉头部（折叠按钮 + 消息计数）
    m_logHeader = new QWidget(m_logFooter);
    m_logHeader->setFixedHeight(24);
    m_logHeader->setStyleSheet(
        "QWidget { background:#CCE4FF; border:none; }"
        "QLabel { color:#1A5FA8; background:transparent; font-family:'Microsoft YaHei'; font-size:10px; padding-left:8px; }"
        "QPushButton { background:transparent; border:none; color:#1A5FA8; font-size:11px; "
        "padding:0px 8px; font-family:'Microsoft YaHei'; }"
        "QPushButton:hover { color:#0D47A1; background:transparent; }"
    );
    QHBoxLayout *logHeaderLayout = new QHBoxLayout(m_logHeader);
    logHeaderLayout->setContentsMargins(0, 0, 4, 0);
    logHeaderLayout->setSpacing(4);

    m_logToggleBtn = new QPushButton(u8"\u25B2 收起", m_logHeader);
    m_logToggleBtn->setCursor(Qt::PointingHandCursor);
    m_logToggleBtn->setFocusPolicy(Qt::NoFocus);
    m_logToggleBtn->setFixedWidth(70);
    connect(m_logToggleBtn, &QPushButton::clicked, this, &ResultPanel::onToggleLogFooter);
    logHeaderLayout->addWidget(m_logToggleBtn);

    m_logCountLabel = new QLabel("0 条消息", m_logHeader);
    logHeaderLayout->addWidget(m_logCountLabel);

    QWidget *logHeaderSpacer = new QWidget(m_logHeader);
    logHeaderSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    logHeaderLayout->addWidget(logHeaderSpacer);

    // 日志文本区
    m_logText = new QTextEdit(m_logFooter);
    m_logText->setFont(QFont("Consolas", 10));
    m_logText->setReadOnly(true);
    m_logText->setFrameShape(QFrame::NoFrame);
    m_logText->setMaximumHeight(120);
    m_logText->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_logText->setStyleSheet(
        "QTextEdit { background:#EEF6FF; color:#1A3A5C; border:none; padding:4px 10px; "
        "font-family:'Consolas', 'Microsoft YaHei'; font-size:11px; line-height:1.5; }"
        "QScrollBar:vertical { background:#CCE4FF; width:6px; margin:0px; }"
        "QScrollBar::handle:vertical { background:#90CAF9; border-radius:3px; min-height:20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0px; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }"
    );

    logFooterLayout->addWidget(m_logHeader);
    logFooterLayout->addWidget(m_logText, 0);

    resultLayout->addWidget(m_logFooter, 0);  // stretch=0，折叠后不占高度

    m_tabWidget->addTab(resultWidget, "📋 查询结果");

    rootLayout->addWidget(m_tabWidget);
}

ResultPanel::~ResultPanel() {}

// ── 展开日志抽屉（带动画高度） ──
void ResultPanel::expandLogFooter()
{
    if (m_logExpanded) return;
    m_logExpanded = true;
    m_logFooter->setFixedHeight(24 + 120);  // header(24) + text(120)
    m_logToggleBtn->setText(u8"\u25BC 收起");
}

void ResultPanel::collapseLogFooter()
{
    if (!m_logExpanded) return;
    m_logExpanded = false;
    m_logFooter->setFixedHeight(0);
    m_logToggleBtn->setText(u8"\u25B2 日志");
}

void ResultPanel::onToggleLogFooter()
{
    if (m_logExpanded)
        collapseLogFooter();
    else
        expandLogFooter();
}

void ResultPanel::appendLog(const QString &message, const QString &color)
{
    m_logCount++;
    m_logCountLabel->setText(QString("%1 条消息").arg(m_logCount));

    QString ts = "[" + QTime::currentTime().toString("hh:mm:ss") + "] ";
    QString html = "<span style='color:#888888; font-family:Consolas; font-size:11px;'>" + ts + "</span>"
                   "<span style='color:" + color + "; font-family:Consolas; font-size:11px;'>" + message.toHtmlEscaped() + "</span>";
    m_logText->append(html);

    // 自动滚动到底部
    QScrollBar *sb = m_logText->verticalScrollBar();
    sb->setValue(sb->maximum());

    // 如果日志抽屉是折叠的，展开（方便看到新消息）
    if (!m_logExpanded)
        expandLogFooter();
}

// ── 加载表格数据，同时保存原始副本 ──
void ResultPanel::showTable(const QStringList &columns, const QList<QStringList> &rows)
{
    m_table->setRowCount(0);
    m_table->setColumnCount(0);
    m_originalRows.clear();
    m_newRowIds.clear();
    m_deletedRows.clear();
    m_dirtyRowIds.clear();
    m_currentRows.clear();
    m_nextNewRowId = 0;

    if (columns.isEmpty() || rows.isEmpty()) {
        m_table->hide();
        m_emptyLabel->show();
        m_statsLabel->setText("No data");
        return;
    }

    m_startTime = QTime::currentTime();
    m_lastColumns = columns;

    m_table->setColumnCount(columns.size());
    m_table->setHorizontalHeaderLabels(columns);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);

    // 分配列宽
    QList<int> colWidths;
    for (int c = 0; c < columns.size(); ++c) {
        int maxW = m_table->fontMetrics().horizontalAdvance(columns[c]) + 32;
        for (const QStringList &row : rows) {
            if (c < row.size())
                maxW = qMax(maxW, m_table->fontMetrics().horizontalAdvance(row[c]) + 32);
        }
        colWidths.append(qBound(50, maxW, 400));
    }
    for (int c = 0; c < columns.size(); ++c)
        m_table->setColumnWidth(c, colWidths[c]);

    // 填充数据（阻塞 itemChanged 信号，防止 onCellChanged 将所有行标记为脏）
    m_table->blockSignals(true);
    m_table->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r) {
        m_originalRows[r] = rows[r];
        m_currentRows[r] = rows[r];
        for (int c = 0; c < rows[r].size(); ++c) {
            QTableWidgetItem *item = new QTableWidgetItem(rows[r][c]);
            item->setTextAlignment(Qt::AlignCenter);
            m_table->setItem(r, c, item);
        }
    }
    m_table->blockSignals(false);

    m_table->show();
    m_emptyLabel->hide();
    // 日志抽屉保持当前状态，不主动切换
    // collapseLogFooter();  // 注释掉：不要自动收日志，避免打断用户查看

    double elapsed = m_startTime.msecsTo(QTime::currentTime()) / 1000.0;
    m_statsLabel->setText(QString("All rows fetched: %1 in %2s")
                               .arg(rows.size()).arg(elapsed, 0, 'f', 2));
}

void ResultPanel::showLog(const QString &message)
{
    appendLog(message, "#CCCCCC");
}

void ResultPanel::showError(const QString &message)
{
    appendLog(message, "#FF6B6B");
    m_statsLabel->setText(QString("<span style='color:#CC0000'>Error</span>"));
}

void ResultPanel::addHistory(const QString &sql)
{
    Q_UNUSED(sql);
}

void ResultPanel::clear()
{
    m_table->setRowCount(0);
    m_table->setColumnCount(0);
    m_table->hide();
    m_emptyLabel->show();
    m_logText->clear();
    m_logCount = 0;
    m_logCountLabel->setText("0 条消息");
    collapseLogFooter();
    m_statsLabel->setText("Ready");
    m_lastColumns.clear();
    m_originalRows.clear();
    m_newRowIds.clear();
    m_deletedRows.clear();
    m_dirtyRowIds.clear();
    m_currentRows.clear();
    m_nextNewRowId = 0;
}

void ResultPanel::onExport()
{
    if (m_table->rowCount() == 0) return;

    QString path = QFileDialog::getSaveFileName(
        this, "导出 CSV", QDir::homePath() + "/export.csv", "CSV Files (*.csv)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    for (int c = 0; c < m_lastColumns.size(); ++c) {
        QString cell = m_lastColumns[c];
        if (cell.contains(',') || cell.contains('"') || cell.contains('\n'))
            cell = QString("\"%1\"").arg(cell).replace("\"", "\"\"");
        out << cell << (c < m_lastColumns.size() - 1 ? "," : "");
    }
    out << "\n";

    for (int r = 0; r < m_table->rowCount(); ++r) {
        for (int c = 0; c < m_table->columnCount(); ++c) {
            QString cell = m_table->item(r, c) ? m_table->item(r, c)->text() : "";
            if (cell.contains(',') || cell.contains('"') || cell.contains('\n'))
                cell = QString("\"%1\"").arg(cell).replace("\"", "\"\"");
            out << cell << (c < m_table->columnCount() - 1 ? "," : "");
        }
        out << "\n";
    }
    file.close();
    m_statsLabel->setText(QString("Exported %1 rows").arg(m_table->rowCount()));
}

// ── 新增行 ──
void ResultPanel::onAddRow()
{
    int newRow = m_table->rowCount();
    m_table->insertRow(newRow);
    for (int c = 0; c < m_table->columnCount(); ++c)
        m_table->setItem(newRow, c, new QTableWidgetItem(""));
    m_newRowIds.insert(newRow);
    m_currentRows[newRow] = QStringList();
    for (int c = 0; c < m_table->columnCount(); ++c)
        m_currentRows[newRow].append("");
    m_table->setCurrentCell(newRow, 0);
    m_table->editItem(m_table->item(newRow, 0));
    m_statsLabel->setText(QString("+%1 行（待插入）").arg(m_newRowIds.size()));
    expandLogFooter();
    appendLog(QString("➕ 新增空白行 #%1（请编辑后保存）").arg(newRow), "#888888");
}

// ── 删除选中行 ──
void ResultPanel::onDeleteRow()
{
    QList<QTableWidgetSelectionRange> ranges = m_table->selectedRanges();
    if (ranges.isEmpty()) {
        m_statsLabel->setText("<span style='color:#FF8F00'>请先选中要删除的行</span>");
        return;
    }
    QList<int> rowsToDelete;
    for (const QTableWidgetSelectionRange &range : ranges) {
        for (int r = range.topRow(); r <= range.bottomRow(); ++r)
            rowsToDelete.append(r);
    }
    std::sort(rowsToDelete.begin(), rowsToDelete.end(), std::greater<int>());

    int realDeleted = 0;
    for (int r : rowsToDelete) {
        if (m_newRowIds.contains(r)) {
            m_newRowIds.remove(r);
            m_currentRows.remove(r);
        } else {
            if (m_originalRows.contains(r)) {
                QString pk = m_originalRows[r].value(0, "");
                m_deletedRows[pk] = m_originalRows[r];
                m_originalRows.remove(r);
                m_currentRows.remove(r);
                realDeleted++;
            }
        }
        m_dirtyRowIds.remove(r);
        m_table->removeRow(r);
    }

    QMap<int, QStringList> newOriginal, newCurrent;
    QSet<int> newNewIds;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        if (m_newRowIds.contains(r))
            newNewIds.insert(r);
        else if (m_originalRows.contains(r))
            newOriginal[r] = m_originalRows[r];
        if (m_currentRows.contains(r))
            newCurrent[r] = m_currentRows[r];
    }
    m_originalRows = newOriginal;
    m_currentRows = newCurrent;
    m_newRowIds = newNewIds;

    int totalChanges = m_deletedRows.size() + m_dirtyRowIds.size() + m_newRowIds.size();
    m_statsLabel->setText(QString("已删除 %1 行，%2 处待保存变更")
                              .arg(realDeleted).arg(totalChanges));
    expandLogFooter();
    appendLog(QString("➖ 标记删除 %1 行（待保存）").arg(realDeleted), "#FF8F00");
}

// ── 单元格内容变化 → 标记该行为脏行 ──
void ResultPanel::onCellChanged(QTableWidgetItem *item)
{
    if (!item) return;
    int row = item->row();

    if (m_currentRows.contains(row)) {
        while (m_currentRows[row].size() <= item->column())
            m_currentRows[row].append("");
        m_currentRows[row][item->column()] = item->text();
    }

    if (m_newRowIds.contains(row)) return;

    if (m_originalRows.contains(row)) {
        m_dirtyRowIds.insert(row);
    }
}

// ── 获取某行当前数据 ──
QStringList ResultPanel::currentRowData(int row) const
{
    QStringList data;
    for (int c = 0; c < m_table->columnCount(); ++c) {
        QTableWidgetItem *it = m_table->item(row, c);
        data.append(it ? it->text() : "");
    }
    return data;
}

bool ResultPanel::isNewRow(int row) const
{
    return m_newRowIds.contains(row);
}

bool ResultPanel::isRowDirty(int row) const
{
    return m_dirtyRowIds.contains(row);
}

// ── 精准对比：返回变更信息 ──
ResultPanel::ChangeInfo ResultPanel::diffWithOriginal() const
{
    ChangeInfo info;
    info.originalRows = m_originalRows;
    info.currentRows = m_currentRows;

    for (auto it = m_originalRows.constBegin(); it != m_originalRows.constEnd(); ++it) {
        int row = it.key();
        const QStringList &orig = it.value();

        if (!m_currentRows.contains(row)) {
            if (m_deletedRows.isEmpty() || !m_deletedRows.contains(orig.value(0, ""))) {
                QString pk = orig.value(0, "");
                if (!pk.isEmpty())
                    info.deletedRows[pk] = orig;
            }
            continue;
        }

        const QStringList &curr = m_currentRows[row];
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
    return info;
}

bool ResultPanel::hasUnsavedChanges() const
{
    return !m_dirtyRowIds.isEmpty() || !m_newRowIds.isEmpty() || !m_deletedRows.isEmpty();
}

void ResultPanel::markAllCommitted()
{
    m_originalRows.clear();
    for (int r = 0; r < m_table->rowCount(); ++r) {
        m_originalRows[r] = currentRowData(r);
    }
    m_currentRows = m_originalRows;
    m_dirtyRowIds.clear();
    m_newRowIds.clear();
    m_deletedRows.clear();
}

// ── 保存按钮：显示变更预览，日志抽屉自动展开 ──
void ResultPanel::onSave()
{
    if (m_lastColumns.isEmpty() || m_table->rowCount() == 0) {
        m_statsLabel->setText("<span style='color:#FF8F00'>请先执行 SELECT 加载数据</span>");
        return;
    }

    if (!hasUnsavedChanges()) {
        m_statsLabel->setText("<span style='color:#757575'>数据未变更，无需保存</span>");
        return;
    }

    ChangeInfo info = diffWithOriginal();
    int upd = info.updatedCells.size();
    int ins = info.newRowIds.size();
    int del = info.deletedRows.size();

    // 日志抽屉自动展开并显示变更预览
    expandLogFooter();
    m_logText->clear();
    m_logCount = 0;
    m_logCountLabel->setText("0 条消息");

    appendLog("=== 变更预览 ===", "#FFD700");
    if (upd > 0) appendLog(QString("✏️ 待 UPDATE %1 行").arg(upd), "#4FC3F7");
    if (ins > 0) appendLog(QString("➕ 待 INSERT %1 行").arg(ins), "#81C784");
    if (del > 0) appendLog(QString("➖ 待 DELETE %1 行").arg(del), "#FF8A65");
    appendLog("请在主界面工具栏点击 💾 保存，或在下方编辑器执行对应 SQL", "#888888");

    m_statsLabel->setText(
        QString("<span style='color:#E65100'>%1 UPDATE | %2 INSERT | %3 DELETE | 共 %4 处变更</span>")
            .arg(upd).arg(ins).arg(del).arg(upd + ins + del));
}
