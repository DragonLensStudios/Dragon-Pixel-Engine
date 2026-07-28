#pragma once

#include "AutomationBroker.h"
#include "AssetService.h"
#include "ComponentModuleService.h"
#include "AssetPreviewService.h"
#include "AuthoringViewport.h"
#include "EditorModels.h"
#include "GameViewport.h"
#include "InputMapService.h"
#include "MetadataManifestService.h"
#include "PrefabService.h"
#include "ProjectLifecycleService.h"
#include "ScriptEditorService.h"
#include "SelectionService.h"
#include "TileDocumentService.h"
#include "TileImportService.h"
#include "TilePaletteWidget.h"
#include "TileSetWizard.h"
#include "WorkerClient.h"

#include <dragonpixel/metadata/registry.h>
#include <dragonpixel/scene/scene.h>
#include <dragonpixel/serialization/atomic_file.h>

#include <QComboBox>
#include <QDockWidget>
#include <QJsonObject>
#include <QListView>
#include <QMainWindow>
#include <QMatrix4x4>
#include <QStandardItemModel>
#include <QTableView>
#include <QTemporaryDir>
#include <QTreeView>

#include <functional>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class QAction;
class QCheckBox;
class QCloseEvent;
class QFileSystemWatcher;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QMimeData;
class QProcess;
class QPushButton;
class QStackedWidget;
class QTimer;
class QToolButton;
class EditorInteractionTests;

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
    using ComponentNamePrompt = std::function<std::optional<QString>(ProjectComponentLanguage language)>;
    using TilemapNamePrompt = std::function<std::optional<QString>(const QString& suggested_name)>;

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
    void set_component_name_prompt(ComponentNamePrompt prompt) { component_name_prompt_ = std::move(prompt); }
    void set_tilemap_name_prompt(TilemapNamePrompt prompt) { tilemap_name_prompt_ = std::move(prompt); }

