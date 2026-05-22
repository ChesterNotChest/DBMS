#include "editor_panel.h"
#include <QFont>
#include <QPainter>
#include <QPalette>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <QScrollBar>
#include <QTextBlock>

// SqlHighlighter
SqlHighlighter::SqlHighlighter(QTextDocument *parent) : QSyntaxHighlighter(parent)
{
    QVector<SqlHighlighter::Rule> rules;

    auto add = [&](const QStringList &words, const QString &color, bool bold) {
        SqlHighlighter::Rule r;
        r.words = words; r.color = color; r.bold = bold;
        rules.append(r);
    };

    add({"SELECT","FROM","WHERE","AND","OR","NOT","IN","LIKE","BETWEEN","IS","NULL",
         "AS","ON","JOIN","INNER","LEFT","RIGHT","OUTER","CROSS","NATURAL",
         "GROUP","BY","HAVING","ORDER","ASC","DESC","LIMIT","OFFSET","DISTINCT","ALL",
         "UNION","INTERSECT","EXCEPT","EXISTS","CASE","WHEN","THEN","ELSE","END",
         "CAST","COALESCE","IFNULL","NULLIF"},
        "#0000FF", true);

    add({"CREATE","TABLE","DATABASE","INDEX","VIEW","DROP","ALTER","ADD","COLUMN",
         "PRIMARY","KEY","FOREIGN","REFERENCES","UNIQUE","CHECK","DEFAULT",
         "AUTO_INCREMENT","INSERT","INTO","VALUES","UPDATE","SET","DELETE",
         "TRUNCATE","USE","SHOW","DESCRIBE","DESC","EXPLAIN"},
        "#0000FF", true);

    add({"INT","SMALLINT","FLOAT","VARCHAR"},
        "#0000FF", true);

    add({"COUNT","SUM","AVG","MAX","MIN","ROUND","LENGTH","UPPER","LOWER","TRIM",
         "SUBSTR","SUBSTRING","REPLACE","ABS","NOW","DATE","TIME"},
        "#0000FF", false);

    for (const SqlHighlighter::Rule &r : rules) {
        QTextCharFormat fmt;
        fmt.setForeground(QColor(r.color));
        if (r.bold) fmt.setFontWeight(QFont::Bold);
        for (const QString &w : r.words) {
            HighlightingRule hr;
            hr.pattern = QRegularExpression("\\b" + w + "\\b",
                             QRegularExpression::CaseInsensitiveOption);
            hr.format = fmt;
            m_rules.append(hr);
        }
    }

    HighlightingRule sr;
    sr.pattern = QRegularExpression("'(?:[^'\\\\]|\\\\.)*'");
    sr.format.setForeground(QColor("#CC0000"));
    m_rules.append(sr);

    HighlightingRule nr;
    nr.pattern = QRegularExpression("\\b\\d+(\\.\\d+)?\\b");
    nr.format.setForeground(QColor("#098658"));
    m_rules.append(nr);

    HighlightingRule cr;
    cr.pattern = QRegularExpression("--.*$");
    cr.format.setForeground(QColor("#008000"));
    cr.format.setFontItalic(true);
    m_rules.append(cr);
}

void SqlHighlighter::highlightBlock(const QString &text)
{
    for (const HighlightingRule &r : m_rules) {
        QRegularExpressionMatchIterator it = r.pattern.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            setFormat(m.capturedStart(), m.capturedLength(), r.format);
        }
    }
}

// LineNumberArea
LineNumberArea::LineNumberArea(LineNumberTextEdit *editor)
    : QWidget(editor), m_editor(editor)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
}

QSize LineNumberArea::sizeHint() const
{
    return QSize(m_editor->lineNumberWidth(), 0);
}

void LineNumberArea::paintEvent(QPaintEvent *)
{
    m_editor->paintLineNumberArea(this, rect());
}

