#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <functional>
#include <memory>

enum class ProjectTemplateKind
{
    two_d,
    three_d,
};

struct ProjectTemplateDescriptor final
{
    QString template_id;
    int template_version{};
    QString name;
    ProjectTemplateKind kind{ProjectTemplateKind::two_d};
    QString engine_range;
    QString manifest_path;
    QJsonObject manifest;
};

struct ProjectLifecycleDiagnostic final
{
    QString code;
    QString message;
    QString path;
};

struct ProjectCreationRequest final
{
    QString template_manifest_path;
    QString project_name;
    QString destination_path;
};

struct CleanSceneRequest final
{
    QString project_manifest_path;
    QString scene_name;
    QString relative_scene_path;
    ProjectTemplateKind kind{ProjectTemplateKind::two_d};
};

struct ProjectLifecycleResult final
{
    bool succeeded{};
    bool cancelled{};
    QString operation_id;
    QString project_manifest_path;
    QString scene_path;
    QString staging_path;
    QStringList created_paths;
    QVector<ProjectLifecycleDiagnostic> diagnostics;
};

enum class ProjectLifecycleFault
{
    none,
    after_staging,
    after_generation,
    before_commit,
    before_scene_commit,
};

class ProjectLifecycleCancellation final
{
public:
    void cancel() noexcept { cancelled_.store(true); }
    [[nodiscard]] bool is_cancelled() const noexcept { return cancelled_.load(); }

private:
    std::atomic_bool cancelled_{};
};

// Public editor boundary for project/template discovery and authoritative
// project/scene creation. Implementations own staging and commit; widgets never
// write project files directly.
class IProjectLifecycleService
{
public:
    virtual ~IProjectLifecycleService() = default;

    [[nodiscard]] virtual QVector<ProjectTemplateDescriptor> discover_templates(
        const QString& templates_root,
        QVector<ProjectLifecycleDiagnostic>* diagnostics = nullptr) const = 0;
    [[nodiscard]] virtual ProjectLifecycleResult dry_run(
        const ProjectCreationRequest& request) const = 0;
    [[nodiscard]] virtual ProjectLifecycleResult create_project(
        const ProjectCreationRequest& request,
        const std::shared_ptr<ProjectLifecycleCancellation>& cancellation = {},
        ProjectLifecycleFault fault = ProjectLifecycleFault::none) const = 0;
    [[nodiscard]] virtual ProjectLifecycleResult create_clean_scene(
        const CleanSceneRequest& request,
        const std::shared_ptr<ProjectLifecycleCancellation>& cancellation = {},
        ProjectLifecycleFault fault = ProjectLifecycleFault::none) const = 0;
    [[nodiscard]] virtual QStringList recoverable_staging_paths(
        const QString& destination_parent) const = 0;
    [[nodiscard]] virtual bool discard_recoverable_staging(
        const QString& staging_path,
        ProjectLifecycleDiagnostic* diagnostic = nullptr) const = 0;
    [[nodiscard]] virtual QStringList recent_projects() const = 0;
    virtual void record_recent_project(const QString& manifest_path) const = 0;
};

class ProjectLifecycleService final : public IProjectLifecycleService
{
public:
    using IdProvider = std::function<QString()>;

    explicit ProjectLifecycleService(
        QString recent_projects_store = {},
        IdProvider id_provider = {});

    [[nodiscard]] QVector<ProjectTemplateDescriptor> discover_templates(
        const QString& templates_root,
        QVector<ProjectLifecycleDiagnostic>* diagnostics = nullptr) const override;
    [[nodiscard]] ProjectLifecycleResult dry_run(
        const ProjectCreationRequest& request) const override;
    [[nodiscard]] ProjectLifecycleResult create_project(
        const ProjectCreationRequest& request,
        const std::shared_ptr<ProjectLifecycleCancellation>& cancellation = {},
        ProjectLifecycleFault fault = ProjectLifecycleFault::none) const override;
    [[nodiscard]] ProjectLifecycleResult create_clean_scene(
        const CleanSceneRequest& request,
        const std::shared_ptr<ProjectLifecycleCancellation>& cancellation = {},
        ProjectLifecycleFault fault = ProjectLifecycleFault::none) const override;
    [[nodiscard]] QStringList recoverable_staging_paths(
        const QString& destination_parent) const override;
    [[nodiscard]] bool discard_recoverable_staging(
        const QString& staging_path,
        ProjectLifecycleDiagnostic* diagnostic = nullptr) const override;
    [[nodiscard]] QStringList recent_projects() const override;
    void record_recent_project(const QString& manifest_path) const override;

private:
    [[nodiscard]] QString next_id() const;

    QString recent_projects_store_;
    IdProvider id_provider_;
};
