#pragma once

#include "AutomationBroker.h"
#include "AssetPreviewService.h"
#include "AuthoringViewport.h"
#include "EditorModels.h"
#include "PrefabService.h"
#include "WorkerClient.h"

#include <dragonpixel/metadata/registry.h>
#include <dragonpixel/scene/scene.h>

#include <QComboBox>
#include <QDockWidget>
#include <QJsonObject>
#include <QListView>
#include <QMainWindow>
#include <QStandardItemModel>
#include <QTableView>
#include <QTemporaryDir>
#include <QTreeView>

#include <functional>
#include <cstdint>
#include <optional>
#include <vector>

class QAction;
class QCloseEvent;
class QFileSystemWatcher;
class QLineEdit;
class QProcess;

class EditorWindow final : public QMainWindow
{
    Q_OBJECT

public:
    enum class UnsavedDecision
    {
        save,
        discard,
        cancel,
    };

    using UnsavedPrompt = std::function<UnsavedDecision(const QString& scene_name)>;
    using DeletePrompt = std::function<bool(const QString& root_name, int entity_count)>;
    using PrefabPathPrompt = std::function<QString(const QString& suggested_path)>;
    using PrefabLevelPrompt = std::function<int(const QStringList& levels)>;

    explicit EditorWindow(QString initial_document, QWidget* parent = nullptr);

    void start_self_test(const QString& adapter);
    void crash_self_test_worker();
    [[nodiscard]] bool self_test_ready() const;
    [[nodiscard]] bool self_test_recovered() const;
    [[nodiscard]] QString self_test_diagnostics() const;
    [[nodiscard]] const QString& authoring_scene_path() const noexcept { return scene_path_; }

    void set_unsaved_prompt(UnsavedPrompt prompt) { unsaved_prompt_ = std::move(prompt); }
    void set_delete_prompt(DeletePrompt prompt) { delete_prompt_ = std::move(prompt); }
    void set_prefab_path_prompt(PrefabPathPrompt prompt) { prefab_path_prompt_ = std::move(prompt); }
    void set_prefab_level_prompt(PrefabLevelPrompt prompt) { prefab_level_prompt_ = std::move(prompt); }

signals:
    void gizmo_preview_scene_changed(
        const QStringList& ordered_entity_ids,
        const QVector3D& first_position);
    void gizmo_preview_scene_restored();
    void gizmo_transaction_committed(int entity_count);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void build_interface();
    bool load_project(const QString& path);
    bool close_project(bool ask_to_save = true);
    bool load_scene(const QString& path);
    bool commit_scene(
        dragonpixel::scene::scene candidate,
        const QString& path,
        const QString& project_manifest,
        const QString& project_root,
        std::size_t migration_count);
    bool save_scene();
    bool confirm_discard_or_save();
    void rebuild_hierarchy();
    void rebuild_scene_summary();
    void rebuild_assets();
    void schedule_project_refresh();
    void copy_console_selection();
    void export_console_selection();
    void navigate_console_entry(const QModelIndex& proxy_index);
    void inspect_selected_entities();
    bool edit_hierarchy_entity(
        const dragonpixel::core::uuid& id,
        const QString& name,
        bool enabled);
    bool drag_reparent_entity(
        const dragonpixel::core::uuid& id,
        const std::optional<dragonpixel::core::uuid>& parent,
        std::optional<std::size_t> sibling_index);
    void edit_inspector_item(QStandardItem* item);
    void create_preset(dragonpixel::scene::entity_preset preset, const QString& asset_override = {});
    void duplicate_selected();
    void delete_selected_subtree();
    void reparent_entity();
    void add_component();
    void remove_component();
    void undo();
    void redo();
    void assign_selected_asset(const QModelIndex& source_index);
    void instantiate_prefab(const QString& source_path);
    void create_prefab_from_selection();
    void apply_prefab();
    void revert_selected_prefab();
    void revert_all_prefab();
    void repair_prefab();
    void unpack_prefab(bool completely);
    void report_prefab_result(PrefabOperationResult result);
    [[nodiscard]] bool apply_authoring_transaction(
        std::vector<dragonpixel::scene::command> commands,
        std::string description);
    void activate_project_item(const QModelIndex& proxy_index);
    void apply_workspace(const QString& workspace);
    void reset_workspace();
    void after_scene_mutation(
        const QString& message,
        const QList<dragonpixel::core::uuid>& select = {});
    void update_action_states();
    void update_window_title();
    void update_worker_viewport();
    void update_viewport_selection_geometry(const dragonpixel::scene::scene& geometry_scene);
    void begin_gizmo_preview(AuthoringViewport::GizmoTool tool);
    void preview_gizmo_delta(AuthoringViewport::GizmoTool tool, const QVector3D& delta);
    void cancel_gizmo_preview();
    void apply_gizmo_delta(AuthoringViewport::GizmoTool tool, const QVector3D& delta);
    [[nodiscard]] std::vector<dragonpixel::scene::command> gizmo_commands(
        AuthoringViewport::GizmoTool tool,
        const QVector3D& delta) const;
    [[nodiscard]] bool reload_gizmo_preview(const dragonpixel::scene::scene& preview_scene);
    void refresh_preview();
    void start_automation_self_test();
    [[nodiscard]] AutomationResponse handle_automation_request(
        const QString& method,
        const QJsonObject& parameters);
    void start_play();
    void stop_play();
    void append_console(
        const QString& message,
        const QString& severity = QStringLiteral("Info"),
        const QString& subsystem = QStringLiteral("Editor"),
        const QString& context = {},
        const QString& worker = {},
        const QString& session = {},
        const QString& correlation_id = {},
        const QString& entity_id = {},
        const QString& asset_id = {},
        const QString& navigation_path = {});
    [[nodiscard]] bool run_authoring_self_test();
    [[nodiscard]] QList<dragonpixel::core::uuid> selected_entity_ids() const;
    [[nodiscard]] std::optional<dragonpixel::core::uuid> selected_entity_id() const;
    [[nodiscard]] dragonpixel::scene::entity const* selected_entity() const;
    [[nodiscard]] int project_asset_count() const;

