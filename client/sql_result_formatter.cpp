#include "sql_result_formatter.h"

#include <algorithm>
#include <QVector>

namespace client {

QString rowCountText(int rowCount)
{
    return QStringLiteral("%1 row%2 in set").arg(rowCount).arg(rowCount == 1 ? QString() : QStringLiteral("s"));
}

QString formatResultTable(const QStringList &headers, const QList<QStringList> &rows)
{
    if (headers.isEmpty()) {
        return {};
    }

    QVector<int> widths(headers.size(), 0);
    for (int index = 0; index < headers.size(); ++index) {
        widths[index] = headers.at(index).size();
    }

    QList<QList<QStringList>> expandedRows;
    for (const QStringList &row : rows) {
        QList<QStringList> expandedRow;
        for (int index = 0; index < headers.size(); ++index) {
            const QStringList cellLines = row.value(index).split(QLatin1Char('\n'), Qt::KeepEmptyParts);
            expandedRow.append(cellLines);
            for (const QString &cellLine : cellLines) {
                widths[index] = std::max(widths[index], static_cast<int>(cellLine.size()));
            }
        }
        expandedRows.append(expandedRow);
    }

    auto border = [&]() {
        QString line;
        for (int width : widths) {
            line += QLatin1Char('+');
            line += QString(width + 2, QLatin1Char('-'));
        }
        line += QLatin1Char('+');
        return line;
    };

    auto lineText = [&](const QStringList &values) {
        QString line;
        for (int index = 0; index < headers.size(); ++index) {
            const QString value = values.value(index);
            line += QStringLiteral("| ");
            line += value;
            line += QString(widths.at(index) - value.size(), QLatin1Char(' '));
            line += QLatin1Char(' ');
        }
        line += QLatin1Char('|');
        return line;
    };

    QStringList lines;
    lines << border();
    lines << lineText(headers);
    lines << border();
    for (const QList<QStringList> &row : expandedRows) {
        int rowHeight = 1;
        for (const QStringList &cellLines : row) {
            rowHeight = std::max(rowHeight, static_cast<int>(cellLines.size()));
        }
        for (int lineIndex = 0; lineIndex < rowHeight; ++lineIndex) {
            QStringList values;
            for (int columnIndex = 0; columnIndex < headers.size(); ++columnIndex) {
                values.append(row.value(columnIndex).value(lineIndex));
            }
            lines << lineText(values);
        }
    }
    lines << border();
    return lines.join(QLatin1Char('\n'));
}

QString formatSelectResult(const service::SelectRowsResult &result)
{
    QList<QStringList> rows;
    for (const QStringList &row : result.resultTable.rows) {
        rows.append(row);
    }

    QString output = formatResultTable(result.resultTable.columns, rows);
    if (!output.isEmpty()) {
        output += QLatin1Char('\n');
    }
    output += rowCountText(result.resultTable.rows.size());
    return output;
}

QString formatDescResult(const QString &text)
{
    QList<QStringList> rows;
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        rows.append({line});
    }

    QString output = formatResultTable({QStringLiteral("Definition")}, rows);
    if (!output.isEmpty()) {
        output += QLatin1Char('\n');
    }
    output += rowCountText(rows.size());
    return output;
}

QString formatShowCreateTableResult(const service::SqlExecResult &result)
{
    const QString tableName = result.payload.value(QStringLiteral("tableName")).toString();
    QString output = formatResultTable({QStringLiteral("Table"), QStringLiteral("Create Table")},
                                       {{tableName, result.text}});
    if (!output.isEmpty()) {
        output += QLatin1Char('\n');
    }
    output += rowCountText(result.text.trimmed().isEmpty() ? 0 : 1);
    return output;
}

QString formatSqlExecResultForText(const service::SqlExecResult &result)
{
    if (!result.success) {
        return QStringLiteral("ERROR: ") + result.errorMessage;
    }

    if (result.selectResult.success) {
        return formatSelectResult(result.selectResult);
    }

    if (result.commandType == QStringLiteral("DESC_TABLE")) {
        return formatDescResult(result.text);
    }

    if (result.commandType == QStringLiteral("SHOW_CREATE_TABLE")) {
        return formatShowCreateTableResult(result);
    }

    if (!result.text.trimmed().isEmpty()) {
        return result.text;
    }

    return QStringLiteral("OK");
}

} // namespace client
