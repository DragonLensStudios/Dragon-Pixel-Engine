#pragma once

#include <QDir>
#include <QString>

namespace dpe::editor
{
inline constexpr auto maximum_portable_unix_socket_path_bytes = 100;

[[nodiscard]] inline QString local_socket_endpoint(const QString& endpoint_name)
{
#if defined(Q_OS_WIN)
    return endpoint_name;
#else
    // macOS limits sockaddr_un paths to roughly 104 bytes. Its per-user temporary
    // directory is commonly much longer than Linux's, so keep control sockets in
    // the shared sticky temporary directory and use an unguessable endpoint name.
    const auto endpoint = QDir{QStringLiteral("/tmp")}.filePath(
        endpoint_name + QStringLiteral(".sock"));
    return endpoint.toUtf8().size() <= maximum_portable_unix_socket_path_bytes
        ? endpoint
        : QString{};
#endif
}
}
