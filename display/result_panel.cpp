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

    // ── 查询结果 Tab ──
    QWidget *resultWidget = new QWidget(this);
    QVBoxLayout *resultLayout = new QVBoxLayout(resultWidget);
    resultLayout->setContentsMargins(0, 0, 0, 0);
    resultLayout->setSpacing(0);

    m_table = new QTableWidget(this);
    m_table->setFont(QFont("Microsoft YaHei", 11));
    m_table->setAlternatingRowColors(true);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setGridStyle(Qt::SolidLine);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->verticalHeader()->setVisible(false);
    m_table->setShowGrid(true);
    m_table->setFrameShape(QFrame::NoFrame);

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
    resultLayout->addWidget(m_table, 1);

    // 空状态占位
    m_emptyLabel = new QLabel(resultWidget);
    m_emptyLabel->setFont(QFont("Microsoft YaHei", 11));
    m_emptyLabel->setStyleSheet("QLabel { color:#AAAAAA; background:#FFFFFF; border:none; }");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setText("No data");
    m_emptyLabel->hide();
    resultLayout->addWidget(m_emptyLabel, 1);

    m_tabWidget->addTab(resultWidget, "📋 查询结果");

    // ── 日志 Tab ──
    m_log = new QTextEdit(this);
    m_log->setFont(QFont("Consolas", 10));
    m_log->setReadOnly(true);
    m_log->setFrameShape(QFrame::NoFrame);
    m_log->setStyleSheet(
        "QTextEdit { background:#FFFFFF; color:#333333; border:none; padding:6px 10px; "
        "font-family:'Consolas', 'Microsoft YaHei'; font-size:12px; }"
        "QScrollBar:vertical { background:#FFFFFF; width:7px; margin:0px; }"
        "QScrollBar::handle:vertical { background:#CCCCCC; border-radius:3px; min-height:30px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0px; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }");
    m_tabWidget->addTab(m_log, "📜 日志");

    rootLayout->addWidget(m_tabWidget);
}

ResultPanel::~ResultPanel() {}

void ResultPanel::showTable(const QStringList &columns, const QList<QStringList> &rows)
{
    m_table->setRowCount(0);
    m_table->setColumnCount(0);

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

    // 均匀分配列宽
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

    // 填充数据
    m_table->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r) {
        for (int c = 0; c < rows[r].size(); ++c) {
            QTableWidgetItem *item = new QTableWidgetItem(rows[r][c]);
            item->setTextAlignment(Qt::AlignCenter);
            m_table->setItem(r, c, item);
        }
    }

    m_table->show();
    m_emptyLabel->hide();

    // 切换到结果 Tab
    m_tabWidget->setCurrentIndex(0);

    double elapsed = m_startTime.msecsTo(QTime::currentTime()) / 1000.0;
    m_statsLabel->setText(QString("All rows fetched: %1 in %2s")
                               .arg(rows.size())
                               .arg(elapsed, 0, 'f', 2));
}

void ResultPanel::showLog(const QString &message)
{
    QString ts = "[" + QTime::currentTime().toString("hh:mm:ss") + "] ";
    m_log->append("<span style='color:#555555'>" + ts + "</span>"
                  "<span>" + message.toHtmlEscaped() + "</span>");
    m_tabWidget->setCurrentIndex(1);  // 自动切换到日志 Tab
}

void ResultPanel::showError(const QString &message)
{
    QString ts = "[" + QTime::currentTime().toString("hh:mm:ss") + "] ";
    m_log->append("<span style='color:#CC0000'>" + ts + message.toHtmlEscaped() + "</span>");
    m_statsLabel->setText(QString("<span style='color:#CC0000'>Error</span>"));
    m_tabWidget->setCurrentIndex(1);
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
    m_log->clear();
    m_statsLabel->setText("Ready");
    m_lastColumns.clear();
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
