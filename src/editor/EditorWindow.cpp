#include "EditorWindow.h"

#include <dragonpixel/scene/commands.h>
#include <dragonpixel/metadata/builtin_ids.h>
#include <dragonpixel/serialization/atomic_file.h>
#include <dragonpixel/serialization/scene_json.h>

#include <nlohmann/json.hpp>

#include <QAction>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFile>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStatusBar>
#include <QToolBar>
#include <QTreeWidgetItemIterator>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
constexpr auto entity_id_role = Qt::UserRole;
constexpr auto component_type_role = Qt::UserRole + 1;
constexpr auto property_id_role = Qt::UserRole + 2;

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
}

EditorWindow::EditorWindow(QString initial_document, QWidget* parent)
    : QMainWindow(parent), metadata_(dragonpixel::metadata::registry::slice_one_defaults())
{
    setWindowTitle(QStringLiteral("Dragon Pixel Engine Editor — Slice 1"));
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
    setCentralWidget(viewport_);
    preview_worker_ = new WorkerClient(QStringLiteral("preview"), this);
    play_worker_ = new WorkerClient(QStringLiteral("play"), this);
    connect(preview_worker_, &WorkerClient::frame_ready, viewport_, &AuthoringViewport::set_preview_frame);
    connect(play_worker_, &WorkerClient::frame_ready, viewport_, &AuthoringViewport::set_play_frame);
    connect(preview_worker_, &WorkerClient::status_message, this, [this](const QString& message) {
        append_console(QStringLiteral("[Preview] %1").arg(message));
        statusBar()->showMessage(message, 5000);
    });
    connect(play_worker_, &WorkerClient::status_message, this, [this](const QString& message) {
        append_console(QStringLiteral("[Play] %1").arg(message));
        statusBar()->showMessage(message, 5000);
    });
    connect(preview_worker_, &WorkerClient::runtime_stopped, viewport_, &AuthoringViewport::clear_preview_frame);
    connect(play_worker_, &WorkerClient::runtime_stopped, this, [this] {
        viewport_->set_play_mode(false);
        append_console(QStringLiteral("Play world discarded; authoring scene unchanged."));
    });

    scene_summary_ = new QListWidget(this);
    hierarchy_ = new QTreeWidget(this);
    hierarchy_->setHeaderLabels({QStringLiteral("Entity"), QStringLiteral("Persistent UUID")});
    hierarchy_->setSelectionMode(QAbstractItemView::SingleSelection);
    hierarchy_->setAccessibleName(QStringLiteral("Scene hierarchy"));
    connect(hierarchy_, &QTreeWidget::currentItemChanged, this, [this] {
        inspect_selected_entity();
    });
    connect(hierarchy_, &QTreeWidget::itemChanged, this, &EditorWindow::rename_entity);

    assets_ = new QListWidget(this);
    assets_->setAccessibleName(QStringLiteral("Project assets"));
    inspector_ = new QTreeWidget(this);
    inspector_->setHeaderLabels({QStringLiteral("Property"), QStringLiteral("JSON value")});
    inspector_->setAccessibleName(QStringLiteral("Metadata driven Inspector"));
    connect(inspector_, &QTreeWidget::itemChanged, this, &EditorWindow::edit_property);
    console_ = new QPlainTextEdit(this);
    console_->setReadOnly(true);
    console_->setMaximumBlockCount(2000);
    console_->setAccessibleName(QStringLiteral("Diagnostics console"));

    auto make_dock = [this](const QString& title, QWidget* widget, Qt::DockWidgetArea area) {
        auto* dock = new QDockWidget(title, this);
        dock->setObjectName(title);
        dock->setWidget(widget);
        dock->setAccessibleName(title);
        addDockWidget(area, dock);
        return dock;
    };
    scene_dock_ = make_dock(QStringLiteral("Scene"), scene_summary_, Qt::LeftDockWidgetArea);
    hierarchy_dock_ = make_dock(QStringLiteral("Hierarchy"), hierarchy_, Qt::LeftDockWidgetArea);
    assets_dock_ = make_dock(QStringLiteral("Project / Assets"), assets_, Qt::LeftDockWidgetArea);
    inspector_dock_ = make_dock(QStringLiteral("Inspector"), inspector_, Qt::RightDockWidgetArea);
    console_dock_ = make_dock(QStringLiteral("Console"), console_, Qt::BottomDockWidgetArea);
    tabifyDockWidget(scene_dock_, hierarchy_dock_);
    hierarchy_dock_->raise();

    auto* runtime_bar = addToolBar(QStringLiteral("Runtime"));
    runtime_bar->setObjectName(QStringLiteral("RuntimeToolbar"));
    adapter_ = new QComboBox(runtime_bar);
    adapter_->addItem(QStringLiteral("MonoGame"), QStringLiteral("monogame"));
    adapter_->addItem(QStringLiteral("KNI (experimental)"), QStringLiteral("kni"));
    adapter_->setAccessibleName(QStringLiteral("Runtime framework adapter"));
    connect(adapter_, &QComboBox::currentIndexChanged, this, [this](int) { refresh_preview(); });
    runtime_bar->addWidget(adapter_);
    auto add_action = [this, runtime_bar](const QString& text, auto handler) {
        auto* action = runtime_bar->addAction(text);
        connect(action, &QAction::triggered, this, handler);
        return action;
    };
    add_action(QStringLiteral("Play"), [this] { start_play(); });
    add_action(QStringLiteral("Pause"), [this] { play_worker_->pause(); });
    add_action(QStringLiteral("Resume"), [this] { play_worker_->resume(); });
    add_action(QStringLiteral("Stop"), [this] { stop_play(); });
    add_action(QStringLiteral("Crash Play Worker"), [this] { play_worker_->force_crash(); });

    auto* edit_bar = addToolBar(QStringLiteral("Authoring"));
    edit_bar->setObjectName(QStringLiteral("AuthoringToolbar"));
    auto add_edit_action = [this, edit_bar](const QString& text, auto handler) {
        auto* action = edit_bar->addAction(text);
        connect(action, &QAction::triggered, this, handler);
    };
    add_edit_action(QStringLiteral("Add Entity"), [this] { add_entity(); });
    add_edit_action(QStringLiteral("Reparent"), [this] { reparent_entity(); });
    add_edit_action(QStringLiteral("Add Component"), [this] { add_component(); });
    add_edit_action(QStringLiteral("Remove Component"), [this] { remove_component(); });

    auto* file_menu = menuBar()->addMenu(QStringLiteral("&File"));
    auto* open_project = file_menu->addAction(QStringLiteral("Open &Project..."));
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
    connect(open, &QAction::triggered, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Open Dragon Pixel scene"), project_root_, QStringLiteral("Dragon Pixel Scene (*.dpescene);;JSON (*.json)"));
        if (!path.isEmpty())
        {
            load_scene(path);
        }
    });
    auto* close = file_menu->addAction(QStringLiteral("&Close Project"));
    connect(close, &QAction::triggered, this, &EditorWindow::close_project);
    auto* save = file_menu->addAction(QStringLiteral("&Save Scene"));
    save->setShortcut(QKeySequence::Save);
    connect(save, &QAction::triggered, this, &EditorWindow::save_scene);
    file_menu->addSeparator();
    file_menu->addAction(QStringLiteral("E&xit"), this, &QWidget::close);

    automation_broker_ = new AutomationBroker(
        [this](const QString& method, const QJsonObject& parameters) {
            return handle_automation_request(method, parameters);
        },
        this);
    connect(automation_broker_, &AutomationBroker::status_message, this, [this](const QString& message) {
        append_console(QStringLiteral("[Automation] %1").arg(message));
    });
    if (!runtime_directory_.isValid()
        || !automation_broker_->start(runtime_directory_.filePath(QStringLiteral("automation-audit.jsonl"))))
    {
        append_console(QStringLiteral("Automation broker is unavailable for this editor session"));
    }
}

