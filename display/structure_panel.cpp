/**
 * structure_panel.cpp - left-side database structure tree.
 *
 * Responsibilities: UI rendering and user interaction only. Data lookup goes
 * through the GUI client runtime so it shares the same auth/session behavior as
 * the editor without directly touching repo files.
 */
#include "structure_panel.h"
#include "client/sql_client_engine.h"
#include "service/service.h"

#include <QAction>
#include <QFile>
#include <QHeaderView>
#include <QMenu>
#include <QTextStream>
#include <QRegularExpression>

namespace {

struct ColumnDescriptor {
    QString name;
    QString type;
    int length = 0;
    bool notNull = false;
    bool primaryKey = false;
    bool foreignKey = false;
    bool unique = false;
    QString defaultValue;
    QString referencedTable;
    QString referencedColumn;
    QString fullLine;
};

static QStringList extractColumnsFromConstraintLine(const QString &line)
{
    QStringList columns;
    int open = line.indexOf('(');
    int close = line.indexOf(')', open + 1);
    if (open < 0 || close < 0 || close <= open) {
        return columns;
    }
    const QStringList inner = line.mid(open + 1, close - open - 1).split(',', Qt::SkipEmptyParts);
    for (QString col : inner) {
        col = col.trimmed();
        if (!col.isEmpty()) {
            columns.append(col);
        }
    }
    return columns;
}

static bool isSeparatorLine(const QString &line)
{
    const QString text = line.trimmed();
    if (text.isEmpty()) {
        return true;
    }
    if (text.startsWith('+') && text.contains('-')) {
        return true;
    }
    if (text.startsWith('|') && text.count('|') >= 2) {
        QString temp = text;
        if (temp.remove(QChar(' ')).remove(QChar('|')).remove(QChar('-')).remove(QChar('+')).isEmpty()) {
            return true;
        }
    }
    return false;
}

static QStringList splitTableRow(const QString &line)
{
    const QString text = line.trimmed();
    if (!text.startsWith('|') || !text.endsWith('|')) {
        return {};
    }
    QString body = text.mid(1, text.length() - 2);
    QStringList cells = body.split('|', Qt::SkipEmptyParts);
    for (QString &cell : cells) {
        cell = cell.trimmed();
    }
    return cells;
}

static QList<ColumnDescriptor> parseDescribeColumns(const QString &text)
{
    QList<ColumnDescriptor> columns;
    QStringList lines = text.split(QLatin1Char('\n'));

    QSet<QString> primaryKeyColumns;
    QSet<QString> foreignKeyColumns;
    QSet<QString> uniqueKeyColumns;

    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty() || isSeparatorLine(line)) {
            continue;
        }

        const QString lowerLine = line.toLower();
        if (line.startsWith(QStringLiteral("CONSTRAINT"), Qt::CaseInsensitive)) {
            if (lowerLine.contains("primary key")) {
                for (const QString &col : extractColumnsFromConstraintLine(line)) {
                    primaryKeyColumns.insert(col.trimmed().toLower());
                }
            }
            if (lowerLine.contains("foreign key")) {
                for (const QString &col : extractColumnsFromConstraintLine(line)) {
                    foreignKeyColumns.insert(col.trimmed().toLower());
                }
            }
            if (lowerLine.contains("unique") && lowerLine.contains("key")) {
                for (const QString &col : extractColumnsFromConstraintLine(line)) {
                    uniqueKeyColumns.insert(col.trimmed().toLower());
                }
            }
            continue;
        }

