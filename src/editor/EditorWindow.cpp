#include "EditorWindow.h"
#include "ProjectIndexService.h"

#include <dragonpixel/scene/commands.h>
#include <dragonpixel/scene/command_validation.h>
#include <dragonpixel/metadata/builtin_ids.h>
#include <dragonpixel/serialization/atomic_file.h>
#include <dragonpixel/serialization/scene_json.h>

#include <nlohmann/json.hpp>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFile>
#include <QFileSystemWatcher>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLineEdit>
#include <QListView>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQuaternion>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableView>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
nlohmann::ordered_json vector_json(const QVector3D& value)
{
    return {{"x", value.x()}, {"y", value.y()}, {"z", value.z()}};
}

QVector3D json_vector(
    const nlohmann::ordered_json& value,
    const QVector3D& fallback = {})
{
    if (!value.is_object())
    {
        return fallback;
    }
    return {
        static_cast<float>(value.value("x", static_cast<double>(fallback.x()))),
        static_cast<float>(value.value("y", static_cast<double>(fallback.y()))),
        static_cast<float>(value.value("z", static_cast<double>(fallback.z()))),
    };
}

nlohmann::ordered_json quaternion_json(const QQuaternion& value)
{
    return {
        {"w", value.scalar()},
        {"x", value.x()},
        {"y", value.y()},
        {"z", value.z()},
    };
}

QQuaternion json_quaternion(const nlohmann::ordered_json& value)
{
    if (!value.is_object())
    {
        return {};
    }
    const QQuaternion result{
        static_cast<float>(value.value("w", 1.0)),
        static_cast<float>(value.value("x", 0.0)),
        static_cast<float>(value.value("y", 0.0)),
        static_cast<float>(value.value("z", 0.0)),
    };
    return result.isNull() ? QQuaternion{} : result.normalized();
}

const dragonpixel::scene::component_record* editable_transform(
    const dragonpixel::scene::entity& entity)
{
    const auto found = std::find_if(
        entity.components.begin(), entity.components.end(), [](const auto& component) {
            return component.type_id == dragonpixel::metadata::builtin_component_ids::transform
                && !component.opaque;
        });
    return found == entity.components.end() ? nullptr : &*found;
}

nlohmann::ordered_json default_value(dragonpixel::metadata::value_type type)
{
    using dragonpixel::metadata::value_type;
    switch (type)
    {
        case value_type::boolean: return false;
        case value_type::integer: return 0;
        case value_type::number: return 0.0;
        case value_type::vector2: return {{"x", 0.0}, {"y", 0.0}};
        case value_type::vector3: return {{"x", 0.0}, {"y", 0.0}, {"z", 0.0}};
        case value_type::quaternion: return {{"w", 1.0}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}};
        case value_type::color: return {{"a", 1.0}, {"b", 1.0}, {"g", 1.0}, {"r", 1.0}};
        default: return "";
    }
}

QString owner_text(dragonpixel::metadata::runtime_owner owner)
{
    return owner == dragonpixel::metadata::runtime_owner::managed
        ? QStringLiteral("managed")
        : QStringLiteral("native");
}

std::filesystem::path filesystem_path(const QString& value)
{
#if defined(Q_OS_WIN)
    return std::filesystem::path{value.toStdWString()};
#else
    return std::filesystem::path{value.toStdString()};
#endif
}

QString editor_settings_path()
{
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir{}.mkpath(directory);
    return QDir{directory}.filePath(QStringLiteral("editor-state.ini"));
}
}

EditorWindow::EditorWindow(QString initial_document, QWidget* parent)
    : QMainWindow(parent), metadata_(dragonpixel::metadata::registry::slice_one_defaults())
{
    prefab_service_.set_metadata(&metadata_);
    setObjectName(QStringLiteral("DragonPixelEditorWindow"));
    setAccessibleName(QStringLiteral("Dragon Pixel Engine Editor"));
    setWindowTitle(QStringLiteral("Dragon Pixel Engine Editor \u2014 Slice 2"));
    resize(1440, 900);
    build_interface();
    if (QFileInfo(initial_document).fileName().compare(
            QStringLiteral("DragonPixelProject.json"), Qt::CaseInsensitive) == 0)
    {
        load_project(initial_document);
    }
    else
    {
        load_scene(initial_document);
    }
}