void EditorWindow::close_project()
{
    stop_play();
    if (preview_worker_ != nullptr)
    {
        preview_worker_->stop_and_discard();
    }
    scene_.reset();
    scene_path_.clear();
    project_manifest_path_.clear();
    project_root_.clear();
    hierarchy_->clear();
    inspector_->clear();
    assets_->clear();
    scene_summary_->clear();
    viewport_->clear_preview_frame();
    viewport_->set_selected_name({});
    setWindowTitle(QStringLiteral("Dragon Pixel Engine Editor — No Project"));
    append_console(QStringLiteral("Project closed; no authoritative scene remains loaded"));
}

void EditorWindow::load_project(const QString& path)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly))
    {
        append_console(QStringLiteral("Could not open project manifest: %1").arg(path));
        return;
    }
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (!document.isObject())
    {
        append_console(QStringLiteral("Project manifest JSON was invalid: %1").arg(parse_error.errorString()));
        return;
    }
    const auto root = document.object();
    const auto project_id = dragonpixel::core::uuid::parse(
        root.value(QStringLiteral("projectId")).toString().toStdString());
    const auto startup_scene = root.value(QStringLiteral("startupScene")).toString();
    if (root.value(QStringLiteral("$schema")).toString()
            != QStringLiteral("https://dragonpixel.dev/schemas/v1/project.schema.json")
        || root.value(QStringLiteral("format")).toString() != QStringLiteral("dpe.project")
        || root.value(QStringLiteral("formatVersion")).toInt() != 1
        || root.value(QStringLiteral("engineVersion")).toString().isEmpty()
        || !project_id || startup_scene.isEmpty() || QDir::isAbsolutePath(startup_scene))
    {
        append_console(QStringLiteral("Project manifest format, identity, or startup scene was invalid"));
        return;
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
        return;
    }

    project_manifest_path_ = QDir::toNativeSeparators(manifest_info.absoluteFilePath());
    load_scene(canonical_scene);
    if (scene_)
    {
        append_console(QStringLiteral("Opened project %1 (%2)")
            .arg(root.value(QStringLiteral("name")).toString(),
                 QString::fromStdString(project_id->to_string())));
    }
}