        QStringList rowCells = splitTableRow(line);
        ColumnDescriptor desc;
        if (!rowCells.isEmpty()) {
            const QString firstCell = rowCells.first().trimmed();
            // Skip header rows like "字段名：类型" or "Field | Type" etc.
            if (firstCell.isEmpty() ||
                firstCell.compare("Field", Qt::CaseInsensitive) == 0 ||
                firstCell.compare("字段名", Qt::CaseInsensitive) == 0 ||
                firstCell.compare("Column", Qt::CaseInsensitive) == 0 ||
                firstCell.compare("--", Qt::CaseInsensitive) == 0 ||
                firstCell.compare("Type", Qt::CaseInsensitive) == 0 ||
                firstCell.compare("类型", Qt::CaseInsensitive) == 0 ||
                firstCell.compare("Null", Qt::CaseInsensitive) == 0 ||
                firstCell.compare("NULL", Qt::CaseInsensitive) == 0 ||
                firstCell.compare("Key", Qt::CaseInsensitive) == 0 ||
                firstCell.compare("Default", Qt::CaseInsensitive) == 0 ||
                firstCell.compare("Extra", Qt::CaseInsensitive) == 0 ||
                firstCell.contains("Field", Qt::CaseInsensitive) ||
                firstCell.contains("字段名", Qt::CaseInsensitive) ||
                firstCell.contains("Type", Qt::CaseInsensitive) ||
                firstCell.contains("类型", Qt::CaseInsensitive) ||
                firstCell.contains("唯一", Qt::CaseInsensitive) ||
                firstCell.contains("Constraint", Qt::CaseInsensitive) ||
                firstCell.contains("约束", Qt::CaseInsensitive) ||
                firstCell.contains("PK", Qt::CaseInsensitive) ||
                firstCell.contains("FK", Qt::CaseInsensitive) ||
                firstCell.startsWith("└", Qt::CaseInsensitive) ||
                firstCell.startsWith("├", Qt::CaseInsensitive) ||
                firstCell.startsWith("-", Qt::CaseInsensitive) ||
                firstCell.startsWith("=", Qt::CaseInsensitive) ||
                firstCell.startsWith("+", Qt::CaseInsensitive) ||
                firstCell.startsWith("|", Qt::CaseInsensitive)) {
                continue;  // Skip header rows, constraint info rows, and separators
            }
            
            desc.name = firstCell;
            
            if (rowCells.size() > 1) {
                QString typeStr = rowCells.at(1);
                
                // Extract base type and length with comprehensive pattern matching
                QRegularExpression typePattern("(\\w+)\\s*(?:\\((?:\\d+|\\d+\\s*,\\s*\\d+)\\))?");
                QRegularExpressionMatchIterator iter = typePattern.globalMatch(typeStr);
                
                if (iter.hasNext()) {
                    QRegularExpressionMatch match = iter.next();
                    QString baseType = match.captured(1).toUpper();
                    
                    // Extract the full type specification including length
                    QString fullTypeSpec = match.captured(0);
                    
                    // Handle duplicate length case (e.g. VARCHAR(8)(8) -> VARCHAR(8))
                    QRegularExpression dupPattern("(\\w+\\s*\\(\\d+(?:,\\s*\\d+)?\\))\\s*\\(\\d+(?:,\\s*\\d+)?\\)");
                    QRegularExpressionMatch dupMatch = dupPattern.match(typeStr);
                    if (dupMatch.hasMatch()) {
                        fullTypeSpec = dupMatch.captured(1).trimmed();
                    }
                    
                    desc.type = fullTypeSpec.toUpper();
                    
                    // Extract length from the corrected type
                    int parenStart = fullTypeSpec.indexOf('(');
                    int parenEnd = fullTypeSpec.indexOf(')', parenStart);
                    if (parenStart > 0 && parenEnd > parenStart) {
                        QString lengthStr = fullTypeSpec.mid(parenStart + 1, parenEnd - parenStart - 1);
                        bool ok;
                        desc.length = lengthStr.toInt(&ok);
                        if (!ok) {
                            // Handle decimal types like DECIMAL(10,2)
                            QString cleanedLength = lengthStr.replace(QRegularExpression("[^\\d,]"), "");
                            if (cleanedLength.contains(',')) {
                                desc.length = cleanedLength.section(',', 0, 0).toInt();
                            } else {
                                desc.length = cleanedLength.toInt(&ok);
                                if (!ok) desc.length = 0;
                            }
                        }
                    }
                } else {
                    // Fallback to original logic if pattern doesn't match
                    desc.type = typeStr.toUpper();
                    
                    int parenStart = typeStr.indexOf('(');
                    int parenEnd = typeStr.indexOf(')', parenStart);
                    if (parenStart > 0 && parenEnd > parenStart) {
                        QString lengthStr = typeStr.mid(parenStart + 1, parenEnd - parenStart - 1);
                        bool ok;
                        desc.length = lengthStr.toInt(&ok);
                        if (!ok) desc.length = 0;
                    }
                }
            }
            
            // New format (7 columns): 字段名 | 类型 | NOT NULL | 主键 | 唯一 | 默认值 | 外键引用
            if (rowCells.size() >= 7) {
                if (rowCells.size() > 2)
                    desc.notNull = (rowCells.at(2).compare("YES", Qt::CaseInsensitive) == 0);
                if (rowCells.size() > 3)
                    desc.primaryKey = rowCells.at(3).contains(QStringLiteral("✓"));
                if (rowCells.size() > 4)
                    desc.unique = rowCells.at(4).contains(QStringLiteral("✓"));
                if (rowCells.size() > 5) {
                    QString defaultVal = rowCells.at(5);
                    if (!defaultVal.isEmpty() && defaultVal.compare("NULL", Qt::CaseInsensitive) != 0)
                        desc.defaultValue = defaultVal;
                }
                if (rowCells.size() > 6) {
                    QString fkCell = rowCells.at(6);
                    if (!fkCell.isEmpty()) {
                        desc.foreignKey = true;
                        int dotPos = fkCell.indexOf('.');
                        if (dotPos > 0) {
                            desc.referencedTable = fkCell.left(dotPos).trimmed();
                            desc.referencedColumn = fkCell.mid(dotPos + 1).trimmed();
                        }
                    }
                }
            } else {
                // Old format (6 columns): 字段名 | 类型 | NOT NULL | 主键 | 默认值 | 外键引用
                if (rowCells.size() > 2) {
                    desc.notNull = (rowCells.at(2).compare("YES", Qt::CaseInsensitive) == 0);
                }
                if (rowCells.size() > 3) {
                    QString keyType = rowCells.at(3).toLower();
                    if (keyType.contains("pri"))
                        desc.primaryKey = true;
                    if (keyType.contains("uni"))
                        desc.unique = true;
                }
                if (rowCells.size() > 4) {
                    QString defaultVal = rowCells.at(4);
                    if (!defaultVal.isEmpty() && defaultVal.compare("NULL", Qt::CaseInsensitive) != 0)
                        desc.defaultValue = defaultVal;
                }
                if (rowCells.size() > 5) {
                    QString fkCell = rowCells.at(5);
                    if (!fkCell.isEmpty()) {
                        desc.foreignKey = true;
                        int dotPos = fkCell.indexOf('.');
                        if (dotPos > 0) {
                            desc.referencedTable = fkCell.left(dotPos).trimmed();
                            desc.referencedColumn = fkCell.mid(dotPos + 1).trimmed();
                        }
                    }
                }
            }
            
            desc.fullLine = rowCells.join(" | ");
            
            if (lowerLine.contains("references")) {
                desc.foreignKey = true;
                int refStart = lowerLine.indexOf("references");
                if (refStart >= 0) {
                    QString refPart = line.mid(refStart + 11).trimmed();
                    int dotPos = refPart.indexOf('.');
                    if (dotPos > 0) {
                        desc.referencedTable = refPart.left(dotPos).trimmed();
                        QString colPart = refPart.mid(dotPos + 1);
                        int parenStart = colPart.indexOf('(');
                        int parenEnd = colPart.indexOf(')');
                        if (parenStart >= 0 && parenEnd > parenStart) {
                            desc.referencedColumn = colPart.mid(parenStart + 1, parenEnd - parenStart - 1).trimmed();
                        }
                    }
                }
            }
        } else {
            desc.name = line.section(QLatin1Char(' '), 0, 0);
            desc.fullLine = line;
            
            if (lowerLine.contains("primary key")) {
                desc.primaryKey = true;
            }
            if (lowerLine.contains("unique")) {
                desc.unique = true;
            }
            if (lowerLine.contains("references")) {
                desc.foreignKey = true;
                int refStart = lowerLine.indexOf("references");
                if (refStart >= 0) {
                    QString refPart = line.mid(refStart + 11).trimmed();
                    int dotPos = refPart.indexOf('.');
                    if (dotPos > 0) {
                        desc.referencedTable = refPart.left(dotPos).trimmed();
                        QString colPart = refPart.mid(dotPos + 1);
                        int parenStart = colPart.indexOf('(');
                        int parenEnd = colPart.indexOf(')');
                        if (parenStart >= 0 && parenEnd > parenStart) {
                            desc.referencedColumn = colPart.mid(parenStart + 1, parenEnd - parenStart - 1).trimmed();
                        }
                    }
                }
            }
        }

