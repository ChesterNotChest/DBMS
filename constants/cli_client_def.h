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
inline constexpr const char *kGuiDefaultClientName = "gui-root-client";
inline constexpr bool kEnableGuiAutoRootLogin = true;
inline constexpr const char *kDefaultServerHost = "127.0.0.1";
inline constexpr int kDefaultServerPort = 54545;
inline constexpr int kRpcProtocolVersion = 1;
inline constexpr int kRpcMaxFrameBytes = 16 * 1024 * 1024;
inline constexpr int kRpcDefaultTimeoutMs = 10000;

} // namespace cliclient

#endif // CONSTANTS_CLI_CLIENT_DEF_H