signals:
    void gizmo_preview_scene_changed(
        const QStringList& ordered_entity_ids,
        const QVector3D& first_position);
    void gizmo_preview_scene_restored();
    void gizmo_transaction_committed(int entity_count);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    friend class EditorInteractionTests;

    void build_interface();
    void show_project_hub();
    void rebuild_project_hub();
    void open_project_dialog();
    void create_project_dialog();
    void create_clean_scene_dialog();
    bool save_scene_as();
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
    void update_project_browser_folder(const QModelIndex& folder_index);
    void update_project_details(const QModelIndex& proxy_index);
    [[nodiscard]] QString current_project_folder_relative() const;
    void import_asset_paths(const QStringList& paths);
    bool handle_project_browser_drop(const QMimeData* data, const QModelIndex& destination_source);
    void show_project_browser_context_menu(const QPoint& point);
    void apply_project_index(ProjectIndexBuildResult candidate);
    void apply_component_module_manifest(QString manifest);
    void schedule_project_refresh();
    void copy_console_selection();
    void export_console_selection();
    void navigate_console_entry(const QModelIndex& proxy_index);
    void inspect_selected_entities();
    void publish_global_selection(SelectionOrigin origin);
    void update_global_selection_presentation();
    [[nodiscard]] QList<dragonpixel::core::uuid> primary_inspector_targets() const;
    void set_primary_inspector_locked(bool locked);
    void create_additional_inspector();
    void refresh_additional_inspectors();
    void clear_inspector_locks();
    bool edit_hierarchy_entity(
        const dragonpixel::core::uuid& id,
        const QString& name,
        bool enabled);
    bool drag_reparent_entities(
        const std::vector<dragonpixel::core::uuid>& ids,
        const std::optional<dragonpixel::core::uuid>& parent,
        std::optional<std::size_t> sibling_index);
    void edit_inspector_item(QStandardItem* item);
    std::optional<dragonpixel::core::uuid> create_preset(
        dragonpixel::scene::entity_preset preset,
        const QString& asset_override = {},
        bool force_scene_root = false,
        const std::optional<dragonpixel::core::uuid>& explicit_parent = {});
    void duplicate_selected();
    void delete_selected_subtree();
    void reparent_entity();
    void group_selected();
    void add_component();
    void add_component_to_targets(const QList<dragonpixel::core::uuid>& targets);
    void remove_component();
    void show_inspector_context_menu(const QPoint& point);
    void show_inspector_context_menu_for(QTreeView* view, const QPoint& point);
    bool assign_inspector_asset_drop(
        QTreeView* view,
        const QModelIndex& index,
        const QMimeData* mime);
    [[nodiscard]] bool show_collection_context_menu(const QModelIndex& index, const QPoint& point);
    void undo();
    void redo();
    void assign_selected_asset(const QModelIndex& source_index);
    void instantiate_prefab(
        const QString& source_path,
        bool force_scene_root = false,
        const std::optional<dragonpixel::core::uuid>& explicit_parent = {});
    void create_prefab_from_selection();
    void apply_prefab();
    void revert_selected_prefab();
    void revert_all_prefab();
    void repair_prefab();
    void unpack_prefab(bool completely);
    void create_tile_set_from_image();
    void create_tilemap_from_selected_tileset();
    [[nodiscard]] bool create_tilemap_from_tileset(
        const QString& tileset_asset_id,
        const QString& name,
        const QString& grid_layout = QStringLiteral("rectangular"));
    [[nodiscard]] bool prompt_create_tilemap_from_tileset(
        const QString& tileset_asset_id,
        const QString& suggested_name);
    [[nodiscard]] std::optional<dragonpixel::core::uuid> attach_tilemap_to_scene(
        const QString& tilemap_asset_id);
    void import_tiled_tilemap();
    [[nodiscard]] bool perform_tiled_tilemap_import(
        const QString& source,
        double pixels_per_unit);
    [[nodiscard]] QString tile_texture_path_for(
        const ProjectIndexEntry* tileset_entry) const;
    void create_project_component(ProjectComponentLanguage language);
    void build_project_components();
    void edit_project_source(const QString& source_path);
    void edit_input_map();
    void refresh_project_input_map();
    bool reload_project_component_metadata();
    [[nodiscard]] bool attach_component_type(
        const QString& type_id,
        const QList<dragonpixel::core::uuid>& entity_ids,
        const QString& transaction_description);
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
    struct TileSceneTarget final
    {
        dragonpixel::core::uuid entity_id;
        QMatrix4x4 local_to_world;
        QMatrix4x4 world_to_local;
        float cell_width{1.0F};
        float cell_height{1.0F};
        dragonpixel::tiles::tile_grid_settings grid;
    };
    [[nodiscard]] std::optional<TileSceneTarget> tile_scene_target() const;
    [[nodiscard]] static QPoint tile_cell_at(
        const QVector3D& world_position,
        const TileSceneTarget& target);
    void update_tile_scene_edit_state();
    void update_tile_scene_overlay(
        const QPoint& cell,
        const TileSceneTarget& target);
    void begin_tile_scene_stroke(const QVector3D& world_position);
    void update_tile_scene_stroke(const QVector3D& world_position);
    void end_tile_scene_stroke(const QVector3D& world_position);
    void cancel_tile_scene_stroke();
    [[nodiscard]] bool place_tile_object(
        const TilePaletteWidget::ObjectBrushSource& source,
        const TileSceneTarget& target,
        int layer,
        const QPoint& cell);
    [[nodiscard]] bool erase_tile_objects(
        const TileSceneTarget& target,
        int layer,
        const QPoint& cell);
    [[nodiscard]] std::vector<dragonpixel::scene::command> tile_object_placement_commands(
        const dragonpixel::core::uuid& entity_id,
        const TileSceneTarget& target,
        int layer,
        const QPoint& cell,
        const QString& source_kind,
        const QString& source) const;
    void begin_gizmo_preview(AuthoringViewport::GizmoTool tool);
    void preview_gizmo_delta(AuthoringViewport::GizmoTool tool, const QVector3D& delta);
    void cancel_gizmo_preview();
    void apply_gizmo_delta(AuthoringViewport::GizmoTool tool, const QVector3D& delta);
    [[nodiscard]] std::vector<dragonpixel::scene::command> gizmo_commands(
        AuthoringViewport::GizmoTool tool,
        const QVector3D& delta) const;
    [[nodiscard]] bool reload_gizmo_preview(const dragonpixel::scene::scene& preview_scene);
    [[nodiscard]] std::string runtime_snapshot_json(const dragonpixel::scene::scene& source_scene) const;
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
    GameViewport* game_viewport_{};
    WorkerClient* preview_worker_{};
    WorkerClient* game_preview_worker_{};
    WorkerClient* play_worker_{};
    AutomationBroker* automation_broker_{};
    QProcess* automation_test_process_{};
    AssetPreviewService* asset_preview_service_{};
    QFileSystemWatcher* project_watcher_{};
    ProjectIndexBuildResult project_index_;
    InputMapService input_map_service_;
    std::optional<InputMapDocument> project_input_map_;
    QString input_map_source_path_;
    quint64 project_generation_{};
    bool project_refresh_pending_{};

    QTreeView* hierarchy_{};
    HierarchyModel* hierarchy_model_{};
    RecursiveFilterProxyModel* hierarchy_filter_{};
    QLineEdit* hierarchy_search_{};
    QTreeView* inspector_{};
    QStandardItemModel* inspector_model_{};
    InspectorDelegate* inspector_delegate_{};
    QWidget* inspector_panel_{};
    QCheckBox* inspector_enabled_{};
    QLineEdit* inspector_name_{};
    QLabel* inspector_identity_{};
    QLineEdit* inspector_search_{};
    QPushButton* inspector_add_component_{};
    QToolButton* inspector_lock_{};
    QTreeView* project_explorer_{};
    ProjectModel* project_model_{};
    ProjectFilterProxyModel* project_filter_{};
    ProjectFolderProxyModel* project_folder_filter_{};
    QLineEdit* project_search_{};
    QComboBox* project_type_filter_{};
    QComboBox* project_status_filter_{};
    QTreeView* project_folder_tree_{};
    QListView* project_thumbnail_view_{};
    QStackedWidget* project_content_stack_{};
    QLabel* project_breadcrumb_{};
    QLabel* project_details_{};
    QPersistentModelIndex project_current_folder_;
    QListView* scene_summary_{};
    QStandardItemModel* scene_summary_model_{};
    QTableView* console_{};
    ConsoleModel* console_model_{};
    RecursiveFilterProxyModel* console_filter_{};
    QLineEdit* console_search_{};
    QComboBox* adapter_{};
    QDockWidget* scene_dock_{};
    QDockWidget* scene_view_dock_{};
    QDockWidget* game_view_dock_{};
    QDockWidget* hierarchy_dock_{};
    QDockWidget* assets_dock_{};
    QDockWidget* inspector_dock_{};
    QDockWidget* tile_palette_dock_{};
    QDockWidget* console_dock_{};
    QDockWidget* onboarding_dock_{};
    QDockWidget* project_hub_dock_{};
    QMenu* view_menu_{};
    QListWidget* project_hub_recent_{};
    QPushButton* project_hub_new_{};
    QPushButton* project_hub_open_{};
    QPushButton* onboarding_add_square_{};
    QPushButton* onboarding_add_circle_{};
    QPushButton* onboarding_add_component_{};
    QPushButton* onboarding_create_csharp_{};
    QPushButton* onboarding_create_cpp_{};
    QPushButton* onboarding_play_{};

    TileDocumentService* tile_document_service_{};
    TilePaletteWidget* tile_palette_{};
    QTimer* tile_preview_timer_{};
    bool tile_scene_stroke_active_{};
    std::optional<QPoint> tile_scene_stroke_start_;
    std::optional<QPoint> tile_scene_last_cell_;
    std::optional<TileSceneTarget> tile_scene_stroke_target_;
    TileCanvas::Tool tile_scene_stroke_tool_{TileCanvas::Tool::paint};
    int tile_scene_stroke_layer_{};
    std::optional<TileDocumentService::Brush> tile_scene_stroke_brush_;
    std::optional<dragonpixel::core::uuid> pinned_tile_scene_target_;

    QAction* save_action_{};
    QAction* new_scene_action_{};
    QAction* save_scene_as_action_{};
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
    QAction* input_map_action_{};
    QAction* input_settings_action_{};
    QAction* import_tiled_tilemap_action_{};

    UnsavedPrompt unsaved_prompt_;
    DeletePrompt delete_prompt_;
    PrefabPathPrompt prefab_path_prompt_;
    PrefabLevelPrompt prefab_level_prompt_;
    ComponentNamePrompt component_name_prompt_;
    TilemapNamePrompt tilemap_name_prompt_;
    SelectionService selection_service_;
    ProjectLifecycleService project_lifecycle_service_;
    AssetService asset_service_;
    TileImportService tile_import_service_{asset_service_};
    PrefabService prefab_service_;
    ScriptEditorService script_editor_service_;
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
    bool primary_inspector_locked_{};
    QString primary_inspector_locked_project_id_;
    QString primary_inspector_locked_scene_id_;
    QList<dragonpixel::core::uuid> primary_inspector_locked_targets_;
    bool inspector_target_override_active_{};
    QList<dragonpixel::core::uuid> inspector_target_override_;
    struct AdditionalInspector final
    {
        int ordinal{};
        QDockWidget* dock{};
        QWidget* panel{};
        QTreeView* view{};
        QStandardItemModel* model{};
        InspectorDelegate* delegate{};
        QCheckBox* enabled{};
        QLineEdit* name{};
        QLabel* identity{};
        QLineEdit* search{};
        QPushButton* add_component{};
        QToolButton* lock{};
        bool locked{};
        QString locked_project_id;
        QString locked_scene_id;
        QList<dragonpixel::core::uuid> locked_targets;
    };
    std::vector<std::unique_ptr<AdditionalInspector>> additional_inspectors_;
    bool play_running_{};
    bool play_paused_{};
    bool preview_simulating_{};
    bool component_modules_available_{};
    bool auto_build_project_scripts_{true};
    dragonpixel::serialization::transaction_save_fault save_fault_for_test_{
        dragonpixel::serialization::transaction_save_fault::none};
    QString component_module_manifest_;
    QJsonObject editor_camera_;
    std::uint64_t camera_revision_{};
    std::uint64_t command_revision_{};
    bool authoring_self_test_passed_{};
    bool automation_self_test_passed_{};
    std::optional<dragonpixel::core::uuid> self_test_entity_id_;
};