        if (!desc.name.isEmpty()) {
            columns.append(desc);
        }
    }

    for (int i = 0; i < columns.size(); ++i) {
        const QString lowerName = columns[i].name.toLower();
        if (primaryKeyColumns.contains(lowerName)) {
            columns[i].primaryKey = true;
        }
        if (foreignKeyColumns.contains(lowerName)) {
            columns[i].foreignKey = true;
        }
        if (uniqueKeyColumns.contains(lowerName)) {
            columns[i].unique = true;
        }
    }

    return columns;
}

static QStringList columnNamesFromDescribeText(const QString &text)
{
    QStringList columnNames;
    for (const ColumnDescriptor &desc : parseDescribeColumns(text)) {
        if (!desc.name.isEmpty()) {
            columnNames.append(desc.name);
        }
    }
    return columnNames;
}

} // namespace

StructurePanel::StructurePanel(QWidget *parent)
    : QWidget(parent)
    , m_currentDatabase("")
    , m_currentTable("")
{
    setupUI();
    loadStructure();
}

StructurePanel::~StructurePanel() {}

void StructurePanel::setupUI()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // VS Code Light sidebar background.
    setStyleSheet("QWidget { background:#F3F3F3; }");

    QLabel *title = new QLabel(QStringLiteral("Objects"));
    title->setFont(QFont("Microsoft YaHei", 12, QFont::Bold));
    title->setStyleSheet(
        "QLabel { color:#333333; padding:10px 14px 6px; background:#F3F3F3; font-weight:600; }");
    layout->addWidget(title);

    m_treeWidget = new QTreeWidget(this);
    m_treeWidget->setHeaderHidden(true); // Hide the header to remove "Object" label
    m_treeWidget->setFont(QFont("Microsoft YaHei", 12));
    m_treeWidget->setAnimated(false); // Disable expansion animation for faster tree interaction
    m_treeWidget->setIndentation(16);
    m_treeWidget->setRootIsDecorated(true);
    m_treeWidget->setUniformRowHeights(true);
    m_treeWidget->setExpandsOnDoubleClick(false);
    // VS Code Light tree styling.
    m_treeWidget->setStyleSheet(
        "QTreeWidget { border:none; background:#F3F3F3; padding:4px 0; outline:none; }"
        "QTreeWidget::item { padding:3px 12px 3px 8px; color:#333333; border-radius:3px; }"
        "QTreeWidget::item:hover { background:#E8E8E8; }"
        "QTreeWidget::item:selected { background:#ADD6FF; color:#000000; }"
        "QTreeWidget::item:selected:active { background:#ADD6FF; color:#000000; }"
        "QHeaderView::section { background:#EEEEEE; font-weight:600; color:#333333; "
        "padding:4px 12px; border:none; border-bottom:1px solid #E0E0E0; font-size:11px; }"
        "QTreeWidget::branch { background:transparent; }"
        "QScrollBar:vertical { background:#F3F3F3; width:8px; }"
        "QScrollBar::handle:vertical { background:#C4C4C4; border-radius:4px; min-height:30px; }"
        "QScrollBar::handle:vertical:hover { background:#A0A0A0; }");

    connect(m_treeWidget, &QTreeWidget::itemDoubleClicked,
            this, &StructurePanel::onTreeItemDoubleClicked);
    connect(m_treeWidget, &QTreeWidget::itemExpanded,
            this, &StructurePanel::onTreeItemExpanded);
    m_treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_treeWidget, &QTreeWidget::customContextMenuRequested,
            this, &StructurePanel::onTreeContextMenuRequested);

    layout->addWidget(m_treeWidget, 1);

    m_statusLabel = new QLabel(QStringLiteral("Database: none\nTable: none"));
    m_statusLabel->setFont(QFont("Microsoft YaHei", 11));
    m_statusLabel->setStyleSheet(
        "QLabel { color:#616161; font-size:11px; padding:6px 12px; background:#F3F3F3; border-top:1px solid #E0E0E0; }");
    layout->addWidget(m_statusLabel);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setContentsMargins(8, 4, 8, 8);
    btnLayout->setSpacing(4);

    QPushButton *btnNew = new QPushButton(QStringLiteral("+ New"));
    btnNew->setFont(QFont("Microsoft YaHei", 11));
    btnNew->setCursor(Qt::PointingHandCursor);
    btnNew->setFocusPolicy(Qt::NoFocus);
    btnNew->setStyleSheet(
        "QPushButton { background:#4CAF50; color:#FFFFFF; border:none; border-radius:3px; "
        "padding:4px 10px; font-size:11px; }"
        "QPushButton:hover { background:#43A047; }");
    connect(btnNew, &QPushButton::clicked, this, &StructurePanel::newDatabaseRequested);
    btnLayout->addWidget(btnNew);

    QPushButton *btnOpen = new QPushButton(QStringLiteral("Open"));
    btnOpen->setFont(QFont("Microsoft YaHei", 11));
    btnOpen->setCursor(Qt::PointingHandCursor);
    btnOpen->setFocusPolicy(Qt::NoFocus);
    btnOpen->setStyleSheet(
        "QPushButton { background:#FFFFFF; color:#555555; border:1px solid #CCCCCC; "
        "border-radius:3px; padding:4px 10px; font-size:11px; }"
        "QPushButton:hover { background:#F0F0F0; border-color:#BBBBBB; }");
    connect(btnOpen, &QPushButton::clicked, this, &StructurePanel::openDatabaseRequested);
    btnLayout->addWidget(btnOpen);

    QPushButton *btnDelete = new QPushButton(QStringLiteral("Delete"));
    btnDelete->setFont(QFont("Microsoft YaHei", 11));
    btnDelete->setCursor(Qt::PointingHandCursor);
    btnDelete->setFocusPolicy(Qt::NoFocus);
    btnDelete->setStyleSheet(
        "QPushButton { background:#FFFFFF; color:#666666; border:1px solid #CCCCCC; "
        "border-radius:3px; padding:4px 10px; font-size:11px; }"
        "QPushButton:hover { background:#FFF0F0; border-color:#FFAAAA; color:#CC3333; }");
    connect(btnDelete, &QPushButton::clicked, this, &StructurePanel::deleteDatabaseRequested);
    btnLayout->addWidget(btnDelete);

    layout->addLayout(btnLayout);
}

