#include "EditorRuntimePaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace
{
QString environment_value(const char* name)
{
    return QProcessEnvironment::systemEnvironment()
        .value(QString::fromLatin1(name)).trimmed();
}

QString bundled_path(const QString& relative_path)
{
    return QDir::cleanPath(
        QDir{QCoreApplication::applicationDirPath()}.filePath(relative_path));
}
}

namespace dragonpixel::editor::runtime_paths
{
QString file(
    const char* environment_name,
    const QString& bundled_relative_path,
    const QString& development_fallback)
{
    const auto configured = environment_value(environment_name);
    if (!configured.isEmpty()) return configured;

    const auto bundled = bundled_path(bundled_relative_path);
    if (QFileInfo{bundled}.isFile()) return bundled;
    return development_fallback;
}

QString directory(
    const char* environment_name,
    const QString& bundled_relative_path,
    const QString& development_fallback)
{
    const auto configured = environment_value(environment_name);
    if (!configured.isEmpty()) return configured;

    const auto bundled = bundled_path(bundled_relative_path);
    if (QFileInfo{bundled}.isDir()) return bundled;
    return development_fallback;
}

QString executable(
    const char* environment_name,
    const QString& development_fallback)
{
    const auto configured = environment_value(environment_name);
    if (!configured.isEmpty()) return configured;
    if (QFileInfo{development_fallback}.isFile()) return development_fallback;

    const auto executable_name = QFileInfo{development_fallback}.fileName();
    const auto discovered = QStandardPaths::findExecutable(executable_name);
    return discovered.isEmpty() ? development_fallback : discovered;
}
}
