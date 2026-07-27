#pragma once

#include <QString>

namespace dragonpixel::editor::runtime_paths
{
QString file(
    const char* environment_name,
    const QString& bundled_relative_path,
    const QString& development_fallback);

QString directory(
    const char* environment_name,
    const QString& bundled_relative_path,
    const QString& development_fallback);

QString executable(
    const char* environment_name,
    const QString& development_fallback);
}