void StructurePanel::loadStructure()
{
    // Avoid selection signals while the tree is being rebuilt.
    m_treeWidget->blockSignals(true);
    m_treeWidget->setUpdatesEnabled(false);
    m_treeWidget->clear();

    QStringList dbNames = firstColumnValuesFromSql(QStringLiteral("SHOW DATABASES;"));
    dbNames.sort();

    for (const QString &dbName : dbNames) {
        if (dbName == "information_schema" || dbName == "mysql" || dbName == "performance_schema" || dbName == "sys") {
            continue;  // Skip system databases
        }

        QTreeWidgetItem *dbItem = new QTreeWidgetItem(m_treeWidget);
        dbItem->setText(0, QStringLiteral("[DB] %1").arg(dbName));
        dbItem->setIcon(0, QIcon(":/icons/database.png"));
        dbItem->setData(0, Qt::UserRole, "database:" + dbName);
        dbItem->setFont(0, QFont("Microsoft YaHei", 9));
        if (dbName == m_currentDatabase) {
            dbItem->setFont(0, QFont("Microsoft YaHei", 9, QFont::Bold));
            dbItem->setForeground(0, QColor("#0066cc"));
            loadTablesForDatabase(dbItem, dbName);
            dbItem->setExpanded(true);
        }
        dbItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    }

    m_treeWidget->setUpdatesEnabled(true);
    m_treeWidget->blockSignals(false);
    updateStatusLabel();
}