void EditorWindow::build_interface()
{
    viewport_ = new AuthoringViewport(this);
    viewport_->setObjectName(QStringLiteral("SceneViewport"));
    setCentralWidget(viewport_);
    preview_worker_ = new WorkerClient(QStringLiteral("preview"), this);
    play_worker_ = new WorkerClient(QStringLiteral("play"), this);
    connect(preview_worker_, &WorkerClient::frame_ready, viewport_, &AuthoringViewport::set_preview_frame);
    connect(play_worker_, &WorkerClient::frame_ready, viewport_, &AuthoringViewport::set_play_frame);
    connect(preview_worker_, &WorkerClient::status_message, this, [this](const QString& message) {
        append_console(
            message,
            QStringLiteral("Info"),
            QStringLiteral("Runtime"),
            {},
            preview_worker_->adapter_name(),
            QStringLiteral("preview"));
        statusBar()->showMessage(message, 5000);
    });
    connect(play_worker_, &WorkerClient::status_message, this, [this](const QString& message) {
        append_console(
            message,
            QStringLiteral("Info"),
            QStringLiteral("Runtime"),
            {},
            play_worker_->adapter_name(),
            QStringLiteral("play"));
        statusBar()->showMessage(message, 5000);
    });
    connect(preview_worker_, &WorkerClient::runtime_stopped, viewport_, &AuthoringViewport::clear_preview_frame);
    connect(preview_worker_, &WorkerClient::preview_simulation_changed, this, [this](bool enabled) {
        preview_simulating_ = enabled;
        if (simulate_action_ != nullptr)
        {
            const QSignalBlocker blocker{simulate_action_};
            simulate_action_->setChecked(enabled);
        }
        update_action_states();
    });
    connect(play_worker_, &WorkerClient::runtime_stopped, this, [this] {
        viewport_->set_play_mode(false);
        play_running_ = false;
        play_paused_ = false;
        update_action_states();
        append_console(QStringLiteral("Play world discarded; authoring scene unchanged."), QStringLiteral("Info"), QStringLiteral("Runtime"));
    });
    editor_camera_ = {
        {QStringLiteral("orthographic"), false},
        {QStringLiteral("position"), QJsonArray{0.0, 2.0, 7.0}},
        {QStringLiteral("target"), QJsonArray{0.0, 0.0, 0.0}},
        {QStringLiteral("fieldOfViewDegrees"), 60.0},
        {QStringLiteral("orthographicSize"), 10.0},
    };
    connect(viewport_, &AuthoringViewport::viewport_resized, this, [this](const QSize& size) {
        preview_worker_->resize_viewport(size);
        play_worker_->resize_viewport(size);
    });
    connect(viewport_, &AuthoringViewport::camera_changed, this,
        [this](bool orthographic, const QVector3D& position, const QVector3D& target, float fov, float size) {
            editor_camera_ = {
                {QStringLiteral("orthographic"), orthographic},
                {QStringLiteral("position"), QJsonArray{position.x(), position.y(), position.z()}},
                {QStringLiteral("target"), QJsonArray{target.x(), target.y(), target.z()}},
                {QStringLiteral("fieldOfViewDegrees"), fov},
                {QStringLiteral("orthographicSize"), size},
            };
            ++camera_revision_;
            update_worker_viewport();
        });
    connect(viewport_, &AuthoringViewport::frame_clicked, this, [this](const QPoint& frame_position) {
        (viewport_->is_play_mode() ? play_worker_ : preview_worker_)->pick(frame_position);
    });
    const auto apply_pick = [this](const QString& entity_id, std::uint64_t) {
        const auto id = dragonpixel::core::uuid::parse(entity_id.toStdString());
        if (!id)
        {
            hierarchy_->clearSelection();
            inspect_selected_entities();
            return;
        }
        const auto proxy = hierarchy_filter_->mapFromSource(hierarchy_model_->index_for_entity(*id));
        if (!proxy.isValid())
        {
            return;
        }
        hierarchy_->selectionModel()->clearSelection();
        hierarchy_->selectionModel()->select(proxy, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        hierarchy_->setCurrentIndex(proxy);
        append_console(QStringLiteral("Picked GameObject %1 from the adapter ID buffer").arg(entity_id),
            QStringLiteral("Info"), QStringLiteral("Viewport"), entity_id);
    };
    connect(preview_worker_, &WorkerClient::pick_ready, this, apply_pick);
    connect(play_worker_, &WorkerClient::pick_ready, this, apply_pick);
    connect(viewport_, &AuthoringViewport::gizmo_started, this, &EditorWindow::begin_gizmo_preview);
    connect(viewport_, &AuthoringViewport::gizmo_previewed, this, &EditorWindow::preview_gizmo_delta);
    connect(viewport_, &AuthoringViewport::gizmo_committed, this, &EditorWindow::apply_gizmo_delta);
    connect(viewport_, &AuthoringViewport::gizmo_cancelled, this, &EditorWindow::cancel_gizmo_preview);
    connect(viewport_, &AuthoringViewport::project_item_dropped, this,
        [this](const QString& path, const QString& kind, const QString& asset_type, const QString& asset_id) {
            if (kind == QStringLiteral("asset"))
            {
                if (asset_type.contains(QStringLiteral("sprite"), Qt::CaseInsensitive))
                {
                    create_preset(dragonpixel::scene::entity_preset::sprite, asset_id);
                }
                else if (asset_type.contains(QStringLiteral("mesh"), Qt::CaseInsensitive))
                {
                    create_preset(dragonpixel::scene::entity_preset::cube, asset_id);
                }
                else
                {
                    append_console(QStringLiteral("Dropped asset is not create-compatible: %1").arg(path),
                        QStringLiteral("Warning"), QStringLiteral("Assets"), path);
                }
            }
            else if (kind == QStringLiteral("scene"))
            {
                load_scene(path);
            }
            else if (kind == QStringLiteral("prefab"))
            {
                instantiate_prefab(path);
            }
        });

    scene_summary_model_ = new QStandardItemModel(this);
    scene_summary_ = new QListView(this);
    scene_summary_->setObjectName(QStringLiteral("SceneSummaryView"));
    scene_summary_->setAccessibleName(QStringLiteral("Scene summary"));
    scene_summary_->setModel(scene_summary_model_);

    hierarchy_model_ = new HierarchyModel(this);
    hierarchy_model_->set_handlers(
        [this](const auto& id, const auto& name, bool enabled) {
            return edit_hierarchy_entity(id, name, enabled);
        },
        [this](const auto& id, const auto& parent, auto sibling) {
            return drag_reparent_entity(id, parent, sibling);
        });
    hierarchy_filter_ = new RecursiveFilterProxyModel(this);
    hierarchy_filter_->setSourceModel(hierarchy_model_);
    hierarchy_search_ = new QLineEdit(this);
    hierarchy_search_->setObjectName(QStringLiteral("HierarchySearch"));
    hierarchy_search_->setAccessibleName(QStringLiteral("Filter GameObjects"));
    hierarchy_search_->setPlaceholderText(QStringLiteral("Filter GameObjects..."));
    connect(hierarchy_search_, &QLineEdit::textChanged, hierarchy_filter_, &QSortFilterProxyModel::setFilterFixedString);
    hierarchy_ = new QTreeView(this);
    hierarchy_->setObjectName(QStringLiteral("HierarchyView"));
    hierarchy_->setAccessibleName(QStringLiteral("Scene GameObject hierarchy"));
    hierarchy_->setModel(hierarchy_filter_);
    hierarchy_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    hierarchy_->setSelectionBehavior(QAbstractItemView::SelectRows);
    hierarchy_->setDragDropMode(QAbstractItemView::InternalMove);
    hierarchy_->setDefaultDropAction(Qt::MoveAction);
    hierarchy_->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    hierarchy_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(hierarchy_->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        inspect_selected_entities();
        update_action_states();
    });
    auto* hierarchy_panel = new QWidget(this);
    hierarchy_panel->setObjectName(QStringLiteral("HierarchyPanel"));
    auto* hierarchy_layout = new QVBoxLayout(hierarchy_panel);
    hierarchy_layout->setContentsMargins(4, 4, 4, 4);
    hierarchy_layout->addWidget(hierarchy_search_);
    hierarchy_layout->addWidget(hierarchy_);

    inspector_model_ = new QStandardItemModel(this);
    inspector_model_->setHorizontalHeaderLabels({QStringLiteral("Property"), QStringLiteral("Value")});
    inspector_delegate_ = new InspectorDelegate(this);
    inspector_ = new QTreeView(this);
    inspector_->setObjectName(QStringLiteral("InspectorView"));
    inspector_->setAccessibleName(QStringLiteral("Typed component Inspector"));
    inspector_->setModel(inspector_model_);
    inspector_->setItemDelegateForColumn(1, inspector_delegate_);
    inspector_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    connect(inspector_model_, &QStandardItemModel::itemChanged, this, &EditorWindow::edit_inspector_item);

    project_model_ = new ProjectModel(this);
    project_filter_ = new ProjectFilterProxyModel(this);
    project_filter_->setSourceModel(project_model_);
    project_search_ = new QLineEdit(this);
    project_search_->setObjectName(QStringLiteral("ProjectSearch"));
    project_search_->setAccessibleName(QStringLiteral("Search project"));
    project_search_->setPlaceholderText(QStringLiteral("Search scenes, prefabs, and assets..."));
    connect(project_search_, &QLineEdit::textChanged, project_filter_, &ProjectFilterProxyModel::set_search_text);
    project_type_filter_ = new QComboBox(this);
    project_type_filter_->setObjectName(QStringLiteral("ProjectTypeFilter"));
    project_type_filter_->setAccessibleName(QStringLiteral("Filter project by type"));
    project_type_filter_->addItem(QStringLiteral("All types"), QString{});
    project_type_filter_->addItem(QStringLiteral("Scenes"), QStringLiteral("scene"));
    project_type_filter_->addItem(QStringLiteral("Prefabs"), QStringLiteral("prefab"));
    project_type_filter_->addItem(QStringLiteral("Assets"), QStringLiteral("asset"));
    connect(project_type_filter_, &QComboBox::currentIndexChanged, this, [this](int) {
        project_filter_->set_type_filter(project_type_filter_->currentData().toString());
    });
    project_status_filter_ = new QComboBox(this);
    project_status_filter_->setObjectName(QStringLiteral("ProjectStatusFilter"));
    project_status_filter_->setAccessibleName(QStringLiteral("Filter project by status"));
    project_status_filter_->addItem(QStringLiteral("All status"), QString{});
    project_status_filter_->addItem(QStringLiteral("Ready"), QStringLiteral("ready"));
    project_status_filter_->addItem(QStringLiteral("Information"), QStringLiteral("information"));
    project_status_filter_->addItem(QStringLiteral("Warning"), QStringLiteral("warning"));
    project_status_filter_->addItem(QStringLiteral("Error"), QStringLiteral("error"));
    connect(project_status_filter_, &QComboBox::currentIndexChanged, this, [this](int) {
        project_filter_->set_status_filter(project_status_filter_->currentData().toString());
    });
    project_explorer_ = new QTreeView(this);
    project_explorer_->setObjectName(QStringLiteral("ProjectExplorerView"));
    project_explorer_->setAccessibleName(QStringLiteral("Project Explorer"));
    project_explorer_->setModel(project_filter_);
    project_explorer_->setDragEnabled(true);
    project_explorer_->setDragDropMode(QAbstractItemView::DragOnly);
    project_explorer_->setDefaultDropAction(Qt::CopyAction);
    project_explorer_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    project_explorer_->setUniformRowHeights(true);
    project_explorer_->setAlternatingRowColors(true);
    connect(project_explorer_, &QTreeView::doubleClicked, this, &EditorWindow::activate_project_item);
    connect(project_explorer_, &QTreeView::activated, this, &EditorWindow::activate_project_item);
    auto* project_panel = new QWidget(this);
    project_panel->setObjectName(QStringLiteral("ProjectExplorerPanel"));
    auto* project_layout = new QVBoxLayout(project_panel);
    project_layout->setContentsMargins(4, 4, 4, 4);
    project_layout->addWidget(project_search_);
    auto* project_filter_row = new QHBoxLayout;
    project_filter_row->addWidget(project_type_filter_);
    project_filter_row->addWidget(project_status_filter_);
    auto* project_refresh = new QPushButton(QStringLiteral("Refresh"), project_panel);
    project_refresh->setObjectName(QStringLiteral("RefreshProjectAction"));
    project_refresh->setAccessibleName(QStringLiteral("Refresh Project Explorer"));
    connect(project_refresh, &QPushButton::clicked, this, &EditorWindow::rebuild_assets);
    project_filter_row->addWidget(project_refresh);
    project_layout->addLayout(project_filter_row);
    project_layout->addWidget(project_explorer_);

    console_model_ = new ConsoleModel(this);
    console_filter_ = new RecursiveFilterProxyModel(this);
    console_filter_->setSourceModel(console_model_);
    console_filter_->setFilterKeyColumn(-1);
    console_search_ = new QLineEdit(this);
    console_search_->setObjectName(QStringLiteral("ConsoleSearch"));
    console_search_->setAccessibleName(QStringLiteral("Filter diagnostics"));
    console_search_->setPlaceholderText(QStringLiteral("Filter diagnostics..."));
    connect(console_search_, &QLineEdit::textChanged, console_filter_, &QSortFilterProxyModel::setFilterFixedString);
    console_ = new QTableView(this);
    console_->setObjectName(QStringLiteral("ConsoleView"));
    console_->setAccessibleName(QStringLiteral("Structured diagnostics console"));
    console_->setModel(console_filter_);
    console_->setSelectionBehavior(QAbstractItemView::SelectRows);
    console_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    console_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    console_->horizontalHeader()->setStretchLastSection(true);
    connect(console_, &QTableView::doubleClicked, this, &EditorWindow::navigate_console_entry);
    auto* console_panel = new QWidget(this);
    console_panel->setObjectName(QStringLiteral("ConsolePanel"));
    auto* console_layout = new QVBoxLayout(console_panel);
    console_layout->setContentsMargins(4, 4, 4, 4);
    console_layout->addWidget(console_search_);
    auto* console_actions = new QHBoxLayout;
    auto* clear_console = new QPushButton(QStringLiteral("Clear"), console_panel);
    clear_console->setObjectName(QStringLiteral("ClearConsoleAction"));
    clear_console->setAccessibleName(QStringLiteral("Clear diagnostics console"));
    connect(clear_console, &QPushButton::clicked, console_model_, &ConsoleModel::clear);
    auto* copy_console = new QPushButton(QStringLiteral("Copy"), console_panel);
    copy_console->setObjectName(QStringLiteral("CopyConsoleAction"));
    copy_console->setAccessibleName(QStringLiteral("Copy selected diagnostics"));
    connect(copy_console, &QPushButton::clicked, this, &EditorWindow::copy_console_selection);
    auto* export_console = new QPushButton(QStringLiteral("Export CSV..."), console_panel);
    export_console->setObjectName(QStringLiteral("ExportConsoleAction"));
    export_console->setAccessibleName(QStringLiteral("Export selected diagnostics as CSV"));
    connect(export_console, &QPushButton::clicked, this, &EditorWindow::export_console_selection);
    console_actions->addWidget(clear_console);
    console_actions->addWidget(copy_console);
    console_actions->addWidget(export_console);
    console_actions->addStretch();
    console_layout->addLayout(console_actions);
    console_layout->addWidget(console_);

    asset_preview_service_ = new AssetPreviewService(this);
    connect(asset_preview_service_, &AssetPreviewService::previewReady, this,
        [this](const AssetPreviewResult& result) {
            if (result.project_generation != project_generation_)
            {
                return;
            }
            const auto key = result.asset_id.isEmpty() ? result.metadata_path : result.asset_id;
            if (!project_model_->set_asset_preview(key, result.icon, result.content_identity))
            {
                append_console(
                    QStringLiteral("Ignored ambiguous or stale asset preview for %1").arg(key),
                    QStringLiteral("Warning"),
                    QStringLiteral("Asset Preview"),
                    result.metadata_path,
                    {},
                    {},
                    QString::number(result.request_id),
                    {},
                    result.asset_id,
                    result.metadata_path);
            }
        });
    connect(asset_preview_service_, &AssetPreviewService::previewDiagnostic, this,
        [this](const AssetPreviewDiagnostic& diagnostic) {
            if (diagnostic.project_generation != project_generation_)
            {
                return;
            }
            append_console(
                QStringLiteral("%1: %2")
                    .arg(asset_preview_diagnostic_code_name(diagnostic.code), diagnostic.message),
                QStringLiteral("Warning"),
                QStringLiteral("Asset Preview"),
                diagnostic.metadata_path,
                {},
                {},
                QString::number(diagnostic.request_id),
                {},
                diagnostic.asset_id,
                diagnostic.metadata_path);
        });
    project_watcher_ = new QFileSystemWatcher(this);
    connect(project_watcher_, &QFileSystemWatcher::fileChanged, this,
        [this](const QString&) { schedule_project_refresh(); });
    connect(project_watcher_, &QFileSystemWatcher::directoryChanged, this,
        [this](const QString&) { schedule_project_refresh(); });

    auto make_dock = [this](const QString& title, const QString& id, QWidget* widget, Qt::DockWidgetArea area) {
        auto* dock = new QDockWidget(title, this);
        dock->setObjectName(QStringLiteral("Dock.%1").arg(id));
        dock->setWidget(widget);
        dock->setAccessibleName(title);
        addDockWidget(area, dock);
        return dock;
    };
    scene_dock_ = make_dock(QStringLiteral("Scene"), QStringLiteral("Scene"), scene_summary_, Qt::LeftDockWidgetArea);
    hierarchy_dock_ = make_dock(QStringLiteral("Hierarchy"), QStringLiteral("Hierarchy"), hierarchy_panel, Qt::LeftDockWidgetArea);
    assets_dock_ = make_dock(QStringLiteral("Project Explorer"), QStringLiteral("ProjectExplorer"), project_panel, Qt::LeftDockWidgetArea);
    inspector_dock_ = make_dock(QStringLiteral("Inspector"), QStringLiteral("Inspector"), inspector_, Qt::RightDockWidgetArea);
    console_dock_ = make_dock(QStringLiteral("Console"), QStringLiteral("Console"), console_panel, Qt::BottomDockWidgetArea);
    tabifyDockWidget(scene_dock_, hierarchy_dock_);
    hierarchy_dock_->raise();

    auto* runtime_bar = addToolBar(QStringLiteral("Runtime"));
    runtime_bar->setObjectName(QStringLiteral("RuntimeToolbar"));
    adapter_ = new QComboBox(runtime_bar);
    adapter_->setObjectName(QStringLiteral("RuntimeAdapterCombo"));
    adapter_->addItem(QStringLiteral("MonoGame"), QStringLiteral("monogame"));
    adapter_->addItem(QStringLiteral("KNI (experimental)"), QStringLiteral("kni"));
    adapter_->setAccessibleName(QStringLiteral("Runtime framework adapter"));
    connect(adapter_, &QComboBox::currentIndexChanged, this, [this](int) { refresh_preview(); });
    runtime_bar->addWidget(adapter_);
    auto add_runtime_action = [this, runtime_bar](const QString& text, const QString& name, auto handler) {
        auto* action = runtime_bar->addAction(text);
        action->setObjectName(name);
        connect(action, &QAction::triggered, this, handler);
        return action;
    };
    play_action_ = add_runtime_action(QStringLiteral("Play"), QStringLiteral("PlayAction"), [this] { start_play(); });
    simulate_action_ = add_runtime_action(
        QStringLiteral("Simulate"),
        QStringLiteral("SimulatePreviewAction"),
        [this] {
            const auto enabled = simulate_action_->isChecked();
            preview_simulating_ = enabled;
            preview_worker_->set_preview_simulation(enabled);
            update_action_states();
        });
    simulate_action_->setCheckable(true);
    simulate_action_->setToolTip(
        QStringLiteral("Run disposable 2D/3D physics in the Edit preview; stopping restores authoring transforms"));
    pause_action_ = add_runtime_action(QStringLiteral("Pause"), QStringLiteral("PauseAction"), [this] {
        play_worker_->pause();
        play_paused_ = true;
        update_action_states();
    });
    resume_action_ = add_runtime_action(QStringLiteral("Resume"), QStringLiteral("ResumeAction"), [this] {
        play_worker_->resume();
        play_paused_ = false;
        update_action_states();
    });
    stop_action_ = add_runtime_action(QStringLiteral("Stop"), QStringLiteral("StopAction"), [this] { stop_play(); });
    add_runtime_action(QStringLiteral("Crash Play Worker"), QStringLiteral("CrashPlayWorkerAction"), [this] { play_worker_->force_crash(); });

    auto* edit_bar = addToolBar(QStringLiteral("Authoring"));
    edit_bar->setObjectName(QStringLiteral("AuthoringToolbar"));
    auto add_edit_action = [this, edit_bar](const QString& text, const QString& name, auto handler) {
        auto* action = edit_bar->addAction(text);
        action->setObjectName(name);
        connect(action, &QAction::triggered, this, handler);
        return action;
    };
    auto* preset_button = new QToolButton(edit_bar);
    preset_button->setObjectName(QStringLiteral("AddGameObjectButton"));
    preset_button->setAccessibleName(QStringLiteral("Add GameObject"));
    preset_button->setText(QStringLiteral("Add GameObject"));
    preset_button->setPopupMode(QToolButton::InstantPopup);
    auto* preset_menu = new QMenu(preset_button);
    auto add_preset = [this, preset_menu](const QString& text, const QString& name, auto preset) {
        auto* action = preset_menu->addAction(text);
        action->setObjectName(name);
        connect(action, &QAction::triggered, this, [this, preset] { create_preset(preset); });
        return action;
    };
    add_preset(QStringLiteral("Empty"), QStringLiteral("AddEmptyGameObjectAction"), dragonpixel::scene::entity_preset::empty);
    add_preset(QStringLiteral("Sprite"), QStringLiteral("AddSpriteGameObjectAction"), dragonpixel::scene::entity_preset::sprite);
    add_preset(QStringLiteral("Cube"), QStringLiteral("AddCubeGameObjectAction"), dragonpixel::scene::entity_preset::cube);
    add_preset(QStringLiteral("Camera"), QStringLiteral("AddCameraGameObjectAction"), dragonpixel::scene::entity_preset::camera);
    add_preset(QStringLiteral("Light"), QStringLiteral("AddLightGameObjectAction"), dragonpixel::scene::entity_preset::light);
    preset_button->setMenu(preset_menu);
    edit_bar->addWidget(preset_button);
    duplicate_action_ = add_edit_action(QStringLiteral("Duplicate"), QStringLiteral("DuplicateGameObjectAction"), [this] { duplicate_selected(); });
    delete_action_ = add_edit_action(QStringLiteral("Delete"), QStringLiteral("DeleteGameObjectAction"), [this] { delete_selected_subtree(); });
    delete_action_->setShortcut(QKeySequence::Delete);
    reparent_action_ = add_edit_action(QStringLiteral("Reparent"), QStringLiteral("ReparentGameObjectAction"), [this] { reparent_entity(); });
    add_component_action_ = add_edit_action(QStringLiteral("Add Component"), QStringLiteral("AddComponentAction"), [this] { add_component(); });
    remove_component_action_ = add_edit_action(QStringLiteral("Remove Component"), QStringLiteral("RemoveComponentAction"), [this] { remove_component(); });
    edit_bar->addSeparator();
    auto* move_gizmo = add_edit_action(QStringLiteral("Move"), QStringLiteral("MoveGizmoAction"), [this] {
        viewport_->set_gizmo_tool(AuthoringViewport::GizmoTool::move);
    });
    move_gizmo->setShortcut(QKeySequence{Qt::Key_W});
    auto* rotate_gizmo = add_edit_action(QStringLiteral("Rotate"), QStringLiteral("RotateGizmoAction"), [this] {
        viewport_->set_gizmo_tool(AuthoringViewport::GizmoTool::rotate);
    });
    rotate_gizmo->setShortcut(QKeySequence{Qt::Key_E});
    auto* scale_gizmo = add_edit_action(QStringLiteral("Scale"), QStringLiteral("ScaleGizmoAction"), [this] {
        viewport_->set_gizmo_tool(AuthoringViewport::GizmoTool::scale);
    });
    scale_gizmo->setShortcut(QKeySequence{Qt::Key_R});
    auto* local_global = add_edit_action(QStringLiteral("Global/Local"), QStringLiteral("GizmoOrientationAction"), [this] {
        static bool local = false;
        local = !local;
        viewport_->set_gizmo_orientation(local
            ? AuthoringViewport::GizmoOrientation::local
            : AuthoringViewport::GizmoOrientation::global);
    });
    local_global->setShortcut(QKeySequence{Qt::Key_X});
    auto* snapping = add_edit_action(QStringLiteral("Snap"), QStringLiteral("GizmoSnapAction"), [this] {
        static bool enabled = false;
        enabled = !enabled;
        viewport_->set_snapping(enabled);
    });
    snapping->setCheckable(true);

    auto* file_menu = menuBar()->addMenu(QStringLiteral("&File"));
    auto* open_project = file_menu->addAction(QStringLiteral("Open &Project..."));
    open_project->setObjectName(QStringLiteral("OpenProjectAction"));
    connect(open_project, &QAction::triggered, this, [this] {
        const auto path = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("Open Dragon Pixel project"),
            project_root_,
            QStringLiteral("Dragon Pixel Project (DragonPixelProject.json);;JSON (*.json)"));
        if (!path.isEmpty())
        {
            load_project(path);
        }
    });
    auto* open = file_menu->addAction(QStringLiteral("&Open Scene..."));
    open->setObjectName(QStringLiteral("OpenSceneAction"));
    connect(open, &QAction::triggered, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Open Dragon Pixel scene"), project_root_, QStringLiteral("Dragon Pixel Scene (*.dpescene);;JSON (*.json)"));
        if (!path.isEmpty())
        {
            load_scene(path);
        }
    });
    auto* close = file_menu->addAction(QStringLiteral("&Close Project"));
    close->setObjectName(QStringLiteral("CloseProjectAction"));
    connect(close, &QAction::triggered, this, [this] { close_project(); });
    save_action_ = file_menu->addAction(QStringLiteral("&Save Scene"));
    save_action_->setObjectName(QStringLiteral("SaveSceneAction"));
    save_action_->setShortcut(QKeySequence::Save);
    connect(save_action_, &QAction::triggered, this, [this] { save_scene(); });
    file_menu->addSeparator();
    file_menu->addAction(QStringLiteral("E&xit"), this, &QWidget::close);

    auto* edit_menu = menuBar()->addMenu(QStringLiteral("&Edit"));
    undo_action_ = edit_menu->addAction(QStringLiteral("&Undo"));
    undo_action_->setObjectName(QStringLiteral("UndoAction"));
    undo_action_->setShortcut(QKeySequence::Undo);
    connect(undo_action_, &QAction::triggered, this, &EditorWindow::undo);
    redo_action_ = edit_menu->addAction(QStringLiteral("&Redo"));
    redo_action_->setObjectName(QStringLiteral("RedoAction"));
    redo_action_->setShortcut(QKeySequence::Redo);
    connect(redo_action_, &QAction::triggered, this, &EditorWindow::redo);
    edit_menu->addSeparator();
    edit_menu->addAction(duplicate_action_);
    edit_menu->addAction(delete_action_);

    auto* game_object_menu = menuBar()->addMenu(QStringLiteral("&GameObject"));
    for (auto* action : preset_menu->actions())
    {
        game_object_menu->addAction(action);
    }
    game_object_menu->addSeparator();
    game_object_menu->addAction(duplicate_action_);
    game_object_menu->addAction(delete_action_);

    auto* prefab_menu = menuBar()->addMenu(QStringLiteral("&Prefab"));
    prefab_menu->setObjectName(QStringLiteral("PrefabMenu"));
    create_prefab_action_ = prefab_menu->addAction(QStringLiteral("Create from Selection..."));
    create_prefab_action_->setObjectName(QStringLiteral("CreatePrefabFromSelectionAction"));
    connect(create_prefab_action_, &QAction::triggered, this, &EditorWindow::create_prefab_from_selection);
    instantiate_prefab_action_ = prefab_menu->addAction(QStringLiteral("Instantiate Selected Project Prefab"));
    instantiate_prefab_action_->setObjectName(QStringLiteral("InstantiatePrefabAction"));
    connect(instantiate_prefab_action_, &QAction::triggered, this, [this] {
        if (project_explorer_->currentIndex().isValid())
        {
            const auto source = project_filter_->mapToSource(project_explorer_->currentIndex().siblingAtColumn(0));
            if (ProjectModel::item_kind(source) == ProjectItemKind::prefab)
            {
                instantiate_prefab(ProjectModel::item_path(source));
                return;
            }
        }
        const auto path = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("Instantiate linked prefab"),
            project_root_,
            QStringLiteral("Dragon Pixel Prefab (*.dpeprefab)"));
        if (!path.isEmpty())
        {
            instantiate_prefab(path);
        }
    });
    prefab_menu->addSeparator();
    apply_prefab_action_ = prefab_menu->addAction(QStringLiteral("Apply to Explicit Prefab Level..."));
    apply_prefab_action_->setObjectName(QStringLiteral("ApplyPrefabAction"));
    connect(apply_prefab_action_, &QAction::triggered, this, &EditorWindow::apply_prefab);
    revert_selected_prefab_action_ = prefab_menu->addAction(QStringLiteral("Revert Selected GameObject"));
    revert_selected_prefab_action_->setObjectName(QStringLiteral("RevertSelectedPrefabAction"));
    connect(revert_selected_prefab_action_, &QAction::triggered, this, &EditorWindow::revert_selected_prefab);
    revert_all_prefab_action_ = prefab_menu->addAction(QStringLiteral("Revert All Overrides"));
    revert_all_prefab_action_->setObjectName(QStringLiteral("RevertAllPrefabAction"));
    connect(revert_all_prefab_action_, &QAction::triggered, this, &EditorWindow::revert_all_prefab);
    repair_prefab_action_ = prefab_menu->addAction(QStringLiteral("Repair / Rebase"));
    repair_prefab_action_->setObjectName(QStringLiteral("RepairPrefabAction"));
    connect(repair_prefab_action_, &QAction::triggered, this, &EditorWindow::repair_prefab);
    prefab_menu->addSeparator();
    unpack_prefab_action_ = prefab_menu->addAction(QStringLiteral("Unpack One Level"));
    unpack_prefab_action_->setObjectName(QStringLiteral("UnpackPrefabAction"));
    connect(unpack_prefab_action_, &QAction::triggered, this, [this] { unpack_prefab(false); });
    unpack_completely_prefab_action_ = prefab_menu->addAction(QStringLiteral("Unpack Completely"));
    unpack_completely_prefab_action_->setObjectName(QStringLiteral("UnpackCompletelyPrefabAction"));
    connect(unpack_completely_prefab_action_, &QAction::triggered, this, [this] { unpack_prefab(true); });

    auto* view_menu = menuBar()->addMenu(QStringLiteral("&View"));
    view_menu->setObjectName(QStringLiteral("ViewMenu"));
    for (auto* dock : {scene_dock_, hierarchy_dock_, assets_dock_, inspector_dock_, console_dock_})
    {
        view_menu->addAction(dock->toggleViewAction());
    }
    view_menu->addSeparator();
    auto* workspace_menu = view_menu->addMenu(QStringLiteral("Workspaces"));
    auto add_workspace = [this, workspace_menu](const QString& label, const QString& id) {
        auto* action = workspace_menu->addAction(label);
        action->setObjectName(QStringLiteral("Workspace%1Action").arg(id));
        connect(action, &QAction::triggered, this, [this, id] { apply_workspace(id); });
    };
    add_workspace(QStringLiteral("2D"), QStringLiteral("2D"));
    add_workspace(QStringLiteral("3D"), QStringLiteral("3D"));
    add_workspace(QStringLiteral("Debug"), QStringLiteral("Debug"));
    auto* reset_layout = view_menu->addAction(QStringLiteral("Reset Layout"));
    reset_layout->setObjectName(QStringLiteral("ResetLayoutAction"));
    connect(reset_layout, &QAction::triggered, this, &EditorWindow::reset_workspace);
    auto* save_layout = view_menu->addAction(QStringLiteral("Save Layout"));
    save_layout->setObjectName(QStringLiteral("SaveLayoutAction"));
    connect(save_layout, &QAction::triggered, this, [this] {
        QSettings settings{editor_settings_path(), QSettings::IniFormat};
        settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
        settings.setValue(QStringLiteral("window/state"), saveState(2));
        settings.sync();
        statusBar()->showMessage(QStringLiteral("Workspace layout saved for this user"), 3000);
    });

    connect(hierarchy_, &QWidget::customContextMenuRequested, this, [this](const QPoint& point) {
        QMenu menu{hierarchy_};
        menu.addAction(duplicate_action_);
        menu.addAction(delete_action_);
        menu.addAction(reparent_action_);
        menu.addSeparator();
        menu.addAction(create_prefab_action_);
        menu.addAction(apply_prefab_action_);
        menu.addAction(revert_selected_prefab_action_);
        menu.addAction(revert_all_prefab_action_);
        menu.addAction(repair_prefab_action_);
        menu.addAction(unpack_prefab_action_);
        menu.addAction(unpack_completely_prefab_action_);
        menu.exec(hierarchy_->viewport()->mapToGlobal(point));
    });

    unsaved_prompt_ = [this](const QString& scene_name) {
        const auto choice = QMessageBox::warning(
            this,
            QStringLiteral("Unsaved changes"),
            QStringLiteral("Save changes to %1?").arg(scene_name),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (choice == QMessageBox::Save)
        {
            return UnsavedDecision::save;
        }
        if (choice == QMessageBox::Discard)
        {
            return UnsavedDecision::discard;
        }
        return UnsavedDecision::cancel;
    };
    delete_prompt_ = [this](const QString& root_name, int entity_count) {
        return QMessageBox::question(
            this,
            QStringLiteral("Delete GameObject subtree"),
            QStringLiteral("Delete %1 and %2 GameObject(s) in its subtree? Incoming references will remain visible for repair.")
                .arg(root_name)
                .arg(entity_count),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel) == QMessageBox::Yes;
    };
    setTabOrder(hierarchy_search_, hierarchy_);
    setTabOrder(hierarchy_, project_search_);
    setTabOrder(project_search_, project_explorer_);
    setTabOrder(project_explorer_, inspector_);
    setTabOrder(inspector_, console_search_);
    setTabOrder(console_search_, console_);

    automation_broker_ = new AutomationBroker(
        [this](const QString& method, const QJsonObject& parameters) {
            return handle_automation_request(method, parameters);
        },
        this);
    connect(automation_broker_, &AutomationBroker::status_message, this, [this](const QString& message) {
        append_console(message, QStringLiteral("Info"), QStringLiteral("Automation"));
    });
    if (!runtime_directory_.isValid()
        || !automation_broker_->start(runtime_directory_.filePath(QStringLiteral("automation-audit.jsonl"))))
    {
        append_console(QStringLiteral("Automation broker is unavailable for this editor session"), QStringLiteral("Warning"));
    }
    reset_workspace();
    QSettings settings{editor_settings_path(), QSettings::IniFormat};
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray(), 2);
    viewport_->set_view_mode(settings.value(QStringLiteral("workspace/current"), QStringLiteral("3D")).toString()
            == QStringLiteral("2D")
        ? AuthoringViewport::ViewMode::two_d
        : AuthoringViewport::ViewMode::three_d);
    update_action_states();
}

