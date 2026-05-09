#ifndef CONSTANTS_CLI_CLIENT_DEF_H
#define CONSTANTS_CLI_CLIENT_DEF_H

namespace cliclient {

inline constexpr const char *kDefaultCliPrompt = "dbms> ";
inline constexpr const char *kCliContinuationPrompt = "   -> ";
inline constexpr const char *kDefaultAnonymousUser = "anonymous";
inline constexpr const char *kRootUserName = "root";
inline constexpr const char *kRootInitialPassword = "";
inline constexpr const char *kAuthDatabaseName = "__dbms_auth";
inline constexpr const char *kUserTableName = "sys_users";
inline constexpr const char *kPrivilegeTableName = "sys_database_privileges";

} // namespace cliclient

#endif // CONSTANTS_CLI_CLIENT_DEF_H