void StructurePanel::loadTablesForDatabase(QTreeWidgetItem *dbItem, const QString &dbName)
{
    // Clear any existing children before loading
    while (dbItem->childCount() > 0) {
        delete dbItem->takeChild(0);
    }
    m_treeWidget->setUpdatesEnabled(false);

    // First switch to the target database (this changes current database context)
    service::SqlExecResult useResult = m_clientEngine->executeSql(m_clientId, 
        QStringLiteral("USE %1;").arg(dbName));
    if (!useResult.success) {
        qDebug() << "Failed to switch to database:" << dbName << useResult.errorMessage;
        m_treeWidget->setUpdatesEnabled(true);
        return;
    }
    
    // Now show tables from the current database
    QStringList tblNames = firstColumnValuesFromSql(QStringLiteral("SHOW TABLES;"));
    tblNames.sort();

    for (const QString &tblName : tblNames) {
        QTreeWidgetItem *tItem = new QTreeWidgetItem(dbItem);
        tItem->setText(0, QStringLiteral("[T] %1").arg(tblName));
        tItem->setIcon(0, QIcon(":/icons/table.png"));
        tItem->setData(0, Qt::UserRole, "table:" + dbName + ":" + tblName);
        tItem->setFont(0, QFont("Consolas", 9));
        tItem->setForeground(0, QColor("#666666"));

        if (dbName == m_currentDatabase && tblName == m_currentTable) {
            addColumnsToTableItem(tItem, dbName, tblName);
            tItem->setExpanded(true);
        }
        tItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    }
    m_treeWidget->setUpdatesEnabled(true);
}