bool EditorWindow::close_project(bool ask_to_save)
{
    if (ask_to_save && !confirm_discard_or_save())
    {
        return false;
    }
    stop_play();
    if (preview_worker_ != nullptr)
    {
        preview_worker_->stop_and_discard();
    }
    gizmo_preview_active_ = false;
    gizmo_preview_scene_.reset();
    gizmo_transform_snapshots_.clear();
    scene_.reset();
    scene_path_.clear();
    project_manifest_path_.clear();
    project_root_.clear();
    prefab_service_.clear();
    hierarchy_model_->rebuild(nullptr);
    inspector_model_->clear();
    inspector_model_->setHorizontalHeaderLabels({QStringLiteral("Property"), QStringLiteral("Value")});
    project_index_ = {};
    ++project_generation_;
    if (asset_preview_service_ != nullptr)
    {
        asset_preview_service_->set_project_generation(project_generation_);
    }
    if (project_watcher_ != nullptr)
    {
        if (!project_watcher_->files().isEmpty())
        {
            project_watcher_->removePaths(project_watcher_->files());
        }
        if (!project_watcher_->directories().isEmpty())
        {
            project_watcher_->removePaths(project_watcher_->directories());
        }
    }
    project_model_->rebuild(project_index_);
    scene_summary_model_->clear();
    viewport_->clear_preview_frame();
    viewport_->set_selected_name({});
    update_window_title();
    update_action_states();
    append_console(QStringLiteral("Project closed; no authoritative scene remains loaded"));
    return true;
}

bool EditorWindow::load_project(const QString& path)
{
    const auto indexed = ProjectIndexService{}.build_candidate(path);
    for (const auto& diagnostic : indexed.diagnostics)
    {
        const auto severity = diagnostic.severity == ProjectIndexDiagnosticSeverity::error
            ? QStringLiteral("Error")
            : diagnostic.severity == ProjectIndexDiagnosticSeverity::warning
                ? QStringLiteral("Warning")
                : QStringLiteral("Info");
        append_console(
            QStringLiteral("%1: %2")
                .arg(project_index_diagnostic_code_name(diagnostic.code), diagnostic.message),
            severity,
            QStringLiteral("Project Index"),
            diagnostic.document_path);
    }
    if (!indexed.succeeded())
    {
        append_console(
            QStringLiteral("Project candidate validation failed; the current project/session was preserved."),
            QStringLiteral("Error"),
            QStringLiteral("Project Index"));
        return false;
    }
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly))
    {
        append_console(QStringLiteral("Could not open project manifest: %1").arg(path));
        return false;
    }
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (!document.isObject())
    {
        append_console(QStringLiteral("Project manifest JSON was invalid: %1").arg(parse_error.errorString()));
        return false;
    }
    const auto root = document.object();
    const auto project_id = dragonpixel::core::uuid::parse(
        root.value(QStringLiteral("projectId")).toString().toStdString());
    const auto format_version = root.value(QStringLiteral("formatVersion")).toInt();
    const auto expected_schema = QStringLiteral("https://dragonpixel.dev/schemas/v%1/project.schema.json")
        .arg(format_version);
    auto startup_scene = root.value(QStringLiteral("startupScene")).toString();
    const auto scene_roots = root.value(QStringLiteral("sceneRoots")).toArray();
    if (startup_scene.isEmpty() && format_version == 2 && !scene_roots.isEmpty())
    {
        startup_scene = scene_roots.at(0).toString();
    }
    if (root.value(QStringLiteral("$schema")).toString() != expected_schema
        || root.value(QStringLiteral("format")).toString() != QStringLiteral("dpe.project")
        || (format_version != 1 && format_version != 2)
        || root.value(QStringLiteral("engineVersion")).toString().isEmpty()
        || !project_id || startup_scene.isEmpty() || QDir::isAbsolutePath(startup_scene)
        || (format_version == 2 && scene_roots.isEmpty()))
    {
        append_console(QStringLiteral("Project manifest format, identity, or startup scene was invalid"));
        return false;
    }

    const auto manifest_info = QFileInfo(path);
    const auto canonical_root = manifest_info.absoluteDir().canonicalPath();
    const auto canonical_scene = QFileInfo(
        manifest_info.absoluteDir().filePath(startup_scene)).canonicalFilePath();
#if defined(Q_OS_WIN)
    constexpr auto path_case = Qt::CaseInsensitive;
#else
    constexpr auto path_case = Qt::CaseSensitive;
#endif
    const auto normalized_root = QDir::fromNativeSeparators(canonical_root);
    const auto normalized_scene = QDir::fromNativeSeparators(canonical_scene);
    const auto root_prefix = normalized_root + QLatin1Char{'/'};
    if (canonical_root.isEmpty() || canonical_scene.isEmpty()
        || !normalized_scene.startsWith(root_prefix, path_case))
    {
        append_console(QStringLiteral("Project startup scene escaped the project root or was missing"));
        return false;
    }

    std::ifstream stream{filesystem_path(canonical_scene), std::ios::binary};
    if (!stream)
    {
        append_console(QStringLiteral("Could not open project startup scene: %1").arg(canonical_scene));
        return false;
    }
    const std::string json{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    auto loaded = dragonpixel::serialization::read_scene_json(json, metadata_);
    if (!loaded.value)
    {
        const auto message = loaded.diagnostics.empty()
            ? QStringLiteral("Unknown load error")
            : QString::fromStdString(loaded.diagnostics.front().message);
        append_console(QStringLiteral("Project startup scene failed validation: %1").arg(message), QStringLiteral("Error"));
        return false;
    }
    if (!commit_scene(
            std::move(*loaded.value),
            canonical_scene,
            QDir::toNativeSeparators(manifest_info.absoluteFilePath()),
            canonical_root,
            loaded.migrations.size()))
    {
        return false;
    }
    if (scene_)
    {
        append_console(QStringLiteral("Opened project %1 (%2)")
            .arg(root.value(QStringLiteral("name")).toString(),
                 QString::fromStdString(project_id->to_string())));
    }
    return scene_.has_value();
}

