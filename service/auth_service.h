#ifndef SERVICE_AUTH_SERVICE_H
#define SERVICE_AUTH_SERVICE_H

#include "service.h"

#include <QString>

namespace service::auth_service {

struct AuthResult
{
    bool success = false;
    QString errorMessage;
    QString userName;
};

TaskResult initializeAuthStore(const QString &dataRoot);

AuthResult authenticate(const QString &userName,
                        const QString &password,
                        const QString &dataRoot);

TaskResult createUser(const QString &requestUser,
                      const QString &newUserName,
                      const QString &password,
                      const QString &dataRoot);

TaskResult dropUser(const QString &requestUser,
                    const QString &targetUserName,
                    const QString &dataRoot);

TaskResult alterUserPassword(const QString &requestUser,
                             const QString &targetUserName,
                             const QString &password,
                             const QString &dataRoot);

TaskResult grantDatabaseAll(const QString &requestUser,
                            const QString &targetUserName,
                            const QString &databaseName,
                            const QString &dataRoot);

TaskResult revokeDatabaseAll(const QString &requestUser,
                             const QString &targetUserName,
                             const QString &databaseName,
                             const QString &dataRoot);

TaskResult authorize(const QString &userName,
                     const QString &commandType,
                     const QString &targetDatabase,
                     const QString &dataRoot);

bool userHasDatabasePrivilege(const QString &userName,
                              const QString &databaseName,
                              const QString &dataRoot,
                              QString *error = nullptr);

} // namespace service::auth_service

#endif // SERVICE_AUTH_SERVICE_H
