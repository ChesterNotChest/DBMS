#include "cli_app.h"

#include "../client/remote_sql_client.h"
#include "../constants/cli_client_def.h"
#include "../utils/console_encoding.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {

struct ConnectionOptions
{
    QString host = QString::fromLatin1(cliclient::kDefaultServerHost);
    quint16 port = static_cast<quint16>(cliclient::kDefaultServerPort);
    QStringList cliArguments;
};

ConnectionOptions extractConnectionOptions(const QStringList &arguments, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    ConnectionOptions options;
    if (!arguments.isEmpty()) {
        options.cliArguments.append(arguments.first());
    }

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

        if (argument == QStringLiteral("--host")) {
            options.host = requireValue(argument);
            if (error != nullptr && !error->isEmpty()) {
                return {};
            }
            continue;
        }
        if (argument == QStringLiteral("--port")) {
            const QString rawPort = requireValue(argument);
            if (error != nullptr && !error->isEmpty()) {
                return {};
            }
            bool ok = false;
            const int port = rawPort.toInt(&ok);
            if (!ok || port <= 0 || port > 65535) {
                if (error != nullptr) {
                    *error = QStringLiteral("--port must be 1..65535");
                }
                return {};
            }
            options.port = static_cast<quint16>(port);
            continue;
        }

        options.cliArguments.append(argument);
    }
    return options;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    utils::configureUtf8Console();

    QTextStream input(stdin);
    QTextStream output(stdout);
    QTextStream errorOutput(stderr);
    utils::configureUtf8TextStream(input);
    utils::configureUtf8TextStream(output);
    utils::configureUtf8TextStream(errorOutput);

    QString optionError;
    const ConnectionOptions connection = extractConnectionOptions(app.arguments(), &optionError);
    if (!optionError.isEmpty()) {
        errorOutput << optionError << Qt::endl;
        return 2;
    }

    client::RemoteSqlClient remoteClient(connection.host,
                                         connection.port,
                                         cliclient::kRpcDefaultTimeoutMs);
    cli::CliApp cliApp(&remoteClient, &input, &output, &errorOutput);

    return cliApp.run(connection.cliArguments);
}