bool EditorWindow::load_scene(const QString& path)
{
    std::ifstream stream{filesystem_path(path), std::ios::binary};
    if (!stream)
    {
        append_console(QStringLiteral("Could not open scene: %1").arg(path));
        return false;
    }
    const std::string json{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    auto loaded = dragonpixel::serialization::read_scene_json(json, metadata_);
    if (!loaded.value)
    {
        const auto message = loaded.diagnostics.empty()
            ? QStringLiteral("Unknown load error")
            : QString::fromStdString(loaded.diagnostics.front().message);
        QMessageBox::critical(this, QStringLiteral("Scene load failed"), message);
        append_console(QStringLiteral("Scene load failed: %1").arg(message));
        return false;
    }

    auto directory = QFileInfo(path).absoluteDir();
    if (directory.dirName().compare(QStringLiteral("Scenes"), Qt::CaseInsensitive) == 0)
    {
        directory.cdUp();
    }
    auto manifest = QString{};
    auto root = directory.absolutePath();
    if (!project_root_.isEmpty())
    {
        const auto candidate_path = QDir::fromNativeSeparators(QFileInfo{path}.absoluteFilePath());
        const auto root_prefix = QDir::fromNativeSeparators(project_root_) + QLatin1Char{'/'};
        if (candidate_path.startsWith(root_prefix, Qt::CaseInsensitive))
        {
            manifest = project_manifest_path_;
            root = project_root_;
        }
    }
    return commit_scene(
        std::move(*loaded.value),
        path,
        manifest,
        root,
        loaded.migrations.size());
}

bool EditorWindow::commit_scene(
    dragonpixel::scene::scene candidate,
    const QString& path,
    const QString& project_manifest,
    const QString& project_root,
    std::size_t migration_count)
{
    PrefabService candidate_prefabs;
    candidate_prefabs.set_metadata(&metadata_);
    candidate_prefabs.set_project_root(project_root);
    auto hydration = candidate_prefabs.hydrate(candidate);
    if (!hydration.scene)
    {
        for (const auto& diagnostic : hydration.diagnostics)
        {
            append_console(diagnostic, QStringLiteral("Error"), QStringLiteral("Prefabs"));
        }
        return false;
    }
    if (!confirm_discard_or_save())
    {
        return false;
    }
    stop_play();
    gizmo_preview_active_ = false;
    gizmo_preview_scene_.reset();
    gizmo_transform_snapshots_.clear();
    prefab_service_ = std::move(candidate_prefabs);
    scene_ = std::move(*hydration.scene);
    scene_->mark_savepoint();
    scene_path_ = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    project_manifest_path_ = project_manifest;
    project_root_ = project_root;
    rebuild_hierarchy();
    rebuild_scene_summary();
    rebuild_assets();
    update_window_title();
    update_action_states();
    append_console(QStringLiteral("Loaded %1 (%2 entities, %3 migration records)")
        .arg(scene_path_)
        .arg(scene_->entities().size())
        .arg(migration_count));
    for (const auto& diagnostic : hydration.diagnostics)
    {
        append_console(diagnostic, QStringLiteral("Warning"), QStringLiteral("Prefabs"));
    }
    refresh_preview();
    return true;
}

bool EditorWindow::save_scene()
{
    if (!scene_ || scene_path_.isEmpty())
    {
        return false;
    }
    prefab_service_.synchronize(*scene_);
    const auto persistent_scene = prefab_service_.persistent_copy(*scene_);
    const auto json = dragonpixel::serialization::write_scene_json(persistent_scene);
    const auto result = dragonpixel::serialization::save_utf8_atomic(
        filesystem_path(scene_path_), json);
    if (result.succeeded)
    {
        append_console(QStringLiteral("Atomically saved scene: %1").arg(scene_path_));
        statusBar()->showMessage(QStringLiteral("Scene saved"), 3000);
        scene_->mark_savepoint();
        update_window_title();
        update_action_states();
        return true;
    }
    else
    {
        QMessageBox::critical(this, QStringLiteral("Save failed"), QString::fromStdString(result.error));
        return false;
    }
}

bool EditorWindow::confirm_discard_or_save()
{
    if (!scene_ || !scene_->is_dirty())
    {
        return true;
    }
    const auto decision = unsaved_prompt_
        ? unsaved_prompt_(QString::fromStdString(scene_->name()))
        : UnsavedDecision::cancel;
    if (decision == UnsavedDecision::save)
    {
        return save_scene();
    }
    return decision == UnsavedDecision::discard;
}

void EditorWindow::rebuild_hierarchy()
{
    const auto selected = selected_entity_ids();
    hierarchy_model_->rebuild(scene_ ? &*scene_ : nullptr);
    hierarchy_->expandAll();
    bool restored = false;
    for (const auto& id : selected)
    {
        const auto source = hierarchy_model_->index_for_entity(id);
        const auto proxy = hierarchy_filter_->mapFromSource(source);
        if (!proxy.isValid())
        {
            continue;
        }
        hierarchy_->selectionModel()->select(
            proxy,
            QItemSelectionModel::Select | QItemSelectionModel::Rows);
        if (!restored)
        {
            hierarchy_->setCurrentIndex(proxy);
            restored = true;
        }
    }
    if (!restored && hierarchy_filter_->rowCount() > 0)
    {
        hierarchy_->setCurrentIndex(hierarchy_filter_->index(0, 0));
    }
}

void EditorWindow::rebuild_scene_summary()
{
    scene_summary_model_->clear();
    if (!scene_)
    {
        return;
    }
    const QStringList rows{
        QStringLiteral("Scene: %1").arg(QString::fromStdString(scene_->name())),
        QStringLiteral("UUID: %1").arg(QString::fromStdString(scene_->id().to_string())),
        QStringLiteral("GameObjects: %1").arg(scene_->entities().size()),
        QStringLiteral("Spatial model: right-handed, Y-up"),
        QStringLiteral("Authoring state: editor authoritative"),
    };
    for (const auto& row : rows)
    {
        auto* item = new QStandardItem{row};
        item->setEditable(false);
        scene_summary_model_->appendRow(item);
    }
}

void EditorWindow::rebuild_assets()
{
    ++project_generation_;
    asset_preview_service_->set_project_generation(project_generation_);
    if (!project_watcher_->files().isEmpty())
    {
        project_watcher_->removePaths(project_watcher_->files());
    }
    if (!project_watcher_->directories().isEmpty())
    {
        project_watcher_->removePaths(project_watcher_->directories());
    }

    if (project_manifest_path_.isEmpty())
    {
        project_index_ = {};
        project_model_->rebuild(project_index_);
        return;
    }

    project_index_ = ProjectIndexService{}.build_candidate(project_manifest_path_);
    project_model_->rebuild(project_index_);
    for (const auto& diagnostic : project_index_.diagnostics)
    {
        const auto severity = diagnostic.severity == ProjectIndexDiagnosticSeverity::error
            ? QStringLiteral("Error")
            : diagnostic.severity == ProjectIndexDiagnosticSeverity::warning
                ? QStringLiteral("Warning")
                : QStringLiteral("Info");
        append_console(
            QStringLiteral("%1: %2")
                .arg(project_index_diagnostic_code_name(diagnostic.code), diagnostic.message),
            severity,
            QStringLiteral("Project Index"),
            diagnostic.document_path,
            {},
            {},
            {},
            {},
            diagnostic.related_id,
            diagnostic.document_path);
    }

    QStringList watched_files;
    QStringList watched_directories;
    const auto add_file = [&watched_files](const QString& path) {
        const auto absolute = QFileInfo{path}.absoluteFilePath();
        if (!absolute.isEmpty() && QFileInfo::exists(absolute) && !watched_files.contains(absolute))
        {
            watched_files.push_back(absolute);
        }
    };
    const auto add_directory = [&watched_directories](const QString& path) {
        const auto absolute = QFileInfo{path}.absoluteFilePath();
        if (!absolute.isEmpty() && QDir{absolute}.exists() && !watched_directories.contains(absolute))
        {
            watched_directories.push_back(absolute);
        }
    };
    add_file(project_manifest_path_);
    if (project_index_.candidate)
    {
        for (const auto& root : project_index_.candidate->roots)
        {
            add_directory(root.absolute_path);
        }
        for (const auto& entry : project_index_.candidate->entries)
        {
            add_file(entry.absolute_path);
            if (!entry.resolved_source_path.isEmpty())
            {
                add_file(entry.resolved_source_path);
            }
            if (entry.kind == ProjectIndexEntryKind::asset)
            {
                static_cast<void>(asset_preview_service_->request_preview(entry));
            }
        }
    }
    if (!watched_files.isEmpty())
    {
        project_watcher_->addPaths(watched_files);
    }
    if (!watched_directories.isEmpty())
    {
        project_watcher_->addPaths(watched_directories);
    }

    project_explorer_->expandAll();
    project_explorer_->resizeColumnToContents(0);
    project_explorer_->resizeColumnToContents(static_cast<int>(ProjectColumn::kind_type));
    project_explorer_->resizeColumnToContents(static_cast<int>(ProjectColumn::overall_status));
    project_explorer_->setColumnHidden(static_cast<int>(ProjectColumn::identifier), true);
    project_explorer_->setColumnHidden(static_cast<int>(ProjectColumn::path), true);
    project_explorer_->setColumnHidden(static_cast<int>(ProjectColumn::structural_status), true);
}

void EditorWindow::schedule_project_refresh()
{
    if (project_refresh_pending_ || project_manifest_path_.isEmpty())
    {
        return;
    }
    project_refresh_pending_ = true;
    QTimer::singleShot(200, this, [this] {
        project_refresh_pending_ = false;
        if (!project_manifest_path_.isEmpty())
        {
            rebuild_assets();
            append_console(
                QStringLiteral("Project Explorer refreshed after an external file change"),
                QStringLiteral("Info"),
                QStringLiteral("Project Index"),
                project_manifest_path_,
                {},
                {},
                {},
                {},
                {},
                project_manifest_path_);
        }
    });
}

void EditorWindow::inspect_selected_entities()
{
    rebuilding_inspector_ = true;
    inspector_model_->clear();
    inspector_model_->setHorizontalHeaderLabels({QStringLiteral("Property"), QStringLiteral("Value")});
    const auto selected_ids = selected_entity_ids();
    if (!scene_ || selected_ids.isEmpty())
    {
        viewport_->set_selected_name({});
        rebuilding_inspector_ = false;
        update_worker_viewport();
        return;
    }
    std::vector<const dragonpixel::scene::entity*> entities;
    for (const auto& id : selected_ids)
    {
        if (const auto* entity = scene_->find_entity(id))
        {
            entities.push_back(entity);
        }
    }
    if (entities.empty())
    {
        viewport_->set_selected_name({});
        rebuilding_inspector_ = false;
        update_worker_viewport();
        return;
    }
    viewport_->set_selected_name(entities.size() == 1
        ? QString::fromStdString(entities.front()->name)
        : QStringLiteral("%1 GameObjects").arg(entities.size()));
    update_viewport_selection_geometry(*scene_);
    QStringList entity_id_strings;
    for (const auto* entity : entities)
    {
        entity_id_strings.push_back(QString::fromStdString(entity->id.to_string()));
    }

    for (const auto& component : entities.front()->components)
    {
        std::vector<const dragonpixel::scene::component_record*> components{&component};
        for (std::size_t index = 1; index < entities.size(); ++index)
        {
            const auto found = std::find_if(
                entities[index]->components.begin(),
                entities[index]->components.end(),
                [&](const auto& candidate) { return candidate.type_id == component.type_id; });
            if (found == entities[index]->components.end())
            {
                components.clear();
                break;
            }
            components.push_back(&*found);
        }
        if (components.empty())
        {
            continue;
        }
        const auto* descriptor = metadata_.find(component.type_id);
        const auto opaque = std::any_of(components.begin(), components.end(), [](const auto* value) {
            return value->opaque;
        });
        const auto label = opaque
            ? QStringLiteral("Opaque \u2014 %1 v%2").arg(QString::fromStdString(component.qualified_name)).arg(component.schema_version)
            : QString::fromStdString(descriptor != nullptr ? descriptor->display_name : component.type_id);
        auto* component_item = new QStandardItem{label};
        component_item->setData(QString::fromStdString(component.type_id), EditorRoles::component_type);
        component_item->setData(entity_id_strings, EditorRoles::entity_ids);
        component_item->setEditable(false);
        auto* owner_item = new QStandardItem{owner_text(component.owner)};
        owner_item->setEditable(false);
        inspector_model_->appendRow({component_item, owner_item});
        if (opaque || descriptor == nullptr)
        {
            auto* raw_name = new QStandardItem{QStringLiteral("Raw preserved record")};
            auto* raw_value = new QStandardItem{QString::fromStdString(component.raw_record.dump())};
            raw_name->setEditable(false);
            raw_value->setEditable(false);
            raw_value->setToolTip(QStringLiteral("Unavailable/newer component data is read-only and will round-trip."));
            component_item->appendRow({raw_name, raw_value});
            continue;
        }
        component_item->setCheckable(true);
        const auto all_enabled = std::all_of(components.begin(), components.end(), [](const auto* value) {
            return value->enabled;
        });
        const auto none_enabled = std::none_of(components.begin(), components.end(), [](const auto* value) {
            return value->enabled;
        });
        component_item->setCheckState(all_enabled ? Qt::Checked : (none_enabled ? Qt::Unchecked : Qt::PartiallyChecked));
        for (const auto& property : descriptor->properties)
        {
            const auto found = component.properties.find(property.property_id);
            const auto value = found == component.properties.end() ? nlohmann::ordered_json{nullptr} : *found;
            const auto mixed = std::any_of(components.begin() + 1, components.end(), [&](const auto* other) {
                const auto other_found = other->properties.find(property.property_id);
                const auto other_value = other_found == other->properties.end()
                    ? nlohmann::ordered_json{nullptr}
                    : *other_found;
                return other_value != value;
            });
            auto* property_name = new QStandardItem{QString::fromStdString(property.display_name)};
            property_name->setEditable(false);
            auto* property_value = new QStandardItem{
                mixed ? QStringLiteral("<mixed>") : QString::fromStdString(value.dump())};
            property_value->setData(entity_id_strings, EditorRoles::entity_ids);
            property_value->setData(QString::fromStdString(component.type_id), EditorRoles::component_type);
            property_value->setData(QString::fromStdString(property.property_id), EditorRoles::property_id);
            property_value->setData(static_cast<int>(property.type), EditorRoles::value_type);
            QStringList choices;
            for (const auto& choice : property.enum_choices)
            {
                choices.push_back(QString::fromStdString(choice));
            }
            property_value->setData(choices, EditorRoles::enum_choices);
            if (property.minimum)
            {
                property_value->setData(*property.minimum, EditorRoles::minimum);
            }
            if (property.maximum)
            {
                property_value->setData(*property.maximum, EditorRoles::maximum);
            }
            if (property.step)
            {
                property_value->setData(*property.step, EditorRoles::step);
            }
            property_value->setData(QString::fromStdString(property.drawer_key), EditorRoles::drawer_key);
            property_value->setToolTip(QStringLiteral("%1%2%3")
                .arg(property.tooltip.empty() ? QStringLiteral("Typed Inspector property") : QString::fromStdString(property.tooltip),
                     property.units.empty() ? QString{} : QStringLiteral(" · %1").arg(QString::fromStdString(property.units)),
                     property.drawer_key.empty() ? QString{} : QStringLiteral(" · Drawer: %1").arg(QString::fromStdString(property.drawer_key))));
            if (!property.read_only)
            {
                property_value->setEditable(true);
            }
            component_item->appendRow({property_name, property_value});
        }
    }
    inspector_->expandAll();
    inspector_->resizeColumnToContents(0);
    rebuilding_inspector_ = false;
    update_worker_viewport();
}

bool EditorWindow::edit_hierarchy_entity(
    const dragonpixel::core::uuid& id,
    const QString& name,
    bool enabled)
{
    if (!scene_ || name.trimmed().isEmpty())
    {
        return false;
    }
    const auto* entity = scene_->find_entity(id);
    if (entity == nullptr)
    {
        return false;
    }
    std::vector<dragonpixel::scene::command> commands;
    if (entity->name != name.trimmed().toStdString())
    {
        commands.emplace_back(dragonpixel::scene::rename_entity_command{id, name.trimmed().toStdString()});
    }
    if (entity->enabled != enabled)
    {
        commands.emplace_back(dragonpixel::scene::set_entity_enabled_command{id, enabled});
    }
    if (commands.empty())
    {
        return true;
    }
    if (!apply_authoring_transaction(std::move(commands), "Edit GameObject"))
    {
        append_console(QStringLiteral("GameObject edit rejected"), QStringLiteral("Warning"));
        return false;
    }
    // Hierarchy edits arrive from QStandardItemModel::itemChanged while the
    // delegate is still committing its QModelIndex. Rebuild the service-backed
    // view only after that commit returns.
    QMetaObject::invokeMethod(this, [this, id] {
        after_scene_mutation(QStringLiteral("GameObject edited through command validation"), {id});
    }, Qt::QueuedConnection);
    return true;
}

bool EditorWindow::drag_reparent_entity(
    const dragonpixel::core::uuid& id,
    const std::optional<dragonpixel::core::uuid>& parent,
    std::optional<std::size_t> sibling_index)
{
    if (!scene_)
    {
        return false;
    }
    if (!apply_authoring_transaction({dragonpixel::scene::command{
            dragonpixel::scene::reparent_entity_command{id, parent, sibling_index}}},
            "Reparent GameObject"))
    {
        append_console(QStringLiteral("Reparent rejected: hierarchy would be invalid"), QStringLiteral("Warning"));
        QMetaObject::invokeMethod(this, [this] {
            rebuild_hierarchy();
        }, Qt::QueuedConnection);
        return false;
    }
    QMetaObject::invokeMethod(this, [this, id] {
        after_scene_mutation(QStringLiteral("GameObject reparented through command validation"), {id});
    }, Qt::QueuedConnection);
    return true;
}

void EditorWindow::edit_inspector_item(QStandardItem* item)
{
    if (rebuilding_inspector_ || !scene_ || item == nullptr)
    {
        return;
    }
    const auto property_id = item->data(EditorRoles::property_id).toString();
    const auto component_type = item->data(EditorRoles::component_type).toString();
    const auto entity_ids = item->data(EditorRoles::entity_ids).toStringList();
    if (component_type.isEmpty() || entity_ids.isEmpty())
    {
        return;
    }
    try
    {
        std::vector<dragonpixel::scene::command> commands;
        if (property_id.isEmpty())
        {
            const auto enabled = item->checkState() == Qt::Checked;
            for (const auto& id_text : entity_ids)
            {
                const auto id = dragonpixel::core::uuid::parse(id_text.toStdString());
                if (id)
                {
                    commands.emplace_back(dragonpixel::scene::set_component_enabled_command{
                        *id, component_type.toStdString(), enabled});
                }
            }
        }
        else
        {
            auto value = nlohmann::ordered_json::parse(item->text().toStdString());
            for (const auto& id_text : entity_ids)
            {
                const auto id = dragonpixel::core::uuid::parse(id_text.toStdString());
                if (id)
                {
                    commands.emplace_back(dragonpixel::scene::set_component_property_command{
                        *id, component_type.toStdString(), property_id.toStdString(), value});
                }
            }
        }
        if (!apply_authoring_transaction(std::move(commands), "Edit Inspector value"))
        {
            throw std::runtime_error{"Command rejected"};
        }
        const auto message = property_id.isEmpty()
            ? QStringLiteral("Component enabled state changed through command validation")
            : QStringLiteral("Edited %1 through command validation").arg(property_id);
        const auto selected = selected_entity_ids();
        // As with inline hierarchy rename, itemChanged is synchronous with the
        // delegate commit. Defer model rebuilds until the editor has released
        // its QModelIndex.
        QMetaObject::invokeMethod(this, [this, message, selected] {
            after_scene_mutation(message, selected);
        }, Qt::QueuedConnection);
    }
    catch (const std::exception& exception)
    {
        append_console(QStringLiteral("Property edit rejected: %1").arg(QString::fromUtf8(exception.what())));
        QMetaObject::invokeMethod(this, [this] {
            inspect_selected_entities();
        }, Qt::QueuedConnection);
    }
}

void EditorWindow::create_preset(dragonpixel::scene::entity_preset preset, const QString& asset_override)
{
    if (!scene_)
    {
        return;
    }
    const auto base_name = [preset] {
        switch (preset)
        {
            case dragonpixel::scene::entity_preset::sprite: return QStringLiteral("Sprite");
            case dragonpixel::scene::entity_preset::cube: return QStringLiteral("Cube");
            case dragonpixel::scene::entity_preset::camera: return QStringLiteral("Camera");
            case dragonpixel::scene::entity_preset::light: return QStringLiteral("Light");
            default: return QStringLiteral("GameObject");
        }
    }();
    auto name = base_name;
    int suffix = 2;
    auto name_exists = [this](const QString& candidate) {
        return std::any_of(scene_->entities().begin(), scene_->entities().end(), [&](const auto& entity) {
            return QString::fromStdString(entity.name).compare(candidate, Qt::CaseInsensitive) == 0;
        });
    };
    while (name_exists(name))
    {
        name = QStringLiteral("%1 %2").arg(base_name).arg(suffix++);
    }
    const auto id = dragonpixel::core::uuid::random_v4();
    const auto selected = selected_entity_ids();
    const auto parent = selected.size() == 1
        ? std::optional<dragonpixel::core::uuid>{selected.front()}
        : std::nullopt;
    std::optional<std::string> primary_asset;
    if (!asset_override.isEmpty())
    {
        primary_asset = asset_override.toStdString();
    }
    else if (project_explorer_->currentIndex().isValid())
    {
        const auto source = project_filter_->mapToSource(project_explorer_->currentIndex());
        const auto asset_id = ProjectModel::asset_id(source);
        const auto asset_type = ProjectModel::asset_type(source).toLower();
        const auto compatible = (preset == dragonpixel::scene::entity_preset::sprite && asset_type.contains(QStringLiteral("sprite")))
            || (preset == dragonpixel::scene::entity_preset::cube && asset_type.contains(QStringLiteral("mesh")));
        if (compatible && !asset_id.isEmpty())
        {
            primary_asset = asset_id.toStdString();
        }
    }
    if (!apply_authoring_transaction({dragonpixel::scene::command{
        dragonpixel::scene::create_preset_command{
            id,
            name.toStdString(),
            preset,
            parent,
            std::nullopt,
            primary_asset,
            std::nullopt}}}, "Create GameObject preset"))
    {
        append_console(QStringLiteral("GameObject preset creation was rejected"), QStringLiteral("Warning"));
        return;
    }
    after_scene_mutation(QStringLiteral("Created %1 preset through one validated transaction").arg(name), {id});
}

void EditorWindow::duplicate_selected()
{
    if (!scene_)
    {
        return;
    }
    const auto root_id = selected_entity_id();
    const auto* root = root_id ? scene_->find_entity(*root_id) : nullptr;
    if (!root_id || root == nullptr)
    {
        return;
    }
    std::vector<dragonpixel::scene::entity_id_remap> remaps;
    std::vector<dragonpixel::core::uuid> frontier{*root_id};
    for (std::size_t index = 0; index < frontier.size(); ++index)
    {
        const auto parent = frontier[index];
        remaps.push_back({parent, dragonpixel::core::uuid::random_v4()});
        for (const auto& entity : scene_->entities())
        {
            if (entity.parent_id == parent)
            {
                frontier.push_back(entity.id);
            }
        }
    }
    const auto duplicate_id = remaps.front().duplicate_id;
    if (apply_authoring_transaction({dragonpixel::scene::command{
        dragonpixel::scene::duplicate_subtree_command{
            *root_id,
            std::move(remaps),
            std::nullopt,
            std::nullopt,
            root->name + " Copy",
            true}}}, "Duplicate GameObject subtree"))
    {
        after_scene_mutation(QStringLiteral("Duplicated GameObject subtree with remapped UUID references"), {duplicate_id});
    }
    else
    {
        append_console(QStringLiteral("Duplicate subtree was rejected"), QStringLiteral("Warning"));
    }
}

void EditorWindow::delete_selected_subtree()
{
    if (!scene_)
    {
        return;
    }
    const auto root_id = selected_entity_id();
    const auto* root = root_id ? scene_->find_entity(*root_id) : nullptr;
    if (!root_id || root == nullptr)
    {
        return;
    }
    std::vector<dragonpixel::core::uuid> descendants{*root_id};
    for (std::size_t index = 0; index < descendants.size(); ++index)
    {
        for (const auto& entity : scene_->entities())
        {
            if (entity.parent_id == descendants[index])
            {
                descendants.push_back(entity.id);
            }
        }
    }
    if (delete_prompt_ && !delete_prompt_(QString::fromStdString(root->name), static_cast<int>(descendants.size())))
    {
        return;
    }
    if (apply_authoring_transaction({dragonpixel::scene::command{
            dragonpixel::scene::delete_subtree_command{*root_id}}},
            "Delete GameObject subtree"))
    {
        after_scene_mutation(QStringLiteral("Deleted %1 GameObject(s); Undo restores payloads and ordering").arg(descendants.size()));
    }
    else
    {
        append_console(QStringLiteral("Delete subtree was rejected"), QStringLiteral("Warning"));
    }
}

void EditorWindow::reparent_entity()
{
    if (!scene_)
    {
        return;
    }
    const auto selected = selected_entity_id();
    if (!selected)
    {
        return;
    }
    QStringList labels{QStringLiteral("<Scene root>")};
    QHash<QString, QString> ids;
    for (const auto& entity : scene_->entities())
    {
        if (entity.id == *selected)
        {
            continue;
        }
        const auto label = QStringLiteral("%1 — %2").arg(QString::fromStdString(entity.name), QString::fromStdString(entity.id.to_string()));
        labels.push_back(label);
        ids.insert(label, QString::fromStdString(entity.id.to_string()));
    }
    bool accepted = false;
    const auto choice = QInputDialog::getItem(this, QStringLiteral("Reparent Entity"), QStringLiteral("New parent"), labels, 0, false, &accepted);
    if (!accepted)
    {
        return;
    }
    std::optional<dragonpixel::core::uuid> parent;
    if (choice != QStringLiteral("<Scene root>"))
    {
        parent = dragonpixel::core::uuid::parse(ids.value(choice).toStdString());
    }
    const auto succeeded = apply_authoring_transaction(
        {dragonpixel::scene::command{dragonpixel::scene::reparent_entity_command{
            *selected, parent, std::nullopt}}},
        "Reparent GameObject");
    append_console(succeeded ? QStringLiteral("Entity reparented through command validation") : QStringLiteral("Reparent rejected: hierarchy would be invalid"));
    if (succeeded)
    {
        after_scene_mutation(QStringLiteral("GameObject reparented through command validation"), {*selected});
    }
    else
    {
        rebuild_hierarchy();
    }
}

void EditorWindow::add_component()
{
    if (!scene_)
    {
        return;
    }
    const auto entity_id = selected_entity_id();
    const auto* entity = selected_entity();
    if (!entity_id || entity == nullptr)
    {
        return;
    }
    QStringList choices;
    QHash<QString, const dragonpixel::metadata::component_descriptor*> descriptors;
    for (const auto& reference : metadata_.descriptors())
    {
        const auto& descriptor = reference.get();
        const auto exists = std::any_of(entity->components.begin(), entity->components.end(), [&](const auto& component) {
            return component.type_id == descriptor.type_id;
        });
        if (!exists)
        {
            const auto label = QStringLiteral("%1 — %2").arg(QString::fromStdString(descriptor.display_name), QString::fromStdString(descriptor.type_id));
            choices.push_back(label);
            descriptors.insert(label, &descriptor);
        }
    }
    if (choices.isEmpty())
    {
        append_console(QStringLiteral("Selected entity already has every Slice 1 component type"));
        return;
    }
    bool accepted = false;
    const auto choice = QInputDialog::getItem(this, QStringLiteral("Add Component"), QStringLiteral("Component"), choices, 0, false, &accepted);
    if (!accepted)
    {
        return;
    }
    const auto* descriptor = descriptors.value(choice, nullptr);
    if (descriptor == nullptr)
    {
        return;
    }
    nlohmann::ordered_json properties = nlohmann::ordered_json::object();
    for (const auto& property : descriptor->properties)
    {
        if (!property.default_json.empty())
        {
            properties[property.property_id] = nlohmann::ordered_json::parse(property.default_json);
        }
        else
        {
            properties[property.property_id] = default_value(property.type);
        }
    }
    dragonpixel::scene::component_record component{
        descriptor->type_id,
        descriptor->schema_version,
        descriptor->owner,
        std::move(properties),
        false,
        nlohmann::ordered_json::object(),
        true,
        descriptor->qualified_name,
    };
    if (apply_authoring_transaction(
            {dragonpixel::scene::command{dragonpixel::scene::upsert_component_command{
                *entity_id, std::move(component)}}},
            "Add component"))
    {
        after_scene_mutation(QStringLiteral("Component added through command validation"), {*entity_id});
    }
}

void EditorWindow::remove_component()
{
    if (!scene_ || !inspector_->currentIndex().isValid())
    {
        return;
    }
    auto index = inspector_->currentIndex();
    while (index.parent().isValid())
    {
        index = index.parent();
    }
    const auto type_id = index.siblingAtColumn(0).data(EditorRoles::component_type).toString();
    const auto entity_ids = selected_entity_ids();
    if (type_id.isEmpty() || entity_ids.isEmpty())
    {
        return;
    }
    std::vector<dragonpixel::scene::command> commands;
    for (const auto& entity_id : entity_ids)
    {
        commands.emplace_back(dragonpixel::scene::remove_component_command{entity_id, type_id.toStdString()});
    }
    if (apply_authoring_transaction(std::move(commands), "Remove component"))
    {
        after_scene_mutation(QStringLiteral("Component removed through command validation"), entity_ids);
    }
}

void EditorWindow::undo()
{
    if (!scene_)
    {
        return;
    }
    if (gizmo_preview_active_)
    {
        cancel_gizmo_preview();
    }

    const auto history_position = scene_->history_position();
    QStringList source_diagnostics;
    if (!prefab_service_.prepare_undo_source_change(history_position, source_diagnostics))
    {
        for (const auto& diagnostic : source_diagnostics)
        {
            append_console(diagnostic, QStringLiteral("Error"), QStringLiteral("Prefabs"));
        }
        return;
    }
    const auto result = scene_->undo();
    if (result.succeeded)
    {
        prefab_service_.synchronize(*scene_);
        after_scene_mutation(QStringLiteral("Undid authoring transaction"));
        return;
    }

    // A source mutation is restored before its paired authoring transaction so
    // the source and scene cannot be observed at different history positions.
    // If the scene unexpectedly rejects Undo, put the source back as well.
    QStringList rollback_diagnostics;
    if (!prefab_service_.prepare_redo_source_change(history_position, rollback_diagnostics))
    {
        for (const auto& diagnostic : rollback_diagnostics)
        {
            append_console(diagnostic, QStringLiteral("Error"), QStringLiteral("Prefabs"));
        }
    }
}

void EditorWindow::redo()
{
    if (!scene_)
    {
        return;
    }
    if (gizmo_preview_active_)
    {
        cancel_gizmo_preview();
    }

    const auto target_history_position = scene_->history_position() + 1;
    QStringList source_diagnostics;
    if (!prefab_service_.prepare_redo_source_change(target_history_position, source_diagnostics))
    {
        for (const auto& diagnostic : source_diagnostics)
        {
            append_console(diagnostic, QStringLiteral("Error"), QStringLiteral("Prefabs"));
        }
        return;
    }
    const auto result = scene_->redo();
    if (result.succeeded)
    {
        prefab_service_.synchronize(*scene_);
        after_scene_mutation(QStringLiteral("Redid authoring transaction"));
        return;
    }

    // Mirror the Undo ordering above: if the scene cannot advance, restore the
    // prefab source to the preceding history state.
    QStringList rollback_diagnostics;
    if (!prefab_service_.prepare_undo_source_change(target_history_position, rollback_diagnostics))
    {
        for (const auto& diagnostic : rollback_diagnostics)
        {
            append_console(diagnostic, QStringLiteral("Error"), QStringLiteral("Prefabs"));
        }
    }
}

void EditorWindow::assign_selected_asset(const QModelIndex& source_index)
{
    if (!scene_ || ProjectModel::item_kind(source_index) != ProjectItemKind::asset)
    {
        return;
    }
    const auto asset_id = ProjectModel::asset_id(source_index);
    const auto asset_type = ProjectModel::asset_type(source_index).toLower();
    if (asset_id.isEmpty())
    {
        append_console(QStringLiteral("Asset metadata has no stable assetId"), QStringLiteral("Warning"), QStringLiteral("Assets"));
        return;
    }
    std::vector<dragonpixel::scene::command> commands;
    for (const auto& entity_id : selected_entity_ids())
    {
        const auto* entity = scene_->find_entity(entity_id);
        if (entity == nullptr)
        {
            continue;
        }
        for (const auto& component : entity->components)
        {
            QString property;
            if (asset_type.contains(QStringLiteral("sprite"))
                && component.type_id == dragonpixel::metadata::builtin_component_ids::sprite)
            {
                property = QStringLiteral("dpe.sprite.asset");
            }
            else if (asset_type.contains(QStringLiteral("mesh"))
                && component.type_id == dragonpixel::metadata::builtin_component_ids::mesh)
            {
                property = QStringLiteral("dpe.mesh.asset");
            }
            else if (asset_type.contains(QStringLiteral("material"))
                && component.type_id == dragonpixel::metadata::builtin_component_ids::mesh)
            {
                property = QStringLiteral("dpe.mesh.material");
            }
            if (!property.isEmpty())
            {
                commands.emplace_back(dragonpixel::scene::set_component_property_command{
                    entity_id,
                    component.type_id,
                    property.toStdString(),
                    asset_id.toStdString()});
                break;
            }
        }
    }
    if (commands.empty())
    {
        append_console(QStringLiteral("Selected asset is not compatible with the selected GameObject(s)"), QStringLiteral("Warning"), QStringLiteral("Assets"));
        return;
    }
    const auto selected = selected_entity_ids();
    if (apply_authoring_transaction(std::move(commands), "Assign asset"))
    {
        after_scene_mutation(QStringLiteral("Assigned asset %1 through validated commands").arg(asset_id), selected);
    }
}

bool EditorWindow::apply_authoring_transaction(
    std::vector<dragonpixel::scene::command> commands,
    std::string description)
{
    if (!scene_ || commands.empty())
    {
        return false;
    }
    dragonpixel::scene::command_validation_context validation{metadata_};
    validation.current_scene = &*scene_;
    validation.asset_is_compatible = [this](
                                         std::string_view reference,
                                         const dragonpixel::metadata::property_descriptor& property) {
        const auto asset_reference = QString::fromUtf8(
            reference.data(), static_cast<qsizetype>(reference.size()));
        if (asset_reference.startsWith(QStringLiteral("builtin://"), Qt::CaseInsensitive)
            || asset_reference.startsWith(QStringLiteral("generated://"), Qt::CaseInsensitive))
        {
            return true;
        }
        if (!project_index_.candidate)
        {
            return false;
        }
        const auto* entry = project_index_.candidate->find_by_id(asset_reference);
        if (entry == nullptr || entry->kind != ProjectIndexEntryKind::asset
            || !entry->structurally_valid)
        {
            return false;
        }
        if (property.reference_filter.empty())
        {
            return true;
        }
        return entry->asset_type.contains(
            QString::fromStdString(property.reference_filter), Qt::CaseInsensitive);
    };
    auto validated = dragonpixel::scene::validate_and_normalize_commands(commands, validation);
    for (const auto& diagnostic : validated.diagnostics)
    {
        const auto severity = diagnostic.severity == dragonpixel::core::diagnostic_severity::error
            ? QStringLiteral("Error")
            : diagnostic.severity == dragonpixel::core::diagnostic_severity::warning
                ? QStringLiteral("Warning")
                : QStringLiteral("Info");
        append_console(
            QStringLiteral("%1: %2")
                .arg(QString::fromStdString(diagnostic.code), QString::fromStdString(diagnostic.message)),
            severity,
            QStringLiteral("Command Validation"),
            QString::fromStdString(diagnostic.context),
            {},
            {},
            QStringLiteral("command-%1").arg(command_revision_ + 1));
    }
    if (!validated.succeeded)
    {
        return false;
    }

    auto augmented = prefab_service_.augment_commands(*scene_, validated.commands);
    for (const auto& diagnostic : augmented.diagnostics)
    {
        append_console(diagnostic, QStringLiteral("Warning"), QStringLiteral("Prefabs"));
    }
    if (!augmented.succeeded)
    {
        return false;
    }
    const auto previous_history_position = scene_->history_position();
    const auto result = scene_->apply_transaction(augmented.commands, std::move(description));
    if (!result.succeeded)
    {
        append_console(
            result.diagnostic
                ? QString::fromStdString(result.diagnostic->message)
                : QStringLiteral("Authoring command transaction was rejected."),
            QStringLiteral("Warning"),
            QStringLiteral("Commands"));
        return false;
    }
    // A successful authoring branch invalidates any source-file mutations on
    // the abandoned redo branch at the same time as the scene history does.
    prefab_service_.truncate_source_journal(previous_history_position);
    prefab_service_.synchronize(*scene_);
    return true;
}

void EditorWindow::report_prefab_result(PrefabOperationResult result)
{
    for (const auto& diagnostic : result.diagnostics)
    {
        append_console(diagnostic, QStringLiteral("Warning"), QStringLiteral("Prefabs"));
    }
    if (!result.succeeded)
    {
        append_console(result.message, QStringLiteral("Warning"), QStringLiteral("Prefabs"));
        update_action_states();
        return;
    }
    QList<dragonpixel::core::uuid> selected;
    for (const auto& id : result.selection)
    {
        selected.push_back(id);
    }
    rebuild_assets();
    after_scene_mutation(result.message, selected);
}

void EditorWindow::instantiate_prefab(const QString& source_path)
{
    if (!scene_ || source_path.isEmpty())
    {
        return;
    }
    const auto selected = selected_entity_ids();
    const auto parent = selected.size() == 1
        ? std::optional<dragonpixel::core::uuid>{selected.front()}
        : std::nullopt;
    report_prefab_result(prefab_service_.instantiate(*scene_, source_path, parent));
}

void EditorWindow::create_prefab_from_selection()
{
    if (!scene_)
    {
        return;
    }
    const auto selected = selected_entity_id();
    const auto* entity = selected ? scene_->find_entity(*selected) : nullptr;
    if (!selected || entity == nullptr)
    {
        append_console(QStringLiteral("Select one locally owned GameObject before creating a prefab."),
            QStringLiteral("Warning"), QStringLiteral("Prefabs"));
        return;
    }
    auto safe_name = QString::fromStdString(entity->name);
    safe_name.replace(QRegularExpression{QStringLiteral("[^A-Za-z0-9_-]+")}, QStringLiteral("_"));
    const auto suggested = QDir{project_root_}.filePath(
        QStringLiteral("Prefabs/%1.dpeprefab").arg(safe_name));
    const auto path = prefab_path_prompt_
        ? prefab_path_prompt_(suggested)
        : QFileDialog::getSaveFileName(
            this,
            QStringLiteral("Create linked prefab from selection"),
            suggested,
            QStringLiteral("Dragon Pixel Prefab (*.dpeprefab)"));
    if (!path.isEmpty())
    {
        report_prefab_result(prefab_service_.create_from_selection(*scene_, *selected, path));
    }
}

void EditorWindow::apply_prefab()
{
    if (!scene_)
    {
        return;
    }
    const auto selected = selected_entity_id();
    if (!selected)
    {
        return;
    }
    const auto levels = prefab_service_.apply_levels_for_entity(*selected);
    if (levels.empty())
    {
        append_console(QStringLiteral("Apply is unavailable because the selected prefab source is missing."),
            QStringLiteral("Warning"), QStringLiteral("Prefabs"));
        return;
    }
    QStringList labels;
    for (const auto& level : levels)
    {
        labels.push_back(level.label);
    }
    int selected_level = -1;
    if (prefab_level_prompt_)
    {
        selected_level = prefab_level_prompt_(labels);
    }
    else
    {
        bool accepted = false;
        const auto choice = QInputDialog::getItem(
            this,
            QStringLiteral("Apply prefab overrides"),
            QStringLiteral("Explicit target level"),
            labels,
            0,
            false,
            &accepted);
        if (accepted)
        {
            selected_level = labels.indexOf(choice);
        }
    }
    if (selected_level >= 0 && selected_level < static_cast<int>(levels.size()))
    {
        report_prefab_result(prefab_service_.apply(
            *scene_, *selected, levels[static_cast<std::size_t>(selected_level)].nesting_path));
    }
}

void EditorWindow::revert_selected_prefab()
{
    if (scene_)
    {
        const auto selected = selected_entity_id();
        if (selected)
        {
            report_prefab_result(prefab_service_.revert_selected(*scene_, *selected));
        }
    }
}

void EditorWindow::revert_all_prefab()
{
    if (scene_)
    {
        const auto selected = selected_entity_id();
        if (selected)
        {
            report_prefab_result(prefab_service_.revert_all(*scene_, *selected));
        }
    }
}

void EditorWindow::repair_prefab()
{
    if (scene_)
    {
        const auto selected = selected_entity_id();
        if (selected)
        {
            report_prefab_result(prefab_service_.repair_rebase(*scene_, *selected));
        }
    }
}

void EditorWindow::unpack_prefab(bool completely)
{
    if (scene_)
    {
        const auto selected = selected_entity_id();
        if (selected)
        {
            report_prefab_result(prefab_service_.unpack(*scene_, *selected, completely));
        }
    }
}

void EditorWindow::activate_project_item(const QModelIndex& proxy_index)
{
    const auto source = project_filter_->mapToSource(proxy_index.siblingAtColumn(0));
    switch (ProjectModel::item_kind(source))
    {
        case ProjectItemKind::scene:
            load_scene(ProjectModel::item_path(source));
            break;
        case ProjectItemKind::asset:
            assign_selected_asset(source);
            break;
        case ProjectItemKind::prefab:
            instantiate_prefab(ProjectModel::item_path(source));
            break;
        default:
            break;
    }
}

void EditorWindow::apply_workspace(const QString& workspace)
{
    const auto debug = workspace == QStringLiteral("Debug");
    const auto mode_2d = workspace == QStringLiteral("2D");
    scene_dock_->setVisible(debug);
    hierarchy_dock_->setVisible(true);
    assets_dock_->setVisible(true);
    inspector_dock_->setVisible(true);
    console_dock_->setVisible(debug);
    viewport_->set_view_mode(mode_2d ? AuthoringViewport::ViewMode::two_d : AuthoringViewport::ViewMode::three_d);
    QSettings settings{editor_settings_path(), QSettings::IniFormat};
    settings.setValue(QStringLiteral("workspace/current"), workspace);
    statusBar()->showMessage(QStringLiteral("%1 workspace active").arg(workspace), 3000);
}

void EditorWindow::reset_workspace()
{
    addDockWidget(Qt::LeftDockWidgetArea, scene_dock_);
    addDockWidget(Qt::LeftDockWidgetArea, hierarchy_dock_);
    addDockWidget(Qt::LeftDockWidgetArea, assets_dock_);
    addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);
    addDockWidget(Qt::BottomDockWidgetArea, console_dock_);
    tabifyDockWidget(scene_dock_, hierarchy_dock_);
    hierarchy_dock_->raise();
    for (auto* dock : {scene_dock_, hierarchy_dock_, assets_dock_, inspector_dock_, console_dock_})
    {
        dock->show();
    }
}

