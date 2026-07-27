#pragma once

#include <QByteArray>
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
    components,
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
    component_source,
    component_manifest,
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
    QString source_ownership;
    QString resolved_source_path;
    QStringList dependencies;
    QJsonObject document;
    bool structurally_valid{true};
};

struct ProjectIndexMigration final
{
    int source_format_version{};
    int target_format_version{};
    QString summary;
};

struct ProjectIndexCandidate final
{
    QString manifest_path;
    QString project_root;
    QString project_id;
    QString name;
    // The on-disk version and the effective in-memory version are distinct so
    // candidate inspection can apply deterministic migrations without writing.
    int source_format_version{};
    int format_version{};
    QString startup_scene;
    QString startup_scene_path;
    // The effective manifest preserves source extensions and is canonicalized
    // for a later explicit staged migration owner.
    QJsonObject manifest;
    QVector<ProjectIndexMigration> migrations;
    QVector<ProjectIndexRoot> roots;
    QStringList discovered_folders;
    QVector<ProjectIndexEntry> entries;
    QHash<QString, qsizetype> entry_indices_by_id;

    [[nodiscard]] const ProjectIndexEntry* find_by_id(const QString& id) const noexcept;
    [[nodiscard]] const ProjectIndexEntry* find_by_path(const QString& absolute_path) const noexcept;
    [[nodiscard]] QByteArray canonical_manifest_json() const;
};

struct ProjectIndexBuildResult final
{
    std::optional<ProjectIndexCandidate> candidate;
    QList<ProjectIndexDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] bool succeeded() const noexcept;
};

struct ProjectIndexLocation final
{
    QString manifest_path;
    QString project_root;
};

struct ProjectIndexLocationValidation final
{
    std::optional<ProjectIndexLocation> location;
    QList<ProjectIndexDiagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept;
};

// Builds a complete, detached candidate. The service only opens project files
// for reading and never owns or mutates the editor's active project/session.
class ProjectIndexService final
{
public:
    // Validates the originally selected manifest spelling without following a
    // link/reparse-point project root or manifest. Callers that may perform
    // recovery writes must use these validated canonical outputs.
    [[nodiscard]] ProjectIndexLocationValidation validate_location(
        const QString& manifest_path) const;
    [[nodiscard]] ProjectIndexBuildResult build_candidate(const QString& manifest_path) const;
};