void EditorWindow::load_scene(const QString& path)
{
    std::ifstream stream{filesystem_path(path), std::ios::binary};
    if (!stream)
    {
        append_console(QStringLiteral("Could not open scene: %1").arg(path));
        return;
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
        return;
    }
    stop_play();
    scene_ = std::move(*loaded.value);
    scene_path_ = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    auto directory = QFileInfo(path).absoluteDir();
    if (directory.dirName().compare(QStringLiteral("Scenes"), Qt::CaseInsensitive) == 0)
    {
        directory.cdUp();
    }
    project_root_ = directory.absolutePath();
    rebuild_hierarchy();
    rebuild_scene_summary();
    rebuild_assets();
    setWindowTitle(QStringLiteral("Dragon Pixel Engine Editor — %1").arg(QString::fromStdString(scene_->name())));
    append_console(QStringLiteral("Loaded %1 (%2 entities, %3 migration records)")
        .arg(scene_path_)
        .arg(scene_->entities().size())
        .arg(loaded.migrations.size()));
    refresh_preview();
}

void EditorWindow::save_scene()
{
    if (!scene_ || scene_path_.isEmpty())
    {
        return;
    }
    const auto json = dragonpixel::serialization::write_scene_json(*scene_);
    const auto result = dragonpixel::serialization::save_utf8_atomic(
        filesystem_path(scene_path_), json);
    if (result.succeeded)
    {
        append_console(QStringLiteral("Atomically saved scene: %1").arg(scene_path_));
        statusBar()->showMessage(QStringLiteral("Scene saved"), 3000);
    }
    else
    {
        QMessageBox::critical(this, QStringLiteral("Save failed"), QString::fromStdString(result.error));
    }
}

void EditorWindow::rebuild_hierarchy()
{
    rebuilding_hierarchy_ = true;
    hierarchy_->clear();
    if (!scene_)
    {
        rebuilding_hierarchy_ = false;
        return;
    }
    QHash<QString, QTreeWidgetItem*> items;
    for (const auto& entity : scene_->entities())
    {
        auto* item = new QTreeWidgetItem{
            {QString::fromStdString(entity.name), QString::fromStdString(entity.id.to_string())}};
        item->setData(0, entity_id_role, QString::fromStdString(entity.id.to_string()));
        item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
        item->setCheckState(0, entity.enabled ? Qt::Checked : Qt::Unchecked);
        items.insert(QString::fromStdString(entity.id.to_string()), item);
    }
    for (const auto& entity : scene_->entities())
    {
        auto* item = items.value(QString::fromStdString(entity.id.to_string()));
        if (entity.parent_id)
        {
            if (auto* parent = items.value(QString::fromStdString(entity.parent_id->to_string()), nullptr))
            {
                parent->addChild(item);
                continue;
            }
        }
        hierarchy_->addTopLevelItem(item);
    }
    hierarchy_->expandAll();
    if (hierarchy_->topLevelItemCount() > 0)
    {
        hierarchy_->setCurrentItem(hierarchy_->topLevelItem(0));
    }
    rebuilding_hierarchy_ = false;
}

