#pragma once

#include <dragonpixel/metadata/registry.h>

#include <QString>
#include <QStringList>

struct MetadataManifestLoadResult final
{
    bool succeeded{true};
    QStringList diagnostics;
    int manifest_count{};
    int component_count{};
    int object_type_count{};
};

class MetadataManifestService final
{
public:
    [[nodiscard]] MetadataManifestLoadResult load_project(
        const QString& project_root,
        const QStringList& component_roots,
        dragonpixel::metadata::registry& registry) const;
};
