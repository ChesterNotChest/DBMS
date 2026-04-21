#ifndef EDITOR_PANEL_H
#define EDITOR_PANEL_H

#include <QWidget>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QSyntaxHighlighter>
#include <QRegularExpression>

class SqlHighlighter;
class LineNumberTextEdit;

class LineNumberArea : public QWidget
{
    Q_OBJECT
public:
    LineNumberArea(LineNumberTextEdit *editor);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    LineNumberTextEdit *m_editor;
};

class LineNumberTextEdit : public QPlainTextEdit
{
    Q_OBJECT
    friend class LineNumberArea;

public:
    explicit LineNumberTextEdit(QWidget *parent = nullptr);
    int lineNumberWidth();

protected:
    void resizeEvent(QResizeEvent *e) override;

private slots:
    void updateLineNumberAreaWidth();

private:
    void paintLineNumberArea(QWidget *area, const QRect &rect);
    LineNumberArea *m_lineNumberArea;
};

class SqlHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit SqlHighlighter(QTextDocument *parent = nullptr);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule {
        QStringList words;
        QString color;
        bool bold;
    };
    struct HighlightingRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QVector<HighlightingRule> m_rules;
};

class EditorPanel : public QWidget
{
    Q_OBJECT

public:
    explicit EditorPanel(QWidget *parent = nullptr);
    ~EditorPanel();

    QString currentSql() const;
    void setSql(const QString &sql);
    void insertSql(const QString &sql);
    void clear();

signals:
    void executeRequested(const QString &sql);

public slots:
    void execute();
    void newQuery();
    void closeCurrentTab();

private:
    LineNumberTextEdit *m_editor = nullptr;
};

#endif // EDITOR_PANEL_H