void StructurePanel::addColumnsToTableItem(QTreeWidgetItem *tItem,
                                           const QString &dbName,
                                           const QString &tableName)
{
    if (tItem->childCount() > 0) {
        return;
    }
    
    // Ensure we're in the correct database context
    service::SqlExecResult useResult = m_clientEngine->executeSql(m_clientId,
        QStringLiteral("USE %1;").arg(dbName));
    if (!useResult.success) {
        qDebug() << "Failed to switch to database for DESC:" << dbName << useResult.errorMessage;
        return;
    }

    QStringList columns;
    service::SqlExecResult result;
    if (m_clientEngine != nullptr && !m_clientId.isEmpty() && !tableName.isEmpty()) {
        // Database context already set above with USE statement
        result = m_clientEngine->executeSql(m_clientId,
            QStringLiteral("DESC %1;").arg(tableName));
        if (result.success) {
            columns = columnNamesFromDescribeText(result.text);
        }
    }

    QList<ColumnDescriptor> columnDescriptors;
    if (result.success) {
        columnDescriptors = parseDescribeColumns(result.text);
    }

    if (columnDescriptors.isEmpty()) {
        for (const QString &colName : columns) {
            QTreeWidgetItem *cItem = new QTreeWidgetItem(tItem);
            cItem->setText(0, QStringLiteral("[C] ") + colName);
            cItem->setData(0, Qt::UserRole, "column:" + dbName + ":" + tableName + ":" + colName);
            cItem->setFont(0, QFont("Consolas", 8));
            cItem->setForeground(0, QColor("#888888"));
        }
        return;
    }

    m_treeWidget->setUpdatesEnabled(false);
    for (const ColumnDescriptor &desc : columnDescriptors) {
        if (desc.name.isEmpty() || desc.name.trimmed().isEmpty()) {
            continue;
        }

        QStringList marks;
        if (desc.primaryKey) marks.append("PK");
        if (desc.foreignKey) marks.append("FK");
        if (desc.unique)     marks.append("UQ");
        if (desc.notNull)    marks.append("NN");

        QString displayText = desc.name;
        if (!desc.type.isEmpty())
            displayText += QStringLiteral(" %1").arg(desc.type.toUpper());
        if (!marks.isEmpty())
            displayText += QStringLiteral(" [%1]").arg(marks.join(" "));
        if (!desc.defaultValue.isEmpty())
            displayText += QStringLiteral(" DEFAULT '%1'").arg(desc.defaultValue);
        if (desc.foreignKey && !desc.referencedTable.isEmpty())
            displayText += QStringLiteral(" -> %1.%2").arg(desc.referencedTable, desc.referencedColumn);

        QTreeWidgetItem *cItem = new QTreeWidgetItem(tItem);
        cItem->setText(0, displayText);
        cItem->setData(0, Qt::UserRole, "column:" + dbName + ":" + tableName + ":" + desc.name);
        cItem->setFont(0, QFont("Consolas", 8));
        cItem->setForeground(0, QColor("#666666"));
    }
    m_treeWidget->setUpdatesEnabled(true);
}

void StructurePanel::addConstraintsToTableItem(QTreeWidgetItem *tItem,
                                               const QString &dbName,
                                               const QString &tableName)
{
    // Removed constraints section as per user request
    // The constraints information is no longer displayed in the tree
}

void StructurePanel::onTreeItemExpanded(QTreeWidgetItem *item)
{
    QString data = item->data(0, Qt::UserRole).toString();

    if (data.startsWith("database:")) {
        QString dbName = data.mid(9);
        // Always reload tables when expanding database to get latest schema
        loadTablesForDatabase(item, dbName);
    } else if (data.startsWith("table:")) {
        QStringList parts = data.mid(6).split(":");
        if (parts.size() == 2) {
            QString dbName = parts[0];
            QString tableName = parts[1];
            // Always reload columns when expanding table to get latest schema
            addColumnsToTableItem(item, dbName, tableName);
        }
    }
}

