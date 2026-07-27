#pragma once

#include <QString>
#include <QStringList>

enum class ProjectComponentLanguage
{
    csharp,
    cpp,
};

enum class ComponentCreationFault
{
    none,
    stage_source_write,
    stage_native_header_write,
    stage_manifest_write,
    stage_build_definition_write,
    staged_validation,
    commit_source,
    commit_native_header,
    commit_manifest,
    commit_build_definition,
};

struct ComponentCreationRequest final
{
    QString project_root;
    QStringList component_roots;
    QString display_name;
    QString category{QStringLiteral("Scripts")};
    ProjectComponentLanguage language{ProjectComponentLanguage::csharp};
    ComponentCreationFault injected_fault{ComponentCreationFault::none};
    QString selected_component_root;
};

struct ComponentCreationResult final
{
    bool succeeded{};
    QString error;
    QString type_id;
    QString module_id;
    QString source_path;
    QString manifest_path;
};

struct ComponentBuildResult final
{
    bool succeeded{};
    bool reused_cache{};
    QString error;
    QStringList diagnostics;
    QString build_hash;
    QString cache_directory;
    QString runtime_manifest_path;
    int managed_component_count{};
    int native_component_count{};
};

class ComponentModuleService final
{
public:
    [[nodiscard]] static ComponentCreationResult create(const ComponentCreationRequest& request);
    [[nodiscard]] static ComponentBuildResult build(
        const QString& project_root,
        const QStringList& component_roots,
        const QString& contracts_assembly);
    [[nodiscard]] static QString active_runtime_manifest(
        const QString& project_root,
        const QStringList& component_roots,
        const QString& contracts_assembly);
};
