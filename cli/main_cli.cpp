#include "cli_app.h"

#include "../client/client_session_pool.h"
#include "../client/sql_client_engine.h"

#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QTextStream input(stdin);
    QTextStream output(stdout);
    QTextStream errorOutput(stderr);

    client::ClientSessionPool sessionPool;
    client::SqlClientEngine clientEngine(&sessionPool);
    cli::CliApp cliApp(&sessionPool, &clientEngine, &input, &output, &errorOutput);

    return cliApp.run(app.arguments());
}