void EditorWindow::after_scene_mutation(
    const QString& message,
    const QList<dragonpixel::core::uuid>& select)
{
    if (scene_)
    {
        prefab_service_.synchronize(*scene_);
    }
    ++command_revision_;
    rebuild_hierarchy();
    if (!select.isEmpty())
    {
        hierarchy_->selectionModel()->clearSelection();
        bool current_set = false;
        for (const auto& id : select)
        {
            const auto proxy = hierarchy_filter_->mapFromSource(hierarchy_model_->index_for_entity(id));
            if (!proxy.isValid())
            {
                continue;
            }
            hierarchy_->selectionModel()->select(proxy, QItemSelectionModel::Select | QItemSelectionModel::Rows);
            if (!current_set)
            {
                hierarchy_->setCurrentIndex(proxy);
                current_set = true;
            }
        }
    }
    rebuild_scene_summary();
    inspect_selected_entities();
    update_window_title();
    update_action_states();
    append_console(message);
    refresh_preview();
}

void EditorWindow::update_action_states()
{
    const auto loaded = scene_.has_value();
    const auto selected = !selected_entity_ids().isEmpty();
    const auto selected_id = selected_entity_id();
    const auto linked = selected_id && prefab_service_.has_instance_for_entity(*selected_id);
    const auto source_available = selected_id
        && prefab_service_.source_available_for_entity(*selected_id);
    const auto has_overrides = selected_id
        && prefab_service_.has_overrides_for_entity(*selected_id);
    if (save_action_ != nullptr)
    {
        save_action_->setEnabled(loaded && scene_->is_dirty());
    }
    if (undo_action_ != nullptr)
    {
        undo_action_->setEnabled(loaded && scene_->can_undo());
    }
    if (redo_action_ != nullptr)
    {
        redo_action_->setEnabled(loaded && scene_->can_redo());
    }
    for (auto* action : {duplicate_action_, delete_action_, reparent_action_, add_component_action_})
    {
        if (action != nullptr)
        {
            action->setEnabled(loaded && selected);
        }
    }
    if (remove_component_action_ != nullptr)
    {
        remove_component_action_->setEnabled(loaded && selected && inspector_->currentIndex().isValid());
    }
    if (duplicate_action_ != nullptr)
    {
        duplicate_action_->setEnabled(loaded && selected && !linked);
    }
    if (create_prefab_action_ != nullptr)
    {
        create_prefab_action_->setEnabled(loaded && selected && !linked);
        instantiate_prefab_action_->setEnabled(loaded);
        apply_prefab_action_->setEnabled(linked && source_available && has_overrides);
        revert_selected_prefab_action_->setEnabled(linked && source_available && has_overrides);
        revert_all_prefab_action_->setEnabled(linked && source_available && has_overrides);
        repair_prefab_action_->setEnabled(linked && source_available);
        unpack_prefab_action_->setEnabled(linked && source_available);
        unpack_completely_prefab_action_->setEnabled(linked);
    }
    if (play_action_ != nullptr)
    {
        play_action_->setEnabled(loaded && !play_running_);
        simulate_action_->setEnabled(loaded && !play_running_);
        pause_action_->setEnabled(play_running_ && !play_paused_);
        resume_action_->setEnabled(play_running_ && play_paused_);
        stop_action_->setEnabled(play_running_);
    }
    if (adapter_ != nullptr)
    {
        adapter_->setEnabled(!play_running_);
    }
}

