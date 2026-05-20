#include "sql_server.h"

#include "../constants/cli_client_def.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {

struct ServerOptions
{
    QString host = QString::fromLatin1(cliclient::kDefaultServerHost);
    quint16 port = static_cast<quint16>(cliclient::kDefaultServerPort);
    bool showHelp = false;
};

ServerOptions parseOptions(const QStringList &arguments, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    ServerOptions options;
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
            if (!ok || port < 0 || port > 65535) {
                if (error != nullptr) {
                    *error = QStringLiteral("--port must be 0..65535");
                }
                return {};
            }
            options.port = static_cast<quint16>(port);
            continue;
        }

        if (error != nullptr) {
            *error = QStringLiteral("unknown option '%1'").arg(argument);
        }
        return {};
    }
    return options;
}

void printHelp(QTextStream &out)
{
    out << QStringLiteral("DBMS_SERVER usage:") << Qt::endl;
    out << QStringLiteral("  DBMS_SERVER [--host HOST] [--port PORT]") << Qt::endl;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    QString optionError;
    const ServerOptions options = parseOptions(app.arguments(), &optionError);
    if (!optionError.isEmpty()) {
        err << optionError << Qt::endl;
        return 2;
    }
    if (options.showHelp) {
        printHelp(out);
        return 0;
    }

    server::DbmsServer dbmsServer;
    QString error;
    if (!dbmsServer.start(options.host, options.port, &error)) {
        err << QStringLiteral("failed to start DBMS server: ") << error << Qt::endl;
        return 1;
    }
    out << QStringLiteral("DBMS_SERVER listening on %1:%2")
               .arg(options.host)
               .arg(dbmsServer.serverPort())
        << Qt::endl;
    out.flush();
    return app.exec();
}