void EditorWindow::rebuild_scene_summary()
{
    scene_summary_->clear();
    if (!scene_)
    {
        return;
    }
    scene_summary_->addItem(QStringLiteral("Scene: %1").arg(QString::fromStdString(scene_->name())));
    scene_summary_->addItem(QStringLiteral("UUID: %1").arg(QString::fromStdString(scene_->id().to_string())));
    scene_summary_->addItem(QStringLiteral("Entities: %1").arg(scene_->entities().size()));
    scene_summary_->addItem(QStringLiteral("Spatial model: right-handed, Y-up"));
    scene_summary_->addItem(QStringLiteral("Authoring state: editor authoritative"));
}

void EditorWindow::rebuild_assets()
{
    assets_->clear();
    QDirIterator iterator{project_root_, {QStringLiteral("*.dpeasset"), QStringLiteral("DragonPixelProject.json")}, QDir::Files, QDirIterator::Subdirectories};
    while (iterator.hasNext())
    {
        const auto path = iterator.next();
        assets_->addItem(QDir{project_root_}.relativeFilePath(path));
    }
}

void EditorWindow::inspect_selected_entity()
{
    rebuilding_inspector_ = true;
    inspector_->clear();
    const auto* entity = selected_entity();
    if (entity == nullptr)
    {
        rebuilding_inspector_ = false;
        return;
    }
    viewport_->set_selected_name(QString::fromStdString(entity->name));
    for (const auto& component : entity->components)
    {
        const auto* descriptor = metadata_.find(component.type_id);
        const auto label = component.opaque
            ? QStringLiteral("Opaque — %1 v%2").arg(QString::fromStdString(component.qualified_name)).arg(component.schema_version)
            : QString::fromStdString(descriptor != nullptr ? descriptor->display_name : component.type_id);
        auto* component_item = new QTreeWidgetItem{inspector_, {label, owner_text(component.owner)}};
        component_item->setData(0, component_type_role, QString::fromStdString(component.type_id));
        if (component.opaque || descriptor == nullptr)
        {
            auto* raw = new QTreeWidgetItem{component_item, {QStringLiteral("Raw preserved record"), QString::fromStdString(component.raw_record.dump())}};
            raw->setToolTip(1, QStringLiteral("Unavailable/newer component data is read-only and will round-trip."));
            continue;
        }
        component_item->setFlags(component_item->flags() | Qt::ItemIsUserCheckable);
        component_item->setCheckState(0, component.enabled ? Qt::Checked : Qt::Unchecked);
        for (const auto& property : descriptor->properties)
        {
            const auto found = component.properties.find(property.property_id);
            const auto value = found == component.properties.end() ? nlohmann::ordered_json{nullptr} : *found;
            auto* property_item = new QTreeWidgetItem{
                component_item,
                {QString::fromStdString(property.display_name), QString::fromStdString(value.dump())}};
            property_item->setData(0, entity_id_role, QString::fromStdString(entity->id.to_string()));
            property_item->setData(0, component_type_role, QString::fromStdString(component.type_id));
            property_item->setData(0, property_id_role, QString::fromStdString(property.property_id));
            if (!property.read_only)
            {
                property_item->setFlags(property_item->flags() | Qt::ItemIsEditable);
            }
        }
    }
    inspector_->expandAll();
    inspector_->resizeColumnToContents(0);
    rebuilding_inspector_ = false;
}