// LineNumberTextEdit
LineNumberTextEdit::LineNumberTextEdit(QWidget *parent)
    : QPlainTextEdit(parent)
    , m_lineNumberArea(new LineNumberArea(this))
{
    setFont(QFont("Consolas", 12));
    setTabStopDistance(40);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    setStyleSheet(
        "QPlainTextEdit { background:#FFFFFF; color:#0059B3; border:none; "
        "selection-background-color:#ADD6FF; selection-color:#FFFFFF; }");
    QPalette p = palette();
    p.setColor(QPalette::Text,       QColor("#0059B3"));
    p.setColor(QPalette::Base,       QColor("#FFFFFF"));
    p.setColor(QPalette::Highlight,  QColor("#ADD6FF"));
    p.setColor(QPalette::HighlightedText, QColor("#FFFFFF"));
    setPalette(p);

    setViewportMargins(lineNumberWidth(), 0, 0, 0);
    document()->setDocumentMargin(8);   // 对齐行号栏：文档上边距 = CSS padding

    connect(this, &QPlainTextEdit::textChanged,       this, &LineNumberTextEdit::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest,     this, [this](const QRect &, int dy) {
        if (dy) m_lineNumberArea->scroll(0, dy);
        else   m_lineNumberArea->update();
    });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        m_lineNumberArea->update();
    });
}

int LineNumberTextEdit::lineNumberWidth()
{
    int d = 1, n = qMax(1, blockCount());
    while (n >= 10) { n /= 10; ++d; }
    return 8 + d * 16;
}

void LineNumberTextEdit::updateLineNumberAreaWidth()
{
    setViewportMargins(lineNumberWidth(), 0, 0, 0);
    m_lineNumberArea->update();
}

void LineNumberTextEdit::resizeEvent(QResizeEvent *e)
{
    QPlainTextEdit::resizeEvent(e);
    m_lineNumberArea->setFixedHeight(viewport()->height());
}

void LineNumberTextEdit::paintLineNumberArea(QWidget *area, const QRect &rect)
{
    QPainter painter(area);
    painter.fillRect(rect, QColor("#F3F3F3"));

    painter.setPen(QColor("#CCCCCC"));
    painter.drawLine(rect.right(), rect.top(), rect.right(), rect.bottom());

    painter.setPen(QColor("#888888"));
    painter.setFont(QFont("Consolas", 11));

    QTextBlock block = firstVisibleBlock();
    int blockNum = block.blockNumber();
    qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();
    qreal bottom = top + blockBoundingRect(block).height();

    while (block.isValid() && top <= rect.bottom()) {
        if (block.isVisible() && bottom >= rect.top()) {
            int x = rect.right() - 8
                    - fontMetrics().horizontalAdvance(QString::number(blockNum + 1));
            painter.drawText(x, int(top) + fontMetrics().ascent(),
                             QString::number(blockNum + 1));
        }
        top = bottom;
        bottom += blockBoundingRect(block.next()).height();
        block = block.next();
        ++blockNum;
    }
}

// EditorPanel
EditorPanel::EditorPanel(QWidget *parent) : QWidget(parent)
{
    QVBoxLayout *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    m_editor = new LineNumberTextEdit(this);
    new SqlHighlighter(m_editor->document());
    v->addWidget(m_editor);
}

EditorPanel::~EditorPanel() {}

QString EditorPanel::currentSql() const {
    return m_editor ? m_editor->toPlainText().trimmed() : "";
}
void EditorPanel::setSql(const QString &sql) {
    if (m_editor) m_editor->setPlainText(sql);
}
void EditorPanel::insertSql(const QString &sql) {
    if (m_editor) m_editor->insertPlainText(sql);
}
void EditorPanel::clear() {
    if (m_editor) m_editor->clear();
}
void EditorPanel::execute() {
    QString s = currentSql();
    if (!s.isEmpty()) emit executeRequested(s);
}
void EditorPanel::newQuery() { clear(); }
void EditorPanel::closeCurrentTab() { clear(); }