void StructurePanel::onTreeItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);
    QString data = item->data(0, Qt::UserRole).toString();

    if (data.startsWith("database:")) {
        QString dbName = data.mid(9);
        m_currentDatabase = dbName;
        m_currentTable.clear();
        updateStatusLabel();
        emit databaseSelected(dbName);

    } else if (data.startsWith("table:")) {
        QStringList parts = data.mid(6).split(":");
        if (parts.size() == 2) {
            m_currentDatabase = parts[0];
            m_currentTable = parts[1];
            updateStatusLabel();
            
            // Expand the table node to show columns
            item->setExpanded(true);
            
            // Load columns for the table
            addColumnsToTableItem(item, parts[0], parts[1]);
            
            emit tableSelected(parts[0], parts[1]);
        }

    } else if (data.startsWith("column:")) {
        QStringList parts = data.mid(7).split(":");
        if (parts.size() == 3) {
            m_currentDatabase = parts[0];
            m_currentTable = parts[1];
            updateStatusLabel();
            emit columnSelected(parts[0], parts[1], parts[2]);
        }
    }
}

void StructurePanel::onTreeContextMenuRequested(const QPoint &pos)
{
    QTreeWidgetItem *item = m_treeWidget->itemAt(pos);
    if (item == nullptr) {
        return;
    }

    const QString data = item->data(0, Qt::UserRole).toString();
    QString dbName;
    QString tableName;
    QString columnName;

    if (data.startsWith(QStringLiteral("table:"))) {
        const QStringList parts = data.mid(6).split(QLatin1Char(':'));
        if (parts.size() != 2) {
            return;
        }
        dbName = parts.at(0);
        tableName = parts.at(1);
    } else if (data.startsWith(QStringLiteral("column:"))) {
        const QStringList parts = data.mid(7).split(QLatin1Char(':'));
        if (parts.size() != 3) {
            return;
        }
        dbName = parts.at(0);
        tableName = parts.at(1);
        columnName = parts.at(2);
    } else {
        return;
    }

    QMenu menu(this);
    QAction *editAction = nullptr;
    if (columnName.isEmpty()) {
        editAction = menu.addAction(QStringLiteral("编辑表约束"));
    } else {
        editAction = menu.addAction(QStringLiteral("编辑列属性"));
    }
    QAction *selected = menu.exec(m_treeWidget->viewport()->mapToGlobal(pos));
    if (selected == editAction) {
        m_currentDatabase = dbName;
        m_currentTable = tableName;
        updateStatusLabel();
        if (columnName.isEmpty()) {
            emit editConstraintsRequested(dbName, tableName);
        } else {
            emit editColumnRequested(dbName, tableName, columnName);
        }
    }
}

void StructurePanel::updateStatusLabel()
{
    QString db = m_currentDatabase.isEmpty() ? QStringLiteral("none") : m_currentDatabase;
    QString tbl = m_currentTable.isEmpty() ? QStringLiteral("none") : m_currentTable;
    m_statusLabel->setText(QStringLiteral("Database: %1\nTable: %2").arg(db, tbl));
}

void StructurePanel::refresh()
{
    loadStructure();
}

void StructurePanel::setClientRuntime(client::SqlClientEngine *clientEngine, const QString &clientId)
{
    m_clientEngine = clientEngine;
    m_clientId = clientId;
    loadStructure();
}

QStringList StructurePanel::firstColumnValuesFromSql(const QString &sql) const
{
    QStringList values;
    if (m_clientEngine == nullptr || m_clientId.isEmpty()) {
        return values;
    }

    const service::SqlExecResult result = m_clientEngine->executeSqlPreservingDatabase(m_clientId, sql);
    if (!result.success || !result.selectResult.success) {
        return values;
    }

    for (const QStringList &row : result.selectResult.resultTable.rows) {
        if (!row.isEmpty()) {
            values.append(row.first());
        }
    }
    return values;
}

void StructurePanel::selectDatabase(const QString &dbName)
{
    m_currentDatabase = dbName;
    m_currentTable.clear();
    loadStructure();
}

void StructurePanel::selectTable(const QString &tableName)
{
    m_currentTable = tableName;
    loadStructure();
}

QString StructurePanel::currentDatabase() const
{
    return m_currentDatabase;
}

QString StructurePanel::currentTable() const
{
    return m_currentTable;
}
