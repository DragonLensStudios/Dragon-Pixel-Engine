#include "EditorWindow.h"

#include "EditorRuntimePaths.h"
#include "InputMapEditorDialog.h"
#include "ProjectIndexService.h"

#include <dragonpixel/scene/commands.h>
#include <dragonpixel/scene/command_validation.h>
#include <dragonpixel/metadata/builtin_ids.h>
#include <dragonpixel/serialization/atomic_file.h>
#include <dragonpixel/serialization/scene_json.h>
#include <dragonpixel/tiles/tile_evaluator.h>
#include <dragonpixel/tiles/tile_grid.h>

#include <nlohmann/json.hpp>

#include <QAction>
#include <QApplication>
#include <QBrush>
#include <QClipboard>
#include <QCheckBox>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QMimeData>
#include <QPushButton>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressDialog>
#include <QQuaternion>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStackedWidget>
#include <QStatusBar>
#include <QSplitter>
#include <QTableView>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <numbers>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
constexpr int workspace_state_version = 6;

class ProjectDropTreeView final : public QTreeView
{
public:
    using DropHandler = std::function<void(const QStringList&, const QModelIndex&)>;
    using DomainDropHandler = std::function<bool(const QMimeData*, const QModelIndex&)>;
    using QTreeView::QTreeView;
    void set_external_drop_handler(DropHandler handler) { handler_ = std::move(handler); }
    void set_domain_drop_handler(DomainDropHandler handler) { domain_handler_ = std::move(handler); }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (event->mimeData()->hasUrls()
            || event->mimeData()->hasFormat(QStringLiteral("application/x-dragonpixel-project-item"))
            || event->mimeData()->hasFormat(QStringLiteral("application/x-dragonpixel-entity")))
        {
            event->acceptProposedAction();
            return;
        }
        QTreeView::dragEnterEvent(event);
    }
    void dragMoveEvent(QDragMoveEvent* event) override
    {
        if (event->mimeData()->hasUrls()
            || event->mimeData()->hasFormat(QStringLiteral("application/x-dragonpixel-project-item"))
            || event->mimeData()->hasFormat(QStringLiteral("application/x-dragonpixel-entity")))
        {
            event->acceptProposedAction();
            return;
        }
        QTreeView::dragMoveEvent(event);
    }
    void dropEvent(QDropEvent* event) override
    {
        const auto destination = indexAt(event->position().toPoint());
        if (event->mimeData()->hasUrls() && handler_)
        {
            QStringList paths;
            for (const auto& url : event->mimeData()->urls())
            {
                if (url.isLocalFile()) paths.push_back(url.toLocalFile());
            }
            if (!paths.isEmpty())
            {
                handler_(paths, destination);
                event->acceptProposedAction();
                return;
            }
        }
        if (domain_handler_ && domain_handler_(event->mimeData(), destination))
        {
            event->acceptProposedAction();
            return;
        }
        QTreeView::dropEvent(event);
    }

private:
    DropHandler handler_;
    DomainDropHandler domain_handler_;
};

class ProjectDropListView final : public QListView
{
public:
    using DropHandler = std::function<void(const QStringList&, const QModelIndex&)>;
    using DomainDropHandler = std::function<bool(const QMimeData*, const QModelIndex&)>;
    using QListView::QListView;
    void set_external_drop_handler(DropHandler handler) { handler_ = std::move(handler); }
    void set_domain_drop_handler(DomainDropHandler handler) { domain_handler_ = std::move(handler); }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (event->mimeData()->hasUrls()
            || event->mimeData()->hasFormat(QStringLiteral("application/x-dragonpixel-project-item"))
            || event->mimeData()->hasFormat(QStringLiteral("application/x-dragonpixel-entity")))
        {
            event->acceptProposedAction();
            return;
        }
        QListView::dragEnterEvent(event);
    }
    void dragMoveEvent(QDragMoveEvent* event) override
    {
        if (event->mimeData()->hasUrls()
            || event->mimeData()->hasFormat(QStringLiteral("application/x-dragonpixel-project-item"))
            || event->mimeData()->hasFormat(QStringLiteral("application/x-dragonpixel-entity")))
        {
            event->acceptProposedAction();
            return;
        }
        QListView::dragMoveEvent(event);
    }
    void dropEvent(QDropEvent* event) override
    {
        const auto destination = indexAt(event->position().toPoint());
        if (event->mimeData()->hasUrls() && handler_)
        {
            QStringList paths;
            for (const auto& url : event->mimeData()->urls())
            {
                if (url.isLocalFile()) paths.push_back(url.toLocalFile());
            }
            if (!paths.isEmpty())
            {
                handler_(paths, destination);
                event->acceptProposedAction();
                return;
            }
        }
        if (domain_handler_ && domain_handler_(event->mimeData(), destination))
        {
            event->acceptProposedAction();
            return;
        }
        QListView::dropEvent(event);
    }

private:
    DropHandler handler_;
    DomainDropHandler domain_handler_;
};

class InspectorDropTreeView final : public QTreeView
{
public:
    using DropHandler = std::function<bool(const QModelIndex&, const QMimeData*)>;
    using QTreeView::QTreeView;
    void set_asset_drop_handler(DropHandler handler) { handler_ = std::move(handler); }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (event->mimeData()->hasFormat(QStringLiteral("application/x-dragonpixel-project-item")))
        {
            event->acceptProposedAction();
            return;
        }
        QTreeView::dragEnterEvent(event);
    }
    void dragMoveEvent(QDragMoveEvent* event) override
    {
        if (event->mimeData()->hasFormat(QStringLiteral("application/x-dragonpixel-project-item")))
        {
            event->acceptProposedAction();
            return;
        }
        QTreeView::dragMoveEvent(event);
    }
    void dropEvent(QDropEvent* event) override
    {
        if (handler_ && handler_(indexAt(event->position().toPoint()), event->mimeData()))
        {
            event->acceptProposedAction();
            return;
        }
        QTreeView::dropEvent(event);
    }

private:
    DropHandler handler_;
};

QString high_contrast_indicator_style(const QString& selector)
{
    return QStringLiteral(R"QSS(
%1::indicator {
    width: 18px;
    height: 18px;
    border: 2px solid #9fb2c8;
    border-radius: 4px;
    background-color: #263442;
}
%1::indicator:hover {
    border-color: #d8ecff;
    background-color: #34495e;
}
%1::indicator:checked {
    border-color: #bde5ff;
    background-color: #147dcc;
    image: url(:/dragonpixel/icons/inspector-check.xpm);
}
%1::indicator:checked:hover {
    border-color: #ffffff;
    background-color: #2495eb;
}
%1::indicator:indeterminate {
    border-color: #ffe3a1;
    background-color: #b87509;
    image: url(:/dragonpixel/icons/inspector-partial.xpm);
}
%1::indicator:disabled {
    border-color: #667483;
    background-color: #303942;
}
)QSS").arg(selector);
}

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
        case value_type::component_reference: return nullptr;
        case value_type::object:
        case value_type::dictionary: return nlohmann::ordered_json::object();
        case value_type::list: return nlohmann::ordered_json::array();
        case value_type::polymorphic_object: return nullptr;
        case value_type::string:
        case value_type::entity_reference:
        case value_type::asset_reference: return "";
    }
    return nullptr;
}

nlohmann::ordered_json property_default(const dragonpixel::metadata::property_descriptor& property)
{
    if (!property.default_json.empty())
    {
        auto parsed = nlohmann::ordered_json::parse(property.default_json, nullptr, false);
        if (!parsed.is_discarded())
        {
            return parsed;
        }
    }
    return property.nullable ? nlohmann::ordered_json{nullptr} : default_value(property.type);
}

nlohmann::ordered_json object_envelope_default(
    const dragonpixel::metadata::object_type_descriptor& descriptor)
{
    nlohmann::ordered_json properties = nlohmann::ordered_json::object();
    for (const auto& property : descriptor.properties)
    {
        properties[property.property_id] = property_default(property);
    }
    return {
        {"typeId", descriptor.type_id},
        {"schemaVersion", descriptor.schema_version},
        {"properties", std::move(properties)},
    };
}