void EditorWindow::update_window_title()
{
    if (!scene_)
    {
        setWindowTitle(QStringLiteral("Dragon Pixel Engine Editor \u2014 No Project"));
        return;
    }
    setWindowTitle(QStringLiteral("Dragon Pixel Engine Editor \u2014 %1%2")
        .arg(QString::fromStdString(scene_->name()), scene_->is_dirty() ? QStringLiteral(" *") : QString{}));
}

void EditorWindow::update_worker_viewport()
{
    if (preview_worker_ == nullptr || editor_camera_.isEmpty())
    {
        return;
    }
    QStringList selection;
    for (const auto& id : selected_entity_ids())
    {
        selection.push_back(QString::fromStdString(id.to_string()));
    }
    preview_worker_->update_viewport(editor_camera_, selection, camera_revision_, command_revision_);
}

void EditorWindow::update_viewport_selection_geometry(
    const dragonpixel::scene::scene& geometry_scene)
{
    bool found_geometry = false;
    QVector3D minimum;
    QVector3D maximum;
    QQuaternion first_rotation;
    for (const auto& entity_id : selected_entity_ids())
    {
        const auto* entity = geometry_scene.find_entity(entity_id);
        const auto* transform = entity == nullptr ? nullptr : editable_transform(*entity);
        if (transform == nullptr)
        {
            continue;
        }
        const auto position = json_vector(transform->properties.value(
            "dpe.transform.position",
            nlohmann::ordered_json{{"x", 0.0}, {"y", 0.0}, {"z", 0.0}}));
        if (!found_geometry)
        {
            minimum = position;
            maximum = position;
            first_rotation = json_quaternion(transform->properties.value(
                "dpe.transform.rotation",
                nlohmann::ordered_json{{"w", 1.0}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}}));
            found_geometry = true;
            continue;
        }
        minimum.setX(std::min(minimum.x(), position.x()));
        minimum.setY(std::min(minimum.y(), position.y()));
        minimum.setZ(std::min(minimum.z(), position.z()));
        maximum.setX(std::max(maximum.x(), position.x()));
        maximum.setY(std::max(maximum.y(), position.y()));
        maximum.setZ(std::max(maximum.z(), position.z()));
    }
    if (!found_geometry)
    {
        viewport_->clear_selection_geometry();
        return;
    }
    viewport_->set_selection_geometry(
        (minimum + maximum) * 0.5F,
        minimum,
        maximum,
        first_rotation);
}

std::vector<dragonpixel::scene::command> EditorWindow::gizmo_commands(
    AuthoringViewport::GizmoTool tool,
    const QVector3D& delta) const
{
    std::vector<dragonpixel::scene::command> commands;
    commands.reserve(gizmo_transform_snapshots_.size());
    for (const auto& snapshot : gizmo_transform_snapshots_)
    {
        std::string property_id;
        nlohmann::ordered_json value;
        if (tool == AuthoringViewport::GizmoTool::move)
        {
            property_id = "dpe.transform.position";
            auto effective_delta = delta;
            if (gizmo_preview_orientation_ == AuthoringViewport::GizmoOrientation::local)
            {
                effective_delta = json_quaternion(snapshot.rotation).rotatedVector(delta);
            }
            value = vector_json(json_vector(snapshot.position) + effective_delta);
        }
        else if (tool == AuthoringViewport::GizmoTool::scale)
        {
            property_id = "dpe.transform.scale";
            auto scale = json_vector(snapshot.scale, QVector3D{1.0F, 1.0F, 1.0F}) + delta;
            scale.setX(std::max(0.0001F, scale.x()));
            scale.setY(std::max(0.0001F, scale.y()));
            scale.setZ(std::max(0.0001F, scale.z()));
            value = vector_json(scale);
        }
        else
        {
            property_id = "dpe.transform.rotation";
            const auto current = json_quaternion(snapshot.rotation);
            const auto increment = QQuaternion::fromAxisAndAngle(0.0F, 0.0F, 1.0F, delta.z());
            auto rotated = gizmo_preview_orientation_ == AuthoringViewport::GizmoOrientation::local
                ? current * increment
                : increment * current;
            rotated.normalize();
            value = quaternion_json(rotated);
        }
        commands.emplace_back(dragonpixel::scene::set_component_property_command{
            snapshot.entity_id,
            std::string{dragonpixel::metadata::builtin_component_ids::transform},
            std::move(property_id),
            std::move(value),
        });
    }
    return commands;
}

bool EditorWindow::reload_gizmo_preview(const dragonpixel::scene::scene& preview_scene)
{
    if (!runtime_directory_.isValid() || preview_worker_ == nullptr || adapter_ == nullptr)
    {
        return false;
    }
    const auto snapshot_path = runtime_directory_.filePath(QStringLiteral("preview-mirror.dpescene"));
    const auto result = dragonpixel::serialization::save_utf8_atomic(
        filesystem_path(snapshot_path),
        dragonpixel::serialization::write_scene_json(preview_scene));
    if (!result.succeeded)
    {
        append_console(QStringLiteral("Could not write provisional gizmo preview: %1")
            .arg(QString::fromStdString(result.error)),
            QStringLiteral("Warning"), QStringLiteral("Viewport"));
        return false;
    }
    const auto selected_adapter = adapter_->currentData().toString();
    if (preview_worker_->adapter_name() == selected_adapter && !selected_adapter.isEmpty())
    {
        preview_worker_->reload_snapshot(snapshot_path);
    }
    else
    {
        preview_worker_->start_session(selected_adapter, snapshot_path);
        preview_worker_->resize_viewport(viewport_->size());
    }
    update_worker_viewport();
    return true;
}

void EditorWindow::begin_gizmo_preview(AuthoringViewport::GizmoTool tool)
{
    if (!scene_ || play_running_)
    {
        return;
    }
    if (gizmo_preview_active_)
    {
        cancel_gizmo_preview();
    }
    gizmo_transform_snapshots_.clear();
    for (const auto& entity_id : selected_entity_ids())
    {
        const auto* entity = scene_->find_entity(entity_id);
        const auto* transform = entity == nullptr ? nullptr : editable_transform(*entity);
        if (transform == nullptr)
        {
            continue;
        }
        gizmo_transform_snapshots_.push_back({
            entity_id,
            transform->properties.value(
                "dpe.transform.position",
                nlohmann::ordered_json{{"x", 0.0}, {"y", 0.0}, {"z", 0.0}}),
            transform->properties.value(
                "dpe.transform.rotation",
                nlohmann::ordered_json{{"w", 1.0}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}}),
            transform->properties.value(
                "dpe.transform.scale",
                nlohmann::ordered_json{{"x", 1.0}, {"y", 1.0}, {"z", 1.0}}),
        });
    }
    if (gizmo_transform_snapshots_.empty())
    {
        append_console(QStringLiteral("Selected GameObjects have no editable Transform component"),
            QStringLiteral("Warning"), QStringLiteral("Viewport"));
        return;
    }
    gizmo_preview_tool_ = tool;
    gizmo_preview_orientation_ = viewport_->gizmo_orientation();
    gizmo_authoritative_history_position_ = scene_->history_position();
    gizmo_preview_scene_ = *scene_;
    gizmo_preview_active_ = true;
}

void EditorWindow::preview_gizmo_delta(
    AuthoringViewport::GizmoTool tool,
    const QVector3D& delta)
{
    if (!scene_ || !gizmo_preview_active_ || tool != gizmo_preview_tool_)
    {
        return;
    }
    if (scene_->history_position() != gizmo_authoritative_history_position_)
    {
        cancel_gizmo_preview();
        append_console(QStringLiteral("Gizmo preview cancelled because authoring history changed"),
            QStringLiteral("Warning"), QStringLiteral("Viewport"));
        return;
    }
    auto candidate = *scene_;
    const auto commands = gizmo_commands(tool, delta);
    const auto applied = candidate.apply_transaction(commands, "Provisional transform gizmo");
    if (!applied.succeeded)
    {
        cancel_gizmo_preview();
        append_console(QStringLiteral("Gizmo preview was rejected by scene validation"),
            QStringLiteral("Warning"), QStringLiteral("Viewport"));
        return;
    }
    gizmo_preview_scene_ = std::move(candidate);
    update_viewport_selection_geometry(*gizmo_preview_scene_);
    if (!reload_gizmo_preview(*gizmo_preview_scene_))
    {
        return;
    }

    QStringList ordered_ids;
    ordered_ids.reserve(static_cast<qsizetype>(gizmo_transform_snapshots_.size()));
    for (const auto& snapshot : gizmo_transform_snapshots_)
    {
        ordered_ids.push_back(QString::fromStdString(snapshot.entity_id.to_string()));
    }
    QVector3D first_position;
    const auto* first_entity = gizmo_preview_scene_->find_entity(
        gizmo_transform_snapshots_.front().entity_id);
    const auto* first_transform = first_entity == nullptr ? nullptr : editable_transform(*first_entity);
    if (first_transform != nullptr)
    {
        first_position = json_vector(first_transform->properties.value(
            "dpe.transform.position",
            nlohmann::ordered_json{{"x", 0.0}, {"y", 0.0}, {"z", 0.0}}));
    }
    emit gizmo_preview_scene_changed(ordered_ids, first_position);
}