void EditorWindow::rename_entity(QTreeWidgetItem* item, int column)
{
    if (rebuilding_hierarchy_ || !scene_ || column != 0)
    {
        return;
    }
    const auto id = dragonpixel::core::uuid::parse(item->data(0, entity_id_role).toString().toStdString());
    if (!id)
    {
        return;
    }
    const std::array<dragonpixel::scene::command, 2> transaction{
        dragonpixel::scene::command{dragonpixel::scene::rename_entity_command{*id, item->text(0).toStdString()}},
        dragonpixel::scene::command{dragonpixel::scene::set_entity_enabled_command{
            *id, item->checkState(0) == Qt::Checked}},
    };
    const auto result = scene_->apply_transaction(transaction);
    if (!result.succeeded)
    {
        append_console(QStringLiteral("Rename rejected"));
        rebuild_hierarchy();
    }
    else
    {
        append_console(QStringLiteral("Renamed entity through command validation"));
        rebuild_scene_summary();
        refresh_preview();
    }
}

void EditorWindow::edit_property(QTreeWidgetItem* item, int column)
{
    if (rebuilding_inspector_ || !scene_)
    {
        return;
    }
    if (column == 0 && item->parent() == nullptr)
    {
        const auto component_type = item->data(0, component_type_role).toString();
        const auto entity_id = selected_entity_id();
        if (!component_type.isEmpty() && entity_id)
        {
            const auto result = scene_->apply(dragonpixel::scene::command{
                dragonpixel::scene::set_component_enabled_command{
                    *entity_id, component_type.toStdString(), item->checkState(0) == Qt::Checked}});
            if (result.succeeded)
            {
                append_console(QStringLiteral("Component enabled state changed through command validation"));
                refresh_preview();
            }
        }
        return;
    }
    if (column != 1)
    {
        return;
    }
    const auto property_id = item->data(0, property_id_role).toString();
    const auto component_type = item->data(0, component_type_role).toString();
    const auto entity_id = dragonpixel::core::uuid::parse(item->data(0, entity_id_role).toString().toStdString());
    if (property_id.isEmpty() || component_type.isEmpty() || !entity_id)
    {
        return;
    }
    try
    {
        auto value = nlohmann::ordered_json::parse(item->text(1).toStdString());
        const auto result = scene_->apply(dragonpixel::scene::command{dragonpixel::scene::set_component_property_command{
            *entity_id, component_type.toStdString(), property_id.toStdString(), std::move(value)}});
        if (!result.succeeded)
        {
            throw std::runtime_error{"Command rejected"};
        }
        append_console(QStringLiteral("Edited %1 through command validation").arg(property_id));
        refresh_preview();
    }
    catch (const std::exception& exception)
    {
        append_console(QStringLiteral("Property edit rejected: %1").arg(QString::fromUtf8(exception.what())));
        inspect_selected_entity();
    }
}

void EditorWindow::add_entity()
{
    if (!scene_)
    {
        return;
    }
    bool accepted = false;
    const auto name = QInputDialog::getText(this, QStringLiteral("Add Entity"), QStringLiteral("Name"), QLineEdit::Normal, QStringLiteral("New Entity"), &accepted);
    if (!accepted || name.trimmed().isEmpty())
    {
        return;
    }
    const auto result = scene_->apply(dragonpixel::scene::command{dragonpixel::scene::create_entity_command{
        dragonpixel::core::uuid::random_v4(), name.trimmed().toStdString(), selected_entity_id()}});
    if (result.succeeded)
    {
        rebuild_hierarchy();
        rebuild_scene_summary();
        append_console(QStringLiteral("Entity created through command validation"));
        refresh_preview();
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
    const auto result = scene_->apply(dragonpixel::scene::command{dragonpixel::scene::reparent_entity_command{*selected, parent}});
    append_console(result.succeeded ? QStringLiteral("Entity reparented through command validation") : QStringLiteral("Reparent rejected: hierarchy would be invalid"));
    rebuild_hierarchy();
    if (result.succeeded)
    {
        refresh_preview();
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
        properties[property.property_id] = default_value(property.type);
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
    const auto result = scene_->apply(dragonpixel::scene::command{dragonpixel::scene::upsert_component_command{*entity_id, std::move(component)}});
    if (result.succeeded)
    {
        inspect_selected_entity();
        append_console(QStringLiteral("Component added through command validation"));
        refresh_preview();
    }
}

void EditorWindow::remove_component()
{
    if (!scene_ || inspector_->currentItem() == nullptr)
    {
        return;
    }
    auto* item = inspector_->currentItem();
    while (item->parent() != nullptr)
    {
        item = item->parent();
    }
    const auto type_id = item->data(0, component_type_role).toString();
    const auto entity_id = selected_entity_id();
    if (type_id.isEmpty() || !entity_id)
    {
        return;
    }
    const auto result = scene_->apply(dragonpixel::scene::command{dragonpixel::scene::remove_component_command{
        *entity_id, type_id.toStdString()}});
    if (result.succeeded)
    {
        inspect_selected_entity();
        append_console(QStringLiteral("Component removed through command validation"));
        refresh_preview();
    }
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
    viewport_->clear_preview_frame();
    preview_worker_->start_session(adapter_->currentData().toString(), snapshot_path);
    append_console(QStringLiteral("Preview worker launched from editor-owned authoring mirror"));
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
            const auto result = candidate.apply(command_value);
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

        const auto result = scene_->apply(command_value);
        if (!result.succeeded)
        {
            return failure(-32022, QString::fromStdString(result.diagnostic->message));
        }
        rebuild_hierarchy();
        rebuild_scene_summary();
        refresh_preview();
        append_console(QStringLiteral("Automation command applied through editor validation"));
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
}

void EditorWindow::append_console(const QString& message)
{
    qInfo().noquote() << message;
    console_->appendPlainText(QStringLiteral("[%1] %2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")), message));
}

