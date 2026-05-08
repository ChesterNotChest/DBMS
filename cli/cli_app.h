#ifndef CLI_CLI_APP_H
#define CLI_CLI_APP_H

#include "../client/client_session_pool.h"
#include "../client/sql_client_engine.h"

#include <QTextStream>
#include <QString>
#include <QStringList>

namespace cli {

class CliApp
{
public:
    CliApp(client::ClientSessionPool *sessionPool,
           client::SqlClientEngine *clientEngine,
           QTextStream *input,
           QTextStream *output,
           QTextStream *errorOutput);

    int run(const QStringList &arguments);

private:
    struct Options
    {
        QString dataRoot;
        QString userName;
        QString executeSql;
        bool hasExecuteSql = false;
        bool showHelp = false;
    };

    Options parseOptions(const QStringList &arguments, QString *error) const;
    int runExecuteMode(const QString &clientId, const QString &sql);
    int runRepl(const QString &clientId);
    bool isExitCommand(const QString &line) const;
    bool isHelpCommand(const QString &line) const;
    void printHelp();
    void printResult(const service::SqlExecResult &result);

    client::ClientSessionPool *m_sessionPool = nullptr;
    client::SqlClientEngine *m_clientEngine = nullptr;
    QTextStream *m_input = nullptr;
    QTextStream *m_output = nullptr;
    QTextStream *m_errorOutput = nullptr;
};

} // namespace cli

#endif // CLI_CLI_APP_H