QString owner_text(dragonpixel::metadata::runtime_owner owner)
{
    switch (owner)
    {
        case dragonpixel::metadata::runtime_owner::managed: return QStringLiteral("C#");
        case dragonpixel::metadata::runtime_owner::data_only: return QStringLiteral("data");
        case dragonpixel::metadata::runtime_owner::native: return QStringLiteral("C++");
    }
    return QStringLiteral("unknown");
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

std::string stable_runtime_uuid(const QByteArray& seed)
{
    auto bytes = QCryptographicHash::hash(seed, QCryptographicHash::Sha256).left(16);
    bytes[6] = static_cast<char>((static_cast<unsigned char>(bytes[6]) & 0x0fU) | 0x50U);
    bytes[8] = static_cast<char>((static_cast<unsigned char>(bytes[8]) & 0x3fU) | 0x80U);
    const auto hex = bytes.toHex();
    return QStringLiteral("%1-%2-%3-%4-%5")
        .arg(QString::fromLatin1(hex.mid(0, 8)), QString::fromLatin1(hex.mid(8, 4)),
             QString::fromLatin1(hex.mid(12, 4)), QString::fromLatin1(hex.mid(16, 4)),
             QString::fromLatin1(hex.mid(20, 12))).toStdString();
}

QString save_failure_dialog_text(const QString& detail)
{
    const auto edits_remain = QStringLiteral(
        "Your unsaved edits remain open in the editor.");
    if (detail.contains(QStringLiteral("error 32 (")))
    {
        const auto recovery = detail.contains(QStringLiteral("restored"), Qt::CaseInsensitive)
            ? QStringLiteral("The previous scene and Tilemap files were restored. ")
            : QStringLiteral("Dragon Pixel did not overwrite the existing project files. ");
        return QStringLiteral(
            "Dragon Pixel could not finish replacing the scene because another process is using it. "
            "%1%2\n\nClose any application that has the project file open, then choose Save again."
            "\n\nTechnical details:\n%3")
            .arg(recovery, edits_remain, detail);
    }
    return QStringLiteral(
        "Dragon Pixel could not complete the save transaction. %1\n\nTechnical details:\n%2")
        .arg(edits_remain, detail);
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
    if (!initial_document.isEmpty() && QFileInfo(initial_document).fileName().compare(
            QStringLiteral("DragonPixelProject.json"), Qt::CaseInsensitive) == 0)
    {
        load_project(initial_document);
    }
    else if (!initial_document.isEmpty())
    {
        load_scene(initial_document);
    }
    else
    {
        show_project_hub();
    }
}

void EditorWindow::build_interface()
{
    setDockOptions(
        QMainWindow::AnimatedDocks
        | QMainWindow::AllowNestedDocks
        | QMainWindow::AllowTabbedDocks
        | QMainWindow::GroupedDragging);
    setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    viewport_ = new AuthoringViewport(this);
    viewport_->setObjectName(QStringLiteral("SceneViewport"));
    game_viewport_ = new GameViewport(this);
    auto* game_panel = new QWidget(this);
    game_panel->setObjectName(QStringLiteral("GameViewPanel"));
    auto* game_layout = new QVBoxLayout(game_panel);
    game_layout->setContentsMargins(0, 0, 0, 0);
    game_layout->setSpacing(0);
    auto* game_toolbar = new QWidget(game_panel);
    game_toolbar->setObjectName(QStringLiteral("GameViewToolbar"));
    auto* game_toolbar_layout = new QHBoxLayout(game_toolbar);
    game_toolbar_layout->setContentsMargins(6, 3, 6, 3);
    auto* aspect_label = new QLabel(QStringLiteral("Aspect"), game_toolbar);
    auto* aspect = new QComboBox(game_toolbar);
    aspect->setObjectName(QStringLiteral("GameViewAspect"));
    aspect->setAccessibleName(QStringLiteral("Game view aspect ratio"));
    aspect->addItem(QStringLiteral("Free Aspect"), 0.0);
    aspect->addItem(QStringLiteral("16:9"), 16.0 / 9.0);
    aspect->addItem(QStringLiteral("16:10"), 16.0 / 10.0);
    aspect->addItem(QStringLiteral("4:3"), 4.0 / 3.0);
    connect(aspect, &QComboBox::currentIndexChanged, this, [this, aspect](int) {
        game_viewport_->set_aspect_ratio(aspect->currentData().toDouble());
    });
    auto* game_status = new QLabel(QStringLiteral("Preview · Primary Camera · Fit"), game_toolbar);
    game_status->setObjectName(QStringLiteral("GameViewStatus"));
    game_status->setAccessibleName(QStringLiteral("Game view adapter and frame status"));
    game_toolbar_layout->addWidget(aspect_label);
    game_toolbar_layout->addWidget(aspect);
    game_toolbar_layout->addStretch();
    game_toolbar_layout->addWidget(game_status);
    game_layout->addWidget(game_toolbar);
    game_layout->addWidget(game_viewport_, 1);
    preview_worker_ = new WorkerClient(QStringLiteral("preview"), this);
    game_preview_worker_ = new WorkerClient(QStringLiteral("game-preview"), this);
    play_worker_ = new WorkerClient(QStringLiteral("play"), this);
    connect(preview_worker_, &WorkerClient::frame_ready, viewport_, &AuthoringViewport::set_preview_frame);
    connect(game_preview_worker_, &WorkerClient::frame_ready, game_viewport_, &GameViewport::set_preview_frame);
    connect(play_worker_, &WorkerClient::frame_ready_correlated, this,
        [this](const QImage& image, quint64 input_revision, quint64 frame_revision) {
            game_viewport_->set_play_frame(image, input_revision, frame_revision);
        });
    connect(play_worker_, &WorkerClient::runtime_input_reset, this, [this] {
        game_viewport_->set_runtime_input_ready(false);
        game_viewport_->set_input_enabled(false);
        game_viewport_->retire_play_frame();
    });
    connect(play_worker_, &WorkerClient::runtime_input_ready, this, [this] {
        game_viewport_->set_runtime_input_ready(true);
        game_viewport_->set_input_enabled(play_running_ && !play_paused_);
    });
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
    connect(game_preview_worker_, &WorkerClient::status_message, this, [this](const QString& message) {
        append_console(message, QStringLiteral("Info"), QStringLiteral("Runtime"), {},
            game_preview_worker_->adapter_name(), QStringLiteral("game-preview"));
    });
    connect(preview_worker_, &WorkerClient::tile_brush_proposal_ready,
        this, &EditorWindow::apply_custom_tile_brush_proposal);
    connect(preview_worker_, &WorkerClient::tile_brush_proposal_failed,
        this, &EditorWindow::reject_custom_tile_brush_proposal);
    connect(preview_worker_, &WorkerClient::runtime_stopped, viewport_, &AuthoringViewport::clear_preview_frame);
    connect(game_preview_worker_, &WorkerClient::runtime_stopped, game_viewport_, &GameViewport::clear_preview_frame);
    connect(preview_worker_, &WorkerClient::preview_simulation_changed, this, [this](bool enabled) {
        preview_simulating_ = enabled;
        if (simulate_action_ != nullptr)
        {
            const QSignalBlocker blocker{simulate_action_};
            simulate_action_->setChecked(enabled);
        }
        update_action_states();
    });
    connect(play_worker_, &WorkerClient::runtime_stopped, this, [this, game_status] {
        game_viewport_->set_play_mode(false);
        game_status->setText(QStringLiteral("Preview · Primary Camera · Fit"));
        play_running_ = false;
        play_paused_ = false;
        update_tile_scene_edit_state();
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
        if (!play_running_)
        {
            play_worker_->resize_viewport(size);
        }
    });
    connect(game_viewport_, &GameViewport::viewport_resized, this, [this](const QSize& size) {
        game_preview_worker_->resize_viewport(size);
        if (play_running_)
        {
            play_worker_->resize_viewport(size);
        }
    });
    connect(game_viewport_, &GameViewport::correlated_input_actions_changed, this,
        [this](const QJsonObject& actions, quint64 input_revision) {
            play_worker_->send_correlated_input_actions(actions, input_revision);
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
    connect(viewport_, &AuthoringViewport::tile_pointer_pressed,
        this, &EditorWindow::begin_tile_scene_stroke);
    connect(viewport_, &AuthoringViewport::tile_pointer_moved,
        this, &EditorWindow::update_tile_scene_stroke);
    connect(viewport_, &AuthoringViewport::tile_pointer_released,
        this, &EditorWindow::end_tile_scene_stroke);
    connect(viewport_, &AuthoringViewport::tile_pointer_cancelled,
        this, &EditorWindow::cancel_tile_scene_stroke);
    connect(viewport_, &AuthoringViewport::project_item_dropped, this,
        [this](const QString& project_id, qint64 source_revision, const QString& path,
            const QString& kind, const QString& asset_type, const QString& asset_id) {
            if (project_id != project_model_->drag_project_id()
                || source_revision != static_cast<qint64>(project_model_->drag_revision()))
            {
                append_console(QStringLiteral("Rejected stale or cross-project drag payload"),
                    QStringLiteral("Warning"), QStringLiteral("Drag Drop"));
                return;
            }
            if (kind == QStringLiteral("asset"))
            {
                if (asset_type.contains(QStringLiteral("sprite"), Qt::CaseInsensitive))
                {
                    create_preset(dragonpixel::scene::entity_preset::sprite, asset_id, true);
                }
                else if (asset_type.contains(QStringLiteral("mesh"), Qt::CaseInsensitive))
                {
                    create_preset(dragonpixel::scene::entity_preset::cube, asset_id, true);
                }
                else if (asset_type.contains(QStringLiteral("tilemap"), Qt::CaseInsensitive))
                {
                    create_preset(dragonpixel::scene::entity_preset::tilemap, asset_id, true);
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
                instantiate_prefab(path, true);
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
        [this](const auto& ids, const auto& parent, auto sibling) {
            return drag_reparent_entities(ids, parent, sibling);
        });
    hierarchy_model_->set_project_drop_handler(
        [this](const QString& path, const QString& kind, const QString& asset_type,
            const QString& asset_id, const std::optional<dragonpixel::core::uuid>& parent) {
            if (!scene_) return false;
            const auto before = scene_->entities().size();
            if (kind == QStringLiteral("asset")
                && asset_type.contains(QStringLiteral("sprite"), Qt::CaseInsensitive))
            {
                create_preset(dragonpixel::scene::entity_preset::sprite,
                    asset_id, !parent.has_value(), parent);
            }
            else if (kind == QStringLiteral("asset")
                && asset_type.contains(QStringLiteral("tilemap"), Qt::CaseInsensitive))
            {
                create_preset(dragonpixel::scene::entity_preset::tilemap,
                    asset_id, !parent.has_value(), parent);
            }
            else if (kind == QStringLiteral("prefab"))
            {
                instantiate_prefab(path, !parent.has_value(), parent);
            }
            else return false;
            return scene_ && scene_->entities().size() > before;
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
    hierarchy_->setAcceptDrops(true);
    hierarchy_->setDragDropMode(QAbstractItemView::DragDrop);
    hierarchy_->setDefaultDropAction(Qt::MoveAction);
    hierarchy_->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    hierarchy_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(hierarchy_->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        publish_global_selection(SelectionOrigin::hierarchy);
        if (!primary_inspector_locked_) inspect_selected_entities();
        update_global_selection_presentation();
        refresh_additional_inspectors();
        update_action_states();
    });
    auto* hierarchy_panel = new QWidget(this);
    hierarchy_panel->setObjectName(QStringLiteral("HierarchyPanel"));
    auto* hierarchy_layout = new QVBoxLayout(hierarchy_panel);
    hierarchy_layout->setContentsMargins(4, 4, 4, 4);
    auto* hierarchy_toolbar = new QWidget(hierarchy_panel);
    hierarchy_toolbar->setObjectName(QStringLiteral("HierarchyToolbar"));
    auto* hierarchy_toolbar_layout = new QHBoxLayout(hierarchy_toolbar);
    hierarchy_toolbar_layout->setContentsMargins(0, 0, 0, 0);
    auto* hierarchy_add = new QToolButton(hierarchy_toolbar);
    hierarchy_add->setObjectName(QStringLiteral("HierarchyAddMenu"));
    hierarchy_add->setText(QStringLiteral("Add"));
    hierarchy_add->setAccessibleName(QStringLiteral("Add GameObject"));
    hierarchy_add->setPopupMode(QToolButton::InstantPopup);
    auto* hierarchy_add_menu = new QMenu(hierarchy_add);
    const auto add_preset = [this, hierarchy_add_menu](
        const QString& label, dragonpixel::scene::entity_preset preset, const QString& asset = {}) {
        hierarchy_add_menu->addAction(label, this, [this, preset, asset] {
            create_preset(preset, asset);
        });
    };
    add_preset(QStringLiteral("Empty GameObject"), dragonpixel::scene::entity_preset::empty);
    add_preset(QStringLiteral("Square Sprite"), dragonpixel::scene::entity_preset::sprite, QStringLiteral("builtin://square"));
    add_preset(QStringLiteral("Circle Sprite"), dragonpixel::scene::entity_preset::sprite, QStringLiteral("builtin://circle"));
    add_preset(QStringLiteral("Cube"), dragonpixel::scene::entity_preset::cube);
    add_preset(QStringLiteral("Camera"), dragonpixel::scene::entity_preset::camera);
    add_preset(QStringLiteral("Light"), dragonpixel::scene::entity_preset::light);
    add_preset(QStringLiteral("Tilemap 2D"), dragonpixel::scene::entity_preset::tilemap);
    hierarchy_add_menu->addSeparator();
    hierarchy_add_menu->addAction(QStringLiteral("Empty Child"), this, [this] {
        const auto parent = selected_entity_id();
        if (parent) create_preset(dragonpixel::scene::entity_preset::empty, {}, false, parent);
    });
    hierarchy_add_menu->addAction(QStringLiteral("Empty Parent / Group Selection"), this, &EditorWindow::group_selected);
    hierarchy_add->setMenu(hierarchy_add_menu);
    auto* hierarchy_expand = new QToolButton(hierarchy_toolbar);
    hierarchy_expand->setObjectName(QStringLiteral("HierarchyExpandAll"));
    hierarchy_expand->setText(QStringLiteral("Expand"));
    hierarchy_expand->setAccessibleName(QStringLiteral("Expand all Hierarchy GameObjects"));
    connect(hierarchy_expand, &QToolButton::clicked, hierarchy_, &QTreeView::expandAll);
    auto* hierarchy_collapse = new QToolButton(hierarchy_toolbar);
    hierarchy_collapse->setObjectName(QStringLiteral("HierarchyCollapseAll"));
    hierarchy_collapse->setText(QStringLiteral("Collapse"));
    hierarchy_collapse->setAccessibleName(QStringLiteral("Collapse all Hierarchy GameObjects"));
    connect(hierarchy_collapse, &QToolButton::clicked, hierarchy_, &QTreeView::collapseAll);
    auto* hierarchy_focus = new QToolButton(hierarchy_toolbar);
    hierarchy_focus->setObjectName(QStringLiteral("HierarchyFocusSelection"));
    hierarchy_focus->setText(QStringLiteral("Focus"));
    hierarchy_focus->setAccessibleName(QStringLiteral("Focus Scene View on selected GameObjects"));
    connect(hierarchy_focus, &QToolButton::clicked, viewport_, &AuthoringViewport::focus_on_selection);
    hierarchy_toolbar_layout->addWidget(hierarchy_add);
    hierarchy_toolbar_layout->addWidget(hierarchy_expand);
    hierarchy_toolbar_layout->addWidget(hierarchy_collapse);
    hierarchy_toolbar_layout->addWidget(hierarchy_focus);
    hierarchy_toolbar_layout->addStretch();
    hierarchy_layout->addWidget(hierarchy_toolbar);
    hierarchy_layout->addWidget(hierarchy_search_);
    hierarchy_layout->addWidget(hierarchy_);

    inspector_model_ = new QStandardItemModel(this);
    inspector_model_->setHorizontalHeaderLabels({QStringLiteral("Property"), QStringLiteral("Value")});
    inspector_delegate_ = new InspectorDelegate(this);
    auto* primary_inspector_view = new InspectorDropTreeView(this);
    inspector_ = primary_inspector_view;
    inspector_->setObjectName(QStringLiteral("InspectorView"));
    inspector_->setAccessibleName(QStringLiteral("Typed component Inspector"));
    inspector_->setModel(inspector_model_);
    inspector_->setItemDelegateForColumn(1, inspector_delegate_);
    inspector_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    inspector_->setAlternatingRowColors(false);
    inspector_->setRootIsDecorated(true);
    inspector_->setIndentation(14);
    inspector_->setContextMenuPolicy(Qt::CustomContextMenu);
    inspector_->setAcceptDrops(true);
    inspector_->setDragDropMode(QAbstractItemView::DropOnly);
    primary_inspector_view->set_asset_drop_handler([this, primary_inspector_view](
        const QModelIndex& index, const QMimeData* mime) {
        return assign_inspector_asset_drop(primary_inspector_view, index, mime);
    });
    inspector_->setStyleSheet(QStringLiteral(
        "QTreeView { border: 0; background: palette(base); }"
        "QTreeView::item { min-height: 26px; padding: 2px; }"
        "QTreeView::item:selected { background: palette(highlight); color: palette(highlighted-text); }")
        + high_contrast_indicator_style(QStringLiteral("QTreeView")));
    connect(inspector_model_, &QStandardItemModel::itemChanged, this, &EditorWindow::edit_inspector_item);
    connect(inspector_, &QWidget::customContextMenuRequested, this, &EditorWindow::show_inspector_context_menu);

    inspector_panel_ = new QWidget(this);
    inspector_panel_->setObjectName(QStringLiteral("InspectorPanel"));
    inspector_panel_->setAccessibleName(QStringLiteral("GameObject Inspector"));
    auto* inspector_layout = new QVBoxLayout(inspector_panel_);
    inspector_layout->setContentsMargins(6, 6, 6, 6);
    inspector_layout->setSpacing(6);
    auto* game_object_header = new QFrame(inspector_panel_);
    game_object_header->setObjectName(QStringLiteral("InspectorGameObjectHeader"));
    game_object_header->setFrameShape(QFrame::StyledPanel);
    auto* game_object_layout = new QVBoxLayout(game_object_header);
    game_object_layout->setContentsMargins(8, 8, 8, 8);
    auto* name_row = new QHBoxLayout;
    inspector_enabled_ = new QCheckBox(game_object_header);
    inspector_enabled_->setObjectName(QStringLiteral("InspectorGameObjectEnabled"));
    inspector_enabled_->setAccessibleName(QStringLiteral("GameObject enabled"));
    inspector_enabled_->setToolTip(QStringLiteral("Enable or disable the selected GameObject(s)"));
    inspector_enabled_->setStyleSheet(high_contrast_indicator_style(QStringLiteral("QCheckBox")));
    inspector_enabled_->setMinimumSize(24, 24);
    inspector_name_ = new QLineEdit(game_object_header);
    inspector_name_->setObjectName(QStringLiteral("InspectorGameObjectName"));
    inspector_name_->setAccessibleName(QStringLiteral("GameObject name"));
    inspector_name_->setPlaceholderText(QStringLiteral("Select a GameObject"));
    name_row->addWidget(inspector_enabled_);
    name_row->addWidget(inspector_name_, 1);
    inspector_lock_ = new QToolButton(game_object_header);
    inspector_lock_->setObjectName(QStringLiteral("InspectorLock"));
    inspector_lock_->setText(QStringLiteral("Lock"));
    inspector_lock_->setCheckable(true);
    inspector_lock_->setAccessibleName(QStringLiteral("Lock Inspector targets"));
    inspector_lock_->setToolTip(QStringLiteral("Keep this Inspector on its current GameObjects while global selection changes"));
    connect(inspector_lock_, &QToolButton::toggled, this, &EditorWindow::set_primary_inspector_locked);
    name_row->addWidget(inspector_lock_);
    game_object_layout->addLayout(name_row);
    inspector_identity_ = new QLabel(QStringLiteral("No GameObject selected"), game_object_header);
    inspector_identity_->setObjectName(QStringLiteral("InspectorGameObjectIdentity"));
    inspector_identity_->setAccessibleName(QStringLiteral("GameObject identity and prefab state"));
    inspector_identity_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    game_object_layout->addWidget(inspector_identity_);
    inspector_layout->addWidget(game_object_header);

    inspector_search_ = new QLineEdit(inspector_panel_);
    inspector_search_->setObjectName(QStringLiteral("InspectorSearch"));
    inspector_search_->setAccessibleName(QStringLiteral("Search Inspector components and properties"));
    inspector_search_->setPlaceholderText(QStringLiteral("Search components and properties..."));
    connect(inspector_search_, &QLineEdit::textChanged, this, [this](const QString& text) {
        for (int row = 0; row < inspector_model_->rowCount(); ++row)
        {
            const auto* component = inspector_model_->item(row, 0);
            auto matches = text.trimmed().isEmpty()
                || component->text().contains(text, Qt::CaseInsensitive);
            for (int child = 0; !matches && child < component->rowCount(); ++child)
            {
                matches = component->child(child, 0)->text().contains(text, Qt::CaseInsensitive);
            }
            inspector_->setRowHidden(row, {}, !matches);
        }
    });
    inspector_layout->addWidget(inspector_search_);
    inspector_layout->addWidget(inspector_, 1);
    inspector_add_component_ = new QPushButton(QStringLiteral("Add Component"), inspector_panel_);
    inspector_add_component_->setObjectName(QStringLiteral("InspectorAddComponent"));
    inspector_add_component_->setAccessibleName(QStringLiteral("Search and add a component"));
    connect(inspector_add_component_, &QPushButton::clicked, this, &EditorWindow::add_component);
    inspector_layout->addWidget(inspector_add_component_);

    connect(inspector_name_, &QLineEdit::editingFinished, this, [this] {
        if (rebuilding_inspector_ || !scene_)
        {
            return;
        }
        const auto targets = primary_inspector_targets();
        const auto* entity = targets.size() == 1 ? scene_->find_entity(targets.front()) : nullptr;
        if (entity != nullptr && inspector_name_->text().trimmed() != QString::fromStdString(entity->name))
        {
            static_cast<void>(edit_hierarchy_entity(targets.front(), inspector_name_->text(), entity->enabled));
        }
    });
    connect(inspector_enabled_, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState state) {
        if (rebuilding_inspector_ || !scene_ || state == Qt::PartiallyChecked)
        {
            return;
        }
        std::vector<dragonpixel::scene::command> commands;
        const auto ids = primary_inspector_targets();
        for (const auto& id : ids)
        {
            commands.emplace_back(dragonpixel::scene::set_entity_enabled_command{id, state == Qt::Checked});
        }
        if (!commands.empty() && apply_authoring_transaction(std::move(commands), "Set GameObject enabled"))
        {
            after_scene_mutation(QStringLiteral("GameObject enabled state changed through command validation"), ids);
        }
    });

    project_model_ = new ProjectModel(this);
    project_filter_ = new ProjectFilterProxyModel(this);
    project_filter_->setSourceModel(project_model_);
    project_search_ = new QLineEdit(this);
    project_search_->setObjectName(QStringLiteral("ProjectSearch"));
    project_search_->setAccessibleName(QStringLiteral("Search project"));
    project_search_->setPlaceholderText(QStringLiteral("Search scenes, prefabs, assets, and scripts..."));
    connect(project_search_, &QLineEdit::textChanged, project_filter_, &ProjectFilterProxyModel::set_search_text);
    project_type_filter_ = new QComboBox(this);
    project_type_filter_->setObjectName(QStringLiteral("ProjectTypeFilter"));
    project_type_filter_->setAccessibleName(QStringLiteral("Filter project by type"));
    project_type_filter_->addItem(QStringLiteral("All types"), QString{});
    project_type_filter_->addItem(QStringLiteral("Scenes"), QStringLiteral("scene"));
    project_type_filter_->addItem(QStringLiteral("Prefabs"), QStringLiteral("prefab"));
    project_type_filter_->addItem(QStringLiteral("Tilemaps"), QStringLiteral("tilemap"));
    project_type_filter_->addItem(QStringLiteral("TileSets"), QStringLiteral("tileset"));
    project_type_filter_->addItem(QStringLiteral("Assets"), QStringLiteral("asset"));
    project_type_filter_->addItem(QStringLiteral("Scripts / Components"), QStringLiteral("component"));
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
    project_folder_filter_ = new ProjectFolderProxyModel(this);
    project_folder_filter_->setSourceModel(project_model_);
    auto* project_folders = new ProjectDropTreeView(this);
    project_folder_tree_ = project_folders;
    project_folder_tree_->setObjectName(QStringLiteral("ProjectFolderTree"));
    project_folder_tree_->setAccessibleName(QStringLiteral("Project folder tree"));
    project_folder_tree_->setModel(project_folder_filter_);
    project_folder_tree_->setHeaderHidden(true);
    project_folder_tree_->setAcceptDrops(true);
    project_folder_tree_->setDropIndicatorShown(true);
    project_folder_tree_->setDragDropMode(QAbstractItemView::DropOnly);
    project_folder_tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    project_folder_tree_->setUniformRowHeights(true);
    for (int column = 1; column < static_cast<int>(ProjectColumn::count); ++column)
    {
        project_folder_tree_->setColumnHidden(column, true);
    }
    connect(project_folder_tree_->selectionModel(), &QItemSelectionModel::currentChanged,
        this, [this](const QModelIndex& current) { update_project_browser_folder(current); });

    auto* project_list = new ProjectDropTreeView(this);
    project_explorer_ = project_list;
    project_explorer_->setObjectName(QStringLiteral("ProjectExplorerView"));
    project_explorer_->setAccessibleName(QStringLiteral("Project asset list"));
    project_explorer_->setModel(project_filter_);
    project_explorer_->setDragEnabled(true);
    project_explorer_->setAcceptDrops(true);
    project_explorer_->setDragDropMode(QAbstractItemView::DragDrop);
    project_explorer_->setDefaultDropAction(Qt::CopyAction);
    project_explorer_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    project_explorer_->setUniformRowHeights(true);
    project_explorer_->setAlternatingRowColors(true);
    project_explorer_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(project_explorer_, &QWidget::customContextMenuRequested,
        this, &EditorWindow::show_project_browser_context_menu);
    connect(project_explorer_, &QTreeView::doubleClicked, this, &EditorWindow::activate_project_item);
    connect(project_explorer_, &QTreeView::activated, this, &EditorWindow::activate_project_item);
    const auto select_drop_folder = [this](QModelIndex source) {
        if (source.isValid() && ProjectModel::item_kind(source) != ProjectItemKind::folder)
            source = source.parent();
        if (!source.isValid()) return;
        const auto folder_proxy = project_folder_filter_->mapFromSource(source.siblingAtColumn(0));
        if (folder_proxy.isValid())
        {
            project_folder_tree_->setCurrentIndex(folder_proxy);
            update_project_browser_folder(folder_proxy);
        }
    };
    project_list->set_external_drop_handler([this, select_drop_folder](const QStringList& paths, const QModelIndex& destination) {
        select_drop_folder(project_filter_->mapToSource(destination.siblingAtColumn(0)));
        import_asset_paths(paths);
    });
    project_list->set_domain_drop_handler([this](const QMimeData* mime, const QModelIndex& destination) {
        return handle_project_browser_drop(mime, project_filter_->mapToSource(destination.siblingAtColumn(0)));
    });

    auto* project_thumbnails = new ProjectDropListView(this);
    project_thumbnail_view_ = project_thumbnails;
    project_thumbnail_view_->setObjectName(QStringLiteral("ProjectThumbnailView"));
    project_thumbnail_view_->setAccessibleName(QStringLiteral("Project asset thumbnails"));
    project_thumbnail_view_->setModel(project_filter_);
    project_thumbnail_view_->setViewMode(QListView::IconMode);
    project_thumbnail_view_->setResizeMode(QListView::Adjust);
    project_thumbnail_view_->setMovement(QListView::Static);
    project_thumbnail_view_->setIconSize(QSize{72, 72});
    project_thumbnail_view_->setGridSize(QSize{132, 112});
    project_thumbnail_view_->setWordWrap(true);
    project_thumbnail_view_->setDragEnabled(true);
    project_thumbnail_view_->setAcceptDrops(true);
    project_thumbnail_view_->setDragDropMode(QAbstractItemView::DragDrop);
    project_thumbnail_view_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(project_thumbnail_view_, &QWidget::customContextMenuRequested,
        this, &EditorWindow::show_project_browser_context_menu);
    connect(project_thumbnail_view_, &QListView::doubleClicked, this, &EditorWindow::activate_project_item);
    connect(project_thumbnail_view_, &QListView::activated, this, &EditorWindow::activate_project_item);
    project_thumbnails->set_external_drop_handler([this, select_drop_folder](const QStringList& paths, const QModelIndex& destination) {
        select_drop_folder(project_filter_->mapToSource(destination.siblingAtColumn(0)));
        import_asset_paths(paths);
    });
    project_thumbnails->set_domain_drop_handler([this](const QMimeData* mime, const QModelIndex& destination) {
        return handle_project_browser_drop(mime, project_filter_->mapToSource(destination.siblingAtColumn(0)));
    });
    project_folders->set_external_drop_handler([this](const QStringList& paths, const QModelIndex& destination) {
        if (destination.isValid()) update_project_browser_folder(destination);
        import_asset_paths(paths);
    });
    project_folders->set_domain_drop_handler([this](const QMimeData* mime, const QModelIndex& destination) {
        return handle_project_browser_drop(mime,
            project_folder_filter_->mapToSource(destination.siblingAtColumn(0)));
    });

    project_content_stack_ = new QStackedWidget(this);
    project_content_stack_->setObjectName(QStringLiteral("ProjectContentStack"));
    project_content_stack_->addWidget(project_explorer_);
    project_content_stack_->addWidget(project_thumbnail_view_);
    auto* project_panel = new QWidget(this);
    project_panel->setObjectName(QStringLiteral("ProjectExplorerPanel"));
    auto* project_layout = new QVBoxLayout(project_panel);
    project_layout->setContentsMargins(4, 4, 4, 4);
    project_layout->addWidget(project_search_);
    auto* breadcrumb_row = new QHBoxLayout;
    project_breadcrumb_ = new QLabel(QStringLiteral("Project"), project_panel);
    project_breadcrumb_->setObjectName(QStringLiteral("ProjectBreadcrumb"));
    project_breadcrumb_->setAccessibleName(QStringLiteral("Current project folder breadcrumb"));
    breadcrumb_row->addWidget(project_breadcrumb_, 1);
    auto* list_mode = new QToolButton(project_panel);
    list_mode->setObjectName(QStringLiteral("ProjectListMode"));
    list_mode->setText(QStringLiteral("List"));
    list_mode->setAccessibleName(QStringLiteral("Show project assets as a list"));
    auto* thumbnail_mode = new QToolButton(project_panel);
    thumbnail_mode->setObjectName(QStringLiteral("ProjectThumbnailMode"));
    thumbnail_mode->setText(QStringLiteral("Tiles"));
    thumbnail_mode->setAccessibleName(QStringLiteral("Show project assets as thumbnails"));
    connect(list_mode, &QToolButton::clicked, this, [this] { project_content_stack_->setCurrentIndex(0); });
    connect(thumbnail_mode, &QToolButton::clicked, this, [this] { project_content_stack_->setCurrentIndex(1); });
    breadcrumb_row->addWidget(list_mode);
    breadcrumb_row->addWidget(thumbnail_mode);
    project_layout->addLayout(breadcrumb_row);
    auto* project_filter_row = new QHBoxLayout;
    project_filter_row->addWidget(project_type_filter_);
    project_filter_row->addWidget(project_status_filter_);
    auto* project_refresh = new QPushButton(QStringLiteral("Refresh"), project_panel);
    project_refresh->setObjectName(QStringLiteral("RefreshProjectAction"));
    project_refresh->setAccessibleName(QStringLiteral("Refresh Project Explorer"));
    connect(project_refresh, &QPushButton::clicked, this, &EditorWindow::rebuild_assets);
    project_filter_row->addWidget(project_refresh);
    project_layout->addLayout(project_filter_row);
    auto* project_splitter = new QSplitter(Qt::Horizontal, project_panel);
    project_splitter->setObjectName(QStringLiteral("ProjectBrowserSplitter"));
    project_splitter->setAccessibleName(QStringLiteral("Project folders and asset content"));
    project_splitter->addWidget(project_folder_tree_);
    project_splitter->addWidget(project_content_stack_);
    project_splitter->setStretchFactor(0, 0);
    project_splitter->setStretchFactor(1, 1);
    project_splitter->setSizes({190, 520});
    project_layout->addWidget(project_splitter, 1);
    project_details_ = new QLabel(QStringLiteral("Select an asset to see details."), project_panel);
    project_details_->setObjectName(QStringLiteral("ProjectDetailsPane"));
    project_details_->setAccessibleName(QStringLiteral("Selected project asset details"));
    project_details_->setWordWrap(true);
    project_details_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    project_details_->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    project_details_->setMinimumHeight(48);
    project_layout->addWidget(project_details_);
    connect(project_explorer_->selectionModel(), &QItemSelectionModel::currentChanged,
        this, [this](const QModelIndex& current) { update_project_details(current); });
    connect(project_thumbnail_view_->selectionModel(), &QItemSelectionModel::currentChanged,
        this, [this](const QModelIndex& current) { update_project_details(current); });

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

    tile_document_service_ = new TileDocumentService(this);
    tile_palette_ = new TilePaletteWidget(tile_document_service_, this);
    tile_preview_timer_ = new QTimer(this);
    tile_preview_timer_->setSingleShot(true);
    tile_preview_timer_->setInterval(75);
    connect(tile_preview_timer_, &QTimer::timeout, this, [this] {
        if (tile_document_service_->has_active_stroke())
        {
            tile_preview_timer_->start();
        }
        else if (scene_ && !play_running_)
        {
            refresh_preview();
        }
    });
    connect(tile_palette_, &TilePaletteWidget::authoringStateChanged,
        this, &EditorWindow::update_tile_scene_edit_state);
    connect(tile_palette_, &TilePaletteWidget::customBrushRequested,
        this, [this](int x, int y) { request_custom_tile_brush({x, y}); });
    connect(tile_document_service_, &TileDocumentService::diagnostic, this, [this](const QString& message) {
        append_console(message, QStringLiteral("Warning"), QStringLiteral("Tile Authoring"));
    });
    connect(tile_document_service_, &TileDocumentService::dirtyChanged, this, [this](bool) {
        update_window_title();
        update_action_states();
    });
    connect(tile_document_service_, &TileDocumentService::documentChanged, this, [this] {
        ++tile_document_revision_;
        update_tile_scene_edit_state();
        if (tile_document_service_->is_loaded() && tile_preview_timer_)
            tile_preview_timer_->start();
        if (project_content_stack_ == nullptr) return;
        const auto current = project_content_stack_->currentIndex() == 0
            ? project_explorer_->currentIndex()
            : project_thumbnail_view_->currentIndex();
        if (current.isValid()) update_project_details(current);
    });

    auto* project_hub_panel = new QWidget(this);
    project_hub_panel->setObjectName(QStringLiteral("ProjectHubPanel"));
    project_hub_panel->setAccessibleName(QStringLiteral("Dragon Pixel Project Hub"));
    auto* project_hub_layout = new QVBoxLayout(project_hub_panel);
    project_hub_layout->setContentsMargins(28, 28, 28, 28);
    project_hub_layout->setSpacing(12);
    auto* project_hub_title = new QLabel(QStringLiteral("Dragon Pixel Project Hub"), project_hub_panel);
    project_hub_title->setObjectName(QStringLiteral("ProjectHubTitle"));
    auto hub_title_font = project_hub_title->font();
    hub_title_font.setPointSize(hub_title_font.pointSize() + 7);
    hub_title_font.setBold(true);
    project_hub_title->setFont(hub_title_font);
    project_hub_layout->addWidget(project_hub_title);
    auto* project_hub_copy = new QLabel(
        QStringLiteral("Create a clean 2D or 3D project, open an existing project, or continue from a recent project."),
        project_hub_panel);
    project_hub_copy->setWordWrap(true);
    project_hub_copy->setAccessibleName(QStringLiteral("Project Hub instructions"));
    project_hub_layout->addWidget(project_hub_copy);
    auto* project_hub_buttons = new QWidget(project_hub_panel);
    auto* project_hub_buttons_layout = new QHBoxLayout(project_hub_buttons);
    project_hub_buttons_layout->setContentsMargins(0, 0, 0, 0);
    project_hub_new_ = new QPushButton(QStringLiteral("New Project..."), project_hub_buttons);
    project_hub_new_->setObjectName(QStringLiteral("ProjectHubNewProject"));
    project_hub_new_->setAccessibleName(QStringLiteral("Create a new Dragon Pixel project"));
    project_hub_open_ = new QPushButton(QStringLiteral("Open Project..."), project_hub_buttons);
    project_hub_open_->setObjectName(QStringLiteral("ProjectHubOpenProject"));
    project_hub_open_->setAccessibleName(QStringLiteral("Open an existing Dragon Pixel project"));
    project_hub_buttons_layout->addWidget(project_hub_new_);
    project_hub_buttons_layout->addWidget(project_hub_open_);
    project_hub_buttons_layout->addStretch();
    project_hub_layout->addWidget(project_hub_buttons);
    auto* recent_label = new QLabel(QStringLiteral("Recent Projects"), project_hub_panel);
    recent_label->setObjectName(QStringLiteral("ProjectHubRecentLabel"));
    project_hub_layout->addWidget(recent_label);
    project_hub_recent_ = new QListWidget(project_hub_panel);
    project_hub_recent_->setObjectName(QStringLiteral("ProjectHubRecentProjects"));
    project_hub_recent_->setAccessibleName(QStringLiteral("Recent Dragon Pixel projects"));
    project_hub_layout->addWidget(project_hub_recent_, 1);
    connect(project_hub_new_, &QPushButton::clicked, this, &EditorWindow::create_project_dialog);
    connect(project_hub_open_, &QPushButton::clicked, this, &EditorWindow::open_project_dialog);
    connect(project_hub_recent_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        if (item != nullptr)
        {
            load_project(item->data(Qt::UserRole).toString());
        }
    });

    auto* onboarding_panel = new QWidget(this);
    onboarding_panel->setObjectName(QStringLiteral("GettingStartedPanel"));
    onboarding_panel->setAccessibleName(QStringLiteral("Dragon Pixel getting started guide"));
    auto* onboarding_layout = new QVBoxLayout(onboarding_panel);
    onboarding_layout->setContentsMargins(18, 18, 18, 18);
    onboarding_layout->setSpacing(10);
    auto* onboarding_title = new QLabel(QStringLiteral("Build your first playable object"), onboarding_panel);
    onboarding_title->setObjectName(QStringLiteral("GettingStartedTitle"));
    auto title_font = onboarding_title->font();
    title_font.setPointSize(title_font.pointSize() + 4);
    title_font.setBold(true);
    onboarding_title->setFont(title_font);
    onboarding_layout->addWidget(onboarding_title);
    auto* onboarding_copy = new QLabel(
        QStringLiteral("Create a visible sprite, select it in the Hierarchy, edit its Transform and component fields in the Inspector, then add a script and press Play. Use W, E, and R in Scene View for Move, Rotate, and Scale."),
        onboarding_panel);
    onboarding_copy->setObjectName(QStringLiteral("GettingStartedDescription"));
    onboarding_copy->setWordWrap(true);
    onboarding_copy->setAccessibleName(QStringLiteral("Getting started instructions"));
    onboarding_layout->addWidget(onboarding_copy);
    onboarding_add_square_ = new QPushButton(QStringLiteral("1. Add Square Sprite"), onboarding_panel);
    onboarding_add_square_->setObjectName(QStringLiteral("OnboardingAddSquare"));
    onboarding_add_square_->setAccessibleName(QStringLiteral("Add a square sprite GameObject"));
    connect(onboarding_add_square_, &QPushButton::clicked, this, [this] {
        create_preset(dragonpixel::scene::entity_preset::sprite, QStringLiteral("builtin://square"));
    });
    onboarding_layout->addWidget(onboarding_add_square_);
    onboarding_add_circle_ = new QPushButton(QStringLiteral("2. Add Circle Sprite"), onboarding_panel);
    onboarding_add_circle_->setObjectName(QStringLiteral("OnboardingAddCircle"));
    onboarding_add_circle_->setAccessibleName(QStringLiteral("Add a circle sprite GameObject"));
    connect(onboarding_add_circle_, &QPushButton::clicked, this, [this] {
        create_preset(dragonpixel::scene::entity_preset::sprite, QStringLiteral("builtin://circle"));
    });
    onboarding_layout->addWidget(onboarding_add_circle_);
    onboarding_add_component_ = new QPushButton(QStringLiteral("3. Add Component to Selection"), onboarding_panel);
    onboarding_add_component_->setObjectName(QStringLiteral("OnboardingAddComponent"));
    onboarding_add_component_->setAccessibleName(QStringLiteral("Add a component to the selected GameObject"));
    connect(onboarding_add_component_, &QPushButton::clicked, this, &EditorWindow::add_component);
    onboarding_layout->addWidget(onboarding_add_component_);
    onboarding_create_csharp_ = new QPushButton(QStringLiteral("4. Create and Attach C# Script"), onboarding_panel);
    onboarding_create_csharp_->setObjectName(QStringLiteral("OnboardingCreateCSharpScript"));
    onboarding_create_csharp_->setAccessibleName(QStringLiteral("Create a C sharp script component and attach it to the selection"));
    connect(onboarding_create_csharp_, &QPushButton::clicked, this,
        [this] { create_project_component(ProjectComponentLanguage::csharp); });
    onboarding_layout->addWidget(onboarding_create_csharp_);
    onboarding_create_cpp_ = new QPushButton(QStringLiteral("5. Create and Attach C++ Component"), onboarding_panel);
    onboarding_create_cpp_->setObjectName(QStringLiteral("OnboardingCreateCppComponent"));
    onboarding_create_cpp_->setAccessibleName(QStringLiteral("Create a C plus plus component and attach it to the selection"));
    connect(onboarding_create_cpp_, &QPushButton::clicked, this,
        [this] { create_project_component(ProjectComponentLanguage::cpp); });
    onboarding_layout->addWidget(onboarding_create_cpp_);
    onboarding_play_ = new QPushButton(QStringLiteral("6. Play"), onboarding_panel);
    onboarding_play_->setObjectName(QStringLiteral("OnboardingPlay"));
    onboarding_play_->setAccessibleName(QStringLiteral("Play the current scene"));
    connect(onboarding_play_, &QPushButton::clicked, this, [this] {
        if (play_action_ != nullptr && play_action_->isEnabled()) start_play();
    });
    onboarding_layout->addWidget(onboarding_play_);
    auto* onboarding_hint = new QLabel(
        QStringLiteral("Script fields such as Speed are saved authoring data and stay editable in the Inspector even before Build Components succeeds. Script code runs only in isolated Preview and Play workers."),
        onboarding_panel);
    onboarding_hint->setObjectName(QStringLiteral("GettingStartedScriptHint"));
    onboarding_hint->setWordWrap(true);
    onboarding_hint->setAccessibleName(QStringLiteral("Script authoring safety guidance"));
    onboarding_layout->addWidget(onboarding_hint);
    onboarding_layout->addStretch();
    auto* dismiss_onboarding = new QPushButton(QStringLiteral("Open Scene View"), onboarding_panel);
    dismiss_onboarding->setObjectName(QStringLiteral("OnboardingDismiss"));
    dismiss_onboarding->setAccessibleName(QStringLiteral("Close getting started and open Scene View"));
    connect(dismiss_onboarding, &QPushButton::clicked, this, [this] {
        onboarding_dock_->hide();
        scene_view_dock_->show();
        scene_view_dock_->raise();
        QSettings settings{editor_settings_path(), QSettings::IniFormat};
        settings.setValue(QStringLiteral("onboarding/seen"), true);
    });
    onboarding_layout->addWidget(dismiss_onboarding);

    auto make_dock = [this](const QString& title, const QString& id, QWidget* widget, Qt::DockWidgetArea area) {
        auto* dock = new QDockWidget(title, this);
        dock->setObjectName(QStringLiteral("Dock.%1").arg(id));
        dock->setWidget(widget);
        dock->setAccessibleName(title);
        dock->setAllowedAreas(Qt::AllDockWidgetAreas);
        dock->setFeatures(
            QDockWidget::DockWidgetClosable
            | QDockWidget::DockWidgetMovable
            | QDockWidget::DockWidgetFloatable);
        addDockWidget(area, dock);
        return dock;
    };
    scene_dock_ = make_dock(QStringLiteral("Scene"), QStringLiteral("Scene"), scene_summary_, Qt::LeftDockWidgetArea);
    scene_view_dock_ = make_dock(QStringLiteral("Scene View"), QStringLiteral("SceneView"), viewport_, Qt::RightDockWidgetArea);
    game_view_dock_ = make_dock(QStringLiteral("Game"), QStringLiteral("GameView"), game_panel, Qt::RightDockWidgetArea);
    hierarchy_dock_ = make_dock(QStringLiteral("Hierarchy"), QStringLiteral("Hierarchy"), hierarchy_panel, Qt::LeftDockWidgetArea);
    assets_dock_ = make_dock(QStringLiteral("Project Explorer"), QStringLiteral("ProjectExplorer"), project_panel, Qt::LeftDockWidgetArea);
    inspector_dock_ = make_dock(QStringLiteral("Inspector"), QStringLiteral("Inspector"), inspector_panel_, Qt::RightDockWidgetArea);
    tile_palette_dock_ = make_dock(QStringLiteral("Tile Palette"), QStringLiteral("TilePalette"), tile_palette_, Qt::BottomDockWidgetArea);
    console_dock_ = make_dock(QStringLiteral("Console"), QStringLiteral("Console"), console_panel, Qt::BottomDockWidgetArea);
    onboarding_dock_ = make_dock(QStringLiteral("Getting Started"), QStringLiteral("GettingStarted"), onboarding_panel, Qt::RightDockWidgetArea);
    project_hub_dock_ = make_dock(QStringLiteral("Project Hub"), QStringLiteral("ProjectHub"), project_hub_panel, Qt::RightDockWidgetArea);
    tabifyDockWidget(scene_dock_, hierarchy_dock_);
    tabifyDockWidget(scene_view_dock_, game_view_dock_);
    tabifyDockWidget(scene_view_dock_, onboarding_dock_);
    tabifyDockWidget(scene_view_dock_, project_hub_dock_);
    tabifyDockWidget(console_dock_, tile_palette_dock_);
    hierarchy_dock_->raise();
    scene_view_dock_->raise();
    tile_palette_dock_->hide();
    project_hub_dock_->hide();

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
        game_viewport_->set_input_enabled(false);
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
    auto add_toolbar_preset = [this, preset_menu](const QString& text, const QString& name, auto preset) {
        auto* action = preset_menu->addAction(text);
        action->setObjectName(name);
        connect(action, &QAction::triggered, this, [this, preset] { create_preset(preset); });
        return action;
    };
    add_toolbar_preset(QStringLiteral("Empty"), QStringLiteral("AddEmptyGameObjectAction"), dragonpixel::scene::entity_preset::empty);
    add_toolbar_preset(QStringLiteral("Sprite"), QStringLiteral("AddSpriteGameObjectAction"), dragonpixel::scene::entity_preset::sprite);
    auto* square_sprite = preset_menu->addAction(QStringLiteral("Square Sprite"));
    square_sprite->setObjectName(QStringLiteral("AddSquareSpriteAction"));
    connect(square_sprite, &QAction::triggered, this, [this] {
        create_preset(dragonpixel::scene::entity_preset::sprite, QStringLiteral("builtin://square"));
    });
    auto* circle_sprite = preset_menu->addAction(QStringLiteral("Circle Sprite"));
    circle_sprite->setObjectName(QStringLiteral("AddCircleSpriteAction"));
    connect(circle_sprite, &QAction::triggered, this, [this] {
        create_preset(dragonpixel::scene::entity_preset::sprite, QStringLiteral("builtin://circle"));
    });
    add_toolbar_preset(QStringLiteral("Cube"), QStringLiteral("AddCubeGameObjectAction"), dragonpixel::scene::entity_preset::cube);
    add_toolbar_preset(QStringLiteral("Camera"), QStringLiteral("AddCameraGameObjectAction"), dragonpixel::scene::entity_preset::camera);
    add_toolbar_preset(QStringLiteral("Light"), QStringLiteral("AddLightGameObjectAction"), dragonpixel::scene::entity_preset::light);
    add_toolbar_preset(QStringLiteral("Tilemap 2D"), QStringLiteral("AddTilemapGameObjectAction"), dragonpixel::scene::entity_preset::tilemap);
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
    auto* new_project = file_menu->addAction(QStringLiteral("&New Project..."));
    new_project->setObjectName(QStringLiteral("NewProjectAction"));
    new_project->setShortcut(QKeySequence{Qt::CTRL | Qt::SHIFT | Qt::Key_N});
    connect(new_project, &QAction::triggered, this, &EditorWindow::create_project_dialog);
    new_scene_action_ = file_menu->addAction(QStringLiteral("New &Scene..."));
    new_scene_action_->setObjectName(QStringLiteral("NewSceneAction"));
    new_scene_action_->setShortcut(QKeySequence::New);
    connect(new_scene_action_, &QAction::triggered, this, &EditorWindow::create_clean_scene_dialog);
    file_menu->addSeparator();
    auto* open_project = file_menu->addAction(QStringLiteral("Open &Project..."));
    open_project->setObjectName(QStringLiteral("OpenProjectAction"));
    connect(open_project, &QAction::triggered, this, &EditorWindow::open_project_dialog);
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
    save_scene_as_action_ = file_menu->addAction(QStringLiteral("Save Scene &As..."));
    save_scene_as_action_->setObjectName(QStringLiteral("SaveSceneAsAction"));
    save_scene_as_action_->setShortcut(QKeySequence::SaveAs);
    connect(save_scene_as_action_, &QAction::triggered, this, &EditorWindow::save_scene_as);
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
    edit_menu->addSeparator();
    auto* project_settings_menu = edit_menu->addMenu(QStringLiteral("Project Settings"));
    project_settings_menu->setObjectName(QStringLiteral("ProjectSettingsMenu"));
    input_settings_action_ = project_settings_menu->addAction(QStringLiteral("Input..."));
    input_settings_action_->setObjectName(QStringLiteral("InputSettingsAction"));
    input_settings_action_->setToolTip(QStringLiteral("Configure control maps, actions, keyboard, mouse, and gamepad bindings"));
    connect(input_settings_action_, &QAction::triggered, this, &EditorWindow::edit_input_map);

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

    auto* assets_menu = menuBar()->addMenu(QStringLiteral("&Assets"));
    assets_menu->setObjectName(QStringLiteral("AssetsMenu"));
    auto* import_assets = assets_menu->addAction(QStringLiteral("Import..."));
    import_assets->setObjectName(QStringLiteral("ImportAssetsAction"));
    connect(import_assets, &QAction::triggered, this, [this] {
        import_asset_paths(QFileDialog::getOpenFileNames(this, QStringLiteral("Import Assets"),
            QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
            QStringLiteral("All Files (*.*)")));
    });
    auto* create_asset_folder = assets_menu->addAction(QStringLiteral("Create Folder..."));
    create_asset_folder->setObjectName(QStringLiteral("CreateAssetFolderAction"));
    connect(create_asset_folder, &QAction::triggered, this, [this] {
        if (project_manifest_path_.isEmpty()) return;
        bool accepted = false;
        const auto name = QInputDialog::getText(this, QStringLiteral("Create Folder"),
            QStringLiteral("Folder name"), QLineEdit::Normal,
            QStringLiteral("New Folder"), &accepted).trimmed();
        if (!accepted || name.isEmpty()) return;
        const auto result = asset_service_.create_folder(project_manifest_path_,
            QDir::fromNativeSeparators(QDir{current_project_folder_relative()}.filePath(name)));
        if (!result.succeeded)
            QMessageBox::warning(this, QStringLiteral("Create Folder"),
                result.diagnostics.isEmpty() ? QStringLiteral("Folder creation failed.")
                                             : result.diagnostics.constFirst().message);
        else rebuild_assets();
    });
    assets_menu->addAction(QStringLiteral("Refresh"), this, &EditorWindow::rebuild_assets);
    assets_menu->addSeparator();
    auto* create_tile_set = assets_menu->addAction(QStringLiteral("Create TileSet from Image..."));
    create_tile_set->setObjectName(QStringLiteral("CreateTileSetFromImageAction"));
    create_tile_set->setStatusTip(
        QStringLiteral("Slice a PNG, JPEG, BMP, or GIF sprite sheet into a reusable TileSet"));
    connect(create_tile_set, &QAction::triggered, this, &EditorWindow::create_tile_set_from_image);
    auto* create_tilemap = assets_menu->addAction(QStringLiteral("Create Tilemap from Selected TileSet..."));
    create_tilemap->setObjectName(QStringLiteral("CreateTilemapFromSelectedTileSetAction"));
    create_tilemap->setStatusTip(
        QStringLiteral("Create an empty orthogonal Tilemap that depends on the selected TileSet"));
    connect(create_tilemap, &QAction::triggered,
        this, &EditorWindow::create_tilemap_from_selected_tileset);
    import_tiled_tilemap_action_ = assets_menu->addAction(QStringLiteral("Import Tiled Tilemap..."));
    import_tiled_tilemap_action_->setObjectName(QStringLiteral("ImportTiledTilemapAction"));
    import_tiled_tilemap_action_->setShortcut(QKeySequence{QStringLiteral("Ctrl+Alt+T")});
    import_tiled_tilemap_action_->setStatusTip(
        QStringLiteral("Import an orthogonal Tiled JSON map and atlas into the Tile Palette"));
    connect(import_tiled_tilemap_action_, &QAction::triggered,
        this, &EditorWindow::import_tiled_tilemap);
    input_map_action_ = assets_menu->addAction(QStringLiteral("Input Map..."));
    input_map_action_->setObjectName(QStringLiteral("InputMapAction"));
    connect(input_map_action_, &QAction::triggered, this, &EditorWindow::edit_input_map);

    auto* components_menu = menuBar()->addMenu(QStringLiteral("&Components"));
    components_menu->setObjectName(QStringLiteral("ComponentsMenu"));
    auto* create_csharp_component = components_menu->addAction(QStringLiteral("Create C# Script..."));
    create_csharp_component->setObjectName(QStringLiteral("CreateCSharpComponentAction"));
    connect(create_csharp_component, &QAction::triggered, this,
        [this] { create_project_component(ProjectComponentLanguage::csharp); });
    auto* create_cpp_component = components_menu->addAction(QStringLiteral("Create C++ Component..."));
    create_cpp_component->setObjectName(QStringLiteral("CreateCppComponentAction"));
    connect(create_cpp_component, &QAction::triggered, this,
        [this] { create_project_component(ProjectComponentLanguage::cpp); });
    components_menu->addSeparator();
    auto* build_components = components_menu->addAction(QStringLiteral("Build Components"));
    build_components->setObjectName(QStringLiteral("BuildComponentsAction"));
    build_components->setShortcut(QKeySequence{QStringLiteral("Ctrl+Shift+B")});
    connect(build_components, &QAction::triggered, this, &EditorWindow::build_project_components);

    view_menu_ = menuBar()->addMenu(QStringLiteral("&View"));
    view_menu_->setObjectName(QStringLiteral("ViewMenu"));
    for (auto* dock : {scene_view_dock_, game_view_dock_, scene_dock_, hierarchy_dock_, assets_dock_, inspector_dock_, tile_palette_dock_, console_dock_, onboarding_dock_, project_hub_dock_})
    {
        view_menu_->addAction(dock->toggleViewAction());
    }
    view_menu_->addSeparator();
    auto* new_inspector = view_menu_->addAction(QStringLiteral("New Inspector"));
    new_inspector->setObjectName(QStringLiteral("NewInspectorAction"));
    connect(new_inspector, &QAction::triggered, this, &EditorWindow::create_additional_inspector);
    QSettings inspector_settings{editor_settings_path(), QSettings::IniFormat};
    const auto saved_inspector_count = std::clamp(
        inspector_settings.value(QStringLiteral("inspector/count"), 1).toInt(), 1, 8);
    for (int index = 1; index < saved_inspector_count; ++index) create_additional_inspector();
    view_menu_->addSeparator();
    auto* workspace_menu = view_menu_->addMenu(QStringLiteral("Workspaces"));
    auto add_workspace = [this, workspace_menu](const QString& label, const QString& id) {
        auto* action = workspace_menu->addAction(label);
        action->setObjectName(QStringLiteral("Workspace%1Action").arg(id));
        connect(action, &QAction::triggered, this, [this, id] { apply_workspace(id); });
    };
    add_workspace(QStringLiteral("2D"), QStringLiteral("2D"));
    add_workspace(QStringLiteral("3D"), QStringLiteral("3D"));
    add_workspace(QStringLiteral("Debug"), QStringLiteral("Debug"));
    auto* reset_layout = view_menu_->addAction(QStringLiteral("Reset Layout"));
    reset_layout->setObjectName(QStringLiteral("ResetLayoutAction"));
    connect(reset_layout, &QAction::triggered, this, &EditorWindow::reset_workspace);
    auto* save_layout = view_menu_->addAction(QStringLiteral("Save Layout"));
    save_layout->setObjectName(QStringLiteral("SaveLayoutAction"));
    connect(save_layout, &QAction::triggered, this, [this] {
        QSettings settings{editor_settings_path(), QSettings::IniFormat};
        settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
        settings.setValue(QStringLiteral("window/state"), saveState(workspace_state_version));
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
    const auto saved_workspace = settings.value(QStringLiteral("window/state")).toByteArray();
    if (!saved_workspace.isEmpty()
        && !restoreState(saved_workspace, workspace_state_version))
    {
        settings.remove(QStringLiteral("window/state"));
        reset_workspace();
    }
    if (!settings.value(QStringLiteral("onboarding/seen"), false).toBool())
    {
        onboarding_dock_->show();
        onboarding_dock_->raise();
    }
    viewport_->set_view_mode(settings.value(QStringLiteral("workspace/current"), QStringLiteral("3D")).toString()
            == QStringLiteral("2D")
        ? AuthoringViewport::ViewMode::two_d
        : AuthoringViewport::ViewMode::three_d);
    update_action_states();
}

void EditorWindow::create_tile_set_from_image()
{
    if (project_root_.isEmpty())
    {
        append_console(QStringLiteral("Open a project before creating a TileSet."),
            QStringLiteral("Warning"), QStringLiteral("Tile Authoring"));
        return;
    }
    TileSetWizard wizard{project_root_, this};
    if (wizard.exec() != QDialog::Accepted)
    {
        return;
    }
    const auto& created = wizard.result();
    append_console(QStringLiteral("Created TileSet %1 with %2 tile(s) and contained texture %3")
        .arg(QFileInfo{created.tile_set_path}.completeBaseName())
        .arg(created.tile_count)
        .arg(QFileInfo{created.texture_path}.fileName()),
        QStringLiteral("Info"), QStringLiteral("Tile Authoring"), created.tile_set_path,
        {}, {}, {}, {}, created.tile_set_asset_id, created.tile_set_path);
    rebuild_assets();
    if (wizard.complete_tilemap_workflow())
    {
        const auto map_name = QStringLiteral("%1 Map").arg(wizard.tile_set_name());
        if (!create_tilemap_from_tileset(
                created.tile_set_asset_id, map_name, wizard.grid_layout()))
        {
            statusBar()->showMessage(QStringLiteral(
                "TileSet created, but its Tilemap workflow could not be completed. The TileSet remains available in Project Explorer."),
                8000);
        }
    }
}

void EditorWindow::create_tilemap_from_selected_tileset()
{
    auto* active_view = project_content_stack_->currentIndex() == 0
        ? static_cast<QAbstractItemView*>(project_explorer_)
        : static_cast<QAbstractItemView*>(project_thumbnail_view_);
    const auto current = active_view->currentIndex();
    if (!current.isValid()
        || ProjectModel::item_kind(current) != ProjectItemKind::asset
        || !ProjectModel::asset_type(current).contains(
            QStringLiteral("tileset"), Qt::CaseInsensitive))
    {
        append_console(QStringLiteral("Select one TileSet asset in Project Explorer before creating a Tilemap."),
            QStringLiteral("Warning"), QStringLiteral("Tile Authoring"));
        return;
    }
    const auto suggested = QStringLiteral("%1 Map")
        .arg(current.siblingAtColumn(0).data().toString());
    static_cast<void>(prompt_create_tilemap_from_tileset(
        ProjectModel::asset_id(current), suggested));
}

bool EditorWindow::prompt_create_tilemap_from_tileset(
    const QString& tileset_asset_id,
    const QString& suggested_name)
{
    std::optional<QString> requested;
    if (tilemap_name_prompt_)
    {
        requested = tilemap_name_prompt_(suggested_name);
    }
    else
    {
        bool accepted = false;
        const auto name = QInputDialog::getText(this, QStringLiteral("Create Tilemap"),
            QStringLiteral("Tilemap name"), QLineEdit::Normal,
            suggested_name, &accepted).trimmed();
        if (accepted) requested = name;
    }
    if (!requested || requested->trimmed().isEmpty()) return false;
    return create_tilemap_from_tileset(tileset_asset_id, requested->trimmed());
}

std::optional<dragonpixel::core::uuid> EditorWindow::attach_tilemap_to_scene(
    const QString& tilemap_asset_id)
{
    if (!scene_)
    {
        append_console(QStringLiteral(
            "The Tilemap is ready in Project Explorer and the Tile Palette, but no scene is open for a Tilemap2D GameObject."),
            QStringLiteral("Warning"), QStringLiteral("Tile Authoring"),
            {}, {}, {}, {}, {}, tilemap_asset_id);
        return std::nullopt;
    }

    const auto selected = selected_entity_ids();
    if (selected.size() == 1)
    {
        const auto* entity = scene_->find_entity(selected.front());
        if (entity != nullptr)
        {
            const auto component = std::find_if(
                entity->components.cbegin(), entity->components.cend(), [](const auto& candidate) {
                    return candidate.enabled && !candidate.opaque
                        && candidate.type_id
                            == dragonpixel::metadata::builtin_component_ids::tilemap_2d;
                });
            if (component != entity->components.cend()
                && component->properties.value(
                    "dpe.tilemap.asset", std::string{}).empty())
            {
                if (!apply_authoring_transaction({dragonpixel::scene::command{
                    dragonpixel::scene::set_component_property_command{
                        selected.front(), component->type_id,
                        "dpe.tilemap.asset", tilemap_asset_id.toStdString()}}},
                    "Assign created Tilemap"))
                {
                    append_console(QStringLiteral(
                        "The Tilemap was created and opened, but assigning it to the selected Tilemap2D GameObject was rejected. The map remains available in Project Explorer."),
                        QStringLiteral("Warning"), QStringLiteral("Tile Authoring"),
                        {}, {}, {}, {},
                        QString::fromStdString(selected.front().to_string()), tilemap_asset_id);
                    return std::nullopt;
                }
                after_scene_mutation(QStringLiteral(
                    "Assigned the created Tilemap to the selected Tilemap2D GameObject through one validated transaction"),
                    selected);
                return selected.front();
            }
        }
    }

    const auto created = create_preset(
        dragonpixel::scene::entity_preset::tilemap, tilemap_asset_id, true);
    if (!created)
    {
        append_console(QStringLiteral(
            "The Tilemap was created and opened, but its Tilemap2D GameObject could not be created. The map remains available in Project Explorer."),
            QStringLiteral("Warning"), QStringLiteral("Tile Authoring"),
            {}, {}, {}, {}, {}, tilemap_asset_id);
    }
    return created;
}

bool EditorWindow::create_tilemap_from_tileset(
    const QString& tileset_asset_id,
    const QString& name,
    const QString& grid_layout)
{
    if (project_manifest_path_.isEmpty())
    {
        append_console(QStringLiteral("Open a project before creating a Tilemap."),
            QStringLiteral("Warning"), QStringLiteral("Tile Authoring"));
        return false;
    }
    if (tile_document_service_->is_dirty())
    {
        const auto decision = unsaved_prompt_
            ? unsaved_prompt_(QFileInfo{tile_document_service_->tilemap_path()}.fileName())
            : UnsavedDecision::cancel;
        if (decision == UnsavedDecision::cancel
            || (decision == UnsavedDecision::save && !tile_document_service_->save()))
        {
            return false;
        }
    }
    const auto result = asset_service_.create_tilemap({
        project_manifest_path_, tileset_asset_id, name, true, grid_layout});
    for (const auto& diagnostic : result.diagnostics)
    {
        append_console(diagnostic.message,
            result.succeeded ? QStringLiteral("Info") : QStringLiteral("Warning"),
            QStringLiteral("Tile Authoring"), diagnostic.path,
            {}, {}, result.operation_id, {}, tileset_asset_id, diagnostic.path);
    }
    if (!result.succeeded || result.asset_ids.isEmpty())
    {
        if (result.diagnostics.isEmpty())
        {
            append_console(QStringLiteral("Tilemap creation failed before publication."),
                QStringLiteral("Warning"), QStringLiteral("Tile Authoring"));
        }
        return false;
    }
    rebuild_assets();
    const auto map_id = result.asset_ids.constFirst();
    const auto* map_entry = project_index_.candidate
        ? project_index_.candidate->find_by_id(map_id) : nullptr;
    const auto* set_entry = project_index_.candidate
        ? project_index_.candidate->find_by_id(tileset_asset_id) : nullptr;
    const auto* palette_entry = project_index_.candidate && result.asset_ids.size() > 1
        ? project_index_.candidate->find_by_id(result.asset_ids.at(1)) : nullptr;
    if (map_entry == nullptr || set_entry == nullptr
        || !tile_palette_->load_documents(
            map_entry->resolved_source_path,
            set_entry->resolved_source_path,
            tile_texture_path_for(set_entry),
            palette_entry ? palette_entry->resolved_source_path : QString{}))
    {
        append_console(QStringLiteral("The Tilemap was created but could not be opened in the Tile Palette."),
            QStringLiteral("Warning"), QStringLiteral("Tile Authoring"),
            map_entry ? map_entry->resolved_source_path : QString{},
            {}, {}, result.operation_id, {}, map_id);
        return false;
    }
    tile_palette_dock_->show();
    tile_palette_dock_->raise();
    append_console(QStringLiteral("Created empty Tilemap %1 from the selected TileSet and opened it for painting.")
        .arg(name), QStringLiteral("Info"), QStringLiteral("Tile Authoring"),
        map_entry->resolved_source_path, {}, {}, result.operation_id, {}, map_id,
        map_entry->resolved_source_path);
    const auto scene_target = attach_tilemap_to_scene(map_id);
    if (scene_target)
    {
        apply_workspace(QStringLiteral("2D"));
        tile_palette_dock_->show();
        tile_palette_dock_->raise();
        update_tile_scene_edit_state();
        statusBar()->showMessage(QStringLiteral(
            "Tilemap ready: choose a tile in the Tile Palette and paint in Scene View."), 8000);
    }
    return true;
}

void EditorWindow::import_tiled_tilemap()
{
    if (project_manifest_path_.isEmpty())
    {
        append_console(QStringLiteral("Open a project before importing a Tiled tilemap."),
            QStringLiteral("Warning"), QStringLiteral("Tile Import"));
        return;
    }
    if (tile_document_service_->is_dirty())
    {
        const auto decision = unsaved_prompt_
            ? unsaved_prompt_(QFileInfo{tile_document_service_->tilemap_path()}.fileName())
            : UnsavedDecision::cancel;
        if (decision == UnsavedDecision::cancel
            || (decision == UnsavedDecision::save && !tile_document_service_->save()))
        {
            return;
        }
    }
    const auto source = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Import Tiled Tilemap"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        QStringLiteral("Tiled JSON Maps (*.tmj *.json)"));
    if (source.isEmpty())
    {
        return;
    }
    bool accepted = false;
    const auto pixels_per_unit = QInputDialog::getDouble(
        this,
        QStringLiteral("Tile Scale"),
        QStringLiteral("Pixels per world unit"),
        32.0,
        0.01,
        1'000'000.0,
        2,
        &accepted);
    if (!accepted)
    {
        return;
    }

    (void)perform_tiled_tilemap_import(source, pixels_per_unit);
}

bool EditorWindow::perform_tiled_tilemap_import(
    const QString& source,
    double pixels_per_unit)
{
    QProgressDialog progress{
        QStringLiteral("Importing and validating Tiled tilemap..."),
        QStringLiteral("Cancel"), 0, 0, this};
    progress.setObjectName(QStringLiteral("TiledTilemapImportProgress"));
    progress.setWindowTitle(QStringLiteral("Import Tiled Tilemap"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.show();
    const auto result = tile_import_service_.import_tiled_json({
        project_manifest_path_,
        source,
        QFileInfo{source}.completeBaseName(),
        pixels_per_unit,
        30'000,
        [&progress] { return progress.wasCanceled(); },
    });
    progress.close();
    if (!result.succeeded)
    {
        const auto message = result.diagnostics.isEmpty()
            ? QStringLiteral("Tiled tilemap import failed.")
            : QStringLiteral("%1: %2")
                  .arg(result.diagnostics.constFirst().code,
                      result.diagnostics.constFirst().message);
        append_console(message, QStringLiteral("Error"), QStringLiteral("Tile Import"),
            source, {}, {}, result.operation_id);
        QMessageBox::warning(this, QStringLiteral("Tiled tilemap import failed"), message);
        return false;
    }

    rebuild_assets();
    if (!tile_palette_->load_documents(
            result.tilemap_path, result.tileset_path, result.texture_path,
            result.palette_path))
    {
        append_console(QStringLiteral("The imported assets were published, but the Tile Palette could not open them."),
            QStringLiteral("Error"), QStringLiteral("Tile Import"), result.tilemap_path,
            {}, {}, result.operation_id, {}, result.tilemap_asset_id, result.tilemap_path);
        QMessageBox::warning(this, QStringLiteral("Tile Palette"),
            QStringLiteral("The imported assets are in the Project Explorer, but the Tile Palette could not open them."));
        return false;
    }
    tile_palette_dock_->show();
    tile_palette_dock_->raise();
    append_console(
        QStringLiteral("Imported %1 tiles, %2 layers, and %3 occupied cells from %4")
            .arg(result.tile_count)
            .arg(result.layer_count)
            .arg(result.cell_count)
            .arg(QFileInfo{source}.fileName()),
        QStringLiteral("Info"), QStringLiteral("Tile Import"), result.tilemap_path,
        {}, {}, result.operation_id, {}, result.tilemap_asset_id, result.tilemap_path);
    statusBar()->showMessage(QStringLiteral("Tiled tilemap imported into the Tile Palette"), 5000);
    return true;
}

QString EditorWindow::tile_texture_path_for(
    const ProjectIndexEntry* tileset_entry) const
{
    if (tileset_entry == nullptr || !project_index_.candidate
        || tileset_entry->dependencies.isEmpty())
    {
        return {};
    }
    const auto* texture_entry = project_index_.candidate->find_by_id(
        tileset_entry->dependencies.front());
    if (texture_entry == nullptr
        || texture_entry->kind != ProjectIndexEntryKind::asset
        || !texture_entry->structurally_valid)
    {
        return {};
    }
    return texture_entry->resolved_source_path;
}

void EditorWindow::create_project_component(ProjectComponentLanguage language)
{
    if (project_root_.isEmpty())
    {
        append_console(QStringLiteral("Open a project before creating a component."),
            QStringLiteral("Warning"), QStringLiteral("Components"));
        return;
    }
    std::optional<QString> prompted_name;
    if (component_name_prompt_)
    {
        prompted_name = component_name_prompt_(language);
    }
    else
    {
        bool accepted{};
        const auto label = language == ProjectComponentLanguage::csharp
            ? QStringLiteral("C# script name") : QStringLiteral("C++ component name");
        const auto name = QInputDialog::getText(
            this,
            QStringLiteral("Create Project Script or Component"),
            label,
            QLineEdit::Normal,
            QStringLiteral("NewComponent"),
            &accepted).trimmed();
        if (accepted)
        {
            prompted_name = name;
        }
    }
    if (!prompted_name || prompted_name->trimmed().isEmpty()) return;
    const auto name = prompted_name->trimmed();
    QStringList component_roots;
    if (project_index_.candidate)
    {
        for (const auto& root : project_index_.candidate->roots)
        {
            if (root.kind == ProjectIndexRootKind::components)
                component_roots.push_back(root.declared_path);
        }
    }
    QString selected_component_root;
    if (component_roots.size() == 1)
    {
        selected_component_root = component_roots.front();
    }
    else if (component_roots.size() > 1)
    {
        bool root_accepted{};
        selected_component_root = QInputDialog::getItem(
            this,
            QStringLiteral("Select Component Root"),
            QStringLiteral("Component root"),
            component_roots,
            0,
            false,
            &root_accepted);
        if (!root_accepted) return;
    }
    ComponentCreationRequest request;
    request.project_root = project_root_;
    request.component_roots = component_roots;
    request.display_name = name;
    request.category = QStringLiteral("Scripts");
    request.language = language;
    request.selected_component_root = selected_component_root;
    append_console(
        QStringLiteral("Generating contained %1 source and metadata for %2...")
            .arg(language == ProjectComponentLanguage::csharp
                    ? QStringLiteral("C# script")
                    : QStringLiteral("C++ component"),
                name),
        QStringLiteral("Info"),
        QStringLiteral("Components"));
    const auto created = ComponentModuleService::create(request);
    if (!created.succeeded)
    {
        append_console(created.error, QStringLiteral("Error"), QStringLiteral("Components"));
        statusBar()->showMessage(QStringLiteral("Component creation failed; see Console"), 5000);
        return;
    }
    if (!reload_project_component_metadata())
    {
        append_console(QStringLiteral("The source was created, but its metadata could not be activated. Fix the reported metadata diagnostic before using it."),
            QStringLiteral("Error"), QStringLiteral("Components"), created.manifest_path);
        return;
    }
    apply_component_module_manifest({});
    const auto targets = selected_entity_ids();
    const auto attached = !targets.isEmpty()
        && attach_component_type(
            created.type_id,
            targets,
            QStringLiteral("Attach newly created project component"));
    append_console(QStringLiteral("Created %1 %2 (%3)%4.%5")
        .arg(language == ProjectComponentLanguage::csharp ? QStringLiteral("C# script") : QStringLiteral("C++ component"),
             name,
             created.type_id,
             attached ? QStringLiteral(" and attached it to %1 selected GameObject(s)").arg(targets.size()) : QString{},
             language == ProjectComponentLanguage::csharp
                 ? QStringLiteral(" Building scripts now so it can run in Preview and Play")
                 : QStringLiteral(" Build Components to make it executable in workers")),
        QStringLiteral("Info"), QStringLiteral("Components"), created.source_path,
        {}, {}, {}, {}, {}, created.source_path);
    rebuild_assets();
    if (language == ProjectComponentLanguage::csharp && auto_build_project_scripts_)
    {
        QTimer::singleShot(0, this, [this] {
            if (!project_root_.isEmpty()) build_project_components();
        });
    }
}

bool EditorWindow::reload_project_component_metadata()
{
    if (project_root_.isEmpty() || !project_index_.candidate) return false;
    QStringList component_roots;
    for (const auto& root : project_index_.candidate->roots)
    {
        if (root.kind == ProjectIndexRootKind::components) component_roots.push_back(root.declared_path);
    }
    auto candidate = dragonpixel::metadata::registry::slice_one_defaults();
    const auto loaded = MetadataManifestService{}.load_project(project_root_, component_roots, candidate);
    for (const auto& diagnostic : loaded.diagnostics)
        append_console(diagnostic, loaded.succeeded ? QStringLiteral("Warning") : QStringLiteral("Error"),
            QStringLiteral("Metadata"));
    if (!loaded.succeeded) return false;
    metadata_ = std::move(candidate);
    prefab_service_.set_metadata(&metadata_);
    inspect_selected_entities();
    update_action_states();
    return true;
}

void EditorWindow::build_project_components()
{
    if (project_root_.isEmpty())
    {
        append_console(QStringLiteral("Open a project before building components."),
            QStringLiteral("Warning"), QStringLiteral("Components"));
        return;
    }
    QStringList component_roots;
    if (project_index_.candidate)
    {
        for (const auto& root : project_index_.candidate->roots)
        {
            if (root.kind == ProjectIndexRootKind::components)
                component_roots.push_back(root.declared_path);
        }
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto result = ComponentModuleService::build(
        project_root_, component_roots, dragonpixel::editor::runtime_paths::file(
            "DPE_CONTRACTS_ASSEMBLY",
            QStringLiteral("runtime/contracts/DragonPixel.Contracts.dll"),
            QString::fromUtf8(DPE_CONTRACTS_ASSEMBLY)));
    QApplication::restoreOverrideCursor();
    for (const auto& diagnostic : result.diagnostics)
    {
        const auto compact = diagnostic.trimmed();
        if (!compact.isEmpty()) append_console(compact, result.succeeded ? QStringLiteral("Info") : QStringLiteral("Error"),
            QStringLiteral("Component Build"));
    }
    if (!result.succeeded)
    {
        apply_component_module_manifest({});
        if (play_running_) stop_play();
        preview_worker_->stop_and_discard();
        game_preview_worker_->stop_and_discard();
        refresh_preview();
        append_console(result.error, QStringLiteral("Error"), QStringLiteral("Component Build"));
        QMessageBox::warning(this, QStringLiteral("Component build failed"), result.error);
        return;
    }
    apply_component_module_manifest(result.runtime_manifest_path);
    if (play_running_) stop_play();
    preview_worker_->stop_and_discard();
    game_preview_worker_->stop_and_discard();
    refresh_preview();
    append_console(QStringLiteral("Component build %1: %2 managed and %3 native type(s). Workers restarted from cache %4.")
        .arg(result.reused_cache ? QStringLiteral("reused") : QStringLiteral("completed"))
        .arg(result.managed_component_count)
        .arg(result.native_component_count)
        .arg(result.build_hash.left(12)),
        QStringLiteral("Info"), QStringLiteral("Component Build"), result.runtime_manifest_path);
}

void EditorWindow::edit_project_source(const QString& source_path)
{
    if (project_root_.isEmpty() || !project_index_.candidate)
    {
        append_console(
            QStringLiteral("Open a valid project before editing component source."),
            QStringLiteral("Warning"),
            QStringLiteral("Script Editor"));
        return;
    }

    ScriptEditorRequest request;
    request.project_root = project_root_;
    request.selected_source_path = QDir::isAbsolutePath(source_path)
        ? source_path
        : QDir{project_root_}.filePath(source_path);
    request.contracts_assembly_path = dragonpixel::editor::runtime_paths::file(
        "DPE_CONTRACTS_ASSEMBLY",
        QStringLiteral("runtime/contracts/DragonPixel.Contracts.dll"),
        QString::fromUtf8(DPE_CONTRACTS_ASSEMBLY));
    for (const auto& entry : project_index_.candidate->entries)
    {
        if (entry.kind == ProjectIndexEntryKind::component_source)
        {
            request.component_source_paths.push_back(entry.absolute_path);
        }
        else if (entry.kind == ProjectIndexEntryKind::component_manifest)
        {
            request.component_manifest_paths.push_back(entry.absolute_path);
        }
    }

    const auto result = script_editor_service_.open_in_rider(request);
    if (!result.succeeded)
    {
        append_console(
            result.error,
            QStringLiteral("Error"),
            QStringLiteral("Script Editor"),
            request.selected_source_path,
            {}, {}, {}, {}, {}, request.selected_source_path);
        statusBar()->showMessage(QStringLiteral("Could not open Rider; see Console"), 5000);
        return;
    }
    append_console(
        QStringLiteral("Opened %1 in Rider solution %2")
            .arg(QFileInfo{result.selected_source_path}.fileName(), result.solution_path),
        QStringLiteral("Info"),
        QStringLiteral("Script Editor"),
        result.workspace_directory,
        {}, {}, {}, {}, {}, result.selected_source_path);
    statusBar()->showMessage(
        QStringLiteral("Editing %1 in Rider").arg(QFileInfo{result.selected_source_path}.fileName()),
        5000);
}

void EditorWindow::rebuild_project_hub()
{
    if (project_hub_recent_ == nullptr)
    {
        return;
    }
    project_hub_recent_->clear();
    for (const auto& manifest_path : project_lifecycle_service_.recent_projects())
    {
        auto label = QFileInfo{manifest_path}.absoluteDir().dirName();
        const auto indexed = ProjectIndexService{}.build_candidate(manifest_path);
        if (indexed.candidate && !indexed.candidate->name.isEmpty())
        {
            label = indexed.candidate->name;
        }
        auto* item = new QListWidgetItem{
            QStringLiteral("%1\n%2").arg(label, QDir::toNativeSeparators(manifest_path)),
            project_hub_recent_};
        item->setData(Qt::UserRole, manifest_path);
        item->setToolTip(QDir::toNativeSeparators(manifest_path));
    }
    if (project_hub_recent_->count() == 0)
    {
        auto* empty = new QListWidgetItem{
            QStringLiteral("No recent projects yet"), project_hub_recent_};
        empty->setFlags(Qt::NoItemFlags);
    }
}

void EditorWindow::show_project_hub()
{
    if (project_hub_dock_ == nullptr)
    {
        return;
    }
    rebuild_project_hub();
    project_hub_dock_->show();
    project_hub_dock_->raise();
}

void EditorWindow::open_project_dialog()
{
    const auto path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Open Dragon Pixel project"),
        project_root_.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            : project_root_,
        QStringLiteral("Dragon Pixel Project (DragonPixelProject.json);;JSON (*.json)"));
    if (!path.isEmpty())
    {
        load_project(path);
    }
}

void EditorWindow::create_project_dialog()
{
    QVector<ProjectLifecycleDiagnostic> discovery_diagnostics;
    const auto templates_root = dragonpixel::editor::runtime_paths::directory(
        "DPE_PROJECT_TEMPLATES_ROOT",
        QStringLiteral("templates"),
        QString::fromUtf8(DPE_PROJECT_TEMPLATES_ROOT));
    const auto templates = project_lifecycle_service_.discover_templates(
        templates_root, &discovery_diagnostics);
    if (templates.isEmpty())
    {
        const auto message = discovery_diagnostics.isEmpty()
            ? QStringLiteral("No installed project templates were found.")
            : discovery_diagnostics.constFirst().message;
        QMessageBox::critical(this, QStringLiteral("New Project unavailable"), message);
        append_console(message, QStringLiteral("Error"), QStringLiteral("Project Lifecycle"));
        return;
    }

    QDialog dialog{this};
    dialog.setObjectName(QStringLiteral("NewProjectDialog"));
    dialog.setWindowTitle(QStringLiteral("New Dragon Pixel Project"));
    dialog.setAccessibleName(QStringLiteral("Create a new Dragon Pixel project"));
    dialog.resize(620, 250);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* name = new QLineEdit(QStringLiteral("New Dragon Pixel Project"), &dialog);
    name->setObjectName(QStringLiteral("NewProjectName"));
    name->setAccessibleName(QStringLiteral("New project name"));
    auto* template_combo = new QComboBox(&dialog);
    template_combo->setObjectName(QStringLiteral("NewProjectTemplate"));
    template_combo->setAccessibleName(QStringLiteral("New project template"));
    for (const auto& descriptor : templates)
    {
        template_combo->addItem(descriptor.name, descriptor.manifest_path);
    }
    auto* location_row = new QWidget(&dialog);
    auto* location_layout = new QHBoxLayout(location_row);
    location_layout->setContentsMargins(0, 0, 0, 0);
    auto* location = new QLineEdit(
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), location_row);
    location->setObjectName(QStringLiteral("NewProjectLocation"));
    location->setAccessibleName(QStringLiteral("New project parent folder"));
    auto* browse = new QPushButton(QStringLiteral("Browse..."), location_row);
    browse->setObjectName(QStringLiteral("NewProjectBrowse"));
    browse->setAccessibleName(QStringLiteral("Browse for new project parent folder"));
    location_layout->addWidget(location, 1);
    location_layout->addWidget(browse);
    connect(browse, &QPushButton::clicked, &dialog, [this, location] {
        const auto selected = QFileDialog::getExistingDirectory(
            this, QStringLiteral("Choose project parent folder"), location->text());
        if (!selected.isEmpty())
        {
            location->setText(selected);
        }
    });
    form->addRow(QStringLiteral("Name"), name);
    form->addRow(QStringLiteral("Template"), template_combo);
    form->addRow(QStringLiteral("Location"), location_row);
    layout->addLayout(form);
    auto* destination_preview = new QLabel(&dialog);
    destination_preview->setObjectName(QStringLiteral("NewProjectDestinationPreview"));
    destination_preview->setWordWrap(true);
    destination_preview->setAccessibleName(QStringLiteral("New project destination"));
    layout->addWidget(destination_preview);
    const auto refresh_destination = [name, location, destination_preview] {
        destination_preview->setText(QStringLiteral("Project folder: %1")
            .arg(QDir::toNativeSeparators(QDir{location->text()}.filePath(name->text().trimmed()))));
    };
    connect(name, &QLineEdit::textChanged, &dialog, refresh_destination);
    connect(location, &QLineEdit::textChanged, &dialog, refresh_destination);
    refresh_destination();
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->setObjectName(QStringLiteral("NewProjectButtons"));
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Create"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const auto request = ProjectCreationRequest{
        template_combo->currentData().toString(),
        name->text().trimmed(),
        QDir{location->text()}.filePath(name->text().trimmed()),
    };
    const auto dry_run = project_lifecycle_service_.dry_run(request);
    if (!dry_run.succeeded)
    {
        const auto message = dry_run.diagnostics.isEmpty()
            ? QStringLiteral("The project request was rejected.")
            : dry_run.diagnostics.constFirst().message;
        QMessageBox::warning(this, QStringLiteral("Cannot create project"), message);
        append_console(message, QStringLiteral("Warning"), QStringLiteral("Project Lifecycle"));
        return;
    }
    const auto result = project_lifecycle_service_.create_project(request);
    if (!result.succeeded)
    {
        const auto message = result.diagnostics.isEmpty()
            ? QStringLiteral("Project creation failed.")
            : result.diagnostics.constFirst().message;
        QMessageBox::critical(this, QStringLiteral("Project creation failed"), message);
        append_console(message, QStringLiteral("Error"), QStringLiteral("Project Lifecycle"),
            result.staging_path, {}, {}, result.operation_id);
        return;
    }
    append_console(
        QStringLiteral("Created project %1 from %2")
            .arg(result.project_manifest_path, template_combo->currentText()),
        QStringLiteral("Info"), QStringLiteral("Project Lifecycle"),
        result.project_manifest_path, {}, {}, result.operation_id);
    load_project(result.project_manifest_path);
}

void EditorWindow::create_clean_scene_dialog()
{
    if (!scene_ || project_manifest_path_.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("New Scene"),
            QStringLiteral("Open or create a project before creating a scene."));
        return;
    }
    QDialog dialog{this};
    dialog.setObjectName(QStringLiteral("NewSceneDialog"));
    dialog.setWindowTitle(QStringLiteral("New Clean Scene"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* name = new QLineEdit(QStringLiteral("New Scene"), &dialog);
    name->setObjectName(QStringLiteral("NewSceneName"));
    name->setAccessibleName(QStringLiteral("New scene name"));
    auto* kind = new QComboBox(&dialog);
    kind->setObjectName(QStringLiteral("NewSceneKind"));
    kind->setAccessibleName(QStringLiteral("New scene dimension"));
    kind->addItem(QStringLiteral("2D (orthographic camera)"), QStringLiteral("2d"));
    kind->addItem(QStringLiteral("3D (camera and directional light)"), QStringLiteral("3d"));
    auto* path = new QLineEdit(QStringLiteral("Scenes/NewScene.dpescene"), &dialog);
    path->setObjectName(QStringLiteral("NewScenePath"));
    path->setAccessibleName(QStringLiteral("New scene project-relative path"));
    form->addRow(QStringLiteral("Name"), name);
    form->addRow(QStringLiteral("Type"), kind);
    form->addRow(QStringLiteral("Path"), path);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Create"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }
    if (!confirm_discard_or_save())
    {
        return;
    }
    if (scene_->is_dirty())
    {
        // Discard was explicitly chosen. The next scene replaces this in-memory
        // state, so prevent the load path from prompting a second time.
        scene_->mark_savepoint();
    }
    const auto request = CleanSceneRequest{
        project_manifest_path_,
        name->text().trimmed(),
        QDir::fromNativeSeparators(path->text().trimmed()),
        kind->currentData().toString() == QStringLiteral("3d")
            ? ProjectTemplateKind::three_d
            : ProjectTemplateKind::two_d,
    };
    const auto result = project_lifecycle_service_.create_clean_scene(request);
    if (!result.succeeded)
    {
        const auto message = result.diagnostics.isEmpty()
            ? QStringLiteral("Scene creation failed.")
            : result.diagnostics.constFirst().message;
        QMessageBox::warning(this, QStringLiteral("Cannot create scene"), message);
        append_console(message, QStringLiteral("Warning"), QStringLiteral("Project Lifecycle"),
            result.staging_path, {}, {}, result.operation_id);
        return;
    }
    rebuild_assets();
    load_scene(result.scene_path);
}

bool EditorWindow::save_scene_as()
{
    if (!scene_ || project_manifest_path_.isEmpty())
    {
        return false;
    }
    const auto default_path = QDir{QFileInfo{scene_path_}.absolutePath()}
                                  .filePath(QStringLiteral("%1 Copy.dpescene")
                                      .arg(QFileInfo{scene_path_}.completeBaseName()));
    const auto path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save Dragon Pixel scene as"), default_path,
        QStringLiteral("Dragon Pixel Scene (*.dpescene)"));
    if (path.isEmpty())
    {
        return false;
    }
    const auto destination = QFileInfo{path}.absoluteFilePath();
    const auto indexed = ProjectIndexService{}.build_candidate(project_manifest_path_);
    bool beneath_scene_root = false;
    if (indexed.candidate)
    {
        for (const auto& root : indexed.candidate->roots)
        {
            if (root.kind != ProjectIndexRootKind::scenes)
            {
                continue;
            }
            const auto prefix = QDir::fromNativeSeparators(root.absolute_path) + QLatin1Char{'/'};
            if (QDir::fromNativeSeparators(destination).startsWith(prefix, Qt::CaseInsensitive))
            {
                beneath_scene_root = true;
                break;
            }
        }
    }
    if (!beneath_scene_root)
    {
        QMessageBox::warning(this, QStringLiteral("Save Scene As"),
            QStringLiteral("Scenes must be saved beneath a declared project scene root."));
        return false;
    }
    if (tile_document_service_->is_dirty() && !tile_document_service_->save())
    {
        return false;
    }
    prefab_service_.synchronize(*scene_);
    auto document = nlohmann::ordered_json::parse(
        dragonpixel::serialization::write_scene_json(prefab_service_.persistent_copy(*scene_)));
    document["sceneId"] = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower().toStdString();
    document["name"] = QFileInfo{destination}.completeBaseName().toStdString();
    const auto bytes = document.dump(2) + "\n";
    const auto saved = dragonpixel::serialization::save_utf8_atomic(
        filesystem_path(destination), bytes);
    if (!saved.succeeded)
    {
        QMessageBox::critical(this, QStringLiteral("Save Scene As failed"),
            QString::fromStdString(saved.error));
        return false;
    }
    auto loaded = dragonpixel::serialization::read_scene_json(bytes, metadata_);
    if (!loaded.value)
    {
        QMessageBox::critical(this, QStringLiteral("Save Scene As failed"),
            QStringLiteral("The newly written scene did not pass scene validation."));
        return false;
    }
    scene_->mark_savepoint();
    if (!commit_scene(std::move(*loaded.value), destination,
            project_manifest_path_, project_root_, loaded.migrations.size()))
    {
        return false;
    }
    rebuild_assets();
    return true;
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
    if (game_preview_worker_ != nullptr)
    {
        game_preview_worker_->stop_and_discard();
    }
    gizmo_preview_active_ = false;
    gizmo_preview_scene_.reset();
    gizmo_transform_snapshots_.clear();
    pinned_tile_scene_target_.reset();
    clear_inspector_locks();
    selection_service_.clear(SelectionOrigin::project_lifecycle);
    scene_.reset();
    scene_path_.clear();
    project_manifest_path_.clear();
    project_root_.clear();
    apply_component_module_manifest({});
    prefab_service_.clear();
    tile_document_service_->clear();
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
    refresh_additional_inspectors();
    update_window_title();
    update_action_states();
    append_console(QStringLiteral("Project closed; no authoritative scene remains loaded"));
    show_project_hub();
    return true;
}

bool EditorWindow::load_project(const QString& path)
{
    const ProjectIndexService index_service;
    const auto location = index_service.validate_location(path);
    for (const auto& diagnostic : location.diagnostics)
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
    if (!location.succeeded())
    {
        append_console(
            QStringLiteral("Project location validation failed before recovery; the current project/session was preserved."),
            QStringLiteral("Error"),
            QStringLiteral("Project Index"));
        return false;
    }
    auto canonical_manifest = location.location->manifest_path;
    auto canonical_root = location.location->project_root;
    const auto recovered = dragonpixel::serialization::recover_utf8_transactions(
        filesystem_path(canonical_root));
    if (!recovered.succeeded)
    {
        append_console(
            QStringLiteral("Project document recovery failed; the current project/session was preserved: %1")
                .arg(QString::fromStdString(recovered.error)),
            QStringLiteral("Error"),
            QStringLiteral("Documents"));
        return false;
    }

    auto indexed = index_service.build_candidate(canonical_manifest);
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
    canonical_manifest = indexed.candidate->manifest_path;
    canonical_root = indexed.candidate->project_root;
    const QFileInfo manifest_info{canonical_manifest};
    const auto root = indexed.candidate->manifest;
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
        || (format_version < 1 || format_version > 4)
        || root.value(QStringLiteral("engineVersion")).toString().isEmpty()
        || !project_id || startup_scene.isEmpty() || QDir::isAbsolutePath(startup_scene)
        || (format_version >= 2 && scene_roots.isEmpty())
        || (format_version >= 3 && !root.value(QStringLiteral("componentRoots")).isArray()))
    {
        append_console(QStringLiteral("Project manifest format, identity, or startup scene was invalid"));
        return false;
    }

    const auto canonical_scene = indexed.candidate->startup_scene_path;
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

    auto candidate_metadata = dragonpixel::metadata::registry::slice_one_defaults();
    QStringList component_roots;
    for (const auto& value : root.value(QStringLiteral("componentRoots")).toArray())
    {
        component_roots.push_back(value.toString());
    }
    const auto metadata_result = MetadataManifestService{}.load_project(
        canonical_root, component_roots, candidate_metadata);
    for (const auto& diagnostic : metadata_result.diagnostics)
    {
        append_console(diagnostic, metadata_result.succeeded ? QStringLiteral("Warning") : QStringLiteral("Error"),
            QStringLiteral("Metadata"));
    }
    if (!metadata_result.succeeded)
    {
        append_console(QStringLiteral("Project component metadata was rejected; current project remains open."),
            QStringLiteral("Error"), QStringLiteral("Metadata"));
        return false;
    }

    const auto runtime_modules = ComponentModuleService::active_runtime_manifest(
        canonical_root, component_roots, dragonpixel::editor::runtime_paths::file(
            "DPE_CONTRACTS_ASSEMBLY",
            QStringLiteral("runtime/contracts/DragonPixel.Contracts.dll"),
            QString::fromUtf8(DPE_CONTRACTS_ASSEMBLY)));

    std::ifstream stream{filesystem_path(canonical_scene), std::ios::binary};
    if (!stream)
    {
        append_console(QStringLiteral("Could not open project startup scene: %1").arg(canonical_scene));
        return false;
    }
    const std::string json{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    auto loaded = dragonpixel::serialization::read_scene_json(json, candidate_metadata);
    if (!loaded.value)
    {
        const auto message = loaded.diagnostics.empty()
            ? QStringLiteral("Unknown load error")
            : QString::fromStdString(loaded.diagnostics.front().message);
        append_console(QStringLiteral("Project startup scene failed validation: %1").arg(message), QStringLiteral("Error"));
        return false;
    }
    PrefabService candidate_prefabs;
    candidate_prefabs.set_metadata(&candidate_metadata);
    candidate_prefabs.set_project_root(canonical_root);
    auto hydration = candidate_prefabs.hydrate(*loaded.value);
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
    preview_worker_->stop_and_discard();
    game_preview_worker_->stop_and_discard();
    gizmo_preview_active_ = false;
    gizmo_preview_scene_.reset();
    gizmo_transform_snapshots_.clear();
    clear_inspector_locks();
    tile_document_service_->clear();

    metadata_ = std::move(candidate_metadata);
    candidate_prefabs.set_metadata(&metadata_);
    prefab_service_ = std::move(candidate_prefabs);
    scene_ = std::move(*hydration.scene);
    scene_->mark_savepoint();
    scene_path_ = QDir::toNativeSeparators(QFileInfo(canonical_scene).absoluteFilePath());
    project_manifest_path_ = QDir::toNativeSeparators(manifest_info.absoluteFilePath());
    project_root_ = canonical_root;
    apply_component_module_manifest(runtime_modules);
    apply_project_index(std::move(indexed));
    rebuild_hierarchy();
    publish_global_selection(SelectionOrigin::project_lifecycle);
    inspect_selected_entities();
    update_global_selection_presentation();
    refresh_additional_inspectors();
    rebuild_scene_summary();
    update_window_title();
    update_action_states();
    append_console(QStringLiteral("Loaded %1 (%2 entities, %3 migration records)")
        .arg(scene_path_)
        .arg(scene_->entities().size())
        .arg(loaded.migrations.size()));
    for (const auto& diagnostic : hydration.diagnostics)
    {
        append_console(diagnostic, QStringLiteral("Warning"), QStringLiteral("Prefabs"));
    }
    refresh_preview();
    append_console(QStringLiteral("Loaded %1 project component manifest(s), %2 component(s), and %3 object type(s)")
        .arg(metadata_result.manifest_count).arg(metadata_result.component_count).arg(metadata_result.object_type_count),
        QStringLiteral("Info"), QStringLiteral("Metadata"));
    if (scene_)
    {
        append_console(QStringLiteral("Opened project %1 (%2)")
            .arg(root.value(QStringLiteral("name")).toString(),
                 QString::fromStdString(project_id->to_string())));
        project_lifecycle_service_.record_recent_project(project_manifest_path_);
        if (project_hub_dock_ != nullptr)
        {
            project_hub_dock_->hide();
        }
        scene_view_dock_->show();
        scene_view_dock_->raise();
    }
    return scene_.has_value();
}

bool EditorWindow::load_scene(const QString& path)
{
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
#if defined(Q_OS_WIN)
        constexpr auto scene_path_case = Qt::CaseInsensitive;
#else
        constexpr auto scene_path_case = Qt::CaseSensitive;
#endif
        if (candidate_path.startsWith(root_prefix, scene_path_case))
        {
            manifest = project_manifest_path_;
            root = project_root_;
        }
    }
    const auto recovered = dragonpixel::serialization::recover_utf8_transactions(
        filesystem_path(root));
    if (!recovered.succeeded)
    {
        append_console(
            QStringLiteral("Scene document recovery failed; the current scene was preserved: %1")
                .arg(QString::fromStdString(recovered.error)),
            QStringLiteral("Error"),
            QStringLiteral("Documents"));
        return false;
    }

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
    clear_inspector_locks();
    prefab_service_ = std::move(candidate_prefabs);
    scene_ = std::move(*hydration.scene);
    scene_->mark_savepoint();
    scene_path_ = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    project_manifest_path_ = project_manifest;
    project_root_ = project_root;
    rebuild_hierarchy();
    publish_global_selection(SelectionOrigin::project_lifecycle);
    inspect_selected_entities();
    update_global_selection_presentation();
    refresh_additional_inspectors();
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
    std::vector<dragonpixel::serialization::utf8_transaction_write> writes{
        {filesystem_path(scene_path_), json},
    };
    std::optional<std::string> tile_json;
    std::optional<std::string> palette_json;
    std::optional<std::vector<TileDocumentService::PreparedTileSetSave>> tileset_json;
    if (tile_document_service_->is_dirty())
    {
        tile_json = tile_document_service_->prepare_save();
        if (!tile_json)
        {
            QMessageBox::critical(this, QStringLiteral("Save failed"), tile_document_service_->error());
            return false;
        }
        writes.push_back({filesystem_path(tile_document_service_->tilemap_path()), *tile_json});
        if (tile_document_service_->is_palette_dirty())
        {
            palette_json = tile_document_service_->prepare_palette_save();
            if (!palette_json)
            {
                QMessageBox::critical(this, QStringLiteral("Save failed"), tile_document_service_->error());
                return false;
            }
            writes.push_back({filesystem_path(tile_document_service_->palette_path()), *palette_json});
        }
        tileset_json = tile_document_service_->prepare_tileset_saves();
        if (!tileset_json)
        {
            QMessageBox::critical(this, QStringLiteral("Save failed"), tile_document_service_->error());
            return false;
        }
        for (const auto& save : *tileset_json)
            writes.push_back({filesystem_path(save.path), save.encoded});
    }
    const auto result = dragonpixel::serialization::save_utf8_transaction(
        writes, filesystem_path(project_root_), save_fault_for_test_);
    save_fault_for_test_ = dragonpixel::serialization::transaction_save_fault::none;
    if (result.succeeded)
    {
        if (tile_json)
        {
            tile_document_service_->accept_save(*tile_json);
        }
        if (palette_json) tile_document_service_->accept_palette_save(*palette_json);
        if (tileset_json) tile_document_service_->accept_tileset_saves(*tileset_json);
        append_console(QStringLiteral("Transactionally saved scene: %1").arg(scene_path_));
        statusBar()->showMessage(QStringLiteral("Scene and dirty tile documents saved"), 3000);
        scene_->mark_savepoint();
        update_window_title();
        update_action_states();
        return true;
    }
    append_console(QStringLiteral("Save transaction failed without advancing any document: %1")
        .arg(QString::fromStdString(result.error)), QStringLiteral("Error"), QStringLiteral("Documents"));
    const auto detail = QString::fromStdString(result.error);
    QMessageBox::critical(this, QStringLiteral("Save failed"), save_failure_dialog_text(detail));
    return false;
}

bool EditorWindow::confirm_discard_or_save()
{
    const auto scene_dirty = scene_ && scene_->is_dirty();
    const auto tile_dirty = tile_document_service_ && tile_document_service_->is_dirty();
    if (!scene_dirty && !tile_dirty)
    {
        return true;
    }
    const auto decision = unsaved_prompt_
        ? unsaved_prompt_(scene_dirty && tile_dirty
            ? QStringLiteral("%1 and %2").arg(QString::fromStdString(scene_->name()),
                QFileInfo{tile_document_service_->tilemap_path()}.fileName())
            : scene_dirty ? QString::fromStdString(scene_->name())
                : QFileInfo{tile_document_service_->tilemap_path()}.fileName())
        : UnsavedDecision::cancel;
    if (decision == UnsavedDecision::save)
    {
        return scene_dirty ? save_scene() : tile_document_service_->save();
    }
    return decision == UnsavedDecision::discard;
}

void EditorWindow::rebuild_hierarchy()
{
    const auto selected = selected_entity_ids();
    hierarchy_model_->set_drag_context(
        project_index_.candidate ? project_index_.candidate->project_id : QString{},
        scene_ ? QString::fromStdString(scene_->id().to_string()) : QString{},
        command_revision_,
        project_model_ ? project_model_->drag_revision() : 0);
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

void EditorWindow::update_project_browser_folder(const QModelIndex& folder_index)
{
    if (!folder_index.isValid() || project_folder_filter_ == nullptr)
    {
        return;
    }
    const auto source = project_folder_filter_->mapToSource(folder_index.siblingAtColumn(0));
    if (!source.isValid())
    {
        return;
    }
    project_current_folder_ = QPersistentModelIndex{source};
    const auto content_root = project_filter_->mapFromSource(source);
    project_explorer_->setRootIndex(content_root);
    project_thumbnail_view_->setRootIndex(content_root);
    auto logical = source.data(EditorRoles::project_logical_path).toString();
    if (logical.isEmpty()) logical = QStringLiteral("Project");
    project_breadcrumb_->setText(QStringLiteral("Project / %1")
        .arg(logical == QStringLiteral("Project") ? QString{} : logical));
}

void EditorWindow::update_project_details(const QModelIndex& proxy_index)
{
    if (!proxy_index.isValid())
    {
        project_details_->setText(QStringLiteral("Select an asset to see details."));
        return;
    }
    const auto index = proxy_index.siblingAtColumn(0);
    QStringList lines;
    lines.push_back(index.data().toString());
    lines.push_back(QStringLiteral("Kind: %1 | Status: %2")
        .arg(proxy_index.siblingAtColumn(static_cast<int>(ProjectColumn::kind_type)).data().toString(),
            proxy_index.siblingAtColumn(static_cast<int>(ProjectColumn::overall_status)).data().toString()));
    const auto path = index.data(EditorRoles::project_path).toString();
    if (!path.isEmpty())
    {
        lines.push_back(QStringLiteral("Path: %1").arg(QDir::toNativeSeparators(path)));
    }
    const auto id = index.data(EditorRoles::project_entry_id).toString();
    const auto* entry = project_index_.candidate && !id.isEmpty()
        ? project_index_.candidate->find_by_id(id) : nullptr;
    if (entry == nullptr)
    {
        project_details_->setText(lines.join(QLatin1Char('\n')));
        return;
    }

    lines.push_back(QStringLiteral("ID: %1 | Format: v%2")
        .arg(entry->id).arg(entry->format_version));
    lines.push_back(QStringLiteral("Import: %1 | Dependencies: %2 | Structure: %3")
        .arg(index.data(EditorRoles::project_import_status).toString(),
            index.data(EditorRoles::project_dependency_status).toString(),
            entry->structurally_valid ? QStringLiteral("Valid") : QStringLiteral("Invalid")));
    if (!entry->dependencies.isEmpty())
    {
        QStringList dependencies;
        for (const auto& dependency_id : entry->dependencies)
        {
            const auto* dependency = project_index_.candidate->find_by_id(dependency_id);
            dependencies.push_back(dependency == nullptr
                ? dependency_id
                : QStringLiteral("%1 (%2)").arg(dependency->display_name, dependency_id));
        }
        lines.push_back(QStringLiteral("Uses: %1").arg(dependencies.join(QStringLiteral(", "))));
    }

    if (entry->asset_type.contains(QStringLiteral("tilemap"), Qt::CaseInsensitive))
    {
        std::optional<dragonpixel::tiles::tilemap_document> document;
        if (tile_document_service_->tilemap() != nullptr
            && QFileInfo{tile_document_service_->tilemap_path()}.absoluteFilePath()
                == QFileInfo{entry->resolved_source_path}.absoluteFilePath())
        {
            document = *tile_document_service_->tilemap();
        }
        else
        {
            QFile file{entry->resolved_source_path};
            if (file.size() <= 64 * 1024 * 1024 && file.open(QIODevice::ReadOnly))
            {
                document = dragonpixel::tiles::read_tilemap(file.readAll().toStdString()).document;
            }
        }
        if (document)
        {
            std::size_t occupied_cells{};
            std::size_t visible_layers{};
            for (const auto& layer : document->layers)
            {
                if (layer.visible) ++visible_layers;
                for (const auto& chunk : layer.chunks) occupied_cells += chunk.cells.size();
            }
            lines.push_back(QStringLiteral("Layers: %1 (%2 visible) | Occupied cells: %3")
                .arg(document->layers.size()).arg(visible_layers).arg(occupied_cells));
        }
    }
    else if (entry->asset_type.contains(QStringLiteral("tileset"), Qt::CaseInsensitive))
    {
        QFile file{entry->resolved_source_path};
        if (file.size() <= 64 * 1024 * 1024 && file.open(QIODevice::ReadOnly))
        {
            const auto result = dragonpixel::tiles::read_tile_set(file.readAll().toStdString());
            if (result.document)
            {
                const auto& set = *result.document;
                lines.push_back(QStringLiteral("Cell size: %1 x %2 px | Pixels per unit: %3 | Tiles: %4")
                    .arg(set.cell_size.x).arg(set.cell_size.y)
                    .arg(set.pixels_per_unit).arg(set.tiles.size()));
            }
        }
    }
    project_details_->setText(lines.join(QLatin1Char('\n')));
}

QString EditorWindow::current_project_folder_relative() const
{
    if (project_current_folder_.isValid())
    {
        const auto logical = project_current_folder_.data(
            EditorRoles::project_logical_path).toString();
        if (!logical.isEmpty()) return QDir::fromNativeSeparators(logical);
    }
    if (project_index_.candidate)
    {
        for (const auto& root : project_index_.candidate->roots)
        {
            if (root.kind == ProjectIndexRootKind::assets)
                return QDir::fromNativeSeparators(root.declared_path);
        }
    }
    return QStringLiteral("Assets");
}

void EditorWindow::import_asset_paths(const QStringList& paths)
{
    if (project_manifest_path_.isEmpty() || paths.isEmpty())
    {
        return;
    }
    QDialog dialog{this};
    dialog.setObjectName(QStringLiteral("AssetImportSummaryDialog"));
    dialog.setWindowTitle(QStringLiteral("Import Assets"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* summary = new QLabel(
        QStringLiteral("Import %1 file(s) into Project / %2.")
            .arg(paths.size()).arg(current_project_folder_relative()), &dialog);
    summary->setWordWrap(true);
    layout->addWidget(summary);
    auto* ownership = new QComboBox(&dialog);
    ownership->setObjectName(QStringLiteral("AssetImportOwnership"));
    ownership->setAccessibleName(QStringLiteral("Asset import ownership"));
    ownership->addItem(QStringLiteral("Copy into project (recommended)"), QStringLiteral("copy"));
    ownership->addItem(QStringLiteral("Link external source (read-only)"), QStringLiteral("link"));
    layout->addWidget(ownership);
    auto* note = new QLabel(
        QStringLiteral("PNG and JPEG files become sprite assets. Other files are retained with a no-compatible-importer diagnostic."),
        &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Import"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }
    const auto result = asset_service_.import_files({
        project_manifest_path_,
        paths,
        current_project_folder_relative(),
        ownership->currentData().toString() == QStringLiteral("link")
            ? AssetImportOwnership::link_external_read_only
            : AssetImportOwnership::copy_into_project,
    });
    if (!result.succeeded)
    {
        const auto message = result.diagnostics.isEmpty()
            ? QStringLiteral("Asset import failed.")
            : result.diagnostics.constFirst().message;
        QMessageBox::warning(this, QStringLiteral("Asset import failed"), message);
        append_console(message, QStringLiteral("Warning"), QStringLiteral("Asset Service"),
            {}, {}, {}, result.operation_id);
        return;
    }
    append_console(QStringLiteral("Imported %1 asset(s) into %2")
        .arg(result.asset_ids.size()).arg(current_project_folder_relative()),
        QStringLiteral("Info"), QStringLiteral("Asset Service"),
        {}, {}, {}, result.operation_id);
    rebuild_assets();
}

bool EditorWindow::handle_project_browser_drop(
    const QMimeData* mime,
    const QModelIndex& destination_source)
{
    if (mime == nullptr || !scene_ || !project_index_.candidate) return false;
    auto destination = destination_source;
    if (destination.isValid() && ProjectModel::item_kind(destination) != ProjectItemKind::folder)
        destination = destination.parent();
    auto destination_folder = destination.isValid()
        ? destination.data(EditorRoles::project_logical_path).toString()
        : current_project_folder_relative();
    if (destination_folder.isEmpty()) destination_folder = QStringLiteral("Assets");
    destination_folder = QDir::fromNativeSeparators(destination_folder);
    const auto project_id = project_index_.candidate->project_id;

    if (mime->hasFormat(QStringLiteral("application/x-dragonpixel-entity")))
    {
        const auto payload = QJsonDocument::fromJson(mime->data(
            QStringLiteral("application/x-dragonpixel-entity"))).object();
        const auto items = payload.value(QStringLiteral("items")).toArray();
        const auto current_scene = QString::fromStdString(scene_->id().to_string());
        if (payload.value(QStringLiteral("format")).toString() != QStringLiteral("dpe.drag")
            || payload.value(QStringLiteral("formatVersion")).toInt() != 1
            || payload.value(QStringLiteral("projectId")).toString() != project_id
            || payload.value(QStringLiteral("sceneId")).toString() != current_scene
            || payload.value(QStringLiteral("sourceRevision")).toInteger() != static_cast<qint64>(command_revision_)
            || items.size() != 1 || !items.at(0).isObject())
        {
            append_console(QStringLiteral("Hierarchy-to-Project drop rejected: select one current, locally owned root from this project."),
                QStringLiteral("Warning"), QStringLiteral("Drag and Drop"));
            return false;
        }
        const auto id = dragonpixel::core::uuid::parse(
            items.at(0).toObject().value(QStringLiteral("id")).toString().toStdString());
        const auto* entity = id ? scene_->find_entity(*id) : nullptr;
        if (!id || entity == nullptr || prefab_service_.has_instance_for_entity(*id))
        {
            append_console(QStringLiteral("Prefab drop rejected because the source is missing, linked, or not locally owned."),
                QStringLiteral("Warning"), QStringLiteral("Prefabs"));
            return false;
        }
        auto safe_name = QString::fromStdString(entity->name);
        safe_name.replace(QRegularExpression{QStringLiteral("[^A-Za-z0-9_-]+")}, QStringLiteral("_"));
        const auto path = QDir{project_root_}.filePath(
            QDir{destination_folder}.filePath(safe_name + QStringLiteral(".dpeprefab")));
        if (QFileInfo::exists(path))
        {
            append_console(QStringLiteral("Prefab drop rejected because %1 already exists.").arg(path),
                QStringLiteral("Warning"), QStringLiteral("Prefabs"));
            return false;
        }
        report_prefab_result(prefab_service_.create_from_selection(*scene_, *id, path));
        return true;
    }

    if (!mime->hasFormat(QStringLiteral("application/x-dragonpixel-project-item"))) return false;
    const auto payload = QJsonDocument::fromJson(mime->data(
        QStringLiteral("application/x-dragonpixel-project-item"))).object();
    const auto items = payload.value(QStringLiteral("items")).toArray();
    if (payload.value(QStringLiteral("format")).toString() != QStringLiteral("dpe.drag")
        || payload.value(QStringLiteral("formatVersion")).toInt() != 1
        || payload.value(QStringLiteral("projectId")).toString() != project_id
        || payload.value(QStringLiteral("sourceRevision")).toInteger()
            != static_cast<qint64>(project_model_->drag_revision())
        || items.size() != 1 || !items.at(0).isObject())
    {
        append_console(QStringLiteral("Project move rejected because the drag is stale, cross-project, or ambiguous."),
            QStringLiteral("Warning"), QStringLiteral("Drag and Drop"));
        return false;
    }
    const auto item = items.at(0).toObject();
    if (item.value(QStringLiteral("kind")).toString() == QStringLiteral("folder"))
    {
        const auto absolute_source = item.value(QStringLiteral("path")).toString();
        const auto source_folder = QDir::fromNativeSeparators(
            QDir{project_root_}.relativeFilePath(absolute_source));
        const auto result = asset_service_.move_folder(
            project_manifest_path_, source_folder, destination_folder);
        for (const auto& diagnostic : result.diagnostics)
            append_console(diagnostic.message,
                result.succeeded ? QStringLiteral("Info") : QStringLiteral("Warning"),
                QStringLiteral("Asset Service"), diagnostic.path, {}, {}, result.operation_id);
        if (!result.succeeded) return false;
        append_console(QStringLiteral("Moved Project folder to %1 in one atomic operation.")
            .arg(destination_folder), QStringLiteral("Info"), QStringLiteral("Asset Service"),
            {}, {}, {}, result.operation_id);
        rebuild_assets();
        return true;
    }
    if (item.value(QStringLiteral("kind")).toString() != QStringLiteral("asset")
        || item.value(QStringLiteral("assetId")).toString().isEmpty())
    {
        append_console(QStringLiteral("Only Project folders and stable asset records can be moved by this Project drop path."),
            QStringLiteral("Warning"), QStringLiteral("Asset Service"));
        return false;
    }
    const auto result = asset_service_.move_asset(
        project_manifest_path_, item.value(QStringLiteral("assetId")).toString(), destination_folder);
    for (const auto& diagnostic : result.diagnostics)
        append_console(diagnostic.message, result.succeeded ? QStringLiteral("Info") : QStringLiteral("Warning"),
            QStringLiteral("Asset Service"), diagnostic.path, {}, {}, result.operation_id);
    if (!result.succeeded) return false;
    append_console(QStringLiteral("Moved asset to Project / %1 while preserving its stable asset ID.")
        .arg(destination_folder), QStringLiteral("Info"), QStringLiteral("Asset Service"),
        {}, {}, {}, result.operation_id);
    rebuild_assets();
    return true;
}

void EditorWindow::show_project_browser_context_menu(const QPoint& point)
{
    if (project_manifest_path_.isEmpty())
    {
        return;
    }
    auto* active_view = project_content_stack_->currentIndex() == 0
        ? static_cast<QAbstractItemView*>(project_explorer_)
        : static_cast<QAbstractItemView*>(project_thumbnail_view_);
    const auto current = active_view->currentIndex();
    const auto kind = current.isValid()
        ? ProjectModel::item_kind(current) : ProjectItemKind::project;
    const auto asset_id = current.isValid() ? ProjectModel::asset_id(current) : QString{};
    const auto path = current.isValid() ? ProjectModel::item_path(current) : project_root_;

    QMenu menu{active_view};
    auto* create_folder = menu.addAction(QStringLiteral("Create Folder..."));
    auto* new_scene = menu.addAction(QStringLiteral("New Scene..."));
    auto* import = menu.addAction(QStringLiteral("Import..."));
    auto* create_tilemap = menu.addAction(QStringLiteral("Create Tilemap from TileSet..."));
    create_tilemap->setObjectName(QStringLiteral("ProjectCreateTilemapFromTileSet"));
    menu.addSeparator();
    auto* open_edit = menu.addAction(QStringLiteral("Open / Edit"));
    auto* rename = menu.addAction(QStringLiteral("Rename..."));
    rename->setShortcut(QKeySequence{Qt::Key_F2});
    auto* move = menu.addAction(QStringLiteral("Move..."));
    auto* duplicate = menu.addAction(QStringLiteral("Duplicate"));
    auto* remove = menu.addAction(QStringLiteral("Remove to Project Trash..."));
    remove->setShortcut(QKeySequence::Delete);
    menu.addSeparator();
    auto* restore = menu.addAction(QStringLiteral("Restore from Project Trash..."));
    auto* reveal = menu.addAction(QStringLiteral("Reveal in File Browser"));
    auto* refresh = menu.addAction(QStringLiteral("Refresh"));

    const auto asset_selected = kind == ProjectItemKind::asset && !asset_id.isEmpty();
    const auto tileset_selected = asset_selected
        && ProjectModel::asset_type(current).contains(
            QStringLiteral("tileset"), Qt::CaseInsensitive);
    create_tilemap->setEnabled(tileset_selected);
    open_edit->setEnabled(current.isValid());
    rename->setEnabled(asset_selected);
    move->setEnabled(asset_selected);
    duplicate->setEnabled(asset_selected);
    remove->setEnabled(asset_selected);
    reveal->setEnabled(!path.isEmpty());

    const auto chosen = menu.exec(active_view->viewport()->mapToGlobal(point));
    if (chosen == nullptr) return;
    if (chosen == create_folder)
    {
        bool accepted = false;
        const auto name = QInputDialog::getText(this, QStringLiteral("Create Folder"),
            QStringLiteral("Folder name"), QLineEdit::Normal, QStringLiteral("New Folder"), &accepted).trimmed();
        if (!accepted || name.isEmpty()) return;
        const auto relative = QDir::fromNativeSeparators(
            QDir{current_project_folder_relative()}.filePath(name));
        const auto result = asset_service_.create_folder(project_manifest_path_, relative);
        if (!result.succeeded)
        {
            QMessageBox::warning(this, QStringLiteral("Create Folder"),
                result.diagnostics.isEmpty() ? QStringLiteral("Folder creation failed.")
                                             : result.diagnostics.constFirst().message);
        }
        else rebuild_assets();
    }
    else if (chosen == new_scene)
    {
        create_clean_scene_dialog();
    }
    else if (chosen == import)
    {
        const auto files = QFileDialog::getOpenFileNames(this, QStringLiteral("Import Assets"),
            QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
            QStringLiteral("All Files (*.*)"));
        import_asset_paths(files);
    }
    else if (chosen == create_tilemap)
    {
        const auto suggested = QStringLiteral("%1 Map")
            .arg(current.siblingAtColumn(0).data().toString());
        static_cast<void>(prompt_create_tilemap_from_tileset(asset_id, suggested));
    }
    else if (chosen == open_edit)
    {
        activate_project_item(current);
    }
    else if (chosen == rename)
    {
        bool accepted = false;
        auto suggested = QFileInfo{path}.completeBaseName();
        if (suggested.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)
            || suggested.endsWith(QStringLiteral(".jpg"), Qt::CaseInsensitive)
            || suggested.endsWith(QStringLiteral(".jpeg"), Qt::CaseInsensitive))
            suggested = QFileInfo{suggested}.completeBaseName();
        const auto name = QInputDialog::getText(this, QStringLiteral("Rename Asset"),
            QStringLiteral("New name"), QLineEdit::Normal, suggested, &accepted);
        if (!accepted) return;
        const auto result = asset_service_.rename_asset(project_manifest_path_, asset_id, name);
        if (!result.succeeded)
            QMessageBox::warning(this, QStringLiteral("Rename Asset"),
                result.diagnostics.isEmpty() ? QStringLiteral("Rename failed.") : result.diagnostics.constFirst().message);
        else rebuild_assets();
    }
    else if (chosen == move)
    {
        bool accepted = false;
        const auto folder = QInputDialog::getText(this, QStringLiteral("Move Asset"),
            QStringLiteral("Project-relative asset folder"), QLineEdit::Normal,
            current_project_folder_relative(), &accepted);
        if (!accepted) return;
        const auto result = asset_service_.move_asset(project_manifest_path_, asset_id,
            QDir::fromNativeSeparators(folder));
        if (!result.succeeded)
            QMessageBox::warning(this, QStringLiteral("Move Asset"),
                result.diagnostics.isEmpty() ? QStringLiteral("Move failed.") : result.diagnostics.constFirst().message);
        else rebuild_assets();
    }
    else if (chosen == duplicate)
    {
        const auto result = asset_service_.duplicate_asset(project_manifest_path_, asset_id,
            current_project_folder_relative());
        if (!result.succeeded)
            QMessageBox::warning(this, QStringLiteral("Duplicate Asset"),
                result.diagnostics.isEmpty() ? QStringLiteral("Duplication failed.") : result.diagnostics.constFirst().message);
        else rebuild_assets();
    }
    else if (chosen == remove)
    {
        const auto impact = asset_service_.dependency_impact(project_manifest_path_, asset_id);
        const auto message = impact.dependent_paths.isEmpty()
            ? QStringLiteral("Move this asset into recoverable Project Trash?")
            : QStringLiteral("Move this asset into recoverable Project Trash? %1 indexed document(s) reference it and will report missing dependencies until restored.")
                  .arg(impact.dependent_paths.size());
        if (QMessageBox::question(this, QStringLiteral("Remove Asset"), message,
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
            return;
        const auto result = asset_service_.trash_asset(project_manifest_path_, asset_id);
        if (!result.succeeded)
            QMessageBox::warning(this, QStringLiteral("Remove Asset"),
                result.diagnostics.isEmpty() ? QStringLiteral("Removal failed.") : result.diagnostics.constFirst().message);
        else rebuild_assets();
    }
    else if (chosen == restore)
    {
        const QDir trash{QDir{project_root_}.filePath(QStringLiteral(".dragonpixel/Trash"))};
        const auto operations = trash.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
        bool accepted = false;
        const auto operation = QInputDialog::getItem(this, QStringLiteral("Restore Project Trash"),
            QStringLiteral("Operation"), operations, 0, false, &accepted);
        if (!accepted || operation.isEmpty()) return;
        const auto result = asset_service_.restore_trash(project_manifest_path_, operation);
        if (!result.succeeded)
            QMessageBox::warning(this, QStringLiteral("Restore Project Trash"),
                result.diagnostics.isEmpty() ? QStringLiteral("Restore failed.") : result.diagnostics.constFirst().message);
        else rebuild_assets();
    }
    else if (chosen == reveal)
    {
        const auto reveal_path = QFileInfo{path}.isDir() ? path : QFileInfo{path}.absolutePath();
        QDesktopServices::openUrl(QUrl::fromLocalFile(reveal_path));
    }
    else if (chosen == refresh)
    {
        rebuild_assets();
    }
}

void EditorWindow::rebuild_assets()
{
    apply_project_index(project_manifest_path_.isEmpty()
        ? ProjectIndexBuildResult{}
        : ProjectIndexService{}.build_candidate(project_manifest_path_));
}

void EditorWindow::apply_project_index(ProjectIndexBuildResult candidate)
{
    const auto prior_folder = project_current_folder_.isValid()
        ? project_current_folder_.data(EditorRoles::project_logical_path).toString()
        : QString{};
    const auto prior_index = project_content_stack_ && project_content_stack_->currentIndex() == 1
        ? project_thumbnail_view_->currentIndex()
        : project_explorer_->currentIndex();
    const auto prior_asset_id = prior_index.data(EditorRoles::project_entry_id).toString();
    const auto prior_path = prior_index.data(EditorRoles::project_path).toString();
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

    project_index_ = std::move(candidate);
    if (!project_index_.candidate)
    {
        project_model_->rebuild(project_index_);
        project_current_folder_ = QPersistentModelIndex{};
        project_explorer_->setRootIndex({});
        project_thumbnail_view_->setRootIndex({});
        project_breadcrumb_->setText(QStringLiteral("Project"));
        project_details_->setText(QStringLiteral("Select an asset to see details."));
        project_input_map_.reset();
        input_map_source_path_.clear();
        game_viewport_->set_input_map(input_map_service_.compatibility_map());
        return;
    }

    project_model_->rebuild(project_index_);
    const auto find_source = [this](int role, const QString& value) {
        std::function<QModelIndex(const QModelIndex&)> visit;
        visit = [this, role, &value, &visit](const QModelIndex& parent) -> QModelIndex {
            for (int row = 0; row < project_model_->rowCount(parent); ++row)
            {
                const auto index = project_model_->index(row, 0, parent);
                if (index.data(role).toString().compare(value, Qt::CaseInsensitive) == 0)
                    return index;
                if (const auto nested = visit(index); nested.isValid()) return nested;
            }
            return {};
        };
        return visit({});
    };
    auto folder_source = prior_folder.isEmpty()
        ? QModelIndex{}
        : find_source(EditorRoles::project_logical_path, prior_folder);
    if (!folder_source.isValid())
    {
        auto first_root = QString{};
        for (const auto& root : project_index_.candidate->roots)
        {
            if (root.kind == ProjectIndexRootKind::assets)
            {
                first_root = root.declared_path;
                break;
            }
        }
        folder_source = first_root.isEmpty()
            ? project_model_->index(0, 0)
            : find_source(EditorRoles::project_logical_path, first_root);
    }
    const auto folder_proxy = project_folder_filter_->mapFromSource(folder_source);
    if (folder_proxy.isValid())
    {
        project_folder_tree_->setCurrentIndex(folder_proxy);
        project_folder_tree_->expandAll();
        update_project_browser_folder(folder_proxy);
    }
    auto selection_source = prior_asset_id.isEmpty()
        ? QModelIndex{} : find_source(EditorRoles::project_entry_id, prior_asset_id);
    if (!selection_source.isValid() && !prior_path.isEmpty())
        selection_source = find_source(EditorRoles::project_path, prior_path);
    if (selection_source.isValid())
    {
        const auto selection_proxy = project_filter_->mapFromSource(selection_source);
        if (selection_proxy.isValid())
        {
            project_explorer_->setCurrentIndex(selection_proxy);
            project_thumbnail_view_->setCurrentIndex(selection_proxy);
        }
    }
    refresh_project_input_map();
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
    hierarchy_model_->set_drag_context(
        project_index_.candidate ? project_index_.candidate->project_id : QString{},
        scene_ ? QString::fromStdString(scene_->id().to_string()) : QString{},
        command_revision_, project_model_->drag_revision());
}

void EditorWindow::refresh_project_input_map()
{
    project_input_map_.reset();
    input_map_source_path_.clear();
    const ProjectIndexEntry* input_entry = nullptr;
    if (project_index_.candidate)
    {
        for (const auto& entry : project_index_.candidate->entries)
        {
            if (entry.kind == ProjectIndexEntryKind::asset
                && entry.asset_type.compare(QStringLiteral("input-map"), Qt::CaseInsensitive) == 0
                && entry.structurally_valid && !entry.resolved_source_path.isEmpty())
            {
                input_entry = &entry;
                break;
            }
        }
    }
    if (input_entry == nullptr)
    {
        game_viewport_->set_input_map(input_map_service_.compatibility_map());
        if (!project_manifest_path_.isEmpty())
        {
            append_console(
                QStringLiteral("No valid input-map asset was found; Play uses the compatibility WASD/arrow map."),
                QStringLiteral("Info"), QStringLiteral("Input"));
        }
        return;
    }

    const auto loaded = input_map_service_.load(input_entry->resolved_source_path, project_root_);
    if (!loaded.succeeded())
    {
        for (const auto& diagnostic : loaded.diagnostics)
        {
            append_console(
                QStringLiteral("%1: %2").arg(diagnostic.code, diagnostic.message),
                QStringLiteral("Error"), QStringLiteral("Input"),
                input_entry->resolved_source_path);
        }
        game_viewport_->set_input_map(input_map_service_.compatibility_map());
        return;
    }
    project_input_map_ = *loaded.document;
    input_map_source_path_ = input_entry->resolved_source_path;
    game_viewport_->set_input_map(*project_input_map_);
    append_console(
        QStringLiteral("Loaded input map '%1' with %2 control map(s).")
            .arg(project_input_map_->name)
            .arg(project_input_map_->control_maps.size()),
        QStringLiteral("Info"), QStringLiteral("Input"), input_map_source_path_,
        {}, {}, {}, {}, input_entry->id, input_map_source_path_);
}

void EditorWindow::edit_input_map()
{
    if (!project_input_map_ || input_map_source_path_.isEmpty())
    {
        QMessageBox::information(
            this, QStringLiteral("Input Map"),
            QStringLiteral("This project has no valid input-map asset. Add an input-map asset under a declared asset root first."));
        return;
    }
    InputMapEditorDialog dialog{*project_input_map_, this};
    if (dialog.exec() != QDialog::Accepted) return;
    const auto saved = input_map_service_.save(
        input_map_source_path_, project_root_, dialog.document(),
        project_input_map_->source_hash);
    if (!saved.saved)
    {
        const auto message = saved.diagnostics.isEmpty()
            ? QStringLiteral("Input map save failed.")
            : QStringLiteral("%1: %2")
                  .arg(saved.diagnostics.front().code, saved.diagnostics.front().message);
        append_console(message, QStringLiteral("Error"), QStringLiteral("Input"),
            input_map_source_path_);
        QMessageBox::warning(this, QStringLiteral("Input Map Save Failed"), message);
        return;
    }
    project_input_map_ = dialog.document();
    project_input_map_->source_hash = saved.source_hash;
    game_viewport_->set_input_map(*project_input_map_);
    append_console(
        QStringLiteral("Saved and activated input map '%1'.").arg(project_input_map_->name),
        QStringLiteral("Info"), QStringLiteral("Input"), input_map_source_path_);
}

void EditorWindow::apply_component_module_manifest(QString manifest)
{
    component_module_manifest_ = std::move(manifest);
    component_modules_available_ = !component_module_manifest_.isEmpty();
    for (auto* worker : {preview_worker_, game_preview_worker_, play_worker_})
    {
        worker->set_component_module_manifest(component_module_manifest_);
    }
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

void EditorWindow::publish_global_selection(SelectionOrigin origin)
{
    const auto active = selected_entity_id();
    selection_service_.publish(
        project_index_.candidate ? project_index_.candidate->project_id : QString{},
        scene_ ? QString::fromStdString(scene_->id().to_string()) : QString{},
        selected_entity_ids(), active, origin);
}

void EditorWindow::update_global_selection_presentation()
{
    if (!scene_)
    {
        viewport_->set_selected_name({});
        viewport_->clear_selection_geometry();
        update_tile_scene_edit_state();
        update_worker_viewport();
        return;
    }
    const auto ids = selection_service_.snapshot().ordered_entity_ids;
    std::vector<const dragonpixel::scene::entity*> entities;
    for (const auto& id : ids)
    {
        if (const auto* entity = scene_->find_entity(id)) entities.push_back(entity);
    }
    viewport_->set_selected_name(entities.size() == 1
        ? QString::fromStdString(entities.front()->name)
        : entities.empty() ? QString{} : QStringLiteral("%1 GameObjects").arg(entities.size()));
    update_viewport_selection_geometry(*scene_);
    update_tile_scene_edit_state();
    update_worker_viewport();
}

QList<dragonpixel::core::uuid> EditorWindow::primary_inspector_targets() const
{
    if (inspector_target_override_active_) return inspector_target_override_;
    if (primary_inspector_locked_) return primary_inspector_locked_targets_;
    const auto& snapshot = selection_service_.snapshot();
    return snapshot.ordered_entity_ids.isEmpty() ? selected_entity_ids() : snapshot.ordered_entity_ids;
}

void EditorWindow::set_primary_inspector_locked(bool locked)
{
    if (locked)
    {
        const auto targets = selection_service_.snapshot().ordered_entity_ids;
        if (!scene_ || targets.isEmpty())
        {
            QSignalBlocker blocker{inspector_lock_};
            inspector_lock_->setChecked(false);
            return;
        }
        primary_inspector_locked_ = true;
        primary_inspector_locked_targets_ = targets;
        primary_inspector_locked_project_id_ = selection_service_.snapshot().project_id;
        primary_inspector_locked_scene_id_ = selection_service_.snapshot().scene_id;
        inspector_lock_->setText(QStringLiteral("Locked"));
    }
    else
    {
        primary_inspector_locked_ = false;
        primary_inspector_locked_targets_.clear();
        primary_inspector_locked_project_id_.clear();
        primary_inspector_locked_scene_id_.clear();
        inspector_lock_->setText(QStringLiteral("Lock"));
    }
    inspect_selected_entities();
}

void EditorWindow::create_additional_inspector()
{
    auto state = std::make_unique<AdditionalInspector>();
    state->ordinal = static_cast<int>(additional_inspectors_.size()) + 2;
    auto* panel = new QWidget(this);
    state->panel = panel;
    panel->setObjectName(QStringLiteral("InspectorPanel.%1").arg(state->ordinal));
    panel->setAccessibleName(QStringLiteral("GameObject Inspector %1").arg(state->ordinal));
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);
    auto* header = new QFrame(panel);
    header->setFrameShape(QFrame::StyledPanel);
    auto* header_layout = new QVBoxLayout(header);
    header_layout->setContentsMargins(8, 8, 8, 8);
    auto* name_row = new QHBoxLayout;
    state->enabled = new QCheckBox(header);
    state->enabled->setObjectName(QStringLiteral("InspectorGameObjectEnabled.%1").arg(state->ordinal));
    state->enabled->setAccessibleName(QStringLiteral("Inspector %1 GameObject enabled").arg(state->ordinal));
    state->enabled->setStyleSheet(high_contrast_indicator_style(QStringLiteral("QCheckBox")));
    state->enabled->setMinimumSize(24, 24);
    state->name = new QLineEdit(header);
    state->name->setObjectName(QStringLiteral("InspectorGameObjectName.%1").arg(state->ordinal));
    state->name->setAccessibleName(QStringLiteral("Inspector %1 GameObject name").arg(state->ordinal));
    state->lock = new QToolButton(header);
    state->lock->setObjectName(QStringLiteral("InspectorLock.%1").arg(state->ordinal));
    state->lock->setText(QStringLiteral("Lock"));
    state->lock->setCheckable(true);
    state->lock->setAccessibleName(QStringLiteral("Lock Inspector %1 targets").arg(state->ordinal));
    name_row->addWidget(state->enabled);
    name_row->addWidget(state->name, 1);
    name_row->addWidget(state->lock);
    header_layout->addLayout(name_row);
    state->identity = new QLabel(QStringLiteral("No GameObject selected"), header);
    state->identity->setObjectName(QStringLiteral("InspectorGameObjectIdentity.%1").arg(state->ordinal));
    state->identity->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    state->identity->setAccessibleName(QStringLiteral("Inspector %1 target identity").arg(state->ordinal));
    header_layout->addWidget(state->identity);
    layout->addWidget(header);
    state->search = new QLineEdit(panel);
    state->search->setObjectName(QStringLiteral("InspectorSearch.%1").arg(state->ordinal));
    state->search->setAccessibleName(QStringLiteral("Search Inspector %1").arg(state->ordinal));
    state->search->setPlaceholderText(QStringLiteral("Search components and properties..."));
    layout->addWidget(state->search);
    state->model = new QStandardItemModel(panel);
    state->model->setHorizontalHeaderLabels({QStringLiteral("Property"), QStringLiteral("Value")});
    state->delegate = new InspectorDelegate(panel);
    auto* inspector_view = new InspectorDropTreeView(panel);
    state->view = inspector_view;
    state->view->setObjectName(QStringLiteral("InspectorView.%1").arg(state->ordinal));
    state->view->setAccessibleName(QStringLiteral("Typed component Inspector %1").arg(state->ordinal));
    state->view->setModel(state->model);
    state->view->setItemDelegateForColumn(1, state->delegate);
    state->view->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    state->view->setRootIsDecorated(true);
    state->view->setIndentation(14);
    state->view->setContextMenuPolicy(Qt::CustomContextMenu);
    state->view->setAcceptDrops(true);
    state->view->setDragDropMode(QAbstractItemView::DropOnly);
    state->view->setStyleSheet(QStringLiteral(
        "QTreeView { border: 0; background: palette(base); }"
        "QTreeView::item { min-height: 26px; padding: 2px; }")
        + high_contrast_indicator_style(QStringLiteral("QTreeView")));
    layout->addWidget(state->view, 1);
    state->add_component = new QPushButton(QStringLiteral("Add Component"), panel);
    state->add_component->setObjectName(QStringLiteral("InspectorAddComponent.%1").arg(state->ordinal));
    state->add_component->setAccessibleName(QStringLiteral("Add component to Inspector %1 targets").arg(state->ordinal));
    layout->addWidget(state->add_component);

    auto* dock = new QDockWidget(QStringLiteral("Inspector %1").arg(state->ordinal), this);
    state->dock = dock;
    dock->setObjectName(QStringLiteral("Dock.Inspector.%1").arg(state->ordinal));
    dock->setWidget(panel);
    dock->setAccessibleName(QStringLiteral("Inspector %1").arg(state->ordinal));
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable
        | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    if (inspector_dock_ != nullptr) tabifyDockWidget(inspector_dock_, dock);
    if (view_menu_ != nullptr) view_menu_->addAction(dock->toggleViewAction());

    auto* state_ptr = state.get();
    inspector_view->set_asset_drop_handler([this, inspector_view](
        const QModelIndex& index, const QMimeData* mime) {
        return assign_inspector_asset_drop(inspector_view, index, mime);
    });
    connect(state->model, &QStandardItemModel::itemChanged, this, &EditorWindow::edit_inspector_item);
    connect(state->search, &QLineEdit::textChanged, state->view,
        [state_ptr](const QString& text) {
            for (int row = 0; row < state_ptr->model->rowCount(); ++row)
            {
                const auto* component = state_ptr->model->item(row, 0);
                auto matches = text.trimmed().isEmpty() || component->text().contains(text, Qt::CaseInsensitive);
                for (int child = 0; !matches && child < component->rowCount(); ++child)
                    matches = component->child(child, 0)->text().contains(text, Qt::CaseInsensitive);
                state_ptr->view->setRowHidden(row, {}, !matches);
            }
        });
    connect(state->view, &QWidget::customContextMenuRequested, this,
        [this, state_ptr](const QPoint& point) { show_inspector_context_menu_for(state_ptr->view, point); });
    connect(state->lock, &QToolButton::toggled, this, [this, state_ptr](bool checked) {
        if (checked)
        {
            const auto& snapshot = selection_service_.snapshot();
            if (!scene_ || snapshot.ordered_entity_ids.isEmpty())
            {
                QSignalBlocker blocker{state_ptr->lock};
                state_ptr->lock->setChecked(false);
                return;
            }
            state_ptr->locked = true;
            state_ptr->locked_targets = snapshot.ordered_entity_ids;
            state_ptr->locked_project_id = snapshot.project_id;
            state_ptr->locked_scene_id = snapshot.scene_id;
            state_ptr->lock->setText(QStringLiteral("Locked"));
        }
        else
        {
            state_ptr->locked = false;
            state_ptr->locked_targets.clear();
            state_ptr->locked_project_id.clear();
            state_ptr->locked_scene_id.clear();
            state_ptr->lock->setText(QStringLiteral("Lock"));
        }
        refresh_additional_inspectors();
    });
    connect(state->enabled, &QCheckBox::checkStateChanged, this,
        [this, state_ptr](Qt::CheckState check_state) {
            if (rebuilding_inspector_ || !scene_ || check_state == Qt::PartiallyChecked) return;
            const auto targets = state_ptr->locked
                ? state_ptr->locked_targets : selection_service_.snapshot().ordered_entity_ids;
            std::vector<dragonpixel::scene::command> commands;
            for (const auto& id : targets)
                commands.emplace_back(dragonpixel::scene::set_entity_enabled_command{id, check_state == Qt::Checked});
            if (!commands.empty() && apply_authoring_transaction(std::move(commands), "Set GameObject enabled"))
                after_scene_mutation(QStringLiteral("Inspector %1 changed %2 target enabled state(s)")
                    .arg(state_ptr->ordinal).arg(targets.size()), selected_entity_ids());
        });
    connect(state->name, &QLineEdit::editingFinished, this, [this, state_ptr] {
        if (rebuilding_inspector_ || !scene_) return;
        const auto targets = state_ptr->locked
            ? state_ptr->locked_targets : selection_service_.snapshot().ordered_entity_ids;
        const auto* entity = targets.size() == 1 ? scene_->find_entity(targets.front()) : nullptr;
        if (entity != nullptr && state_ptr->name->text().trimmed() != QString::fromStdString(entity->name))
            static_cast<void>(edit_hierarchy_entity(targets.front(), state_ptr->name->text(), entity->enabled));
    });
    connect(state->add_component, &QPushButton::clicked, this, [this, state_ptr] {
        add_component_to_targets(state_ptr->locked
            ? state_ptr->locked_targets : selection_service_.snapshot().ordered_entity_ids);
    });

    additional_inspectors_.push_back(std::move(state));
    QSettings settings{editor_settings_path(), QSettings::IniFormat};
    settings.setValue(QStringLiteral("inspector/count"), static_cast<int>(additional_inspectors_.size()) + 1);
    refresh_additional_inspectors();
}

void EditorWindow::refresh_additional_inspectors()
{
    if (rebuilding_inspector_) return;
    for (auto& owned_state : additional_inspectors_)
    {
        auto& state = *owned_state;
        const auto targets = state.locked
            ? state.locked_targets : selection_service_.snapshot().ordered_entity_ids;
        const auto current_project = selection_service_.snapshot().project_id;
        const auto current_scene = scene_ ? QString::fromStdString(scene_->id().to_string()) : QString{};
        const auto unavailable_identity = state.locked
            && (state.locked_project_id != current_project || state.locked_scene_id != current_scene
                || std::any_of(targets.begin(), targets.end(), [this](const auto& id) {
                    return !scene_ || scene_->find_entity(id) == nullptr;
                }));
        if (unavailable_identity)
        {
            rebuilding_inspector_ = true;
            state.model->clear();
            state.model->setHorizontalHeaderLabels({QStringLiteral("Property"), QStringLiteral("Value")});
            state.enabled->setEnabled(false);
            state.name->setEnabled(false);
            state.add_component->setEnabled(false);
            state.identity->setText(QStringLiteral("Locked target unavailable - Undo may restore it, or unlock this Inspector"));
            rebuilding_inspector_ = false;
            continue;
        }

        auto* saved_view = inspector_;
        auto* saved_model = inspector_model_;
        auto* saved_delegate = inspector_delegate_;
        auto* saved_panel = inspector_panel_;
        auto* saved_enabled = inspector_enabled_;
        auto* saved_name = inspector_name_;
        auto* saved_identity = inspector_identity_;
        auto* saved_search = inspector_search_;
        auto* saved_add = inspector_add_component_;
        inspector_ = state.view;
        inspector_model_ = state.model;
        inspector_delegate_ = state.delegate;
        inspector_panel_ = state.panel;
        inspector_enabled_ = state.enabled;
        inspector_name_ = state.name;
        inspector_identity_ = state.identity;
        inspector_search_ = state.search;
        inspector_add_component_ = state.add_component;
        inspector_target_override_active_ = true;
        inspector_target_override_ = targets;
        inspect_selected_entities();
        inspector_target_override_.clear();
        inspector_target_override_active_ = false;
        inspector_ = saved_view;
        inspector_model_ = saved_model;
        inspector_delegate_ = saved_delegate;
        inspector_panel_ = saved_panel;
        inspector_enabled_ = saved_enabled;
        inspector_name_ = saved_name;
        inspector_identity_ = saved_identity;
        inspector_search_ = saved_search;
        inspector_add_component_ = saved_add;
    }
}

void EditorWindow::clear_inspector_locks()
{
    primary_inspector_locked_ = false;
    primary_inspector_locked_targets_.clear();
    primary_inspector_locked_project_id_.clear();
    primary_inspector_locked_scene_id_.clear();
    if (inspector_lock_ != nullptr)
    {
        QSignalBlocker blocker{inspector_lock_};
        inspector_lock_->setChecked(false);
        inspector_lock_->setText(QStringLiteral("Lock"));
    }
    for (auto& state : additional_inspectors_)
    {
        state->locked = false;
        state->locked_targets.clear();
        state->locked_project_id.clear();
        state->locked_scene_id.clear();
        QSignalBlocker blocker{state->lock};
        state->lock->setChecked(false);
        state->lock->setText(QStringLiteral("Lock"));
    }
}

void EditorWindow::inspect_selected_entities()
{
    rebuilding_inspector_ = true;
    inspector_model_->clear();
    inspector_model_->setHorizontalHeaderLabels({QStringLiteral("Property"), QStringLiteral("Value")});
    const auto selected_ids = primary_inspector_targets();
    if (!scene_ || selected_ids.isEmpty())
    {
        inspector_enabled_->setEnabled(false);
        inspector_enabled_->setTristate(false);
        inspector_enabled_->setChecked(false);
        inspector_name_->setEnabled(false);
        inspector_name_->clear();
        inspector_identity_->setText(QStringLiteral("No GameObject selected"));
        inspector_add_component_->setEnabled(false);
        rebuilding_inspector_ = false;
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
    if (entities.empty() || ((inspector_target_override_active_ || primary_inspector_locked_)
        && entities.size() != static_cast<std::size_t>(selected_ids.size())))
    {
        inspector_enabled_->setEnabled(false);
        inspector_name_->setEnabled(false);
        inspector_name_->clear();
        inspector_identity_->setText(entities.empty() && !primary_inspector_locked_
            ? QStringLiteral("No GameObject selected")
            : QStringLiteral("Locked target unavailable - Undo may restore it, or unlock this Inspector"));
        inspector_add_component_->setEnabled(false);
        rebuilding_inspector_ = false;
        return;
    }
    const auto all_entities_enabled = std::all_of(entities.begin(), entities.end(), [](const auto* value) {
        return value->enabled;
    });
    const auto no_entities_enabled = std::none_of(entities.begin(), entities.end(), [](const auto* value) {
        return value->enabled;
    });
    inspector_enabled_->setEnabled(true);
    inspector_enabled_->setTristate(entities.size() > 1);
    inspector_enabled_->setCheckState(all_entities_enabled
        ? Qt::Checked
        : (no_entities_enabled ? Qt::Unchecked : Qt::PartiallyChecked));
    inspector_name_->setEnabled(entities.size() == 1);
    inspector_name_->setText(entities.size() == 1
        ? QString::fromStdString(entities.front()->name)
        : QStringLiteral("%1 GameObjects selected").arg(entities.size()));
    inspector_identity_->setText(entities.size() == 1
        ? QStringLiteral("UUID  %1   ·   Authoring GameObject").arg(
            QString::fromStdString(entities.front()->id.to_string()))
        : QStringLiteral("Multi-object editing   ·   %1 compatible components shown").arg(entities.size()));
    inspector_add_component_->setEnabled(true);
    QStringList entity_id_strings;
    for (const auto* entity : entities)
    {
        entity_id_strings.push_back(QString::fromStdString(entity->id.to_string()));
    }

    std::function<void(
        QStandardItem*,
        const dragonpixel::metadata::property_descriptor&,
        const std::vector<nlohmann::ordered_json>&,
        const QString&,
        const QString&,
        const QStringList&,
        int)> append_property;
    append_property = [this, &append_property, &entity_id_strings](
                          QStandardItem* parent,
                          const dragonpixel::metadata::property_descriptor& property,
                          const std::vector<nlohmann::ordered_json>& values,
                          const QString& component_type,
                          const QString& root_property_id,
                          const QStringList& path,
                          int depth) {
        if (values.empty() || depth > 16)
        {
            return;
        }
        const auto mixed = std::any_of(values.begin() + 1, values.end(), [&](const auto& value) {
            return value != values.front();
        });
        auto* property_name = new QStandardItem{QString::fromStdString(property.display_name)};
        property_name->setEditable(false);
        auto* property_value = new QStandardItem{
            mixed ? QStringLiteral("<mixed>") : QString::fromStdString(values.front().dump())};
        property_value->setData(entity_id_strings, EditorRoles::entity_ids);
        property_value->setData(component_type, EditorRoles::component_type);
        property_value->setData(root_property_id, EditorRoles::property_id);
        property_value->setData(path, EditorRoles::property_path);
        property_value->setData(static_cast<int>(property.type), EditorRoles::value_type);
        property_value->setData(mixed, EditorRoles::mixed_value);
        property_value->setData(property.nullable, EditorRoles::nullable_value);
        QStringList choices;
        for (const auto& choice : property.enum_choices)
        {
            choices.push_back(QString::fromStdString(choice));
        }
        property_value->setData(choices, EditorRoles::enum_choices);
        if (property.minimum) property_value->setData(*property.minimum, EditorRoles::minimum);
        if (property.maximum) property_value->setData(*property.maximum, EditorRoles::maximum);
        if (property.step) property_value->setData(*property.step, EditorRoles::step);
        property_value->setData(QString::fromStdString(property.drawer_key), EditorRoles::drawer_key);
        property_value->setToolTip(QStringLiteral("%1%2%3")
            .arg(property.tooltip.empty() ? QStringLiteral("Typed Inspector property") : QString::fromStdString(property.tooltip),
                 property.units.empty() ? QString{} : QStringLiteral(" · %1").arg(QString::fromStdString(property.units)),
                 property.drawer_key.empty() ? QString{} : QStringLiteral(" · Drawer: %1").arg(QString::fromStdString(property.drawer_key))));

        if (property.type == dragonpixel::metadata::value_type::polymorphic_object && property.shape)
        {
            QStringList object_names;
            QStringList object_ids;
            QStringList object_values;
            for (const auto& implementation : metadata_.implementations(property.shape->contract_id))
            {
                object_names.push_back(QString::fromStdString(implementation.get().display_name));
                object_ids.push_back(QString::fromStdString(implementation.get().type_id));
                object_values.push_back(QString::fromStdString(object_envelope_default(implementation.get()).dump()));
            }
            property_value->setData(object_names, EditorRoles::object_type_choices);
            property_value->setData(object_ids, EditorRoles::object_type_ids);
            property_value->setData(object_values, EditorRoles::object_type_values);
        }

        const auto structured = property.type == dragonpixel::metadata::value_type::object
            || property.type == dragonpixel::metadata::value_type::list
            || property.type == dragonpixel::metadata::value_type::dictionary;
        if (!property.read_only && !structured
            && property.type != dragonpixel::metadata::value_type::component_reference)
        {
            property_value->setEditable(true);
        }
        parent->appendRow({property_name, property_value});
        if (mixed || values.front().is_null() || depth == 16)
        {
            return;
        }

        const dragonpixel::metadata::object_type_descriptor* object_type = nullptr;
        nlohmann::ordered_json object_properties;
        QStringList child_prefix = path;
        if (property.type == dragonpixel::metadata::value_type::object && property.shape
            && values.front().is_object())
        {
            object_type = metadata_.find_object_type(property.shape->object_type_id);
            object_properties = values.front();
        }
        else if (property.type == dragonpixel::metadata::value_type::polymorphic_object
            && values.front().is_object())
        {
            object_type = metadata_.find_object_type(values.front().value("typeId", std::string{}));
            object_properties = values.front().value("properties", nlohmann::ordered_json::object());
            child_prefix.push_back(QStringLiteral("properties"));
        }
        if (object_type != nullptr && object_properties.is_object())
        {
            for (const auto& child_property : object_type->properties)
            {
                std::vector<nlohmann::ordered_json> child_values;
                child_values.reserve(values.size());
                for (const auto& selected_value : values)
                {
                    const auto& source = property.type == dragonpixel::metadata::value_type::polymorphic_object
                        ? selected_value.value("properties", nlohmann::ordered_json::object())
                        : selected_value;
                    const auto found = source.find(child_property.property_id);
                    child_values.push_back(found == source.end() ? property_default(child_property) : *found);
                }
                auto child_path = child_prefix;
                child_path.push_back(QString::fromStdString(child_property.property_id));
                append_property(property_name, child_property, child_values, component_type,
                    root_property_id, child_path, depth + 1);
            }
            return;
        }

        if (property.type == dragonpixel::metadata::value_type::list && values.front().is_array()
            && property.shape && !property.shape->arguments.empty())
        {
            const auto& element_shape = property.shape->arguments.front();
            for (std::size_t index = 0; index < values.front().size(); ++index)
            {
                dragonpixel::metadata::property_descriptor element;
                element.property_id = std::to_string(index);
                element.display_name = std::string{"Element "} + std::to_string(index);
                element.type = element_shape.type;
                element.nullable = element_shape.nullable;
                element.reference_filter = element_shape.reference_filter;
                element.shape = element_shape;
                std::vector<nlohmann::ordered_json> element_values;
                for (const auto& selected_value : values)
                {
                    element_values.push_back(selected_value.is_array() && index < selected_value.size()
                        ? selected_value.at(index) : nlohmann::ordered_json{nullptr});
                }
                auto child_path = path;
                child_path.push_back(QString::number(index));
                append_property(property_name, element, element_values, component_type,
                    root_property_id, child_path, depth + 1);
            }
            return;
        }

        if (property.type == dragonpixel::metadata::value_type::dictionary && values.front().is_object()
            && property.shape && !property.shape->arguments.empty())
        {
            const auto& element_shape = property.shape->arguments.front();
            for (auto iterator = values.front().begin(); iterator != values.front().end(); ++iterator)
            {
                dragonpixel::metadata::property_descriptor element;
                element.property_id = iterator.key();
                element.display_name = iterator.key();
                element.type = element_shape.type;
                element.nullable = element_shape.nullable;
                element.reference_filter = element_shape.reference_filter;
                element.shape = element_shape;
                std::vector<nlohmann::ordered_json> element_values;
                for (const auto& selected_value : values)
                {
                    const auto found = selected_value.find(iterator.key());
                    element_values.push_back(found == selected_value.end() ? nlohmann::ordered_json{nullptr} : *found);
                }
                auto child_path = path;
                child_path.push_back(QString::fromStdString(iterator.key()));
                append_property(property_name, element, element_values, component_type,
                    root_property_id, child_path, depth + 1);
            }
        }
    };

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
        auto component_font = component_item->font();
        component_font.setBold(true);
        component_item->setFont(component_font);
        component_item->setBackground(QBrush{palette().color(QPalette::AlternateBase)});
        const auto runtime_unavailable = descriptor != nullptr
            && !descriptor->runtime_module_id.empty() && !component_modules_available_;
        auto* owner_item = new QStandardItem{owner_text(component.owner)
            + (runtime_unavailable ? QStringLiteral(" · Unbuilt") : QString{})};
        owner_item->setEditable(false);
        if (runtime_unavailable)
        {
            owner_item->setForeground(QBrush{QColor{224, 164, 54}});
            owner_item->setToolTip(QStringLiteral("Authoring data is editable and preserved, but Build Components must succeed before this component can execute in Preview or Play."));
        }
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
        if (!descriptor->source_path.empty())
        {
            auto* source_name = new QStandardItem{QStringLiteral("Script Source")};
            auto* source_value = new QStandardItem{QString::fromStdString(descriptor->source_path)};
            source_name->setEditable(false);
            source_value->setEditable(false);
            source_value->setToolTip(QStringLiteral("Contained project source. Exposed fields below are edited and saved through Inspector commands; source execution remains worker-only."));
            component_item->appendRow({source_name, source_value});
        }
        for (const auto& property : descriptor->properties)
        {
            std::vector<nlohmann::ordered_json> values;
            values.reserve(components.size());
            for (const auto* selected_component : components)
            {
                const auto found = selected_component->properties.find(property.property_id);
                values.push_back(found == selected_component->properties.end()
                    ? property_default(property) : *found);
            }
            append_property(component_item, property, values,
                QString::fromStdString(component.type_id),
                QString::fromStdString(property.property_id), {}, 0);
        }
    }
    inspector_->expandAll();
    inspector_->resizeColumnToContents(0);
    rebuilding_inspector_ = false;
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

bool EditorWindow::drag_reparent_entities(
    const std::vector<dragonpixel::core::uuid>& ids,
    const std::optional<dragonpixel::core::uuid>& parent,
    std::optional<std::size_t> sibling_index)
{
    if (!scene_ || ids.empty())
    {
        return false;
    }
    std::vector<dragonpixel::scene::command> commands;
    commands.reserve(ids.size());
    for (std::size_t index = 0; index < ids.size(); ++index)
    {
        const auto target_index = sibling_index
            ? std::optional<std::size_t>{*sibling_index + index}
            : std::optional<std::size_t>{};
        commands.emplace_back(dragonpixel::scene::reparent_entity_command{
            ids[index], parent, target_index});
    }
    if (!apply_authoring_transaction(std::move(commands), "Reparent GameObjects"))
    {
        append_console(QStringLiteral("Grouped reparent rejected without partial movement"), QStringLiteral("Warning"));
        QMetaObject::invokeMethod(this, [this] {
            rebuild_hierarchy();
        }, Qt::QueuedConnection);
        return false;
    }
    QList<dragonpixel::core::uuid> selection;
    for (const auto& id : ids) selection.push_back(id);
    QMetaObject::invokeMethod(this, [this, selection] {
        after_scene_mutation(QStringLiteral("GameObjects reparented in one validated transaction"), selection);
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
    const auto property_path = item->data(EditorRoles::property_path).toStringList();
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
                    if (property_path.isEmpty())
                    {
                        commands.emplace_back(dragonpixel::scene::set_component_property_command{
                            *id, component_type.toStdString(), property_id.toStdString(), value});
                    }
                    else
                    {
                        std::vector<std::string> path;
                        path.reserve(static_cast<std::size_t>(property_path.size()));
                        for (const auto& segment : property_path)
                        {
                            path.push_back(segment.toStdString());
                        }
                        commands.emplace_back(dragonpixel::scene::set_component_property_path_command{
                            *id, component_type.toStdString(), property_id.toStdString(), std::move(path), value});
                    }
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
            refresh_additional_inspectors();
        }, Qt::QueuedConnection);
    }
}

std::optional<dragonpixel::core::uuid> EditorWindow::create_preset(
    dragonpixel::scene::entity_preset preset,
    const QString& asset_override,
    bool force_scene_root,
    const std::optional<dragonpixel::core::uuid>& explicit_parent)
{
    if (!scene_)
    {
        return std::nullopt;
    }
    const auto base_name = [preset, &asset_override] {
        switch (preset)
        {
            case dragonpixel::scene::entity_preset::sprite:
                if (asset_override == QStringLiteral("builtin://square")) return QStringLiteral("Square");
                if (asset_override == QStringLiteral("builtin://circle")) return QStringLiteral("Circle");
                return QStringLiteral("Sprite");
            case dragonpixel::scene::entity_preset::cube: return QStringLiteral("Cube");
            case dragonpixel::scene::entity_preset::camera: return QStringLiteral("Camera");
            case dragonpixel::scene::entity_preset::light: return QStringLiteral("Light");
            case dragonpixel::scene::entity_preset::tilemap: return QStringLiteral("Tilemap");
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
    const auto parent = explicit_parent
        ? explicit_parent
        : !force_scene_root && selected.size() == 1
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
            || (preset == dragonpixel::scene::entity_preset::cube && asset_type.contains(QStringLiteral("mesh")))
            || (preset == dragonpixel::scene::entity_preset::tilemap && asset_type.contains(QStringLiteral("tilemap")));
        if (compatible && !asset_id.isEmpty())
        {
            primary_asset = asset_id.toStdString();
        }
    }
    if (!primary_asset
        && preset == dragonpixel::scene::entity_preset::tilemap
        && tile_document_service_ != nullptr
        && tile_document_service_->tilemap() != nullptr)
    {
        primary_asset = tile_document_service_->tilemap()->asset_id.to_string();
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
        return std::nullopt;
    }
    after_scene_mutation(QStringLiteral("Created %1 preset through one validated transaction").arg(name), {id});
    return id;
}

void EditorWindow::duplicate_selected()
{
    if (!scene_)
    {
        return;
    }
    auto root_ids = selected_entity_ids();
    if (root_ids.isEmpty())
    {
        return;
    }
    const auto selected_roots = root_ids;
    root_ids.erase(std::remove_if(root_ids.begin(), root_ids.end(), [&](const auto& id) {
        const auto* entity = scene_->find_entity(id);
        auto parent = entity ? entity->parent_id : std::optional<dragonpixel::core::uuid>{};
        while (parent)
        {
            if (selected_roots.contains(*parent)) return true;
            const auto* parent_entity = scene_->find_entity(*parent);
            parent = parent_entity ? parent_entity->parent_id
                                   : std::optional<dragonpixel::core::uuid>{};
        }
        return false;
    }), root_ids.end());
    std::vector<dragonpixel::scene::command> commands;
    QList<dragonpixel::core::uuid> duplicates;
    for (const auto& root_id : root_ids)
    {
        const auto* root = scene_->find_entity(root_id);
        if (root == nullptr) return;
        std::vector<dragonpixel::scene::entity_id_remap> remaps;
        std::vector<dragonpixel::core::uuid> frontier{root_id};
        for (std::size_t index = 0; index < frontier.size(); ++index)
        {
            const auto current = frontier[index];
            remaps.push_back({current, dragonpixel::core::uuid::random_v4()});
            for (const auto& entity : scene_->entities())
            {
                if (entity.parent_id == current) frontier.push_back(entity.id);
            }
        }
        duplicates.push_back(remaps.front().duplicate_id);
        dragonpixel::scene::duplicate_subtree_command duplicate{};
        duplicate.root_entity_id = root_id;
        duplicate.id_remaps = std::move(remaps);
        duplicate.duplicate_root_name = root->name + " Copy";
        commands.emplace_back(std::move(duplicate));
    }
    if (apply_authoring_transaction(std::move(commands), "Duplicate GameObject subtrees"))
    {
        after_scene_mutation(QStringLiteral("Duplicated %1 top-level selection(s) with remapped UUID references")
            .arg(duplicates.size()), duplicates);
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
    auto root_ids = selected_entity_ids();
    if (root_ids.isEmpty())
    {
        return;
    }
    const auto selected_roots = root_ids;
    root_ids.erase(std::remove_if(root_ids.begin(), root_ids.end(), [&](const auto& id) {
        const auto* entity = scene_->find_entity(id);
        auto parent = entity ? entity->parent_id : std::optional<dragonpixel::core::uuid>{};
        while (parent)
        {
            if (selected_roots.contains(*parent)) return true;
            const auto* parent_entity = scene_->find_entity(*parent);
            parent = parent_entity ? parent_entity->parent_id
                                   : std::optional<dragonpixel::core::uuid>{};
        }
        return false;
    }), root_ids.end());
    QSet<QString> all_entities;
    for (const auto& root_id : root_ids)
    {
        std::vector<dragonpixel::core::uuid> descendants{root_id};
        for (std::size_t index = 0; index < descendants.size(); ++index)
        {
            all_entities.insert(QString::fromStdString(descendants[index].to_string()));
            for (const auto& entity : scene_->entities())
            {
                if (entity.parent_id == descendants[index]) descendants.push_back(entity.id);
            }
        }
    }
    const auto label = root_ids.size() == 1 && scene_->find_entity(root_ids.front())
        ? QString::fromStdString(scene_->find_entity(root_ids.front())->name)
        : QStringLiteral("%1 selected roots").arg(root_ids.size());
    if (delete_prompt_ && !delete_prompt_(label, all_entities.size()))
    {
        return;
    }
    std::vector<dragonpixel::scene::command> commands;
    for (const auto& root_id : root_ids)
        commands.emplace_back(dragonpixel::scene::delete_subtree_command{root_id});
    if (apply_authoring_transaction(std::move(commands), "Delete GameObject subtrees"))
    {
        after_scene_mutation(QStringLiteral("Deleted %1 GameObject(s) in one transaction; Undo restores payloads and ordering")
            .arg(all_entities.size()));
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
    auto selected = selected_entity_ids();
    if (selected.isEmpty())
    {
        return;
    }
    const auto all_selected = selected;
    selected.erase(std::remove_if(selected.begin(), selected.end(), [&](const auto& id) {
        const auto* entity = scene_->find_entity(id);
        auto parent = entity ? entity->parent_id : std::optional<dragonpixel::core::uuid>{};
        while (parent)
        {
            if (all_selected.contains(*parent)) return true;
            const auto* parent_entity = scene_->find_entity(*parent);
            parent = parent_entity ? parent_entity->parent_id
                                   : std::optional<dragonpixel::core::uuid>{};
        }
        return false;
    }), selected.end());
    QStringList labels{QStringLiteral("<Scene root>")};
    QHash<QString, QString> ids;
    for (const auto& entity : scene_->entities())
    {
        if (all_selected.contains(entity.id))
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
    std::vector<dragonpixel::core::uuid> roots;
    roots.reserve(selected.size());
    for (const auto& id : selected) roots.push_back(id);
    static_cast<void>(drag_reparent_entities(roots, parent, std::nullopt));
}

void EditorWindow::group_selected()
{
    if (!scene_)
    {
        return;
    }
    auto selected = selected_entity_ids();
    if (selected.isEmpty())
    {
        return;
    }
    const auto all_selected = selected;
    selected.erase(std::remove_if(selected.begin(), selected.end(), [&](const auto& id) {
        const auto* entity = scene_->find_entity(id);
        auto parent = entity ? entity->parent_id : std::optional<dragonpixel::core::uuid>{};
        while (parent)
        {
            if (all_selected.contains(*parent)) return true;
            const auto* parent_entity = scene_->find_entity(*parent);
            parent = parent_entity ? parent_entity->parent_id
                                   : std::optional<dragonpixel::core::uuid>{};
        }
        return false;
    }), selected.end());
    if (selected.isEmpty()) return;

    std::optional<dragonpixel::core::uuid> common_parent;
    const auto* first = scene_->find_entity(selected.front());
    if (first) common_parent = first->parent_id;
    for (const auto& id : selected)
    {
        const auto* entity = scene_->find_entity(id);
        if (entity == nullptr || entity->parent_id != common_parent)
        {
            common_parent.reset();
            break;
        }
    }
    const auto group_id = dragonpixel::core::uuid::random_v4();
    std::vector<dragonpixel::scene::command> commands;
    commands.emplace_back(dragonpixel::scene::create_preset_command{
        group_id,
        "Group",
        dragonpixel::scene::entity_preset::empty,
        common_parent,
        std::nullopt,
        std::nullopt,
        std::nullopt});
    for (std::size_t index = 0; index < static_cast<std::size_t>(selected.size()); ++index)
    {
        commands.emplace_back(dragonpixel::scene::reparent_entity_command{
            selected.at(static_cast<qsizetype>(index)), group_id, index});
    }
    if (!apply_authoring_transaction(std::move(commands), "Group GameObjects"))
    {
        append_console(QStringLiteral("Grouping was rejected without partial movement"),
            QStringLiteral("Warning"), QStringLiteral("Hierarchy"));
        return;
    }
    after_scene_mutation(QStringLiteral("Grouped %1 top-level selection(s) in one transaction")
        .arg(selected.size()), {group_id});
}

void EditorWindow::add_component()
{
    add_component_to_targets(primary_inspector_targets());
}

void EditorWindow::add_component_to_targets(const QList<dragonpixel::core::uuid>& entity_ids)
{
    if (!scene_) return;
    if (entity_ids.empty())
    {
        return;
    }
    QHash<QString, const dragonpixel::metadata::component_descriptor*> descriptors;
    for (const auto& reference : metadata_.descriptors())
    {
        const auto& descriptor = reference.get();
        const auto missing_target = std::any_of(entity_ids.begin(), entity_ids.end(), [&](const auto& id) {
            const auto* entity = scene_->find_entity(id);
            return entity != nullptr && std::none_of(entity->components.begin(), entity->components.end(),
                [&](const auto& component) { return component.type_id == descriptor.type_id; });
        });
        if (missing_target && descriptor.addable)
        {
            descriptors.insert(QString::fromStdString(descriptor.type_id), &descriptor);
        }
    }
    QDialog dialog{this};
    dialog.setObjectName(QStringLiteral("AddComponentDialog"));
    dialog.setWindowTitle(QStringLiteral("Add Component"));
    dialog.resize(520, 560);
    auto* layout = new QVBoxLayout{&dialog};
    auto* target_count = new QLabel{
        QStringLiteral("Add to missing GameObjects across %1 Inspector target(s)").arg(entity_ids.size()), &dialog};
    target_count->setObjectName(QStringLiteral("AddComponentTargetCount"));
    target_count->setAccessibleName(QStringLiteral("Add Component target count"));
    layout->addWidget(target_count);
    auto* search = new QLineEdit{&dialog};
    search->setObjectName(QStringLiteral("AddComponentSearch"));
    search->setPlaceholderText(QStringLiteral("Search components by name, category, language, or ID…"));
    search->setAccessibleName(QStringLiteral("Search available components"));
    auto* list = new QListWidget{&dialog};
    list->setObjectName(QStringLiteral("AddComponentList"));
    list->setAccessibleName(QStringLiteral("Available components"));
    list->setAlternatingRowColors(true);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(search);
    layout->addWidget(list, 1);
    auto* buttons = new QDialogButtonBox{QDialogButtonBox::Cancel, &dialog};
    auto* create_csharp = buttons->addButton(QStringLiteral("New C# Script..."), QDialogButtonBox::ActionRole);
    create_csharp->setObjectName(QStringLiteral("CreateCSharpScriptFromInspector"));
    auto* create_cpp = buttons->addButton(QStringLiteral("New C++ Component..."), QDialogButtonBox::ActionRole);
    create_cpp->setObjectName(QStringLiteral("CreateCppComponentFromInspector"));
    auto* add = buttons->addButton(QStringLiteral("Add"), QDialogButtonBox::AcceptRole);
    add->setObjectName(QStringLiteral("ConfirmAddComponent"));
    add->setEnabled(false);
    layout->addWidget(buttons);

    QSettings settings{editor_settings_path(), QSettings::IniFormat};
    const auto recent_type = settings.value(QStringLiteral("inspector/recentComponent")).toString();
    QList<const dragonpixel::metadata::component_descriptor*> ordered;
    ordered.reserve(descriptors.size());
    for (const auto* descriptor : descriptors)
    {
        ordered.push_back(descriptor);
    }
    std::sort(ordered.begin(), ordered.end(), [&recent_type](const auto* left, const auto* right) {
        const auto left_recent = QString::fromStdString(left->type_id) == recent_type;
        const auto right_recent = QString::fromStdString(right->type_id) == recent_type;
        if (left_recent != right_recent) return left_recent;
        if (left->category != right->category) return left->category < right->category;
        return left->display_name < right->display_name;
    });
    for (const auto* descriptor : ordered)
    {
        const auto language = descriptor->language == dragonpixel::metadata::implementation_language::csharp
            ? QStringLiteral("C#")
            : descriptor->language == dragonpixel::metadata::implementation_language::data_only
                ? QStringLiteral("Data") : QStringLiteral("C++");
        const auto category = descriptor->category.empty()
            ? QStringLiteral("Components") : QString::fromStdString(descriptor->category);
        auto* item = new QListWidgetItem{
            QStringLiteral("%1\n%2  ·  %3")
                .arg(QString::fromStdString(descriptor->display_name), category, language), list};
        item->setData(Qt::UserRole, QString::fromStdString(descriptor->type_id));
        item->setData(Qt::UserRole + 1, QStringLiteral("%1 %2 %3 %4")
            .arg(QString::fromStdString(descriptor->display_name), category, language,
                 QString::fromStdString(descriptor->type_id)).toLower());
        item->setToolTip(QString::fromStdString(descriptor->tooltip));
        item->setSizeHint(QSize{0, 50});
    }
    connect(search, &QLineEdit::textChanged, list, [list](const QString& text) {
        const auto tokens = text.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (int row = 0; row < list->count(); ++row)
        {
            const auto haystack = list->item(row)->data(Qt::UserRole + 1).toString();
            list->item(row)->setHidden(!std::all_of(tokens.begin(), tokens.end(), [&](const auto& token) {
                return haystack.contains(token);
            }));
        }
    });
    connect(list, &QListWidget::currentItemChanged, add, [add](QListWidgetItem* current) {
        add->setEnabled(current != nullptr);
    });
    connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    constexpr int create_csharp_result = QDialog::Accepted + 1;
    constexpr int create_cpp_result = QDialog::Accepted + 2;
    connect(create_csharp, &QPushButton::clicked, &dialog, [&dialog] {
        dialog.done(create_csharp_result);
    });
    connect(create_cpp, &QPushButton::clicked, &dialog, [&dialog] {
        dialog.done(create_cpp_result);
    });
    if (list->count() > 0) list->setCurrentRow(0);
    search->setFocus();
    const auto dialog_result = dialog.exec();
    if (dialog_result == create_csharp_result)
    {
        create_project_component(ProjectComponentLanguage::csharp);
        return;
    }
    if (dialog_result == create_cpp_result)
    {
        create_project_component(ProjectComponentLanguage::cpp);
        return;
    }
    if (dialog_result != QDialog::Accepted || list->currentItem() == nullptr)
    {
        return;
    }
    const auto type_id = list->currentItem()->data(Qt::UserRole).toString();
    if (attach_component_type(type_id, entity_ids, QStringLiteral("Add component")))
    {
        settings.setValue(QStringLiteral("inspector/recentComponent"), type_id);
    }
}

bool EditorWindow::attach_component_type(
    const QString& type_id,
    const QList<dragonpixel::core::uuid>& entity_ids,
    const QString& transaction_description)
{
    if (!scene_ || entity_ids.isEmpty())
    {
        return false;
    }
    const auto* descriptor = metadata_.find(type_id.toStdString());
    if (descriptor == nullptr || !descriptor->addable)
    {
        append_console(
            QStringLiteral("Component type %1 is unavailable or cannot be attached.").arg(type_id),
            QStringLiteral("Error"),
            QStringLiteral("Components"));
        return false;
    }

    nlohmann::ordered_json properties = nlohmann::ordered_json::object();
    for (const auto& property : descriptor->properties)
    {
        properties[property.property_id] = property_default(property);
    }

    std::vector<dragonpixel::scene::command> commands;
    commands.reserve(static_cast<std::size_t>(entity_ids.size()));
    for (const auto& entity_id : entity_ids)
    {
        const auto* entity = scene_->find_entity(entity_id);
        if (entity == nullptr)
        {
            append_console(
                QStringLiteral("Component %1 was not attached because an Inspector target is no longer available.")
                    .arg(QString::fromStdString(descriptor->display_name)),
                QStringLiteral("Warning"),
                QStringLiteral("Components"));
            return false;
        }
        if (std::any_of(entity->components.begin(), entity->components.end(), [&](const auto& component) {
                return component.type_id == descriptor->type_id;
            }))
        {
            continue;
        }
        dragonpixel::scene::component_record component{
            descriptor->type_id,
            descriptor->schema_version,
            descriptor->owner,
            properties,
            false,
            nlohmann::ordered_json::object(),
            true,
            descriptor->qualified_name,
        };
        commands.emplace_back(dragonpixel::scene::upsert_component_command{entity_id, std::move(component)});
    }

    if (commands.empty()) return true;
    if (!apply_authoring_transaction(
            std::move(commands),
            transaction_description.toStdString()))
    {
        return false;
    }
    after_scene_mutation(
        QStringLiteral("Component added to %1 GameObject(s) through one validated transaction")
            .arg(entity_ids.size()),
        entity_ids);
    return true;
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
    const auto entity_ids = primary_inspector_targets();
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

void EditorWindow::show_inspector_context_menu(const QPoint& point)
{
    show_inspector_context_menu_for(inspector_, point);
}

bool EditorWindow::assign_inspector_asset_drop(
    QTreeView* view,
    const QModelIndex& requested_index,
    const QMimeData* mime)
{
    if (!scene_ || !project_index_.candidate || view == nullptr || mime == nullptr
        || !mime->hasFormat(QStringLiteral("application/x-dragonpixel-project-item")))
        return false;
    const auto payload = QJsonDocument::fromJson(mime->data(
        QStringLiteral("application/x-dragonpixel-project-item"))).object();
    const auto items = payload.value(QStringLiteral("items")).toArray();
    if (payload.value(QStringLiteral("format")).toString() != QStringLiteral("dpe.drag")
        || payload.value(QStringLiteral("formatVersion")).toInt() != 1
        || payload.value(QStringLiteral("projectId")).toString() != project_index_.candidate->project_id
        || payload.value(QStringLiteral("sourceRevision")).toInteger()
            != static_cast<qint64>(project_model_->drag_revision())
        || items.size() != 1 || !items.at(0).isObject())
    {
        append_console(QStringLiteral("Inspector asset assignment rejected because the drag is stale, cross-project, or ambiguous."),
            QStringLiteral("Warning"), QStringLiteral("Drag and Drop"));
        return false;
    }
    const auto item = items.at(0).toObject();
    const auto asset_id = item.value(QStringLiteral("assetId")).toString();
    const auto* entry = project_index_.candidate->find_by_id(asset_id);
    auto value_index = requested_index.siblingAtColumn(1);
    const auto value_type = static_cast<dragonpixel::metadata::value_type>(
        value_index.data(EditorRoles::value_type).toInt());
    if (!value_index.isValid() || value_type != dragonpixel::metadata::value_type::asset_reference
        || item.value(QStringLiteral("kind")).toString() != QStringLiteral("asset")
        || entry == nullptr || !entry->structurally_valid)
    {
        append_console(QStringLiteral("Drop a compatible indexed asset onto an Inspector asset-reference field."),
            QStringLiteral("Warning"), QStringLiteral("Inspector"));
        return false;
    }
    const auto component_type = value_index.data(EditorRoles::component_type).toString();
    const auto property_id = value_index.data(EditorRoles::property_id).toString();
    const auto* descriptor = metadata_.find(component_type.toStdString());
    if (descriptor == nullptr)
    {
        append_console(QStringLiteral("Inspector asset assignment rejected because component metadata is unavailable."),
            QStringLiteral("Warning"), QStringLiteral("Inspector"));
        return false;
    }
    const auto property = std::find_if(descriptor->properties.begin(), descriptor->properties.end(),
        [&](const auto& candidate) { return candidate.property_id == property_id.toStdString(); });
    if (property == descriptor->properties.end()
        || (!property->reference_filter.empty() && !entry->asset_type.contains(
            QString::fromStdString(property->reference_filter), Qt::CaseInsensitive)))
    {
        append_console(QStringLiteral("Inspector asset assignment rejected before mutation because the asset type is incompatible with this field."),
            QStringLiteral("Warning"), QStringLiteral("Inspector"));
        return false;
    }
    const auto encoded = QString::fromStdString(
        nlohmann::ordered_json(asset_id.toStdString()).dump());
    if (!view->model()->setData(value_index, encoded, Qt::EditRole)) return false;
    append_console(QStringLiteral("Assigned asset %1 to %2 Inspector target(s); validation remains all-or-nothing.")
        .arg(asset_id).arg(value_index.data(EditorRoles::entity_ids).toStringList().size()),
        QStringLiteral("Info"), QStringLiteral("Inspector"), {}, {}, {}, {}, {}, asset_id);
    return true;
}

void EditorWindow::show_inspector_context_menu_for(QTreeView* view, const QPoint& point)
{
    if (!scene_)
    {
        return;
    }
    auto index = view->indexAt(point);
    if (!index.isValid()) index = view->currentIndex();
    if (view == inspector_ && show_collection_context_menu(index, point)) return;
    while (index.parent().isValid()) index = index.parent();
    index = index.siblingAtColumn(0);
    const auto type_id = index.data(EditorRoles::component_type).toString();
    const auto* descriptor = metadata_.find(type_id.toStdString());
    QList<dragonpixel::core::uuid> entity_ids;
    for (const auto& id_text : index.data(EditorRoles::entity_ids).toStringList())
    {
        if (const auto id = dragonpixel::core::uuid::parse(id_text.toStdString())) entity_ids.push_back(*id);
    }
    if (type_id.isEmpty() || descriptor == nullptr || entity_ids.empty()) return;
    view->setCurrentIndex(index);

    QMenu menu{view};
    QAction* edit_source = nullptr;
    if (!descriptor->source_path.empty())
    {
        edit_source = menu.addAction(
            descriptor->language == dragonpixel::metadata::implementation_language::csharp
                ? QStringLiteral("Edit Script in Rider")
                : QStringLiteral("Edit Component Source in Rider"));
        edit_source->setObjectName(QStringLiteral("InspectorEditScriptInRider"));
        menu.addSeparator();
    }
    auto* reset = menu.addAction(QStringLiteral("Reset Component"));
    reset->setObjectName(QStringLiteral("InspectorResetComponent"));
    auto* move_up = menu.addAction(QStringLiteral("Move Up"));
    move_up->setObjectName(QStringLiteral("InspectorMoveComponentUp"));
    auto* move_down = menu.addAction(QStringLiteral("Move Down"));
    move_down->setObjectName(QStringLiteral("InspectorMoveComponentDown"));
    menu.addSeparator();
    auto* copy = menu.addAction(QStringLiteral("Copy Component"));
    copy->setObjectName(QStringLiteral("InspectorCopyComponent"));
    auto* paste = menu.addAction(QStringLiteral("Paste Component Values"));
    paste->setObjectName(QStringLiteral("InspectorPasteComponent"));
    menu.addSeparator();
    auto* diagnostics = menu.addAction(QStringLiteral("Show Diagnostics"));
    diagnostics->setObjectName(QStringLiteral("InspectorComponentDiagnostics"));
    auto* remove = menu.addAction(QStringLiteral("Remove Component"));
    remove->setObjectName(QStringLiteral("InspectorRemoveComponent"));
    reset->setEnabled(descriptor->resettable);
    remove->setEnabled(descriptor->removable);
    const auto clipboard_payload = nlohmann::ordered_json::parse(
        QApplication::clipboard()->text().toStdString(), nullptr, false);
    paste->setEnabled(clipboard_payload.is_object()
        && clipboard_payload.value("format", std::string{}) == "dpe.component-clipboard"
        && clipboard_payload.value("typeId", std::string{}) == descriptor->type_id);

    const auto* chosen = menu.exec(view->viewport()->mapToGlobal(point));
    if (chosen == nullptr) return;
    if (chosen == edit_source)
    {
        edit_project_source(QString::fromStdString(descriptor->source_path));
        return;
    }
    if (chosen == remove)
    {
        std::vector<dragonpixel::scene::command> commands;
        for (const auto& entity_id : entity_ids)
            commands.emplace_back(dragonpixel::scene::remove_component_command{entity_id, type_id.toStdString()});
        if (apply_authoring_transaction(std::move(commands), "Remove component"))
            after_scene_mutation(QStringLiteral("Component removed through command validation"), selected_entity_ids());
        return;
    }
    if (chosen == diagnostics)
    {
        append_console(QStringLiteral("%1 · %2 · schema v%3 · %4 properties · module %5")
            .arg(QString::fromStdString(descriptor->display_name), owner_text(descriptor->owner))
            .arg(descriptor->schema_version)
            .arg(descriptor->properties.size())
            .arg(descriptor->runtime_module_id.empty() ? QStringLiteral("built-in")
                : QString::fromStdString(descriptor->runtime_module_id)),
            QStringLiteral("Info"), QStringLiteral("Inspector"), type_id);
        return;
    }
    if (chosen == copy)
    {
        const auto* entity = scene_->find_entity(entity_ids.front());
        if (entity == nullptr) return;
        const auto component = std::find_if(entity->components.begin(), entity->components.end(), [&](const auto& value) {
            return value.type_id == descriptor->type_id;
        });
        if (component == entity->components.end()) return;
        QApplication::clipboard()->setText(QString::fromStdString(nlohmann::ordered_json{
            {"format", "dpe.component-clipboard"},
            {"typeId", component->type_id},
            {"schemaVersion", component->schema_version},
            {"enabled", component->enabled},
            {"properties", component->properties},
        }.dump()));
        statusBar()->showMessage(QStringLiteral("Component values copied"), 2000);
        return;
    }

    std::vector<dragonpixel::scene::command> commands;
    if (chosen == reset)
    {
        nlohmann::ordered_json defaults = nlohmann::ordered_json::object();
        for (const auto& property : descriptor->properties) defaults[property.property_id] = property_default(property);
        for (const auto& entity_id : entity_ids)
        {
            commands.emplace_back(dragonpixel::scene::upsert_component_command{entity_id, {
                descriptor->type_id, descriptor->schema_version, descriptor->owner, defaults,
                false, nlohmann::ordered_json::object(), true, descriptor->qualified_name}});
        }
    }
    else if (chosen == paste)
    {
        const auto properties = clipboard_payload.value("properties", nlohmann::ordered_json::object());
        for (const auto& entity_id : entity_ids)
        {
            for (const auto& property : descriptor->properties)
            {
                const auto found = properties.find(property.property_id);
                if (found != properties.end() && !property.read_only)
                {
                    commands.emplace_back(dragonpixel::scene::set_component_property_command{
                        entity_id, descriptor->type_id, property.property_id, *found});
                }
            }
        }
    }
    else if (chosen == move_up || chosen == move_down)
    {
        for (const auto& entity_id : entity_ids)
        {
            const auto* entity = scene_->find_entity(entity_id);
            if (entity == nullptr) continue;
            const auto component = std::find_if(entity->components.begin(), entity->components.end(), [&](const auto& value) {
                return value.type_id == descriptor->type_id;
            });
            if (component == entity->components.end()) continue;
            const auto source = static_cast<std::size_t>(std::distance(entity->components.begin(), component));
            const auto destination = chosen == move_up
                ? (source == 0 ? source : source - 1)
                : std::min(source + 1, entity->components.size() - 1);
            if (source != destination)
                commands.emplace_back(dragonpixel::scene::reorder_component_command{entity_id, descriptor->type_id, destination});
        }
    }
    if (!commands.empty() && apply_authoring_transaction(std::move(commands), "Inspector component card action"))
    {
        after_scene_mutation(QStringLiteral("Component card action completed through validation"), selected_entity_ids());
    }
}

bool EditorWindow::show_collection_context_menu(const QModelIndex& requested_index, const QPoint& point)
{
    if (!requested_index.isValid() || !scene_) return false;
    auto current = requested_index;
    QModelIndex collection;
    while (current.isValid())
    {
        const auto value_index = current.siblingAtColumn(1);
        const auto type = static_cast<dragonpixel::metadata::value_type>(
            value_index.data(EditorRoles::value_type).toInt());
        if (type == dragonpixel::metadata::value_type::list
            || type == dragonpixel::metadata::value_type::dictionary)
        {
            collection = value_index;
            break;
        }
        current = current.parent();
    }
    if (!collection.isValid()) return false;
    const auto type = static_cast<dragonpixel::metadata::value_type>(collection.data(EditorRoles::value_type).toInt());
    const auto root_property = collection.data(EditorRoles::property_id).toString();
    const auto collection_path = collection.data(EditorRoles::property_path).toStringList();
    const auto component_type = collection.data(EditorRoles::component_type).toString();
    const auto* descriptor = metadata_.find(component_type.toStdString());
    if (descriptor == nullptr || collection.data(EditorRoles::mixed_value).toBool()) return true;
    auto root_descriptor = std::find_if(descriptor->properties.begin(), descriptor->properties.end(), [&](const auto& value) {
        return value.property_id == root_property.toStdString();
    });
    if (root_descriptor == descriptor->properties.end() || !root_descriptor->shape
        || root_descriptor->shape->arguments.empty()) return true;

    auto value = nlohmann::ordered_json::parse(collection.data(Qt::EditRole).toString().toStdString(), nullptr, false);
    if (value.is_discarded()) return true;
    QMenu menu{inspector_};
    auto* add = menu.addAction(type == dragonpixel::metadata::value_type::list
        ? QStringLiteral("Add Element") : QStringLiteral("Add Entry…"));
    add->setObjectName(QStringLiteral("InspectorCollectionAdd"));
    auto* remove = menu.addAction(type == dragonpixel::metadata::value_type::list
        ? QStringLiteral("Remove Element") : QStringLiteral("Remove Entry"));
    remove->setObjectName(QStringLiteral("InspectorCollectionRemove"));
    QAction* move_up = nullptr;
    QAction* move_down = nullptr;
    if (type == dragonpixel::metadata::value_type::list)
    {
        move_up = menu.addAction(QStringLiteral("Move Element Up"));
        move_down = menu.addAction(QStringLiteral("Move Element Down"));
    }
    const auto requested_path = requested_index.siblingAtColumn(1).data(EditorRoles::property_path).toStringList();
    const auto nested = requested_path.size() > collection_path.size();
    remove->setEnabled(nested);
    if (move_up) move_up->setEnabled(nested);
    if (move_down) move_down->setEnabled(nested);
    const auto* chosen = menu.exec(inspector_->viewport()->mapToGlobal(point));
    if (chosen == nullptr) return true;

    const auto& element_shape = root_descriptor->shape->arguments.front();
    if (chosen == add)
    {
        if (type == dragonpixel::metadata::value_type::list)
        {
            value.push_back(element_shape.nullable ? nlohmann::ordered_json{nullptr} : default_value(element_shape.type));
        }
        else
        {
            bool accepted = false;
            const auto key = QInputDialog::getText(this, QStringLiteral("Add dictionary entry"),
                QStringLiteral("Unique string key"), QLineEdit::Normal, {}, &accepted).trimmed();
            if (!accepted || key.isEmpty() || value.contains(key.toStdString())) return true;
            value[key.toStdString()] = element_shape.nullable
                ? nlohmann::ordered_json{nullptr} : default_value(element_shape.type);
        }
    }
    else
    {
        if (!nested) return true;
        const auto segment = requested_path.at(collection_path.size());
        if (type == dragonpixel::metadata::value_type::list)
        {
            bool valid = false;
            const auto item_index = segment.toInt(&valid);
            if (!valid || item_index < 0 || item_index >= static_cast<int>(value.size())) return true;
            if (chosen == remove)
            {
                value.erase(value.begin() + item_index);
            }
            else
            {
                const auto destination = chosen == move_up ? item_index - 1 : item_index + 1;
                if (destination < 0 || destination >= static_cast<int>(value.size())) return true;
                std::swap(value[static_cast<std::size_t>(item_index)], value[static_cast<std::size_t>(destination)]);
            }
        }
        else if (chosen == remove)
        {
            value.erase(segment.toStdString());
        }
    }

    QList<dragonpixel::core::uuid> collection_targets;
    for (const auto& id_text : collection.data(EditorRoles::entity_ids).toStringList())
    {
        if (const auto id = dragonpixel::core::uuid::parse(id_text.toStdString()))
            collection_targets.push_back(*id);
    }
    std::vector<dragonpixel::scene::command> commands;
    for (const auto& id : collection_targets)
    {
        if (collection_path.isEmpty())
        {
            commands.emplace_back(dragonpixel::scene::set_component_property_command{
                id, component_type.toStdString(), root_property.toStdString(), value});
        }
        else
        {
            std::vector<std::string> path;
            for (const auto& segment : collection_path) path.push_back(segment.toStdString());
            commands.emplace_back(dragonpixel::scene::set_component_property_path_command{
                id, component_type.toStdString(), root_property.toStdString(), std::move(path), value});
        }
    }
    if (apply_authoring_transaction(std::move(commands), "Edit Inspector collection"))
    {
        after_scene_mutation(QStringLiteral("Collection edit completed through one validated transaction"), selected_entity_ids());
    }
    return true;
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
            else if (asset_type.contains(QStringLiteral("tilemap"))
                && component.type_id == dragonpixel::metadata::builtin_component_ids::tilemap_2d)
            {
                property = QStringLiteral("dpe.tilemap.asset");
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

void EditorWindow::instantiate_prefab(
    const QString& source_path,
    bool force_scene_root,
    const std::optional<dragonpixel::core::uuid>& explicit_parent)
{
    if (!scene_ || source_path.isEmpty())
    {
        return;
    }
    const auto selected = selected_entity_ids();
    const auto parent = explicit_parent
        ? explicit_parent
        : !force_scene_root && selected.size() == 1
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
    const auto selected_ids = selected_entity_ids();
    if (selected_ids.size() != 1)
    {
        append_console(QStringLiteral("Prefab creation requires exactly one locally owned root; descendants are included automatically and multi-root selections are rejected."),
            QStringLiteral("Warning"), QStringLiteral("Prefabs"));
        return;
    }
    const auto selected = std::optional<dragonpixel::core::uuid>{selected_ids.front()};
    const auto* entity = selected ? scene_->find_entity(*selected) : nullptr;
    if (!selected || entity == nullptr)
    {
        append_console(QStringLiteral("Select one locally owned GameObject before creating a prefab."),
            QStringLiteral("Warning"), QStringLiteral("Prefabs"));
        return;
    }
    if (prefab_service_.has_instance_for_entity(*selected))
    {
        append_console(QStringLiteral("Linked prefab content cannot become a new prefab source until it is unpacked into locally owned GameObjects."),
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
        {
            const auto asset_type = ProjectModel::asset_type(source).toLower();
            if (asset_type == QStringLiteral("input-map"))
            {
                edit_input_map();
                break;
            }
            if ((asset_type.contains(QStringLiteral("tilemap"))
                    || asset_type.contains(QStringLiteral("tileset"))
                    || asset_type.contains(QStringLiteral("tilepalette")))
                && project_index_.candidate)
            {
                const auto asset_id = ProjectModel::asset_id(source);
                const ProjectIndexEntry* map_entry = nullptr;
                const ProjectIndexEntry* set_entry = nullptr;
                const ProjectIndexEntry* palette_entry = nullptr;
                if (asset_type.contains(QStringLiteral("tilemap")))
                {
                    map_entry = project_index_.candidate->find_by_id(asset_id);
                    if (map_entry != nullptr && !map_entry->dependencies.isEmpty())
                    {
                        set_entry = project_index_.candidate->find_by_id(map_entry->dependencies.front());
                    }
                }
                else if (asset_type.contains(QStringLiteral("tilepalette")))
                {
                    palette_entry = project_index_.candidate->find_by_id(asset_id);
                    if (palette_entry != nullptr)
                    {
                        for (const auto& candidate : project_index_.candidate->entries)
                        {
                            const auto covers_palette = std::all_of(
                                palette_entry->dependencies.begin(), palette_entry->dependencies.end(),
                                [&](const auto& dependency) { return candidate.dependencies.contains(dependency); });
                            if (candidate.kind == ProjectIndexEntryKind::asset
                                && candidate.asset_type.contains(QStringLiteral("tilemap"), Qt::CaseInsensitive)
                                && covers_palette)
                            {
                                map_entry = &candidate;
                                break;
                            }
                        }
                    }
                }
                else
                {
                    set_entry = project_index_.candidate->find_by_id(asset_id);
                    for (const auto& candidate : project_index_.candidate->entries)
                    {
                        if (candidate.kind == ProjectIndexEntryKind::asset
                            && candidate.asset_type.contains(QStringLiteral("tilemap"), Qt::CaseInsensitive)
                            && candidate.dependencies.contains(asset_id))
                        {
                            map_entry = &candidate;
                            break;
                        }
                    }
                    if (map_entry == nullptr && set_entry != nullptr)
                    {
                        const auto suggested = QStringLiteral("%1 Map")
                            .arg(QFileInfo{set_entry->resolved_source_path}.completeBaseName());
                        static_cast<void>(prompt_create_tilemap_from_tileset(
                            asset_id, suggested));
                        break;
                    }
                }
                bool may_open = true;
                if (map_entry != nullptr && tile_document_service_->is_dirty()
                    && QFileInfo{tile_document_service_->tilemap_path()}.absoluteFilePath()
                        != QFileInfo{map_entry->resolved_source_path}.absoluteFilePath())
                {
                    const auto decision = unsaved_prompt_
                        ? unsaved_prompt_(QFileInfo{tile_document_service_->tilemap_path()}.fileName())
                        : UnsavedDecision::cancel;
                    may_open = decision == UnsavedDecision::discard
                        || (decision == UnsavedDecision::save && tile_document_service_->save());
                }
                if (!may_open)
                {
                    break;
                }
                QStringList tile_set_paths;
                QStringList texture_paths;
                if (map_entry != nullptr)
                {
                    for (const auto& dependency : map_entry->dependencies)
                    {
                        const auto* dependency_entry = project_index_.candidate->find_by_id(dependency);
                        if (dependency_entry != nullptr
                            && dependency_entry->asset_type.contains(QStringLiteral("tileset"), Qt::CaseInsensitive))
                        {
                            if (set_entry == nullptr) set_entry = dependency_entry;
                            tile_set_paths.push_back(dependency_entry->resolved_source_path);
                            texture_paths.push_back(tile_texture_path_for(dependency_entry));
                        }
                    }
                    if (palette_entry == nullptr)
                    {
                        for (const auto& candidate : project_index_.candidate->entries)
                        {
                            const auto covered = std::all_of(candidate.dependencies.begin(), candidate.dependencies.end(),
                                [&](const auto& dependency) { return map_entry->dependencies.contains(dependency); });
                            if (candidate.kind == ProjectIndexEntryKind::asset
                                && candidate.asset_type.contains(QStringLiteral("tilepalette"), Qt::CaseInsensitive)
                                && !candidate.dependencies.isEmpty() && covered)
                            {
                                palette_entry = &candidate;
                                break;
                            }
                        }
                    }
                }
                if (map_entry != nullptr && set_entry != nullptr
                    && tile_palette_->load_documents(
                        map_entry->resolved_source_path,
                        tile_set_paths,
                        texture_paths,
                        palette_entry ? palette_entry->resolved_source_path : QString{}))
                {
                    tile_palette_dock_->show();
                    tile_palette_dock_->raise();
                    append_console(QStringLiteral("Opened %1 with %2 in the Tile Palette")
                        .arg(map_entry->display_name, set_entry->display_name),
                        QStringLiteral("Info"), QStringLiteral("Tile Authoring"), map_entry->resolved_source_path,
                        {}, {}, {}, {}, map_entry->id, map_entry->resolved_source_path);
                }
                else
                {
                    append_console(QStringLiteral("A tilemap and its contained TileSet dependency are required."),
                        QStringLiteral("Warning"), QStringLiteral("Tile Authoring"));
                }
                break;
            }
            assign_selected_asset(source);
            break;
        }
        case ProjectItemKind::prefab:
            instantiate_prefab(ProjectModel::item_path(source));
            break;
        case ProjectItemKind::component_source:
            edit_project_source(ProjectModel::item_path(source));
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
    scene_view_dock_->setVisible(true);
    game_view_dock_->setVisible(true);
    tile_palette_dock_->setVisible(mode_2d);
    viewport_->set_view_mode(mode_2d ? AuthoringViewport::ViewMode::two_d : AuthoringViewport::ViewMode::three_d);
    update_tile_scene_edit_state();
    QSettings settings{editor_settings_path(), QSettings::IniFormat};
    settings.setValue(QStringLiteral("workspace/current"), workspace);
    statusBar()->showMessage(QStringLiteral("%1 workspace active").arg(workspace), 3000);
}

void EditorWindow::reset_workspace()
{
    const std::array docks{
        scene_view_dock_, game_view_dock_, scene_dock_, hierarchy_dock_,
        assets_dock_, inspector_dock_, tile_palette_dock_, console_dock_, onboarding_dock_,
        project_hub_dock_};
    for (auto* dock : docks)
    {
        removeDockWidget(dock);
        dock->setFloating(false);
    }
    for (const auto& inspector : additional_inspectors_)
    {
        removeDockWidget(inspector->dock);
        inspector->dock->setFloating(false);
    }

    addDockWidget(Qt::LeftDockWidgetArea, hierarchy_dock_);
    splitDockWidget(hierarchy_dock_, assets_dock_, Qt::Vertical);
    tabifyDockWidget(hierarchy_dock_, scene_dock_);

    addDockWidget(Qt::RightDockWidgetArea, scene_view_dock_);
    splitDockWidget(scene_view_dock_, inspector_dock_, Qt::Horizontal);
    for (const auto& inspector : additional_inspectors_)
    {
        addDockWidget(Qt::RightDockWidgetArea, inspector->dock);
        tabifyDockWidget(inspector_dock_, inspector->dock);
    }
    splitDockWidget(scene_view_dock_, console_dock_, Qt::Vertical);
    tabifyDockWidget(scene_view_dock_, game_view_dock_);
    tabifyDockWidget(scene_view_dock_, onboarding_dock_);
    tabifyDockWidget(scene_view_dock_, project_hub_dock_);
    tabifyDockWidget(console_dock_, tile_palette_dock_);

    resizeDocks({hierarchy_dock_, scene_view_dock_, inspector_dock_}, {280, 860, 320}, Qt::Horizontal);
    resizeDocks({hierarchy_dock_, assets_dock_}, {560, 260}, Qt::Vertical);
    resizeDocks({scene_view_dock_, console_dock_}, {620, 220}, Qt::Vertical);

    hierarchy_dock_->raise();
    scene_view_dock_->raise();
    console_dock_->raise();
    for (auto* dock : docks)
    {
        dock->show();
    }
    for (const auto& inspector : additional_inspectors_)
    {
        inspector->dock->show();
    }
    inspector_dock_->raise();
    if (scene_)
    {
        project_hub_dock_->hide();
    }
    else
    {
        show_project_hub();
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
    publish_global_selection(SelectionOrigin::command);
    inspect_selected_entities();
    update_global_selection_presentation();
    refresh_additional_inspectors();
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
        save_action_->setEnabled(loaded && (scene_->is_dirty()
            || (tile_document_service_ && tile_document_service_->is_dirty())));
    }
    if (new_scene_action_ != nullptr)
    {
        new_scene_action_->setEnabled(loaded && !project_manifest_path_.isEmpty());
    }
    if (save_scene_as_action_ != nullptr)
    {
        save_scene_as_action_->setEnabled(loaded && !project_manifest_path_.isEmpty());
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
    if (input_map_action_ != nullptr)
    {
        input_map_action_->setEnabled(project_input_map_.has_value());
    }
    if (import_tiled_tilemap_action_ != nullptr)
    {
        import_tiled_tilemap_action_->setEnabled(!project_manifest_path_.isEmpty());
    }
    if (input_settings_action_ != nullptr)
    {
        input_settings_action_->setEnabled(project_input_map_.has_value());
    }
    if (onboarding_add_square_ != nullptr)
    {
        onboarding_add_square_->setEnabled(loaded);
        onboarding_add_circle_->setEnabled(loaded);
        onboarding_add_component_->setEnabled(loaded && selected);
        onboarding_create_csharp_->setEnabled(loaded);
        onboarding_create_cpp_->setEnabled(loaded);
        onboarding_play_->setEnabled(loaded && !play_running_);
    }
}

void EditorWindow::update_window_title()
{
    if (!scene_)
    {
        setWindowTitle(QStringLiteral("Dragon Pixel Engine Editor \u2014 No Project"));
        return;
    }
    const auto dirty = scene_->is_dirty() || (tile_document_service_ && tile_document_service_->is_dirty());
    setWindowTitle(QStringLiteral("Dragon Pixel Engine Editor \u2014 %1%2")
        .arg(QString::fromStdString(scene_->name()), dirty ? QStringLiteral(" *") : QString{}));
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

std::optional<EditorWindow::TileSceneTarget> EditorWindow::tile_scene_target() const
{
    if (!scene_ || !tile_document_service_ || !tile_document_service_->tilemap()
        || !tile_document_service_->tileset())
    {
        return std::nullopt;
    }
    const auto selected = selected_entity_ids();
    const auto target_id = tile_palette_ != nullptr && tile_palette_->target_pinned()
            && pinned_tile_scene_target_
        ? pinned_tile_scene_target_
        : selected.size() == 1
            ? std::optional<dragonpixel::core::uuid>{selected.front()}
            : std::nullopt;
    if (!target_id) return std::nullopt;
    const auto* entity = scene_->find_entity(*target_id);
    if (entity == nullptr) return std::nullopt;
    const auto tilemap_component = std::find_if(
        entity->components.cbegin(), entity->components.cend(), [](const auto& component) {
            return component.enabled && !component.opaque
                && component.type_id
                    == dragonpixel::metadata::builtin_component_ids::tilemap_2d;
        });
    if (tilemap_component == entity->components.cend()) return std::nullopt;
    const auto asset = tilemap_component->properties.value(
        "dpe.tilemap.asset", std::string{});
    if (asset != tile_document_service_->tilemap()->asset_id.to_string())
    {
        return std::nullopt;
    }

    std::vector<const dragonpixel::scene::entity*> chain;
    const auto* current = entity;
    while (current != nullptr)
    {
        if (std::any_of(chain.cbegin(), chain.cend(), [&](const auto* ancestor) {
                return ancestor->id == current->id;
            }))
        {
            return std::nullopt;
        }
        chain.push_back(current);
        current = current->parent_id ? scene_->find_entity(*current->parent_id) : nullptr;
    }
    QMatrix4x4 local_to_world;
    for (auto iterator = chain.crbegin(); iterator != chain.crend(); ++iterator)
    {
        const auto* transform = editable_transform(**iterator);
        if (transform == nullptr) continue;
        QMatrix4x4 local;
        local.translate(json_vector(transform->properties.value(
            "dpe.transform.position",
            nlohmann::ordered_json{{"x", 0.0}, {"y", 0.0}, {"z", 0.0}})));
        local.rotate(json_quaternion(transform->properties.value(
            "dpe.transform.rotation",
            nlohmann::ordered_json{{"w", 1.0}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}})));
        local.scale(json_vector(transform->properties.value(
            "dpe.transform.scale",
            nlohmann::ordered_json{{"x", 1.0}, {"y", 1.0}, {"z", 1.0}}),
            QVector3D{1.0F, 1.0F, 1.0F}));
        local_to_world *= local;
    }
    bool invertible = false;
    const auto world_to_local = local_to_world.inverted(&invertible);
    const auto* tileset = tile_document_service_->tileset();
    if (!invertible || tileset->pixels_per_unit <= 0.0
        || tileset->cell_size.x <= 0 || tileset->cell_size.y <= 0)
    {
        return std::nullopt;
    }
    return TileSceneTarget{
        *target_id, local_to_world, world_to_local,
        static_cast<float>(tileset->cell_size.x / tileset->pixels_per_unit),
        static_cast<float>(tileset->cell_size.y / tileset->pixels_per_unit),
        tile_document_service_->tilemap()->grid};
}

QPoint EditorWindow::tile_cell_at(
    const QVector3D& world_position,
    const TileSceneTarget& target)
{
    const auto local = target.world_to_local.map(world_position);
    const auto cell = dragonpixel::tiles::unproject_cell(target.grid,
        {local.x() / target.cell_width, local.y() / target.cell_height});
    return {cell.x, cell.y};
}

void EditorWindow::update_tile_scene_overlay(
    const QPoint& cell,
    const TileSceneTarget& target)
{
    const auto origin = dragonpixel::tiles::project_cell(
        target.grid, {cell.x(), cell.y()});
    const auto next_x = dragonpixel::tiles::project_cell(
        target.grid, {cell.x() + 1, cell.y()});
    const auto next_y = dragonpixel::tiles::project_cell(
        target.grid, {cell.x(), cell.y() + 1});
    const QVector3D local_origin{
        static_cast<float>(origin.x) * target.cell_width,
        static_cast<float>(origin.y) * target.cell_height,
        0.0F};
    viewport_->set_tile_cell_overlay(
        target.local_to_world.map(local_origin),
        target.local_to_world.mapVector({
            static_cast<float>(next_x.x - origin.x) * target.cell_width,
            static_cast<float>(next_x.y - origin.y) * target.cell_height,
            0.0F}),
        target.local_to_world.mapVector({
            static_cast<float>(next_y.x - origin.x) * target.cell_width,
            static_cast<float>(next_y.y - origin.y) * target.cell_height,
            0.0F}));
}

void EditorWindow::update_tile_scene_edit_state()
{
    if (tile_palette_ != nullptr && tile_palette_->target_pinned())
    {
        if (!pinned_tile_scene_target_)
        {
            const auto selected = selected_entity_ids();
            if (selected.size() == 1) pinned_tile_scene_target_ = selected.front();
        }
    }
    else pinned_tile_scene_target_.reset();
    const auto target = tile_scene_target();
    const auto enabled = target.has_value() && !play_running_
        && viewport_->view_mode() == AuthoringViewport::ViewMode::two_d;
    if (tile_scene_stroke_active_
        && (!enabled || !tile_scene_stroke_target_
            || tile_scene_stroke_target_->entity_id != target->entity_id))
    {
        cancel_tile_scene_stroke();
    }
    viewport_->set_tile_edit_enabled(enabled);
    if (!enabled)
    {
        viewport_->clear_tile_cell_overlay();
        return;
    }
    update_tile_scene_overlay(tile_scene_last_cell_.value_or(QPoint{}), *target);
}

std::vector<dragonpixel::scene::command> EditorWindow::tile_object_placement_commands(
    const dragonpixel::core::uuid& entity_id,
    const TileSceneTarget& target,
    int layer,
    const QPoint& cell,
    const QString& source_kind,
    const QString& source) const
{
    if (tile_document_service_ == nullptr || tile_document_service_->tilemap() == nullptr
        || layer < 0
        || layer >= static_cast<int>(tile_document_service_->tilemap()->layers.size())) return {};
    const auto& map = *tile_document_service_->tilemap();
    const auto projected = dragonpixel::tiles::project_cell(
        target.grid, {cell.x(), cell.y()});
    dragonpixel::scene::component_record placement{
        std::string{dragonpixel::metadata::builtin_component_ids::tile_object_placement_2d},
        1,
        dragonpixel::metadata::runtime_owner::native,
        {
            {"dpe.tileobject.map", map.asset_id.to_string()},
            {"dpe.tileobject.layer",
                map.layers[static_cast<std::size_t>(layer)].layer_id.to_string()},
            {"dpe.tileobject.cell_x", cell.x()},
            {"dpe.tileobject.cell_y", cell.y()},
            {"dpe.tileobject.source_kind", source_kind.toStdString()},
            {"dpe.tileobject.source", source.toStdString()},
        },
        false,
        nlohmann::ordered_json::object(),
        true,
        "DragonPixel.Native.TileObjectPlacement2DComponent",
    };
    return {
        dragonpixel::scene::set_component_property_command{
            entity_id,
            std::string{dragonpixel::metadata::builtin_component_ids::transform},
            "dpe.transform.position",
            nlohmann::ordered_json{
                {"x", projected.x * target.cell_width},
                {"y", projected.y * target.cell_height},
                {"z", 0.0},
            }},
        dragonpixel::scene::upsert_component_command{entity_id, std::move(placement)},
    };
}

bool EditorWindow::place_tile_object(
    const TilePaletteWidget::ObjectBrushSource& source,
    const TileSceneTarget& target,
    int layer,
    const QPoint& cell)
{
    if (!scene_ || !project_index_.candidate || source.project_id.isEmpty()
        || source.project_id != project_index_.candidate->project_id)
    {
        append_console(QStringLiteral("GameObject Brush rejected a stale or cross-project source."),
            QStringLiteral("Warning"), QStringLiteral("Tile Authoring"));
        return false;
    }
    if (source.kind == TilePaletteWidget::ObjectBrushKind::prefab)
    {
        const auto relative_source = QDir{project_root_}.relativeFilePath(source.source_path);
        auto result = prefab_service_.instantiate(
            *scene_, source.source_path, target.entity_id,
            [this, &target, layer, &cell, relative_source](const auto& root_id) {
                return tile_object_placement_commands(root_id, target, layer, cell,
                    QStringLiteral("prefab"), QDir::fromNativeSeparators(relative_source));
            });
        for (const auto& diagnostic : result.diagnostics)
            append_console(diagnostic, QStringLiteral("Warning"), QStringLiteral("Tile Authoring"));
        if (!result.succeeded)
        {
            append_console(result.message, QStringLiteral("Warning"), QStringLiteral("Tile Authoring"));
            return false;
        }
        after_scene_mutation(result.message);
        return true;
    }

    if (source.scene_id != QString::fromStdString(scene_->id().to_string())
        || source.source_revision > static_cast<qint64>(command_revision_)
        || source.entity_ids.empty())
    {
        append_console(QStringLiteral("GameObject Brush rejected a stale or cross-scene selection."),
            QStringLiteral("Warning"), QStringLiteral("Tile Authoring"));
        return false;
    }
    std::vector<dragonpixel::scene::command> commands;
    for (const auto& root_id : source.entity_ids)
    {
        const auto* root = scene_->find_entity(root_id);
        if (root == nullptr || root_id == target.entity_id || editable_transform(*root) == nullptr)
        {
            append_console(QStringLiteral(
                "GameObject Brush source is missing, is the active target, or has no editable Transform."),
                QStringLiteral("Warning"), QStringLiteral("Tile Authoring"));
            return false;
        }
        std::vector<dragonpixel::scene::entity_id_remap> remaps;
        std::vector<dragonpixel::core::uuid> frontier{root_id};
        for (std::size_t index = 0; index < frontier.size(); ++index)
        {
            const auto current = frontier[index];
            remaps.push_back({current, dragonpixel::core::uuid::random_v4()});
            for (const auto& entity : scene_->entities())
                if (entity.parent_id == current) frontier.push_back(entity.id);
        }
        const auto duplicate_root_id = remaps.front().duplicate_id;
        auto placement = tile_object_placement_commands(
            duplicate_root_id, target, layer, cell, QStringLiteral("scene-object"),
            QString::fromStdString(root_id.to_string()));
        if (placement.size() != 2
            || !std::holds_alternative<dragonpixel::scene::set_component_property_command>(placement[0])
            || !std::holds_alternative<dragonpixel::scene::upsert_component_command>(placement[1]))
            return false;
        auto placed_transform = *editable_transform(*root);
        const auto& position = std::get<dragonpixel::scene::set_component_property_command>(
            placement[0]);
        placed_transform.properties[position.property_id] = position.value;
        auto marker = std::get<dragonpixel::scene::upsert_component_command>(
            std::move(placement[1])).component;
        commands.emplace_back(dragonpixel::scene::duplicate_subtree_command{
            root_id,
            std::move(remaps),
            target.entity_id,
            std::nullopt,
            root->name + " Tile Object",
            false,
            {std::move(placed_transform), std::move(marker)},
        });
    }
    if (!apply_authoring_transaction(std::move(commands), "Place GameObject Brush selection"))
    {
        append_console(QStringLiteral("GameObject Brush placement was rejected without scene mutation."),
            QStringLiteral("Warning"), QStringLiteral("Tile Authoring"));
        return false;
    }
    after_scene_mutation(QStringLiteral(
        "Placed %1 GameObject Brush root(s) with stable grid ownership metadata.")
            .arg(source.entity_ids.size()));
    return true;
}

bool EditorWindow::erase_tile_objects(
    const TileSceneTarget& target,
    int layer,
    const QPoint& cell)
{
    if (!scene_ || !tile_document_service_ || !tile_document_service_->tilemap()
        || layer < 0
        || layer >= static_cast<int>(tile_document_service_->tilemap()->layers.size())) return false;
    const auto map_id = tile_document_service_->tilemap()->asset_id.to_string();
    const auto layer_id = tile_document_service_->tilemap()
        ->layers[static_cast<std::size_t>(layer)].layer_id.to_string();
    std::vector<dragonpixel::scene::command> commands;
    for (const auto& entity : scene_->entities())
    {
        if (entity.parent_id != target.entity_id) continue;
        const auto marker = std::find_if(entity.components.begin(), entity.components.end(),
            [](const auto& component) {
                return !component.opaque
                    && component.type_id
                        == dragonpixel::metadata::builtin_component_ids::tile_object_placement_2d;
            });
        if (marker == entity.components.end()) continue;
        const auto& properties = marker->properties;
        if (properties.value("dpe.tileobject.map", std::string{}) == map_id
            && properties.value("dpe.tileobject.layer", std::string{}) == layer_id
            && properties.value("dpe.tileobject.cell_x", std::numeric_limits<int>::min()) == cell.x()
            && properties.value("dpe.tileobject.cell_y", std::numeric_limits<int>::min()) == cell.y())
            commands.emplace_back(dragonpixel::scene::delete_subtree_command{entity.id});
    }
    if (commands.empty()) return false;
    const auto count = commands.size();
    if (!apply_authoring_transaction(std::move(commands), "Erase GameObject Brush placements"))
    {
        append_console(QStringLiteral("GameObject Brush erase was rejected without scene mutation."),
            QStringLiteral("Warning"), QStringLiteral("Tile Authoring"));
        return false;
    }
    after_scene_mutation(QStringLiteral(
        "Erased %1 GameObject Brush root(s); unrelated scene objects were preserved.").arg(count));
    return true;
}

void EditorWindow::request_custom_tile_brush(const QPoint& cell)
{
    if (preview_worker_ == nullptr || tile_palette_ == nullptr
        || tile_document_service_ == nullptr || play_running_
        || !tile_palette_->custom_extension_brush_active()) return;
    if (!pending_custom_tile_brushes_.isEmpty())
    {
        append_console(
            QStringLiteral("A Custom Extension Brush proposal is already pending."),
            QStringLiteral("Info"),
            QStringLiteral("Tile Authoring"));
        return;
    }
    const auto* map = tile_document_service_->tilemap();
    const auto brush = tile_palette_->active_brush();
    const auto layer = tile_palette_->active_layer();
    if (map == nullptr || !brush || layer < 0
        || layer >= static_cast<int>(map->layers.size())) return;
    const dragonpixel::tiles::tile_definition* definition{};
    for (const auto& set : tile_document_service_->tilesets())
    {
        if (set.asset_id != brush->tile_set_id) continue;
        const auto tile = std::find_if(set.tiles.cbegin(), set.tiles.cend(),
            [&](const auto& candidate) { return candidate.tile_id == brush->tile_id; });
        if (tile != set.tiles.cend()) definition = &*tile;
        break;
    }
    if (definition == nullptr || definition->kind != dragonpixel::tiles::tile_kind::custom
        || definition->custom_type_id.empty()) return;

    const auto& active_layer = map->layers[static_cast<std::size_t>(layer)];
    QJsonArray neighbors;
    for (const auto offset : dragonpixel::tiles::neighbor_offsets(map->grid.layout))
    {
        QJsonObject neighbor{
            {QStringLiteral("x"), offset.x},
            {QStringLiteral("y"), offset.y},
        };
        if (const auto candidate = tile_document_service_->brush_at(
                layer, cell.x() + offset.x, cell.y() + offset.y))
        {
            neighbor.insert(QStringLiteral("tileSetId"),
                QString::fromStdString(candidate->tile_set_id.to_string()));
            neighbor.insert(QStringLiteral("tileId"),
                QString::fromStdString(candidate->tile_id.to_string()));
        }
        neighbors.push_back(neighbor);
    }
    const auto neighborhood_json = QString::fromUtf8(
        QJsonDocument{QJsonObject{{QStringLiteral("neighbors"), neighbors}}}
            .toJson(QJsonDocument::Compact));
    const auto seed = dragonpixel::tiles::stable_tile_seed(
        map->asset_id,
        active_layer.layer_id,
        {cell.x(), cell.y()},
        definition->custom_type_id);
    QJsonObject request{
        {QStringLiteral("kind"), QStringLiteral("stamp")},
        {QStringLiteral("origin"), QJsonObject{
            {QStringLiteral("x"), cell.x()},
            {QStringLiteral("y"), cell.y()},
        }},
        {QStringLiteral("brush"), QJsonObject{
            {QStringLiteral("flipX"), brush->flip_x},
            {QStringLiteral("flipY"), brush->flip_y},
            {QStringLiteral("rotationQuarterTurns"),
                static_cast<int>(brush->rotation_quarter_turns)},
            {QStringLiteral("elevation"), brush->elevation},
        }},
    };
    QJsonObject parameters{
        {QStringLiteral("pluginId"), QString::fromStdString(definition->custom_type_id)},
        {QStringLiteral("context"), QJsonObject{
            {QStringLiteral("cellX"), cell.x()},
            {QStringLiteral("cellY"), cell.y()},
            {QStringLiteral("elevation"), brush->elevation},
            {QStringLiteral("layout"), static_cast<int>(map->grid.layout)},
            {QStringLiteral("deterministicSeed"),
                QString::number(static_cast<qulonglong>(seed))},
            {QStringLiteral("elapsedSeconds"), 0.0},
            {QStringLiteral("mapId"), QString::fromStdString(map->asset_id.to_string())},
            {QStringLiteral("layerId"), QString::fromStdString(active_layer.layer_id.to_string())},
            {QStringLiteral("tileSetId"), QString::fromStdString(brush->tile_set_id.to_string())},
            {QStringLiteral("tileId"), QString::fromStdString(brush->tile_id.to_string())},
            {QStringLiteral("payloadJson"),
                QString::fromStdString(definition->opaque_payload_json)},
            {QStringLiteral("neighborhoodJson"), neighborhood_json},
        }},
        {QStringLiteral("request"), request},
    };
    const auto token = ++next_custom_tile_brush_token_;
    pending_custom_tile_brushes_.insert(token, {
        map->asset_id,
        active_layer.layer_id,
        layer,
        tile_document_revision_,
        *brush,
    });
    preview_worker_->propose_tile_brush(token, std::move(parameters));
}

void EditorWindow::apply_custom_tile_brush_proposal(
    quint64 request_token,
    const QJsonArray& commands)
{
    const auto found = pending_custom_tile_brushes_.find(request_token);
    if (found == pending_custom_tile_brushes_.end()) return;
    const auto pending = *found;
    pending_custom_tile_brushes_.erase(found);
    const auto* map = tile_document_service_ == nullptr
        ? nullptr : tile_document_service_->tilemap();
    if (map == nullptr || pending.document_revision != tile_document_revision_
        || map->asset_id != pending.map_id || pending.layer < 0
        || pending.layer >= static_cast<int>(map->layers.size())
        || map->layers[static_cast<std::size_t>(pending.layer)].layer_id
            != pending.layer_id)
    {
        append_console(
            QStringLiteral("Discarded a stale Custom Extension Brush proposal without mutation."),
            QStringLiteral("Warning"),
            QStringLiteral("Tile Authoring"));
        return;
    }
    if (commands.size() > 4096)
    {
        append_console(
            QStringLiteral("Rejected a Custom Extension Brush proposal above the 4096-command limit."),
            QStringLiteral("Warning"),
            QStringLiteral("Tile Authoring"));
        return;
    }
    struct ProposedPaint final
    {
        int x{};
        int y{};
        dragonpixel::core::uuid tile_set_id;
        dragonpixel::core::uuid tile_id;
    };
    std::vector<ProposedPaint> paints;
    paints.reserve(static_cast<std::size_t>(commands.size()));
    for (const auto& value : commands)
    {
        if (!value.isObject())
        {
            reject_custom_tile_brush_proposal(request_token,
                QStringLiteral("DPE-TILE-EXT-MALFORMED-PROPOSAL"),
                QStringLiteral("A proposed Tile brush command is not an object."));
            return;
        }
        const auto command = value.toObject();
        if (command.size() != 5 || command.value(QStringLiteral("kind")).toString()
                != QStringLiteral("paint")
            || !command.value(QStringLiteral("x")).isDouble()
            || !command.value(QStringLiteral("y")).isDouble())
        {
            reject_custom_tile_brush_proposal(request_token,
                QStringLiteral("DPE-TILE-EXT-MALFORMED-PROPOSAL"),
                QStringLiteral("A proposed Tile brush command has an invalid shape."));
            return;
        }
        const auto x64 = command.value(QStringLiteral("x")).toInteger(
            std::numeric_limits<qint64>::min());
        const auto y64 = command.value(QStringLiteral("y")).toInteger(
            std::numeric_limits<qint64>::min());
        const auto tile_set_text = command.value(QStringLiteral("tileSetId")).toString();
        const auto tile_text = command.value(QStringLiteral("tileId")).toString();
        const auto tile_set_id = dragonpixel::core::uuid::parse(tile_set_text.toStdString());
        const auto tile_id = dragonpixel::core::uuid::parse(tile_text.toStdString());
        if (x64 < std::numeric_limits<int>::min() || x64 > std::numeric_limits<int>::max()
            || y64 < std::numeric_limits<int>::min() || y64 > std::numeric_limits<int>::max()
            || !tile_set_id || !tile_id
            || tile_set_text != QString::fromStdString(tile_set_id->to_string())
            || tile_text != QString::fromStdString(tile_id->to_string()))
        {
            reject_custom_tile_brush_proposal(request_token,
                QStringLiteral("DPE-TILE-EXT-MALFORMED-PROPOSAL"),
                QStringLiteral("A proposed Tile brush command has invalid coordinates or IDs."));
            return;
        }
        const auto owner = std::find_if(
            tile_document_service_->tilesets().cbegin(),
            tile_document_service_->tilesets().cend(),
            [&](const auto& candidate) { return candidate.asset_id == *tile_set_id; });
        if (owner == tile_document_service_->tilesets().cend()
            || std::none_of(owner->tiles.cbegin(), owner->tiles.cend(),
                [&](const auto& candidate) { return candidate.tile_id == *tile_id; }))
        {
            reject_custom_tile_brush_proposal(request_token,
                QStringLiteral("DPE-TILE-EXT-UNRESOLVED-PROPOSAL"),
                QStringLiteral("A proposed Tile brush command references an unloaded tile."));
            return;
        }
        paints.push_back({static_cast<int>(x64), static_cast<int>(y64),
            *tile_set_id, *tile_id});
    }
    tile_document_service_->begin_stroke();
    bool changed{};
    for (const auto& paint : paints)
    {
        auto brush = pending.brush;
        brush.tile_set_id = paint.tile_set_id;
        brush.tile_id = paint.tile_id;
        changed = tile_document_service_->paint_cell(
            pending.layer, paint.x, paint.y, brush) || changed;
    }
    tile_document_service_->commit_stroke();
    append_console(
        changed
            ? QStringLiteral("Applied %1 Custom Extension Brush command(s) as one Undo transaction.")
                .arg(paints.size())
            : QStringLiteral("Custom Extension Brush proposal produced no authoring change."),
        QStringLiteral("Info"),
        QStringLiteral("Tile Authoring"));
}

void EditorWindow::reject_custom_tile_brush_proposal(
    quint64 request_token,
    const QString& error_code,
    const QString& error_message)
{
    pending_custom_tile_brushes_.remove(request_token);
    append_console(
        QStringLiteral("%1: %2").arg(error_code, error_message),
        QStringLiteral("Warning"),
        QStringLiteral("Tile Authoring"));
}

void EditorWindow::begin_tile_scene_stroke(const QVector3D& world_position)
{
    cancel_tile_scene_stroke();
    const auto target = tile_scene_target();
    if (!target || play_running_
        || viewport_->view_mode() != AuthoringViewport::ViewMode::two_d)
    {
        return;
    }
    const auto cell = tile_cell_at(world_position, *target);
    tile_scene_last_cell_ = cell;
    update_tile_scene_overlay(cell, *target);
    const auto layer = tile_palette_->active_layer();
    const auto tool = tile_palette_->active_tool();
    if (tool == TileCanvas::Tool::eyedropper)
    {
        if (const auto brush = tile_document_service_->brush_at(layer, cell.x(), cell.y()))
            tile_palette_->select_brush(*brush);
        return;
    }
    if (tool == TileCanvas::Tool::select) return;
    if (tool == TileCanvas::Tool::paint
        && tile_palette_->custom_extension_brush_active())
    {
        request_custom_tile_brush(cell);
        return;
    }
    if (const auto object_brush = tile_palette_->active_object_brush())
    {
        if (tool == TileCanvas::Tool::paint)
            static_cast<void>(place_tile_object(*object_brush, *target, layer, cell));
        else if (tool == TileCanvas::Tool::erase)
            static_cast<void>(erase_tile_objects(*target, layer, cell));
        return;
    }
    const auto brush = tile_palette_->active_brush_at(cell.x(), cell.y());
    if (tool != TileCanvas::Tool::erase && !brush) return;

    tile_document_service_->begin_stroke();
    tile_scene_stroke_active_ = true;
    tile_scene_stroke_start_ = cell;
    tile_scene_stroke_target_ = target;
    tile_scene_stroke_tool_ = tool;
    tile_scene_stroke_layer_ = layer;
    tile_scene_stroke_brush_ = brush;
    if (tool == TileCanvas::Tool::paint)
    {
        for (const auto& [target_cell, target_brush]
            : tile_palette_->active_brush_pattern_at(cell.x(), cell.y()))
            static_cast<void>(tile_document_service_->paint_cell(
                layer, target_cell.x(), target_cell.y(), target_brush));
    }
    else if (tool == TileCanvas::Tool::erase)
        static_cast<void>(tile_document_service_->erase_cell(
            layer, cell.x(), cell.y()));
    else if (tool == TileCanvas::Tool::rectangle)
        static_cast<void>(tile_document_service_->preview_rectangle(
            layer, cell.x(), cell.y(), cell.x(), cell.y(), brush->tile_id,
            false, brush->flip_x, brush->flip_y,
            brush->rotation_quarter_turns));
    else if (tool == TileCanvas::Tool::line)
        static_cast<void>(tile_document_service_->preview_line(
            layer, cell.x(), cell.y(), cell.x(), cell.y(), *brush));
    else if (tool == TileCanvas::Tool::fill)
    {
        static_cast<void>(tile_document_service_->flood_fill(
            layer, cell.x(), cell.y(), *brush));
        tile_document_service_->commit_stroke();
        tile_scene_stroke_active_ = false;
        tile_scene_stroke_start_.reset();
        tile_scene_stroke_target_.reset();
        tile_scene_stroke_brush_.reset();
    }
}

void EditorWindow::update_tile_scene_stroke(const QVector3D& world_position)
{
    if (!tile_scene_stroke_active_ || !tile_scene_stroke_target_) return;
    const auto cell = tile_cell_at(world_position, *tile_scene_stroke_target_);
    update_tile_scene_overlay(cell, *tile_scene_stroke_target_);
    if (tile_scene_last_cell_ == cell) return;
    const auto previous = *tile_scene_last_cell_;
    tile_scene_last_cell_ = cell;
    if (tile_scene_stroke_tool_ == TileCanvas::Tool::paint
        || tile_scene_stroke_tool_ == TileCanvas::Tool::erase)
    {
        const auto layout = tile_document_service_->tilemap()
            ? tile_document_service_->tilemap()->grid.layout
            : dragonpixel::tiles::grid_layout::rectangular;
        const auto cells = dragonpixel::tiles::grid_line(layout,
            {previous.x(), previous.y()}, {cell.x(), cell.y()});
        if (cells.size() > 4096U)
        {
            append_console(QStringLiteral("Tile stroke segment exceeded the 4096-cell safety limit."),
                QStringLiteral("Warning"), QStringLiteral("Tile Authoring"));
            return;
        }
        for (const auto& stroke_cell : cells)
        {
            const auto x = stroke_cell.x;
            const auto y = stroke_cell.y;
            if (tile_scene_stroke_tool_ == TileCanvas::Tool::paint
                && tile_scene_stroke_brush_)
            {
                for (const auto& [target_cell, target_brush]
                    : tile_palette_->active_brush_pattern_at(x, y))
                    static_cast<void>(tile_document_service_->paint_cell(
                        tile_scene_stroke_layer_, target_cell.x(), target_cell.y(), target_brush));
            }
            else if (tile_scene_stroke_tool_ == TileCanvas::Tool::erase)
                static_cast<void>(tile_document_service_->erase_cell(
                    tile_scene_stroke_layer_, x, y));
        }
    }
    else if (tile_scene_stroke_tool_ == TileCanvas::Tool::rectangle
        && tile_scene_stroke_brush_ && tile_scene_stroke_start_)
        static_cast<void>(tile_document_service_->preview_rectangle(
            tile_scene_stroke_layer_, tile_scene_stroke_start_->x(),
            tile_scene_stroke_start_->y(), cell.x(), cell.y(),
            tile_scene_stroke_brush_->tile_id, false,
            tile_scene_stroke_brush_->flip_x,
            tile_scene_stroke_brush_->flip_y,
            tile_scene_stroke_brush_->rotation_quarter_turns));
    else if (tile_scene_stroke_tool_ == TileCanvas::Tool::line
        && tile_scene_stroke_brush_ && tile_scene_stroke_start_)
        static_cast<void>(tile_document_service_->preview_line(
            tile_scene_stroke_layer_, tile_scene_stroke_start_->x(),
            tile_scene_stroke_start_->y(), cell.x(), cell.y(),
            *tile_scene_stroke_brush_));
}

void EditorWindow::end_tile_scene_stroke(const QVector3D& world_position)
{
    if (!tile_scene_stroke_active_) return;
    update_tile_scene_stroke(world_position);
    tile_document_service_->commit_stroke();
    tile_scene_stroke_active_ = false;
    tile_scene_stroke_start_.reset();
    tile_scene_stroke_target_.reset();
    tile_scene_stroke_brush_.reset();
    if (tile_preview_timer_) tile_preview_timer_->start();
}

void EditorWindow::cancel_tile_scene_stroke()
{
    if (tile_scene_stroke_active_)
    {
        tile_document_service_->cancel_stroke();
    }
    tile_scene_stroke_active_ = false;
    tile_scene_stroke_start_.reset();
    tile_scene_stroke_target_.reset();
    tile_scene_stroke_brush_.reset();
}

std::string EditorWindow::runtime_snapshot_json(const dragonpixel::scene::scene& source_scene) const
{
    auto root = nlohmann::ordered_json::parse(dragonpixel::serialization::write_scene_json(source_scene));
    root["snapshotFormatVersion"] = 5;
    auto assets = nlohmann::ordered_json::array();
    auto tile_sets = nlohmann::ordered_json::array();
    auto tilemaps = nlohmann::ordered_json::array();
    std::unordered_map<std::string, dragonpixel::tiles::tile_set_document> resolved_tile_sets;
    std::unordered_map<std::string, dragonpixel::tiles::tilemap_document> resolved_tilemaps;
    if (!project_index_.candidate)
    {
        root["assets"] = std::move(assets);
        root["tileSets"] = std::move(tile_sets);
        root["tilemaps"] = std::move(tilemaps);
        return root.dump(2) + "\n";
    }

    std::unordered_map<std::string, const ProjectIndexEntry*> assets_by_id;
    for (const auto& entry : project_index_.candidate->entries)
    {
        if (entry.kind == ProjectIndexEntryKind::asset && !entry.id.isEmpty())
            assets_by_id[entry.id.toStdString()] = &entry;
    }

    for (const auto& entry : project_index_.candidate->entries)
    {
        if (entry.kind != ProjectIndexEntryKind::asset
            || entry.asset_type.compare(QStringLiteral("sprite"), Qt::CaseInsensitive) != 0)
        {
            continue;
        }
        const auto binding = asset_service_.runtime_binding(project_manifest_path_, entry.id);
        if (!binding.succeeded)
        {
            continue;
        }
        assets.push_back({
            {"assetId", binding.asset_id.toStdString()},
            {"assetType", "sprite"},
            {"immutablePath", ""},
            {"contentHash", binding.content_hash.toStdString()},
            {"mediaType", binding.media_type.toStdString()},
            {"embeddedBytesBase64", binding.immutable_bytes.toBase64().toStdString()},
        });
    }

    for (const auto& entry : project_index_.candidate->entries)
    {
        if (entry.kind != ProjectIndexEntryKind::asset || entry.resolved_source_path.isEmpty()) continue;
        QFile file{entry.resolved_source_path};
        if (!file.open(QIODevice::ReadOnly)) continue;
        if (entry.asset_type.contains(QStringLiteral("tileset"), Qt::CaseInsensitive))
        {
            std::optional<dragonpixel::tiles::tile_set_document> current;
            if (tile_document_service_)
            {
                const auto open = std::find_if(tile_document_service_->tilesets().cbegin(),
                    tile_document_service_->tilesets().cend(), [&](const auto& candidate) {
                        return candidate.asset_id.to_string() == entry.id.toStdString();
                    });
                if (open != tile_document_service_->tilesets().cend()) current = *open;
            }
            if (!current)
            {
                const auto parsed = dragonpixel::tiles::read_tile_set(file.readAll().toStdString());
                if (parsed.succeeded()) current = *parsed.document;
            }
            if (!current) continue;
            resolved_tile_sets[current->asset_id.to_string()] = *current;
            auto snapshot = nlohmann::ordered_json::parse(
                dragonpixel::tiles::write_tile_set(*current));
            snapshot["cellWidth"] = current->cell_size.x;
            snapshot["cellHeight"] = current->cell_size.y;
            auto texture_bytes = nlohmann::ordered_json::object();
            for (const auto& texture_id : current->texture_asset_ids)
            {
                const auto texture_entry = assets_by_id.find(texture_id.to_string());
                if (texture_entry == assets_by_id.end()
                    || QFileInfo{texture_entry->second->resolved_source_path}.suffix().compare(
                        QStringLiteral("png"), Qt::CaseInsensitive) != 0)
                {
                    continue;
                }
                QFile texture{texture_entry->second->resolved_source_path};
                if (texture.open(QIODevice::ReadOnly))
                    texture_bytes[texture_id.to_string()] = texture.readAll().toBase64().toStdString();
            }
            const auto primary_texture = current->texture_asset_id.to_string();
            snapshot["texturePngBase64"] = texture_bytes.value(primary_texture, std::string{});
            snapshot["texturePngBase64ByAssetId"] = std::move(texture_bytes);
            for (auto& tile : snapshot["tiles"])
            {
                const auto source = tile.value("source", nlohmann::ordered_json::object());
                tile["sourceX"] = source.value("x", 0);
                tile["sourceY"] = source.value("y", 0);
                tile["sourceWidth"] = source.value("width", 1);
                tile["sourceHeight"] = source.value("height", 1);
            }
            tile_sets.push_back(std::move(snapshot));
        }
        else if (entry.asset_type.contains(QStringLiteral("tilemap"), Qt::CaseInsensitive))
        {
            std::optional<dragonpixel::tiles::tilemap_document> current;
            if (tile_document_service_ && tile_document_service_->tilemap()
                && tile_document_service_->tilemap()->asset_id.to_string() == entry.id.toStdString())
            {
                current = *tile_document_service_->tilemap();
            }
            else
            {
                const auto parsed = dragonpixel::tiles::read_tilemap(file.readAll().toStdString());
                if (parsed.succeeded()) current = *parsed.document;
            }
            if (!current) continue;
            resolved_tilemaps[current->asset_id.to_string()] = *current;
            const auto serialized = nlohmann::ordered_json::parse(
                dragonpixel::tiles::write_tilemap(*current));
            const auto definition_for = [&](dragonpixel::tiles::tile_reference reference)
                -> const dragonpixel::tiles::tile_definition* {
                const auto set = resolved_tile_sets.find(reference.tile_set_id.to_string());
                if (set == resolved_tile_sets.end()) return nullptr;
                const auto tile = std::find_if(set->second.tiles.cbegin(), set->second.tiles.cend(),
                    [&](const auto& candidate) { return candidate.tile_id == reference.tile_id; });
                return tile == set->second.tiles.cend() ? nullptr : &*tile;
            };
            auto layers = nlohmann::ordered_json::array();
            for (const auto& layer : current->layers)
            {
                std::unordered_map<std::uint64_t, dragonpixel::tiles::tile_reference> occupied;
                const auto cell_key = [](int x, int y) {
                    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32U)
                        | static_cast<std::uint32_t>(y);
                };
                for (const auto& chunk : layer.chunks)
                {
                    for (const auto& cell : chunk.cells)
                    {
                        const auto x = (chunk.x * 32) + static_cast<int>(cell.index % 32U);
                        const auto y = (chunk.y * 32) + static_cast<int>(cell.index / 32U);
                        occupied[cell_key(x, y)] = {
                            cell.tile_set_id.is_nil() ? current->tile_set_dependencies.front() : cell.tile_set_id,
                            cell.tile_id};
                    }
                }
                auto cells = nlohmann::ordered_json::array();
                for (const auto& chunk : layer.chunks)
                {
                    for (const auto& cell : chunk.cells)
                    {
                        const auto x = (chunk.x * 32) + static_cast<int>(cell.index % 32U);
                        const auto y = (chunk.y * 32) + static_cast<int>(cell.index / 32U);
                        const dragonpixel::tiles::tile_reference reference{
                            cell.tile_set_id.is_nil()
                                ? current->tile_set_dependencies.front() : cell.tile_set_id,
                            cell.tile_id};
                        auto resolved = reference;
                        std::optional<dragonpixel::tiles::sprite_reference> resolved_sprite;
                        auto placeholder = false;
                        if (const auto* definition = definition_for(reference))
                        {
                            const auto evaluation = dragonpixel::tiles::evaluate_tile(
                                *definition, reference, current->grid.layout, {x, y}, 0.0,
                                dragonpixel::tiles::stable_tile_seed(
                                    current->asset_id, layer.layer_id, {x, y}, "runtime-rule"),
                                [&](dragonpixel::tiles::integer_point neighbor)
                                    -> std::optional<dragonpixel::tiles::tile_reference> {
                                    const auto found = occupied.find(cell_key(neighbor.x, neighbor.y));
                                    return found == occupied.end() ? std::nullopt
                                        : std::optional{found->second};
                                }, definition_for);
                            resolved = evaluation.output;
                            resolved_sprite = evaluation.sprite;
                            placeholder = evaluation.placeholder;
                        }
                        nlohmann::ordered_json cell_value{
                            {"x", x}, {"y", y},
                            {"tileSetId", reference.tile_set_id.to_string()},
                            {"tileId", cell.tile_id.to_string()},
                            {"resolvedTileSetId", resolved.tile_set_id.to_string()},
                            {"resolvedTileId", resolved.tile_id.to_string()},
                            {"placeholder", placeholder},
                            {"flipX", cell.flip_x},
                            {"flipY", cell.flip_y},
                            {"rotationQuarterTurns", cell.rotation_quarter_turns},
                            {"tint", {{"r", cell.tint.red}, {"g", cell.tint.green},
                                {"b", cell.tint.blue}, {"a", cell.tint.alpha}}},
                            {"offset", {{"x", cell.offset.x}, {"y", cell.offset.y}}},
                            {"rotationDegrees", cell.rotation_degrees},
                            {"scale", {{"x", cell.scale.x}, {"y", cell.scale.y}}},
                            {"elevation", cell.elevation},
                            {"lockColor", cell.lock_color},
                            {"lockTransform", cell.lock_transform},
                        };
                        if (resolved_sprite)
                        {
                            cell_value["resolvedSprite"] = {
                                {"textureAssetId", resolved_sprite->texture_asset_id.to_string()},
                                {"source", {{"x", resolved_sprite->source.x}, {"y", resolved_sprite->source.y},
                                    {"width", resolved_sprite->source.width},
                                    {"height", resolved_sprite->source.height}}},
                            };
                        }
                        cells.push_back(std::move(cell_value));
                    }
                }
                layers.push_back({
                    {"layerId", layer.layer_id.to_string()},
                    {"name", layer.name},
                    {"visible", layer.visible},
                    {"order", layer.order},
                    {"renderer", {
                        {"tint", {{"r", layer.tint.red}, {"g", layer.tint.green},
                            {"b", layer.tint.blue}, {"a", layer.tint.alpha}}},
                        {"materialAssetId", layer.material_asset_id.is_nil()
                            ? nlohmann::ordered_json(nullptr)
                            : nlohmann::ordered_json(layer.material_asset_id.to_string())},
                        {"sortOrder", layer.sort_order},
                        {"mode", layer.renderer_mode == dragonpixel::tiles::tile_renderer_mode::individual
                            ? "individual" : "chunk"},
                        {"animationRate", layer.animation_rate},
                        {"cullingPadding", {{"x", layer.culling_padding.x},
                            {"y", layer.culling_padding.y}}},
                    }},
                    {"cells", std::move(cells)},
                });
            }
            tilemaps.push_back({
                {"assetId", current->asset_id.to_string()},
                {"tileSetDependencies", [&] {
                    auto dependencies = nlohmann::ordered_json::array();
                    for (const auto& dependency : current->tile_set_dependencies)
                        dependencies.push_back(dependency.to_string());
                    return dependencies;
                }()},
                {"grid", serialized.at("grid")},
                {"layers", std::move(layers)},
            });
        }
    }
    root["assets"] = std::move(assets);
    root["tileSets"] = std::move(tile_sets);
    root["tilemaps"] = std::move(tilemaps);
    struct collider_settings final
    {
        bool enabled{};
        bool sensor{};
        bool composite{};
        double friction{0.5};
        double restitution{};
        int layer{};
        int mask{65535};
    };
    struct pending_collider final
    {
        std::string stable_key;
        std::string name;
        double x{};
        double y{};
        double rotation_radians{};
        double width{1.0};
        double height{1.0};
        std::vector<dragonpixel::tiles::double_point> points;
        std::uint32_t sibling_order{};
    };
    auto generated_colliders = nlohmann::ordered_json::array();
    const auto original_entities = root.value("entities", nlohmann::ordered_json::array());
    for (const auto& entity : original_entities)
    {
        if (!entity.value("enabled", true)) continue;
        std::string map_id;
        collider_settings settings;
        for (const auto& component : entity.value("components", nlohmann::ordered_json::array()))
        {
            if (!component.value("enabled", true)) continue;
            const auto type_id = component.value("typeId", std::string{});
            const auto properties = component.value("properties", nlohmann::ordered_json::object());
            if (type_id == dragonpixel::metadata::builtin_component_ids::tilemap_2d)
            {
                map_id = properties.value("dpe.tilemap.asset", std::string{});
            }
            else if (type_id == dragonpixel::metadata::builtin_component_ids::tilemap_collider_2d)
            {
                settings.enabled = true;
                settings.sensor = properties.value("dpe.tilemap.collider.sensor", false);
                settings.composite = properties.value("dpe.tilemap.collider.composite", false);
                settings.friction = properties.value("dpe.tilemap.collider.friction", 0.5);
                settings.restitution = properties.value("dpe.tilemap.collider.restitution", 0.0);
                settings.layer = std::clamp(properties.value("dpe.tilemap.collider.layer", 0), 0, 15);
                settings.mask = std::clamp(properties.value("dpe.tilemap.collider.mask", 65535), 0, 65535);
            }
        }
        const auto map = resolved_tilemaps.find(map_id);
        if (!settings.enabled || map == resolved_tilemaps.end()
            || map->second.tile_set_dependencies.empty()) continue;

        const auto append_generated = [&](const pending_collider& collider) {
            auto collider_properties = nlohmann::ordered_json{
                {"dpe.physics2d.offset", {{"x", 0.0}, {"y", 0.0}}},
                {"dpe.physics.sensor", settings.sensor},
                {"dpe.physics.density", 1.0},
                {"dpe.physics.friction", settings.friction},
                {"dpe.physics.restitution", settings.restitution},
                {"dpe.physics.layer", settings.layer},
                {"dpe.physics.mask", settings.mask},
            };
            std::string collider_type;
            std::string qualified_name;
            if (collider.points.empty())
            {
                collider_type = dragonpixel::metadata::builtin_component_ids::box_collider_2d;
                qualified_name = "DragonPixel.Native.BoxCollider2DComponent";
                collider_properties["dpe.physics2d.size"] = {
                    {"x", collider.width}, {"y", collider.height}};
            }
            else
            {
                collider_type = dragonpixel::metadata::builtin_component_ids::polygon_collider_2d;
                qualified_name = "DragonPixel.Native.PolygonCollider2DComponent";
                auto points = nlohmann::ordered_json::array();
                for (const auto point : collider.points)
                    points.push_back({{"x", point.x}, {"y", point.y}});
                collider_properties["dpe.physics2d.points"] = std::move(points);
            }
            const auto generated_id = stable_runtime_uuid(QByteArray::fromStdString(
                entity.value("id", std::string{}) + ":" + collider.stable_key));
            generated_colliders.push_back({
                {"id", generated_id}, {"name", collider.name},
                {"parentId", entity.value("id", std::string{})},
                {"siblingOrder", collider.sibling_order}, {"enabled", true},
                {"runtimeGenerated", true},
                {"components", nlohmann::ordered_json::array({
                    {
                        {"typeId", std::string{dragonpixel::metadata::builtin_component_ids::transform}},
                        {"qualifiedName", "DragonPixel.Native.TransformComponent"},
                        {"schemaVersion", 2}, {"owner", "native"}, {"enabled", true},
                        {"properties", {
                            {"dpe.transform.position", {{"x", collider.x}, {"y", collider.y}, {"z", 0.0}}},
                            {"dpe.transform.rotation", {
                                {"w", std::cos(collider.rotation_radians * 0.5)},
                                {"x", 0.0}, {"y", 0.0},
                                {"z", std::sin(collider.rotation_radians * 0.5)}}},
                            {"dpe.transform.scale", {{"x", 1.0}, {"y", 1.0}, {"z", 1.0}}},
                        }},
                    },
                    {
                        {"typeId", std::string{dragonpixel::metadata::builtin_component_ids::rigid_body_2d}},
                        {"qualifiedName", "DragonPixel.Native.RigidBody2DComponent"},
                        {"schemaVersion", 1}, {"owner", "native"}, {"enabled", true},
                        {"properties", {{"dpe.physics2d.body_mode", "static"}}},
                    },
                    {
                        {"typeId", collider_type}, {"qualifiedName", qualified_name},
                        {"schemaVersion", 1}, {"owner", "native"}, {"enabled", true},
                        {"properties", std::move(collider_properties)},
                    },
                })},
            });
        };

        for (const auto& tile_layer : map->second.layers)
        {
            if (!tile_layer.visible) continue;
            std::vector<pending_collider> pending;
            std::set<std::pair<int, int>> composite_cells;
            for (const auto& chunk : tile_layer.chunks)
            {
                for (const auto& cell : chunk.cells)
                {
                    const auto set_id = cell.tile_set_id.is_nil()
                        ? map->second.tile_set_dependencies.front() : cell.tile_set_id;
                    const auto set = resolved_tile_sets.find(set_id.to_string());
                    if (set == resolved_tile_sets.end()) continue;
                    const auto tile = std::find_if(set->second.tiles.begin(), set->second.tiles.end(),
                        [&](const auto& value) { return value.tile_id == cell.tile_id; });
                    if (tile == set->second.tiles.end()
                        || (tile->collider_mode == dragonpixel::tiles::tile_collider_mode::none
                            && !tile->collision)) continue;
                    const auto cell_x = (chunk.x * 32) + static_cast<int>(cell.index % 32U);
                    const auto cell_y = (chunk.y * 32) + static_cast<int>(cell.index / 32U);
                    const auto projected = dragonpixel::tiles::project_cell(
                        map->second.grid, {cell_x, cell_y}, cell.elevation);
                    const auto rotation_radians = (cell.rotation_degrees
                        + static_cast<double>(cell.rotation_quarter_turns) * 90.0)
                        * std::numbers::pi / 180.0;
                    const auto base_key = tile_layer.layer_id.to_string() + ":"
                        + std::to_string(cell_x) + ":" + std::to_string(cell_y);
                    const auto base_name = std::string{"Generated Tile Collider "}
                        + std::to_string(cell_x) + "," + std::to_string(cell_y);
                    const auto scale_x = std::abs(cell.scale.x);
                    const auto scale_y = std::abs(cell.scale.y);
                    if (tile->collision)
                    {
                        pending.push_back({base_key, base_name,
                            projected.x + cell.offset.x + tile->collision->offset_x,
                            projected.y + cell.offset.y + tile->collision->offset_y,
                            rotation_radians, tile->collision->width * scale_x,
                            tile->collision->height * scale_y, {}, cell.index});
                        continue;
                    }
                    if (tile->collider_mode == dragonpixel::tiles::tile_collider_mode::sprite_outline)
                    {
                        std::vector<dragonpixel::tiles::double_point> local_outline;
                        local_outline.reserve(tile->collision_outline.size());
                        for (const auto point : tile->collision_outline)
                        {
                            auto x = (point.x - tile->pivot.x) * map->second.grid.cell_size.x * scale_x;
                            auto y = (point.y - tile->pivot.y) * map->second.grid.cell_size.y * scale_y;
                            if (cell.flip_x) x = -x;
                            if (cell.flip_y) y = -y;
                            local_outline.push_back({x, y});
                        }
                        const auto triangles = dragonpixel::tiles::triangulate_polygon(local_outline);
                        for (std::size_t index = 0; index < triangles.size(); ++index)
                        {
                            pending_collider collider;
                            collider.stable_key = base_key + ":outline:" + std::to_string(index);
                            collider.name = base_name + " Outline " + std::to_string(index + 1);
                            collider.x = projected.x + cell.offset.x
                                + (map->second.grid.tile_anchor.x - 0.5)
                                    * map->second.grid.cell_size.x;
                            collider.y = projected.y + cell.offset.y
                                + (map->second.grid.tile_anchor.y - 0.5)
                                    * map->second.grid.cell_size.y;
                            collider.rotation_radians = rotation_radians;
                            collider.points.assign(triangles[index].begin(), triangles[index].end());
                            collider.sibling_order = cell.index;
                            pending.push_back(std::move(collider));
                        }
                        continue;
                    }
                    const auto can_composite = settings.composite
                        && map->second.grid.layout == dragonpixel::tiles::grid_layout::rectangular
                        && map->second.grid.cell_gap == dragonpixel::tiles::double_point{}
                        && cell.offset == dragonpixel::tiles::double_point{}
                        && cell.scale == dragonpixel::tiles::double_point{1.0, 1.0}
                        && cell.rotation_quarter_turns == 0 && cell.rotation_degrees == 0.0;
                    if (can_composite)
                    {
                        composite_cells.emplace(cell_x, cell_y);
                        continue;
                    }
                    if (map->second.grid.layout == dragonpixel::tiles::grid_layout::rectangular)
                    {
                        pending.push_back({base_key, base_name,
                            projected.x + cell.offset.x, projected.y + cell.offset.y,
                            rotation_radians, map->second.grid.cell_size.x * scale_x,
                            map->second.grid.cell_size.y * scale_y, {}, cell.index});
                        continue;
                    }
                    auto footprint = dragonpixel::tiles::grid_collision_polygon(
                        map->second.grid, {cell_x, cell_y}, cell.elevation);
                    for (auto& point : footprint)
                    {
                        point.x = (point.x - projected.x) * scale_x;
                        point.y = (point.y - projected.y) * scale_y;
                    }
                    pending.push_back({base_key, base_name,
                        projected.x + cell.offset.x, projected.y + cell.offset.y,
                        rotation_radians, 1.0, 1.0, std::move(footprint), cell.index});
                }
            }

            while (!composite_cells.empty())
            {
                const auto [minimum_x, minimum_y] = *composite_cells.begin();
                auto maximum_x = minimum_x;
                while (composite_cells.contains({maximum_x + 1, minimum_y})) ++maximum_x;
                auto maximum_y = minimum_y;
                for (;;)
                {
                    const auto next_y = maximum_y + 1;
                    auto complete_row = true;
                    for (auto x = minimum_x; x <= maximum_x; ++x)
                        complete_row = complete_row && composite_cells.contains({x, next_y});
                    if (!complete_row) break;
                    maximum_y = next_y;
                }
                for (auto x = minimum_x; x <= maximum_x; ++x)
                    for (auto y = minimum_y; y <= maximum_y; ++y)
                        composite_cells.erase({x, y});
                const auto first = dragonpixel::tiles::project_cell(
                    map->second.grid, {minimum_x, minimum_y});
                const auto last = dragonpixel::tiles::project_cell(
                    map->second.grid, {maximum_x, maximum_y});
                const auto key = tile_layer.layer_id.to_string() + ":composite:"
                    + std::to_string(minimum_x) + ":" + std::to_string(minimum_y) + ":"
                    + std::to_string(maximum_x) + ":" + std::to_string(maximum_y);
                pending.push_back({key, "Generated Composite Tile Collider",
                    (first.x + last.x) * 0.5, (first.y + last.y) * 0.5, 0.0,
                    static_cast<double>(maximum_x - minimum_x + 1)
                        * map->second.grid.cell_size.x,
                    static_cast<double>(maximum_y - minimum_y + 1)
                        * map->second.grid.cell_size.y, {}, 0});
            }
            std::sort(pending.begin(), pending.end(), [](const auto& left, const auto& right) {
                return left.stable_key < right.stable_key;
            });
            for (const auto& collider : pending) append_generated(collider);
        }
    }
    for (auto& generated : generated_colliders)
        root["entities"].push_back(std::move(generated));
    return root.dump(2) + "\n";
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
        runtime_snapshot_json(preview_scene));
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
    if (!scene_ || !runtime_directory_.isValid() || preview_worker_ == nullptr
        || game_preview_worker_ == nullptr || adapter_ == nullptr)
    {
        return;
    }
    const auto snapshot_path = runtime_directory_.filePath(QStringLiteral("preview-mirror.dpescene"));
    const auto json = runtime_snapshot_json(*scene_);
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
    if (game_preview_worker_->has_frame() && game_preview_worker_->adapter_name() == selected_adapter)
    {
        game_preview_worker_->reload_snapshot(snapshot_path);
    }
    else
    {
        game_viewport_->clear_preview_frame();
        game_preview_worker_->start_session(selected_adapter, snapshot_path);
        game_preview_worker_->resize_viewport(game_viewport_->size());
        append_console(QStringLiteral("Live Game preview launched with the primary scene camera"));
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
    const auto tools_root = dragonpixel::editor::runtime_paths::directory(
        "DPE_PYTHON_TOOLS_ROOT",
        QStringLiteral("tools/python"),
        QString::fromUtf8(DPE_PYTHON_TOOLS_ROOT));
    const auto existing_python_path = environment.value(QStringLiteral("PYTHONPATH"));
    environment.insert(
        QStringLiteral("PYTHONPATH"),
        existing_python_path.isEmpty()
            ? tools_root
            : tools_root + QDir::listSeparator() + existing_python_path);
    automation_test_process_->setProcessEnvironment(environment);
    automation_test_process_->setProgram(dragonpixel::editor::runtime_paths::executable(
        "DPE_PYTHON_EXECUTABLE", QString::fromUtf8(DPE_PYTHON_EXECUTABLE)));
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
    const auto json = runtime_snapshot_json(*scene_);
    const auto result = dragonpixel::serialization::save_utf8_atomic(filesystem_path(snapshot_path), json);
    if (!result.succeeded)
    {
        append_console(QStringLiteral("Could not create immutable play snapshot: %1")
            .arg(QString::fromStdString(result.error)));
        return;
    }
    game_viewport_->set_play_mode(true);
    game_viewport_->set_runtime_input_ready(false);
    onboarding_dock_->hide();
    project_hub_dock_->hide();
    game_view_dock_->show();
    game_view_dock_->raise();
    play_running_ = true;
    play_paused_ = false;
    update_tile_scene_edit_state();
    update_action_states();
    play_worker_->start_session(adapter_->currentData().toString(), snapshot_path);
    if (!play_running_)
    {
        return;
    }
    append_console(QStringLiteral("Play launched from immutable snapshot; saved scene remains editor-owned"));
}

void EditorWindow::stop_play()
{
    if (game_viewport_ != nullptr)
    {
        game_viewport_->set_play_mode(false);
    }
    if (play_worker_ != nullptr)
    {
        play_worker_->stop_and_discard();
    }
    play_running_ = false;
    play_paused_ = false;
    update_tile_scene_edit_state();
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
        settings.setValue(QStringLiteral("window/state"), saveState(workspace_state_version));
        settings.setValue(QStringLiteral("inspector/count"), static_cast<int>(additional_inspectors_.size()) + 1);
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
        && project_asset_count() >= 2 && preview_worker_->has_frame()
        && game_preview_worker_->has_frame() && play_worker_->has_frame()
        && preview_worker_->process_id() > 0 && game_preview_worker_->process_id() > 0
        && play_worker_->process_id() > 0
        && preview_worker_->process_id() != game_preview_worker_->process_id()
        && preview_worker_->process_id() != play_worker_->process_id()
        && game_preview_worker_->process_id() != play_worker_->process_id();
}

QString EditorWindow::self_test_diagnostics() const
{
    return QStringLiteral(
        "authoring=%1 automation=%2 scene=%3 entities=%4 metadata=%5 hierarchy=%6 inspector=%7 assets=%8 "
        "previewFrame=%9 previewPid=%10 gameFrame=%11 gamePid=%12 playFrame=%13 playPid=%14 project=%15")
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
        .arg(game_preview_worker_->has_frame())
        .arg(game_preview_worker_->process_id())
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
        {QStringLiteral("*.dpeasset"),
         QStringLiteral("*.dpeinputmap"),
         QStringLiteral("*.dpetileset"),
         QStringLiteral("*.dpetilemap"),
         QStringLiteral("*.png")},
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