std::optional<dragonpixel::core::uuid> EditorWindow::selected_entity_id() const
{
    const auto* item = hierarchy_->currentItem();
    if (item == nullptr)
    {
        return std::nullopt;
    }
    return dragonpixel::core::uuid::parse(item->data(0, entity_id_role).toString().toStdString());
}

dragonpixel::scene::entity const* EditorWindow::selected_entity() const
{
    const auto id = selected_entity_id();
    return scene_ && id ? scene_->find_entity(*id) : nullptr;
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
        && hierarchy_->topLevelItemCount() >= 5 && inspector_->topLevelItemCount() > 0
        && assets_->count() >= 2 && preview_worker_->has_frame() && play_worker_->has_frame()
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
        .arg(hierarchy_->topLevelItemCount())
        .arg(inspector_->topLevelItemCount())
        .arg(assets_->count())
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

    close_project();
    if (scene_ || !project_manifest_path_.isEmpty() || !scene_path_.isEmpty()
        || hierarchy_->topLevelItemCount() != 0 || inspector_->topLevelItemCount() != 0)
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
        || reopened_entity->components.at(1).enabled || assets_->count() < 2)
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
    const auto opaque_entity_text = QString::fromStdString(opaque_entity->id.to_string());
    bool opaque_diagnostic_visible = false;
    for (QTreeWidgetItemIterator iterator{hierarchy_}; *iterator != nullptr; ++iterator)
    {
        if ((*iterator)->data(0, entity_id_role).toString() == opaque_entity_text)
        {
            hierarchy_->setCurrentItem(*iterator);
            inspect_selected_entity();
            const auto* component_item = inspector_->topLevelItem(0);
            opaque_diagnostic_visible = inspector_->topLevelItemCount() == 1
                && component_item != nullptr
                && component_item->text(0).startsWith(QStringLiteral("Opaque"))
                && component_item->childCount() == 1
                && component_item->child(0)->text(0) == QStringLiteral("Raw preserved record")
                && !(component_item->child(0)->flags() & Qt::ItemIsEditable);
            break;
        }
    }
    if (!opaque_diagnostic_visible)
    {
        return false;
    }

    const auto entity_text = QString::fromStdString(entity_id.to_string());
    for (QTreeWidgetItemIterator iterator{hierarchy_}; *iterator != nullptr; ++iterator)
    {
        if ((*iterator)->data(0, entity_id_role).toString() == entity_text)
        {
            hierarchy_->setCurrentItem(*iterator);
            inspect_selected_entity();
            refresh_preview();
            append_console(QStringLiteral(
                "Authoring commands and atomic project save/close/reopen passed"));
            return inspector_->topLevelItemCount() == 2;
        }
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
