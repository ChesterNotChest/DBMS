#ifndef RESULT_PANEL_H
#define RESULT_PANEL_H

#include <QWidget>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QTime>
#include <QStringList>

class ResultPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ResultPanel(QWidget *parent = nullptr);
    ~ResultPanel();

    void showTable(const QStringList &columns, const QList<QStringList> &rows);
    void showLog(const QString &message);
    void showError(const QString &message);
    void addHistory(const QString &sql);
    void clear();

private:
    void onExport();

    QTabWidget *m_tabWidget = nullptr;
    QTableWidget *m_table = nullptr;
    QTextEdit *m_log = nullptr;
    QWidget *m_topBar = nullptr;
    QLabel *m_statsLabel = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QPushButton *m_infoBtn = nullptr;
    QPushButton *m_exportBtn = nullptr;
    QTime m_startTime;
    QStringList m_lastColumns;
};

#endif // RESULT_PANEL_H