    dragonpixel::metadata::registry metadata_;
    std::optional<dragonpixel::scene::scene> scene_;
    QString scene_path_;
    QString project_manifest_path_;
    QString project_root_;
    QTemporaryDir runtime_directory_;
    AuthoringViewport* viewport_{};
    WorkerClient* preview_worker_{};
    WorkerClient* play_worker_{};
    AutomationBroker* automation_broker_{};
    QProcess* automation_test_process_{};
    AssetPreviewService* asset_preview_service_{};
    QFileSystemWatcher* project_watcher_{};
    ProjectIndexBuildResult project_index_;
    quint64 project_generation_{};
    bool project_refresh_pending_{};

    QTreeView* hierarchy_{};
    HierarchyModel* hierarchy_model_{};
    RecursiveFilterProxyModel* hierarchy_filter_{};
    QLineEdit* hierarchy_search_{};
    QTreeView* inspector_{};
    QStandardItemModel* inspector_model_{};
    InspectorDelegate* inspector_delegate_{};
    QTreeView* project_explorer_{};
    ProjectModel* project_model_{};
    ProjectFilterProxyModel* project_filter_{};
    QLineEdit* project_search_{};
    QComboBox* project_type_filter_{};
    QComboBox* project_status_filter_{};
    QListView* scene_summary_{};
    QStandardItemModel* scene_summary_model_{};
    QTableView* console_{};
    ConsoleModel* console_model_{};
    RecursiveFilterProxyModel* console_filter_{};
    QLineEdit* console_search_{};
    QComboBox* adapter_{};
    QDockWidget* scene_dock_{};
    QDockWidget* hierarchy_dock_{};
    QDockWidget* assets_dock_{};
    QDockWidget* inspector_dock_{};
    QDockWidget* console_dock_{};

    QAction* save_action_{};
    QAction* undo_action_{};
    QAction* redo_action_{};
    QAction* duplicate_action_{};
    QAction* delete_action_{};
    QAction* reparent_action_{};
    QAction* add_component_action_{};
    QAction* remove_component_action_{};
    QAction* play_action_{};
    QAction* simulate_action_{};
    QAction* pause_action_{};
    QAction* resume_action_{};
    QAction* stop_action_{};
    QAction* create_prefab_action_{};
    QAction* instantiate_prefab_action_{};
    QAction* apply_prefab_action_{};
    QAction* revert_selected_prefab_action_{};
    QAction* revert_all_prefab_action_{};
    QAction* repair_prefab_action_{};
    QAction* unpack_prefab_action_{};
    QAction* unpack_completely_prefab_action_{};

    UnsavedPrompt unsaved_prompt_;
    DeletePrompt delete_prompt_;
    PrefabPathPrompt prefab_path_prompt_;
    PrefabLevelPrompt prefab_level_prompt_;
    PrefabService prefab_service_;
    struct GizmoTransformSnapshot final
    {
        dragonpixel::core::uuid entity_id;
        nlohmann::ordered_json position;
        nlohmann::ordered_json rotation;
        nlohmann::ordered_json scale;
    };
    std::vector<GizmoTransformSnapshot> gizmo_transform_snapshots_;
    std::optional<dragonpixel::scene::scene> gizmo_preview_scene_;
    std::size_t gizmo_authoritative_history_position_{};
    AuthoringViewport::GizmoTool gizmo_preview_tool_{AuthoringViewport::GizmoTool::move};
    AuthoringViewport::GizmoOrientation gizmo_preview_orientation_{AuthoringViewport::GizmoOrientation::global};
    bool gizmo_preview_active_{};
    bool rebuilding_inspector_{};
    bool play_running_{};
    bool play_paused_{};
    bool preview_simulating_{};
    QJsonObject editor_camera_;
    std::uint64_t camera_revision_{};
    std::uint64_t command_revision_{};
    bool authoring_self_test_passed_{};
    bool automation_self_test_passed_{};
    std::optional<dragonpixel::core::uuid> self_test_entity_id_;
};
