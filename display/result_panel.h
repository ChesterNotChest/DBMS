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
#include <QSet>
#include <QMap>

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
    QTableWidget *getTable() { return m_table; }
    QStringList getLastColumns() const { return m_lastColumns; }
    QSet<QString> getDeletedColumnNames() const { return m_deletedColumnNames; }

    // 智能保存：返回变更信息，供 MainWindow 执行 SQL
    struct ChangeInfo {
        // UPDATE: rowIndex -> (colIndex -> newValue)
        QMap<int, QMap<int, QString>> updatedCells;
        // new row IDs that were added
        QSet<int> newRowIds;
        // original data of deleted rows (pk -> rowData)
        QMap<QString, QStringList> deletedRows;
        // all original rows (for UPDATE WHERE pk=)
        QMap<int, QStringList> originalRows;
        // all current rows (for comparing)
        QMap<int, QStringList> currentRows;
        // added columns: colIndex -> column name
        QMap<int, QString> addedColumns;
        // deleted columns: colIndex -> column name
        QMap<int, QString> deletedColumns;
    };
    ChangeInfo diffWithOriginal() const;
    bool hasUnsavedChanges() const;
    void markAllCommitted();
    void undoLastChange();

signals:
    void saveRequested();

private slots:
    void onAddRow();
    void onDeleteRow();
    void onAddColumn();
    void onDeleteColumn();
    void onSave();
    void onCellChanged(QTableWidgetItem *item);
    void onToggleLogFooter();

private:
    void onExport();
    void appendLog(const QString &message, const QString &color);
    void expandLogFooter();
    void collapseLogFooter();
    // 内部：收集某行的当前值
    QStringList currentRowData(int row) const;
    // 内部：判断某行是否为新行（m_newRowIds.contains(row)）
    bool isNewRow(int row) const;
    // 内部：判断某行是否被修改
    bool isRowDirty(int row) const;

    QTabWidget *m_tabWidget = nullptr;
    QTableWidget *m_table = nullptr;
    QWidget *m_topBar = nullptr;
    QLabel *m_statsLabel = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QPushButton *m_infoBtn = nullptr;
    QPushButton *m_exportBtn = nullptr;
    QTime m_startTime;
    QStringList m_lastColumns;

    // ── 统一日志抽屉（嵌入在查询结果 Tab 底部） ──
    QWidget *m_logFooter = nullptr;       // 整个抽屉容器
    QWidget *m_logHeader = nullptr;      // 抽屉头部（折叠按钮 + 计数）
    QTextEdit *m_logText = nullptr;      // 日志内容
    QPushButton *m_logToggleBtn = nullptr;// 折叠/展开按钮
    QLabel *m_logCountLabel = nullptr;   // 消息计数
    bool m_logExpanded = false;          // 是否展开
    int m_logCount = 0;                  // 消息总数

    // ── 智能编辑追踪 ──
    // 原始数据（key=行号，value=该行各列原始值）
    QMap<int, QStringList> m_originalRows;
    // 新增的行号集合
    QSet<int> m_newRowIds;
    // 已删除的原始行数据（key=原始PK值，value=原始行数据）
    QMap<QString, QStringList> m_deletedRows;
    // 被修改过的行号集合
    QSet<int> m_dirtyRowIds;
    // 当前各行的实时数据（用于比较）
    QMap<int, QStringList> m_currentRows;
    // 下一个新行ID
    int m_nextNewRowId = 0;
    // 新增的列（列定义）
    QStringList m_addedColumns;
    // 已删除的列（列名）
    QSet<QString> m_deletedColumnNames;
    // 已删除的列（原始索引 -> 列名，用于撤销）
    QMap<int, QString> m_deletedColumnsWithIndex;
    // 原始列名列表（用于撤销）
    QStringList m_originalColumns;
};

#endif // RESULT_PANEL_H
