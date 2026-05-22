#ifndef CLI_CLI_APP_H
#define CLI_CLI_APP_H

#include "../client/sql_client_runtime.h"

#include <QTextStream>
#include <QString>
#include <QStringList>

namespace cli {

class CliApp
{
public:
    explicit CliApp(client::SqlClientRuntime *clientRuntime,
           QTextStream *input,
           QTextStream *output,
           QTextStream *errorOutput);

    int run(const QStringList &arguments);

private:
    struct Options
    {
        QString dataRoot;
        QString userName;
        QString password;
        QString executeSql;
        bool hasExecuteSql = false;
        bool promptPassword = false;
        bool showHelp = false;
    };

    Options parseOptions(const QStringList &arguments, QString *error) const;
    bool ensureAuthenticated(const QString &clientId, const Options &options);
    QString promptValue(const QString &label, bool *ok);
    int runExecuteMode(const QString &clientId, const QString &sql);
    int runRepl(const QString &clientId);
    bool isExitCommand(const QString &line) const;
    bool isHelpCommand(const QString &line) const;
    void printHelp();
    void printResult(const service::SqlExecResult &result);

    client::SqlClientRuntime *m_clientRuntime = nullptr;
    QTextStream *m_input = nullptr;
    QTextStream *m_output = nullptr;
    QTextStream *m_errorOutput = nullptr;
};

} // namespace cli

#endif // CLI_CLI_APP_H