void EditorWindow::cancel_gizmo_preview()
{
    if (!gizmo_preview_active_)
    {
        return;
    }
    gizmo_preview_active_ = false;
    gizmo_preview_scene_.reset();
    gizmo_transform_snapshots_.clear();
    if (scene_)
    {
        update_viewport_selection_geometry(*scene_);
        static_cast<void>(reload_gizmo_preview(*scene_));
    }
    emit gizmo_preview_scene_restored();
    append_console(QStringLiteral("Gizmo preview cancelled; exact authoritative transforms restored"),
        QStringLiteral("Info"), QStringLiteral("Viewport"));
}

void EditorWindow::apply_gizmo_delta(
    AuthoringViewport::GizmoTool tool,
    const QVector3D& delta)
{
    if (!scene_ || !gizmo_preview_active_ || tool != gizmo_preview_tool_)
    {
        return;
    }
    if (scene_->history_position() != gizmo_authoritative_history_position_)
    {
        cancel_gizmo_preview();
        append_console(QStringLiteral("Gizmo commit cancelled because authoring history changed"),
            QStringLiteral("Warning"), QStringLiteral("Viewport"));
        return;
    }
    if (delta.lengthSquared() < 0.0000001F)
    {
        cancel_gizmo_preview();
        return;
    }

    auto commands = gizmo_commands(tool, delta);
    QList<dragonpixel::core::uuid> affected;
    affected.reserve(static_cast<qsizetype>(gizmo_transform_snapshots_.size()));
    for (const auto& snapshot : gizmo_transform_snapshots_)
    {
        affected.push_back(snapshot.entity_id);
    }
    gizmo_preview_active_ = false;
    gizmo_preview_scene_.reset();
    gizmo_transform_snapshots_.clear();
    if (!apply_authoring_transaction(std::move(commands), "Transform gizmo"))
    {
        update_viewport_selection_geometry(*scene_);
        static_cast<void>(reload_gizmo_preview(*scene_));
        emit gizmo_preview_scene_restored();
        append_console(QStringLiteral("Gizmo transform was rejected by command validation"),
            QStringLiteral("Warning"), QStringLiteral("Viewport"));
        return;
    }
    after_scene_mutation(QStringLiteral("Committed one undoable gizmo transaction for %1 GameObject(s)")
        .arg(affected.size()), affected);
    emit gizmo_transaction_committed(affected.size());
}

void EditorWindow::refresh_preview()
{
    if (!scene_ || !runtime_directory_.isValid() || preview_worker_ == nullptr || adapter_ == nullptr)
    {
        return;
    }
    const auto snapshot_path = runtime_directory_.filePath(QStringLiteral("preview-mirror.dpescene"));
    const auto json = dragonpixel::serialization::write_scene_json(*scene_);
    const auto result = dragonpixel::serialization::save_utf8_atomic(filesystem_path(snapshot_path), json);
    if (!result.succeeded)
    {
        append_console(QStringLiteral("Could not refresh preview mirror: %1")
            .arg(QString::fromStdString(result.error)));
        return;
    }
    const auto selected_adapter = adapter_->currentData().toString();
    if (preview_worker_->has_frame() && preview_worker_->adapter_name() == selected_adapter)
    {
        preview_worker_->reload_snapshot(snapshot_path);
        update_worker_viewport();
        append_console(QStringLiteral("Preview mirror reloaded in place without restarting the worker"));
    }
    else
    {
        viewport_->clear_preview_frame();
        preview_worker_->start_session(selected_adapter, snapshot_path);
        preview_worker_->resize_viewport(viewport_->size());
        append_console(QStringLiteral("Preview worker launched from editor-owned authoring mirror"));
    }
}

AutomationResponse EditorWindow::handle_automation_request(
    const QString& method,
    const QJsonObject& parameters)
{
    auto failure = [](int code, const QString& message) {
        return AutomationResponse{false, {}, code, message};
    };
    if (!scene_)
    {
        return failure(-32020, QStringLiteral("No authoring scene is open."));
    }
    if (method == QStringLiteral("inspectScene"))
    {
        QJsonObject result{
            {QStringLiteral("sceneId"), QString::fromStdString(scene_->id().to_string())},
            {QStringLiteral("entityCount"), static_cast<int>(scene_->entities().size())},
            {QStringLiteral("authoritativeOwner"), QStringLiteral("editor")},
        };
        const auto requested_id = parameters.value(QStringLiteral("entityId")).toString();
        if (!requested_id.isEmpty())
        {
            const auto id = dragonpixel::core::uuid::parse(requested_id.toStdString());
            const auto* entity = id ? scene_->find_entity(*id) : nullptr;
            if (entity == nullptr)
            {
                return failure(-32021, QStringLiteral("Requested entity was not found."));
            }
            result[QStringLiteral("entity")] = QJsonObject{
                {QStringLiteral("id"), requested_id},
                {QStringLiteral("name"), QString::fromStdString(entity->name)},
                {QStringLiteral("enabled"), entity->enabled},
                {QStringLiteral("componentCount"), static_cast<int>(entity->components.size())},
            };
        }
        return {true, result, 0, {}};
    }
    if (method == QStringLiteral("applyCommand"))
    {
        const auto command_object = parameters.value(QStringLiteral("command")).toObject();
        if (command_object.value(QStringLiteral("type")).toString() != QStringLiteral("renameEntity"))
        {
            return failure(-32602, QStringLiteral("Slice 1 automation supports only renameEntity."));
        }
        const auto id_text = command_object.value(QStringLiteral("entityId")).toString();
        const auto entity_id = dragonpixel::core::uuid::parse(id_text.toStdString());
        const auto name = command_object.value(QStringLiteral("name")).toString().trimmed();
        if (!entity_id || name.isEmpty())
        {
            return failure(-32602, QStringLiteral("renameEntity requires a valid entityId and non-empty name."));
        }
        const dragonpixel::scene::command command_value{dragonpixel::scene::rename_entity_command{
            *entity_id, name.toStdString()}};
        const auto dry_run = parameters.value(QStringLiteral("dryRun")).toBool(false);
        if (dry_run)
        {
            auto candidate = *scene_;
            const std::vector<dragonpixel::scene::command> commands{command_value};
            const auto augmented = prefab_service_.augment_commands(*scene_, commands);
            if (!augmented.succeeded)
            {
                return failure(-32022, augmented.diagnostics.value(0,
                    QStringLiteral("Prefab-aware command validation rejected the edit.")));
            }
            const auto result = candidate.apply_transaction(augmented.commands, "Dry-run automation command");
            if (!result.succeeded)
            {
                return failure(-32022, QString::fromStdString(result.diagnostic->message));
            }
            return {true, {
                {QStringLiteral("applied"), false},
                {QStringLiteral("wouldApply"), true},
                {QStringLiteral("entityId"), id_text},
                {QStringLiteral("name"), name},
            }, 0, {}};
        }

        if (!apply_authoring_transaction({command_value}, "Automation command"))
        {
            return failure(-32022, QStringLiteral("Prefab-aware command validation rejected the edit."));
        }
        after_scene_mutation(
            QStringLiteral("Automation command applied through prefab-aware editor validation"),
            {*entity_id});
        return {true, {
            {QStringLiteral("applied"), true},
            {QStringLiteral("wouldApply"), true},
            {QStringLiteral("entityId"), id_text},
            {QStringLiteral("name"), name},
        }, 0, {}};
    }
    if (method == QStringLiteral("cancel"))
    {
        return {true, {
            {QStringLiteral("cancelled"), true},
            {QStringLiteral("requestId"), parameters.value(QStringLiteral("requestId"))},
        }, 0, {}};
    }
    return failure(-32601, QStringLiteral("Unknown automation method: %1").arg(method));
}

void EditorWindow::start_automation_self_test()
{
    if (automation_broker_ == nullptr || automation_broker_->endpoint().isEmpty()
        || !self_test_entity_id_)
    {
        append_console(QStringLiteral("Automation self-test could not start"));
        return;
    }
    automation_test_process_ = new QProcess(this);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("DPE_AUTOMATION_ENDPOINT"), automation_broker_->endpoint());
    environment.insert(QStringLiteral("DPE_AUTOMATION_TOKEN"), automation_broker_->capability_token());
    environment.insert(
        QStringLiteral("DPE_AUTOMATION_ENTITY_ID"), QString::fromStdString(self_test_entity_id_->to_string()));
    const auto tools_root = QString::fromUtf8(DPE_PYTHON_TOOLS_ROOT);
    const auto existing_python_path = environment.value(QStringLiteral("PYTHONPATH"));
    environment.insert(
        QStringLiteral("PYTHONPATH"),
        existing_python_path.isEmpty()
            ? tools_root
            : tools_root + QDir::listSeparator() + existing_python_path);
    automation_test_process_->setProcessEnvironment(environment);
    automation_test_process_->setProgram(QString::fromUtf8(DPE_PYTHON_EXECUTABLE));
    automation_test_process_->setArguments({QStringLiteral("-m"), QStringLiteral("dragonpixel_tools.self_test")});
    automation_test_process_->setProcessChannelMode(QProcess::SeparateChannels);
    connect(automation_test_process_, &QProcess::finished, this, [this](int exit_code, QProcess::ExitStatus status) {
        const auto output = automation_test_process_->readAllStandardOutput().trimmed();
        const auto error = automation_test_process_->readAllStandardError().trimmed();
        QFile audit{automation_broker_->audit_path()};
        const auto audit_opened = audit.open(QIODevice::ReadOnly);
        const auto audit_bytes = audit_opened ? audit.readAll() : QByteArray{};
        const auto* entity = self_test_entity_id_ ? scene_->find_entity(*self_test_entity_id_) : nullptr;
        automation_self_test_passed_ = exit_code == 0 && status == QProcess::NormalExit
            && output.contains("\"succeeded\":true")
            && entity != nullptr && entity->name == "Automation Applied Entity"
            && audit_bytes.contains("\"method\":\"applyCommand\"")
            && !audit_bytes.contains(automation_broker_->capability_token().toUtf8());
        if (!automation_self_test_passed_)
        {
            append_console(QStringLiteral("Automation self-test failed: %1 %2")
                .arg(QString::fromUtf8(output), QString::fromUtf8(error)));
            return;
        }
        append_console(QStringLiteral("External Python automation, dry-run, command validation, and redacted audit passed"));
        start_play();
    });
    connect(automation_test_process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
        {
            append_console(QStringLiteral("Automation self-test process failed to start"));
        }
    });
    automation_test_process_->start();
}

void EditorWindow::start_play()
{
    if (!scene_ || !runtime_directory_.isValid())
    {
        return;
    }
    if (preview_simulating_)
    {
        preview_simulating_ = false;
        const QSignalBlocker blocker{simulate_action_};
        simulate_action_->setChecked(false);
        preview_worker_->set_preview_simulation(false);
    }
    const auto snapshot_path = runtime_directory_.filePath(QStringLiteral("play-snapshot.dpescene"));
    const auto json = dragonpixel::serialization::write_scene_json(*scene_);
    const auto result = dragonpixel::serialization::save_utf8_atomic(filesystem_path(snapshot_path), json);
    if (!result.succeeded)
    {
        append_console(QStringLiteral("Could not create immutable play snapshot: %1")
            .arg(QString::fromStdString(result.error)));
        return;
    }
    viewport_->set_play_mode(true);
    play_worker_->start_session(adapter_->currentData().toString(), snapshot_path);
    play_running_ = true;
    play_paused_ = false;
    update_action_states();
    append_console(QStringLiteral("Play launched from immutable snapshot; saved scene remains editor-owned"));
}

void EditorWindow::stop_play()
{
    if (play_worker_ != nullptr)
    {
        play_worker_->stop_and_discard();
    }
    if (viewport_ != nullptr)
    {
        viewport_->set_play_mode(false);
    }
    play_running_ = false;
    play_paused_ = false;
    update_action_states();
}

void EditorWindow::copy_console_selection()
{
    QList<int> rows;
    if (console_ != nullptr && console_->selectionModel() != nullptr)
    {
        for (const auto& proxy_index : console_->selectionModel()->selectedRows())
        {
            const auto source_index = console_filter_->mapToSource(proxy_index);
            if (source_index.isValid() && !rows.contains(source_index.row()))
            {
                rows.push_back(source_index.row());
            }
        }
    }
    if (auto* clipboard = QApplication::clipboard(); clipboard != nullptr)
    {
        clipboard->setText(console_model_->copy_plain_text(rows));
        statusBar()->showMessage(
            rows.isEmpty()
                ? QStringLiteral("Copied all diagnostics")
                : QStringLiteral("Copied %1 diagnostic row(s)").arg(rows.size()),
            3000);
    }
}

void EditorWindow::export_console_selection()
{
    QList<int> rows;
    if (console_ != nullptr && console_->selectionModel() != nullptr)
    {
        for (const auto& proxy_index : console_->selectionModel()->selectedRows())
        {
            const auto source_index = console_filter_->mapToSource(proxy_index);
            if (source_index.isValid() && !rows.contains(source_index.row()))
            {
                rows.push_back(source_index.row());
            }
        }
    }
    const auto initial_directory = project_root_.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        : project_root_;
    const auto path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export diagnostics"),
        QDir{initial_directory}.filePath(QStringLiteral("DragonPixelDiagnostics.csv")),
        QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty())
    {
        return;
    }
    QSaveFile file{path};
    if (!file.open(QIODevice::WriteOnly)
        || file.write(console_model_->export_csv(rows).toUtf8()) < 0
        || !file.commit())
    {
        append_console(
            QStringLiteral("Could not export diagnostics to %1: %2").arg(path, file.errorString()),
            QStringLiteral("Error"),
            QStringLiteral("Console"),
            path,
            {},
            {},
            {},
            {},
            {},
            path);
        return;
    }
    statusBar()->showMessage(QStringLiteral("Diagnostics exported to %1").arg(path), 5000);
}

