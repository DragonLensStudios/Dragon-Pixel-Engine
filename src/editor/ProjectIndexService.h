#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

enum class ProjectIndexDiagnosticSeverity
{
    information,
    warning,
    error,
};

enum class ProjectIndexDiagnosticCode
{
    manifest_not_found,
    unreadable_document,
    invalid_json,
    invalid_project_format,
    unsupported_project_version,
    invalid_schema,
    missing_field,
    invalid_identifier,
    unsafe_path,
    missing_root,
    duplicate_root,
    unsorted_roots,
    unexpected_document_format,
    unsupported_document_version,
    duplicate_identifier,
    missing_startup_scene,
    startup_scene_not_indexed,
    missing_asset_source,
    unsupported_asset_source,
    missing_dependency,
    dependency_cycle,
};

[[nodiscard]] QString project_index_diagnostic_code_name(ProjectIndexDiagnosticCode code);

struct ProjectIndexDiagnostic final
{
    ProjectIndexDiagnosticSeverity severity{ProjectIndexDiagnosticSeverity::error};
    ProjectIndexDiagnosticCode code{ProjectIndexDiagnosticCode::invalid_json};
    QString message;
    QString document_path;
    QString json_pointer;
    QString related_id;
};

enum class ProjectIndexRootKind
{
    scenes,
    assets,
};

struct ProjectIndexRoot final
{
    ProjectIndexRootKind kind{ProjectIndexRootKind::assets};
    QString declared_path;
    QString absolute_path;
};

enum class ProjectIndexEntryKind
{
    scene,
    prefab,
    asset,
};

struct ProjectIndexEntry final
{
    ProjectIndexEntryKind kind{ProjectIndexEntryKind::asset};
    QString id;
    QString display_name;
    QString logical_path;
    QString absolute_path;
    int format_version{};
    QString asset_type;
    QString source;
    QString resolved_source_path;
    QStringList dependencies;
    QJsonObject document;
    bool structurally_valid{true};
};

struct ProjectIndexCandidate final
{
    QString manifest_path;
    QString project_root;
    QString project_id;
    QString name;
    int format_version{};
    QString startup_scene;
    QString startup_scene_path;
    QVector<ProjectIndexRoot> roots;
    QVector<ProjectIndexEntry> entries;
    QHash<QString, qsizetype> entry_indices_by_id;

    [[nodiscard]] const ProjectIndexEntry* find_by_id(const QString& id) const noexcept;
    [[nodiscard]] const ProjectIndexEntry* find_by_path(const QString& absolute_path) const noexcept;
};

struct ProjectIndexBuildResult final
{
    std::optional<ProjectIndexCandidate> candidate;
    QList<ProjectIndexDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] bool succeeded() const noexcept;
};

// Builds a complete, detached candidate. The service only opens project files
// for reading and never owns or mutates the editor's active project/session.
class ProjectIndexService final
{
public:
    [[nodiscard]] ProjectIndexBuildResult build_candidate(const QString& manifest_path) const;
};
