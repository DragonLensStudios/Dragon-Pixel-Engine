#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

struct ScriptEditorRequest final
{
    QString project_root;
    QString selected_source_path;
    QString contracts_assembly_path;
    QStringList component_source_paths;
    QStringList component_manifest_paths;
    QString rider_executable;
    int line{1};
};

struct ScriptEditorInvocation final
{
    QString program;
    QStringList arguments;
    QString working_directory;
};

struct ScriptEditorResult final
{
    bool succeeded{};
    QString error;
    QString rider_executable;
    QString workspace_directory;
    QString solution_path;
    QString project_path;
    QString selected_source_path;
    QVector<ScriptEditorInvocation> invocations;
};

// Owns only the external-editor handoff. It regenerates disposable IDE state
// from already indexed source files and never opens, reflects, builds, or loads
// project code inside Dragon Pixel Editor.
class ScriptEditorService final
{
public:
    using ProcessLauncher = std::function<bool(
        const QString& program,
        const QStringList& arguments,
        const QString& working_directory,
        QString& error)>;

    explicit ScriptEditorService(ProcessLauncher launcher = {});

    [[nodiscard]] ScriptEditorResult open_in_rider(const ScriptEditorRequest& request) const;
    [[nodiscard]] static QString find_rider_executable();
    [[nodiscard]] static bool is_component_source_path(const QString& path);

private:
    ProcessLauncher launcher_;
};
