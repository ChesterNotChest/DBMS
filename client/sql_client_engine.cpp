#include "sql_client_engine.h"

#include "../constants/cli_client_def.h"
#include "../service/auth_service.h"
#include "../service/service.h"

namespace client {

namespace {

bool isRootUser(const QString &userName)
{
    return userName.trimmed() == QString::fromLatin1(cliclient::kRootUserName);
}

void hideAuthDatabaseFromResult(service::SqlExecResult *result)
{
    if (result == nullptr || !result->selectResult.success) {
        return;
    }

    repo::TableData &table = result->selectResult.resultTable;
    const int databaseColumn = table.columns.indexOf(QStringLiteral("database_name"));
    if (databaseColumn < 0) {
        return;
    }

    const QString authDatabase = QString::fromLatin1(cliclient::kAuthDatabaseName);
    for (int rowIndex = table.rows.size() - 1; rowIndex >= 0; --rowIndex) {
        const QStringList &row = table.rows.at(rowIndex);
        if (databaseColumn < row.size() && row.at(databaseColumn) == authDatabase) {
            table.rows.removeAt(rowIndex);
        }
    }
    result->selectResult.affectedRowCount = table.rows.size();
}

QString formatSelectText(const service::SelectRowsResult &result)
{
    QString output;
    const repo::TableData &table = result.resultTable;
    for (const QString &column : table.columns) {
        output += column + QStringLiteral("\t");
    }
    output += QStringLiteral("\n");
    for (const QStringList &row : table.rows) {
        for (const QString &value : row) {
            output += value + QStringLiteral("\t");
        }
        output += QStringLiteral("\n");
    }
    return output;
}

} // namespace

ScopedServiceContext::ScopedServiceContext(ClientSession *session)
    : m_session(session)
    , m_previousDataRoot(service::getDataRoot())
    , m_previousDatabase(service::currentDatabase)
    , m_previousUser(service::currentUser)
{
    if (m_session == nullptr) {
        return;
    }

    service::setDataRoot(m_session->dataRoot);
    service::currentDatabase = m_session->currentDatabase;
    service::currentUser = m_session->userName;
}

ScopedServiceContext::~ScopedServiceContext()
{
    if (m_session != nullptr) {
        m_session->dataRoot = service::getDataRoot();
        m_session->currentDatabase = service::currentDatabase;
        m_session->userName = service::currentUser;
    }

    service::setDataRoot(m_previousDataRoot);
    service::currentDatabase = m_previousDatabase;
    service::currentUser = m_previousUser;
}

SqlClientEngine::SqlClientEngine(ClientSessionPool *sessionPool)
    : m_sessionPool(sessionPool)
{
}

QString SqlClientEngine::createSession(const QString &dataRoot, const QString &userName)
{
    if (m_sessionPool == nullptr) {
        return {};
    }
    return m_sessionPool->createSession(dataRoot, userName);
}

bool SqlClientEngine::closeSession(const QString &clientId)
{
    return m_sessionPool != nullptr && m_sessionPool->closeSession(clientId);
}

int SqlClientEngine::sessionCount() const
{
    return m_sessionPool == nullptr ? 0 : m_sessionPool->sessionCount();
}

service::SqlExecResult SqlClientEngine::login(const QString &clientId,
                                              const QString &userName,
                                              const QString &password)
{
    if (m_sessionPool == nullptr) {
        return {false, QStringLiteral("client session pool is not available")};
    }

    QString error;
    ClientSession *clientSession = m_sessionPool->session(clientId, &error);
    if (clientSession == nullptr) {
        return {false, error};
    }

    ScopedServiceContext context(clientSession);
    const service::TaskResult initResult = service::auth_service::initializeAuthStore(clientSession->dataRoot);
    if (!initResult.success) {
        return {false, initResult.errorMessage, initResult.errorMessage};
    }

    const service::auth_service::AuthResult result =
        service::auth_service::authenticate(userName, password, clientSession->dataRoot);
    if (!result.success) {
        return {false, result.errorMessage, result.errorMessage};
    }

    clientSession->userName = result.userName;
    clientSession->authenticated = true;
    service::currentUser = result.userName;
    return {true, {}, QStringLiteral("Logged in as '%1'").arg(result.userName), 0};
}

service::SqlExecResult SqlClientEngine::executeSql(const QString &clientId, const QString &sql)
{
    if (m_sessionPool == nullptr) {
        return {false, QStringLiteral("client session pool is not available")};
    }

    QString error;
    ClientSession *clientSession = m_sessionPool->session(clientId, &error);
    if (clientSession == nullptr) {
        return {false, error};
    }

    ScopedServiceContext context(clientSession);
    service::SqlDispatcher dispatcher;
    const QStringList statements = service::SqlDispatcher::splitStatements(sql);
    if (statements.isEmpty()) {
        return {false, QStringLiteral("SQL is empty")};
    }

    service::SqlExecResult lastResult;
    for (const QString &statement : statements) {
        const sqlparser::ParseResult parsed = sqlparser::parseSql(statement);
        lastResult = executeParsedStatement(clientSession, &dispatcher, parsed);
        if (!lastResult.success) {
            return lastResult;
        }
    }
    return lastResult;
}

service::SqlExecResult SqlClientEngine::executeSqlPreservingDatabase(const QString &clientId, const QString &sql)
{
    if (m_sessionPool == nullptr) {
        return {false, QStringLiteral("client session pool is not available")};
    }

    QString error;
    ClientSession *clientSession = m_sessionPool->session(clientId, &error);
    if (clientSession == nullptr) {
        return {false, error};
    }

    const QString previousDatabase = clientSession->currentDatabase;
    service::SqlExecResult result = executeSql(clientId, sql);
    clientSession->currentDatabase = previousDatabase;
    return result;
}

service::SqlExecResult SqlClientEngine::executeParsedStatement(ClientSession *clientSession,
                                                               service::SqlDispatcher *dispatcher,
                                                               const sqlparser::ParseResult &parsed)
{
    if (clientSession == nullptr || dispatcher == nullptr) {
        return {false, QStringLiteral("client session is not available")};
    }
    if (!parsed.success) {
        return {false, parsed.errorMessage, parsed.errorMessage, -1, {}, parsed.commandType, parsed.payload};
    }

    const service::TaskResult initResult = service::auth_service::initializeAuthStore(clientSession->dataRoot);
    if (!initResult.success) {
        return {false, initResult.errorMessage, initResult.errorMessage, -1, {}, parsed.commandType, parsed.payload};
    }

    if (parsed.commandType == QStringLiteral("LOGIN")) {
        return executeLogin(clientSession, parsed);
    }

    if (!clientSession->authenticated) {
        return {false,
                QStringLiteral("authentication required: use LOGIN user IDENTIFIED BY password"),
                QStringLiteral("authentication required"),
                -1,
                {},
                parsed.commandType,
                parsed.payload};
    }

    const service::SqlExecResult authResult = authorizeStatement(*clientSession, parsed);
    if (!authResult.success) {
        return authResult;
    }

    service::SqlExecResult result = dispatcher->dispatch(parsed);
    if (parsed.commandType == QStringLiteral("SHOW_DATABASES") && !isRootUser(clientSession->userName)) {
        hideAuthDatabaseFromResult(&result);
        if (result.success && result.selectResult.success) {
            result.text = formatSelectText(result.selectResult);
        }
    }
    return result;
}

service::SqlExecResult SqlClientEngine::executeLogin(ClientSession *clientSession,
                                                     const sqlparser::ParseResult &parsed)
{
    const QString userName = parsed.payload.value(QStringLiteral("userName")).toString();
    const QString password = parsed.payload.value(QStringLiteral("password")).toString();
    const service::auth_service::AuthResult result =
        service::auth_service::authenticate(userName, password, clientSession->dataRoot);
    if (!result.success) {
        return {false, result.errorMessage, result.errorMessage, -1, {}, parsed.commandType, parsed.payload};
    }

    clientSession->userName = result.userName;
    clientSession->authenticated = true;
    service::currentUser = result.userName;
    return {true,
            {},
            QStringLiteral("Logged in as '%1'").arg(result.userName),
            0,
            {},
            parsed.commandType,
            parsed.payload};
}

service::SqlExecResult SqlClientEngine::authorizeStatement(const ClientSession &clientSession,
                                                           const sqlparser::ParseResult &parsed) const
{
    const QString targetDatabase = targetDatabaseForStatement(clientSession, parsed);
    const QString targetTable = targetTableForStatement(clientSession, parsed);
    const service::TaskResult result = service::auth_service::authorize(clientSession.userName,
                                                                        parsed.commandType,
                                                                        targetDatabase,
                                                                        targetTable,
                                                                        clientSession.dataRoot);
    if (result.success) {
        return {true, {}, {}, result.affectedRowCount, {}, parsed.commandType, parsed.payload};
    }
    return {false, result.errorMessage, result.errorMessage, -1, {}, parsed.commandType, parsed.payload};
}

QString SqlClientEngine::targetDatabaseForStatement(const ClientSession &clientSession,
                                                    const sqlparser::ParseResult &parsed) const
{
    if (parsed.payload.contains(QStringLiteral("databaseName"))) {
        return parsed.payload.value(QStringLiteral("databaseName")).toString();
    }
    return clientSession.currentDatabase;
}

QString SqlClientEngine::targetTableForStatement(const ClientSession &clientSession,
                                                 const sqlparser::ParseResult &parsed) const
{
    if (parsed.payload.contains(QStringLiteral("tableName"))) {
        return parsed.payload.value(QStringLiteral("tableName")).toString();
    }
    return QString();
}

} // namespace client
