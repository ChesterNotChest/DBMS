#include "cli_app.h"

#include "../constants/cli_client_def.h"

#include <algorithm>

namespace cli {

CliApp::CliApp(client::ClientSessionPool *sessionPool,
               client::SqlClientEngine *clientEngine,
               QTextStream *input,
               QTextStream *output,
               QTextStream *errorOutput)
    : m_sessionPool(sessionPool)
    , m_clientEngine(clientEngine)
    , m_input(input)
    , m_output(output)
    , m_errorOutput(errorOutput)
{
}

int CliApp::run(const QStringList &arguments)
{
    if (m_sessionPool == nullptr || m_clientEngine == nullptr
        || m_input == nullptr || m_output == nullptr || m_errorOutput == nullptr) {
        return 2;
    }

    QString optionError;
    const Options options = parseOptions(arguments, &optionError);
    if (!optionError.isEmpty()) {
        *m_errorOutput << optionError << Qt::endl;
        return 2;
    }
    if (options.showHelp) {
        printHelp();
        return 0;
    }

    const QString clientId = m_sessionPool->createSession(options.dataRoot, options.userName);
    if (!ensureAuthenticated(clientId, options)) {
        return 1;
    }
    return options.hasExecuteSql
               ? runExecuteMode(clientId, options.executeSql)
               : runRepl(clientId);
}

CliApp::Options CliApp::parseOptions(const QStringList &arguments, QString *error) const
{
    if (error != nullptr) {
        error->clear();
    }

    Options options;
    for (int index = 1; index < arguments.size(); ++index) {
        const QString argument = arguments.at(index);
        auto requireValue = [&](const QString &name) -> QString {
            if (index + 1 >= arguments.size()) {
                if (error != nullptr) {
                    *error = QStringLiteral("%1 requires a value").arg(name);
                }
                return {};
            }
            ++index;
            return arguments.at(index);
        };

        if (argument == QStringLiteral("--help") || argument == QStringLiteral("-h")) {
            options.showHelp = true;
            continue;
        }
        if (argument == QStringLiteral("--data-root")) {
            options.dataRoot = requireValue(argument);
            if (error != nullptr && !error->isEmpty()) {
                return {};
            }
            continue;
        }
        if (argument == QStringLiteral("-u")) {
            options.userName = requireValue(argument);
            if (error != nullptr && !error->isEmpty()) {
                return {};
            }
            continue;
        }
        if (argument == QStringLiteral("-p")) {
            if (index + 1 < arguments.size() && !arguments.at(index + 1).startsWith(QLatin1Char('-'))) {
                ++index;
                options.password = arguments.at(index);
            } else {
                options.promptPassword = true;
            }
            continue;
        }
        if (argument == QStringLiteral("--execute") || argument == QStringLiteral("-e")) {
            options.executeSql = requireValue(argument);
            options.hasExecuteSql = true;
            if (error != nullptr && !error->isEmpty()) {
                return {};
            }
            continue;
        }

        if (error != nullptr) {
            *error = QStringLiteral("unknown option '%1'").arg(argument);
        }
        return {};
    }

    return options;
}

bool CliApp::ensureAuthenticated(const QString &clientId, const Options &options)
{
    QString userName = options.userName.trimmed();
    QString password = options.password;
    bool ok = true;

    if (userName.isEmpty()) {
        userName = promptValue(QStringLiteral("Username: "), &ok).trimmed();
        if (!ok) {
            return false;
        }
    }
    if (options.promptPassword || password.isNull()) {
        password = promptValue(QStringLiteral("Password: "), &ok);
        if (!ok) {
            return false;
        }
    }

    const service::SqlExecResult result = m_clientEngine->login(clientId, userName, password);
    if (!result.success) {
        printResult(result);
        return false;
    }
    if (!result.text.trimmed().isEmpty()) {
        *m_output << result.text << Qt::endl;
    }
    return true;
}

QString CliApp::promptValue(const QString &label, bool *ok)
{
    if (ok != nullptr) {
        *ok = true;
    }
    *m_output << label;
    m_output->flush();
    const QString value = m_input->readLine();
    if (value.isNull()) {
        if (ok != nullptr) {
            *ok = false;
        }
        return {};
    }
    return value;
}

int CliApp::runExecuteMode(const QString &clientId, const QString &sql)
{
    const service::SqlExecResult result = m_clientEngine->executeSql(clientId, sql);
    printResult(result);
    return result.success ? 0 : 1;
}

int CliApp::runRepl(const QString &clientId)
{
    QString buffer;
    while (true) {
        *m_output << (buffer.trimmed().isEmpty()
                          ? QString::fromLatin1(cliclient::kDefaultCliPrompt)
                          : QString::fromLatin1(cliclient::kCliContinuationPrompt));
        m_output->flush();

        const QString line = m_input->readLine();
        if (line.isNull()) {
            return 0;
        }

        const QString trimmed = line.trimmed();
        if (buffer.trimmed().isEmpty() && isExitCommand(trimmed)) {
            return 0;
        }
        if (buffer.trimmed().isEmpty() && isHelpCommand(trimmed)) {
            printHelp();
            continue;
        }

        buffer += line;
        buffer += QLatin1Char('\n');
        if (!trimmed.endsWith(QLatin1Char(';'))) {
            continue;
        }

        const service::SqlExecResult result = m_clientEngine->executeSql(clientId, buffer);
        printResult(result);
        buffer.clear();
    }
}

bool CliApp::isExitCommand(const QString &line) const
{
    const QString normalized = line.toLower();
    return normalized == QStringLiteral("quit")
           || normalized == QStringLiteral("exit")
           || normalized == QStringLiteral(".quit")
           || normalized == QStringLiteral(".exit");
}

bool CliApp::isHelpCommand(const QString &line) const
{
    return line.toLower() == QStringLiteral(".help");
}

void CliApp::printHelp()
{
    *m_output << QStringLiteral("DBMS_CLI usage:") << Qt::endl;
    *m_output << QStringLiteral("  DBMS_CLI [--data-root PATH] [-u NAME] [-p [PASSWORD]]") << Qt::endl;
    *m_output << QStringLiteral("  DBMS_CLI -u NAME -p --execute \"SQL;\"") << Qt::endl;
    *m_output << QStringLiteral("REPL commands: .help, quit, exit") << Qt::endl;
}

void CliApp::printResult(const service::SqlExecResult &result)
{
    QTextStream &stream = result.success ? *m_output : *m_errorOutput;
    if (!result.success) {
        stream << QStringLiteral("ERROR: ") << result.errorMessage << Qt::endl;
        return;
    }

    if (result.selectResult.success) {
        stream << formatSelectResult(result.selectResult) << Qt::endl;
        return;
    }

    if (result.commandType == QStringLiteral("DESC_TABLE")) {
        stream << formatDescResult(result.text) << Qt::endl;
        return;
    }

    if (result.commandType == QStringLiteral("SHOW_CREATE_TABLE")) {
        stream << formatShowCreateTableResult(result) << Qt::endl;
        return;
    }

    if (!result.text.trimmed().isEmpty()) {
        stream << result.text << Qt::endl;
        return;
    }

    stream << QStringLiteral("OK") << Qt::endl;
}

QString CliApp::formatTable(const QStringList &headers, const QList<QStringList> &rows) const
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

QString CliApp::formatSelectResult(const service::SelectRowsResult &result) const
{
    QList<QStringList> rows;
    for (const QStringList &row : result.resultTable.rows) {
        rows.append(row);
    }

    QString output = formatTable(result.resultTable.columns, rows);
    if (!output.isEmpty()) {
        output += QLatin1Char('\n');
    }
    output += rowCountText(result.resultTable.rows.size());
    return output;
}

QString CliApp::formatDescResult(const QString &text) const
{
    QList<QStringList> rows;
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        rows.append({line});
    }

    QString output = formatTable({QStringLiteral("Definition")}, rows);
    if (!output.isEmpty()) {
        output += QLatin1Char('\n');
    }
    output += rowCountText(rows.size());
    return output;
}

QString CliApp::formatShowCreateTableResult(const service::SqlExecResult &result) const
{
    const QString tableName = result.payload.value(QStringLiteral("tableName")).toString();
    QString output = formatTable({QStringLiteral("Table"), QStringLiteral("Create Table")},
                                 {{tableName, result.text}});
    if (!output.isEmpty()) {
        output += QLatin1Char('\n');
    }
    output += rowCountText(result.text.trimmed().isEmpty() ? 0 : 1);
    return output;
}

QString CliApp::rowCountText(int rowCount) const
{
    return QStringLiteral("%1 row%2 in set").arg(rowCount).arg(rowCount == 1 ? QString() : QStringLiteral("s"));
}

} // namespace cli
