#include "cli_app.h"

#include "../constants/cli_client_def.h"

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
        if (argument == QStringLiteral("--user") || argument == QStringLiteral("-u")) {
            options.userName = requireValue(argument);
            if (error != nullptr && !error->isEmpty()) {
                return {};
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
    *m_output << QStringLiteral("  DBMS_CLI [--data-root PATH] [--user NAME]") << Qt::endl;
    *m_output << QStringLiteral("  DBMS_CLI --execute \"SQL;\"") << Qt::endl;
    *m_output << QStringLiteral("REPL commands: .help, quit, exit") << Qt::endl;
}

void CliApp::printResult(const service::SqlExecResult &result)
{
    QTextStream &stream = result.success ? *m_output : *m_errorOutput;
    if (!result.success) {
        stream << QStringLiteral("ERROR: ") << result.errorMessage << Qt::endl;
        return;
    }

    if (!result.text.trimmed().isEmpty()) {
        stream << result.text << Qt::endl;
        return;
    }

    stream << QStringLiteral("OK") << Qt::endl;
}

} // namespace cli