void EditorWindow::navigate_console_entry(const QModelIndex& proxy_index)
{
    const auto source_index = console_filter_->mapToSource(proxy_index);
    if (!source_index.isValid())
    {
        return;
    }
    const auto* source_entry = console_model_->entry_at(source_index.row());
    if (source_entry == nullptr)
    {
        return;
    }
    const auto entry = *source_entry;
    if (!entry.entity_id.isEmpty())
    {
        const auto entity_id = dragonpixel::core::uuid::parse(entry.entity_id.toStdString());
        const auto hierarchy_source = entity_id ? hierarchy_model_->index_for_entity(*entity_id) : QModelIndex{};
        const auto hierarchy_proxy = hierarchy_filter_->mapFromSource(hierarchy_source);
        if (hierarchy_proxy.isValid())
        {
            hierarchy_dock_->show();
            hierarchy_->selectionModel()->clearSelection();
            hierarchy_->selectionModel()->select(
                hierarchy_proxy, QItemSelectionModel::Select | QItemSelectionModel::Rows);
            hierarchy_->setCurrentIndex(hierarchy_proxy);
            hierarchy_->scrollTo(hierarchy_proxy);
            hierarchy_->setFocus(Qt::OtherFocusReason);
            return;
        }
    }

    QModelIndex project_source;
    std::function<bool(const QModelIndex&)> find_project_item;
    find_project_item = [&](const QModelIndex& parent) {
        for (auto row = 0; row < project_model_->rowCount(parent); ++row)
        {
            const auto index = project_model_->index(row, 0, parent);
            const auto asset_matches = !entry.asset_id.isEmpty()
                && index.data(EditorRoles::asset_id).toString() == entry.asset_id;
            const auto path_matches = !entry.navigation_path.isEmpty()
                && QFileInfo{index.data(EditorRoles::project_path).toString()}.absoluteFilePath()
                    == QFileInfo{entry.navigation_path}.absoluteFilePath();
            if (asset_matches || path_matches)
            {
                project_source = index;
                return true;
            }
            if (find_project_item(index))
            {
                return true;
            }
        }
        return false;
    };
    static_cast<void>(find_project_item({}));
    const auto project_proxy = project_filter_->mapFromSource(project_source);
    if (project_proxy.isValid())
    {
        assets_dock_->show();
        auto parent = project_proxy.parent();
        while (parent.isValid())
        {
            project_explorer_->expand(parent);
            parent = parent.parent();
        }
        project_explorer_->selectionModel()->clearSelection();
        project_explorer_->selectionModel()->select(
            project_proxy, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        project_explorer_->setCurrentIndex(project_proxy);
        project_explorer_->scrollTo(project_proxy);
        project_explorer_->setFocus(Qt::OtherFocusReason);
        return;
    }

    statusBar()->showMessage(QStringLiteral("The diagnostic target is not present in the current project"), 4000);
}

void EditorWindow::append_console(
    const QString& message,
    const QString& severity,
    const QString& subsystem,
    const QString& context,
    const QString& worker,
    const QString& session,
    const QString& correlation_id,
    const QString& entity_id,
    const QString& asset_id,
    const QString& navigation_path)
{
    qInfo().noquote() << message;
    if (console_model_ != nullptr)
    {
        auto resolved_entity_id = entity_id;
        auto resolved_asset_id = asset_id;
        auto resolved_navigation_path = navigation_path;
        if (resolved_entity_id.isEmpty() && scene_)
        {
            auto candidate_id = context;
            const QRegularExpression entity_pattern{
                QStringLiteral(R"((?:^|\s)entity=([0-9a-fA-F-]{36})(?:\s|$))")};
            if (const auto match = entity_pattern.match(context); match.hasMatch())
            {
                candidate_id = match.captured(1);
            }
            const auto parsed = dragonpixel::core::uuid::parse(candidate_id.toStdString());
            if (parsed && scene_->find_entity(*parsed) != nullptr)
            {
                resolved_entity_id = candidate_id;
            }
        }
        if (project_index_.candidate)
        {
            if (resolved_asset_id.isEmpty())
            {
                if (const auto* entry = project_index_.candidate->find_by_id(context);
                    entry != nullptr && entry->kind == ProjectIndexEntryKind::asset)
                {
                    resolved_asset_id = entry->id;
                    resolved_navigation_path = entry->absolute_path;
                }
            }
            if (resolved_navigation_path.isEmpty())
            {
                if (const auto* entry = project_index_.candidate->find_by_path(context); entry != nullptr)
                {
                    resolved_navigation_path = entry->absolute_path;
                    if (entry->kind == ProjectIndexEntryKind::asset)
                    {
                        resolved_asset_id = entry->id;
                    }
                }
            }
        }
        console_model_->append({
            QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")),
            severity,
            subsystem,
            context,
            message,
            worker,
            session,
            correlation_id,
            resolved_entity_id,
            resolved_asset_id,
            resolved_navigation_path,
        });
        if (console_ != nullptr)
        {
            console_->scrollToBottom();
        }
    }
}

QList<dragonpixel::core::uuid> EditorWindow::selected_entity_ids() const
{
    QList<dragonpixel::core::uuid> result;
    if (hierarchy_ == nullptr || hierarchy_->selectionModel() == nullptr)
    {
        return result;
    }
    for (const auto& proxy : hierarchy_->selectionModel()->selectedRows(0))
    {
        const auto source = hierarchy_filter_->mapToSource(proxy);
        const auto id = dragonpixel::core::uuid::parse(
            source.data(EditorRoles::entity_id).toString().toStdString());
        if (id && !result.contains(*id))
        {
            result.push_back(*id);
        }
    }
    return result;
}

std::optional<dragonpixel::core::uuid> EditorWindow::selected_entity_id() const
{
    if (hierarchy_ == nullptr || !hierarchy_->currentIndex().isValid())
    {
        return std::nullopt;
    }
    const auto source = hierarchy_filter_->mapToSource(hierarchy_->currentIndex().siblingAtColumn(0));
    return dragonpixel::core::uuid::parse(
        source.data(EditorRoles::entity_id).toString().toStdString());
}

dragonpixel::scene::entity const* EditorWindow::selected_entity() const
{
    const auto id = selected_entity_id();
    return scene_ && id ? scene_->find_entity(*id) : nullptr;
}

int EditorWindow::project_asset_count() const
{
    std::function<int(const QModelIndex&)> count = [&](const QModelIndex& parent) {
        int result = 0;
        for (int row = 0; row < project_model_->rowCount(parent); ++row)
        {
            const auto index = project_model_->index(row, 0, parent);
            if (ProjectModel::item_kind(index) == ProjectItemKind::asset)
            {
                ++result;
            }
            result += count(index);
        }
        return result;
    };
    return count({});
}

void EditorWindow::closeEvent(QCloseEvent* event)
{
    if (confirm_discard_or_save())
    {
        QSettings settings{editor_settings_path(), QSettings::IniFormat};
        settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
        settings.setValue(QStringLiteral("window/state"), saveState(2));
        settings.sync();
        event->accept();
    }
    else
    {
        event->ignore();
    }
}

void EditorWindow::start_self_test(const QString& adapter)
{
    authoring_self_test_passed_ = run_authoring_self_test();
    if (!authoring_self_test_passed_)
    {
        append_console(QStringLiteral("Authoring command/save/reload self-test failed"));
        return;
    }
    const auto index = adapter_->findData(adapter);
    if (index >= 0)
    {
        adapter_->setCurrentIndex(index);
    }
    start_automation_self_test();
}

bool EditorWindow::self_test_ready() const
{
    return authoring_self_test_passed_ && automation_self_test_passed_
        && scene_.has_value() && scene_->entities().size() >= 6 && metadata_.size() >= 7
        && hierarchy_model_->rowCount() >= 5 && inspector_model_->rowCount() > 0
        && project_asset_count() >= 2 && preview_worker_->has_frame() && play_worker_->has_frame()
        && preview_worker_->process_id() > 0 && play_worker_->process_id() > 0
        && preview_worker_->process_id() != play_worker_->process_id();
}

QString EditorWindow::self_test_diagnostics() const
{
    return QStringLiteral(
        "authoring=%1 automation=%2 scene=%3 entities=%4 metadata=%5 hierarchy=%6 inspector=%7 assets=%8 "
        "previewFrame=%9 previewPid=%10 playFrame=%11 playPid=%12 project=%13")
        .arg(authoring_self_test_passed_)
        .arg(automation_self_test_passed_)
        .arg(scene_.has_value())
        .arg(scene_ ? scene_->entities().size() : 0)
        .arg(metadata_.size())
        .arg(hierarchy_model_->rowCount())
        .arg(inspector_model_->rowCount())
        .arg(project_asset_count())
        .arg(preview_worker_->has_frame())
        .arg(preview_worker_->process_id())
        .arg(play_worker_->has_frame())
        .arg(play_worker_->process_id())
        .arg(!project_manifest_path_.isEmpty());
}

bool EditorWindow::run_authoring_self_test()
{
    if (!scene_ || scene_->entities().empty() || !runtime_directory_.isValid()
        || project_manifest_path_.isEmpty())
    {
        return false;
    }

    const auto parent_id = scene_->entities().front().id;
    const auto entity_id = dragonpixel::core::uuid::random_v4();
    self_test_entity_id_ = entity_id;

    dragonpixel::scene::component_record transform{
        std::string{dragonpixel::metadata::builtin_component_ids::transform},
        2,
        dragonpixel::metadata::runtime_owner::native,
        {
            {"dpe.transform.position", {{"x", 3.0}, {"y", 2.0}, {"z", 1.0}}},
            {"dpe.transform.rotation", {{"w", 1.0}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}}},
            {"dpe.transform.scale", {{"x", 1.0}, {"y", 1.0}, {"z", 1.0}}},
        },
        false,
        nlohmann::ordered_json::object(),
        true,
        "DragonPixel.Native.TransformComponent",
    };
    dragonpixel::scene::component_record rotator{
        std::string{dragonpixel::metadata::builtin_component_ids::rotator},
        1,
        dragonpixel::metadata::runtime_owner::managed,
        {
            {"dpe.rotator.degrees_per_second", 45.0},
            {"dpe.rotator.target", parent_id.to_string()},
        },
        false,
        nlohmann::ordered_json::object(),
        true,
        "DragonPixel.Managed.RotatorComponent",
    };
    const std::vector<dragonpixel::scene::command> transaction{
        dragonpixel::scene::create_entity_command{entity_id, "Self Test Entity", std::nullopt},
        dragonpixel::scene::rename_entity_command{entity_id, "Self Test Renamed Entity"},
        dragonpixel::scene::reparent_entity_command{entity_id, parent_id},
        dragonpixel::scene::set_entity_enabled_command{entity_id, false},
        dragonpixel::scene::upsert_component_command{entity_id, std::move(transform)},
        dragonpixel::scene::upsert_component_command{entity_id, std::move(rotator)},
        dragonpixel::scene::set_component_property_command{
            entity_id,
            std::string{dragonpixel::metadata::builtin_component_ids::rotator},
            "dpe.rotator.degrees_per_second",
            123.5},
        dragonpixel::scene::set_component_enabled_command{
            entity_id, std::string{dragonpixel::metadata::builtin_component_ids::rotator}, false},
    };
    const auto transaction_result = scene_->apply_transaction(transaction);
    if (!transaction_result.succeeded || transaction_result.applied_count != transaction.size())
    {
        return false;
    }

    const auto expected_json = dragonpixel::serialization::write_scene_json(*scene_);

    const auto original_scene_path = scene_path_;
    const auto original_project_path = project_manifest_path_;
    const auto original_project_root = project_root_;
    const auto escape_target = filesystem_path(
        runtime_directory_.filePath(QStringLiteral("outside-project.dpescene")));
    if (!dragonpixel::serialization::save_utf8_atomic(escape_target, expected_json).succeeded)
    {
        return false;
    }
    const auto invalid_project_directory = runtime_directory_.filePath(QStringLiteral("invalid-project"));
    if (!QDir{}.mkpath(invalid_project_directory))
    {
        return false;
    }
    const auto invalid_project = nlohmann::ordered_json{
        {"$schema", "https://dragonpixel.dev/schemas/v1/project.schema.json"},
        {"format", "dpe.project"},
        {"formatVersion", 1},
        {"engineVersion", "0.1.0-slice1"},
        {"projectId", "197b44ff-06d2-43b1-b209-820b0619c846"},
        {"name", "Escaping project must be rejected"},
        {"startupScene", "../outside-project.dpescene"},
    }.dump(2) + "\n";
    const auto invalid_project_path = filesystem_path(
        QDir{invalid_project_directory}.filePath(QStringLiteral("DragonPixelProject.json")));
    if (!dragonpixel::serialization::save_utf8_atomic(invalid_project_path, invalid_project).succeeded)
    {
        return false;
    }
    load_project(QString::fromStdString(invalid_project_path.string()));
    if (scene_path_ != original_scene_path || project_manifest_path_ != original_project_path)
    {
        return false;
    }

    const auto round_trip_project_root =
        runtime_directory_.filePath(QStringLiteral("roundtrip-project"));
    const auto round_trip_scenes_root =
        QDir{round_trip_project_root}.filePath(QStringLiteral("Scenes"));
    const auto round_trip_assets_root =
        QDir{round_trip_project_root}.filePath(QStringLiteral("Assets"));
    if (!QDir{}.mkpath(round_trip_scenes_root) || !QDir{}.mkpath(round_trip_assets_root))
    {
        return false;
    }

    const auto round_trip_path_text =
        QDir{round_trip_scenes_root}.filePath(QStringLiteral("Main.dpescene"));
    const auto round_trip_path = filesystem_path(round_trip_path_text);
    const auto save_result = dragonpixel::serialization::save_utf8_atomic(round_trip_path, expected_json);
    if (!save_result.succeeded)
    {
        return false;
    }

    std::size_t copied_assets = 0;
    QDirIterator asset_iterator{
        original_project_root,
        {QStringLiteral("*.dpeasset")},
        QDir::Files,
        QDirIterator::Subdirectories};
    while (asset_iterator.hasNext())
    {
        const auto source_path = asset_iterator.next();
        const auto relative_path = QDir{original_project_root}.relativeFilePath(source_path);
        const auto destination_path = QDir{round_trip_project_root}.filePath(relative_path);
        if (!QDir{}.mkpath(QFileInfo{destination_path}.absolutePath()))
        {
            return false;
        }
        QFile asset_file{source_path};
        if (!asset_file.open(QIODevice::ReadOnly))
        {
            return false;
        }
        const auto bytes = asset_file.readAll();
        const std::string contents{bytes.constData(), static_cast<std::size_t>(bytes.size())};
        if (!dragonpixel::serialization::save_utf8_atomic(
                filesystem_path(destination_path), contents).succeeded)
        {
            return false;
        }
        ++copied_assets;
    }
    if (copied_assets < 2)
    {
        return false;
    }

    const auto round_trip_project = nlohmann::ordered_json{
        {"$schema", "https://dragonpixel.dev/schemas/v1/project.schema.json"},
        {"format", "dpe.project"},
        {"formatVersion", 1},
        {"engineVersion", "0.1.0-slice1"},
        {"projectId", "620e3bd4-0e69-433b-8c3f-887e7ddc9af8"},
        {"name", "Slice 1 Round Trip Project"},
        {"startupScene", "Scenes/Main.dpescene"},
        {"assetRoots", nlohmann::ordered_json::array({"Assets"})},
    }.dump(2) + "\n";
    const auto round_trip_project_path_text =
        QDir{round_trip_project_root}.filePath(QStringLiteral("DragonPixelProject.json"));
    if (!dragonpixel::serialization::save_utf8_atomic(
            filesystem_path(round_trip_project_path_text), round_trip_project).succeeded)
    {
        return false;
    }

    std::ifstream stream{round_trip_path, std::ios::binary};
    const std::string saved_json{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    auto reloaded = dragonpixel::serialization::read_scene_json(saved_json, metadata_);
    if (!reloaded.value || dragonpixel::serialization::write_scene_json(*reloaded.value) != expected_json)
    {
        return false;
    }

    const auto* reloaded_entity = reloaded.value->find_entity(entity_id);
    if (reloaded_entity == nullptr || reloaded_entity->name != "Self Test Renamed Entity"
        || reloaded_entity->parent_id != parent_id || reloaded_entity->enabled
        || reloaded_entity->components.size() != 2
        || reloaded_entity->components.at(1).enabled)
    {
        return false;
    }

    close_project(false);
    if (scene_ || !project_manifest_path_.isEmpty() || !scene_path_.isEmpty()
        || hierarchy_model_->rowCount() != 0 || inspector_model_->rowCount() != 0)
    {
        return false;
    }
    load_project(round_trip_project_path_text);
    if (!scene_ || project_manifest_path_.isEmpty() || scene_path_.isEmpty())
    {
        return false;
    }
    const auto* reopened_entity = scene_->find_entity(entity_id);
    if (reopened_entity == nullptr || reopened_entity->name != "Self Test Renamed Entity"
        || reopened_entity->parent_id != parent_id || reopened_entity->enabled
        || reopened_entity->components.size() != 2
        || reopened_entity->components.at(1).enabled || project_asset_count() < 2)
    {
        return false;
    }

    const auto opaque_entity = std::find_if(
        scene_->entities().begin(),
        scene_->entities().end(),
        [](const dragonpixel::scene::entity& entity) {
            return std::any_of(
                entity.components.begin(),
                entity.components.end(),
                [](const dragonpixel::scene::component_record& component) {
                    return component.opaque;
                });
        });
    if (opaque_entity == scene_->entities().end())
    {
        return false;
    }
    const auto opaque_source = hierarchy_model_->index_for_entity(opaque_entity->id);
    const auto opaque_proxy = hierarchy_filter_->mapFromSource(opaque_source);
    hierarchy_->selectionModel()->clearSelection();
    hierarchy_->setCurrentIndex(opaque_proxy);
    hierarchy_->selectionModel()->select(opaque_proxy, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    inspect_selected_entities();
    const auto* component_item = inspector_model_->item(0, 0);
    const auto* raw_item = component_item != nullptr ? component_item->child(0, 0) : nullptr;
    const auto opaque_diagnostic_visible = inspector_model_->rowCount() == 1
        && component_item != nullptr
        && component_item->text().startsWith(QStringLiteral("Opaque"))
        && component_item->rowCount() == 1
        && raw_item != nullptr
        && raw_item->text() == QStringLiteral("Raw preserved record")
        && !(raw_item->flags() & Qt::ItemIsEditable);
    if (!opaque_diagnostic_visible)
    {
        return false;
    }

    const auto entity_source = hierarchy_model_->index_for_entity(entity_id);
    const auto entity_proxy = hierarchy_filter_->mapFromSource(entity_source);
    if (entity_proxy.isValid())
    {
        hierarchy_->selectionModel()->clearSelection();
        hierarchy_->setCurrentIndex(entity_proxy);
        hierarchy_->selectionModel()->select(entity_proxy, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        inspect_selected_entities();
        refresh_preview();
        append_console(QStringLiteral(
            "Authoring commands and atomic project save/close/reopen passed"));
        return inspector_model_->rowCount() == 2;
    }
    return false;
}

void EditorWindow::crash_self_test_worker()
{
    play_worker_->force_crash();
}

bool EditorWindow::self_test_recovered() const
{
    return play_worker_->recovery_count() > 0 && play_worker_->recovered_after_crash()
        && preview_worker_->has_frame() && preview_worker_->process_id() != play_worker_->process_id();
}
