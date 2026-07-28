#include "EditorWindow.h"

#include <dragonpixel/metadata/builtin_ids.h>
#include <dragonpixel/serialization/scene_json.h>

#include <QAction>
#include <QComboBox>
#include <QClipboard>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QDirIterator>
#include <QDialog>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QItemSelectionModel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QPixmap>
#include <QRegularExpression>
#include <QSet>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QtTest/QTest>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>

namespace
{
std::filesystem::path filesystem_path(const QString& path)
{
#if defined(Q_OS_WIN)
    return std::filesystem::path{path.toStdWString()};
#else
    return std::filesystem::path{path.toUtf8().constData()};
#endif
}

bool create_directory_symlink(const QString& target, const QString& link)
{
    std::error_code error;
    std::filesystem::create_directory_symlink(
        filesystem_path(target), filesystem_path(link), error);
    return !error;
}

bool create_file_symlink(const QString& target, const QString& link)
{
    std::error_code error;
    std::filesystem::create_symlink(
        filesystem_path(target), filesystem_path(link), error);
    return !error;
}

bool remove_filesystem_link(const QString& link)
{
    std::error_code error;
    const auto removed = std::filesystem::remove(filesystem_path(link), error);
    return removed && !error;
}

int recursive_rows(const QAbstractItemModel* model, const QModelIndex& parent = {})
{
    int result = model->rowCount(parent);
    for (int row = 0; row < model->rowCount(parent); ++row)
    {
        result += recursive_rows(model, model->index(row, 0, parent));
    }
    return result;
}

QModelIndex find_text(
    const QAbstractItemModel* model,
    const QString& text,
    const QModelIndex& parent = {})
{
    for (int row = 0; row < model->rowCount(parent); ++row)
    {
        const auto index = model->index(row, 0, parent);
        if (index.data().toString() == text)
        {
            return index;
        }
        const auto nested = find_text(model, text, index);
        if (nested.isValid())
        {
            return nested;
        }
    }
    return {};
}

QModelIndex find_role(
    const QAbstractItemModel* model,
    int role,
    const QVariant& value,
    const QModelIndex& parent = {})
{
    for (int row = 0; row < model->rowCount(parent); ++row)
    {
        const auto index = model->index(row, 0, parent);
        if (index.data(role) == value)
        {
            return index;
        }
        const auto nested = find_role(model, role, value, index);
        if (nested.isValid())
        {
            return nested;
        }
    }
    return {};
}

bool model_contains(const QAbstractItemModel* model, const QString& text)
{
    for (int row = 0; row < model->rowCount(); ++row)
    {
        for (int column = 0; column < model->columnCount(); ++column)
        {
            if (model->index(row, column).data().toString().contains(text))
            {
                return true;
            }
        }
    }
    return false;
}

QModelIndex find_table_text(const QAbstractItemModel* model, const QString& text)
{
    for (int row = 0; row < model->rowCount(); ++row)
    {
        for (int column = 0; column < model->columnCount(); ++column)
        {
            const auto index = model->index(row, column);
            if (index.data().toString().contains(text))
            {
                return index;
            }
        }
    }
    return {};
}

QVector3D inspector_position(const QTreeView* inspector)
{
    const auto position = find_text(inspector->model(), QStringLiteral("Position"));
    if (!position.isValid())
    {
        return {1000000.0F, 1000000.0F, 1000000.0F};
    }
    const auto encoded = QStringLiteral("[%1]")
        .arg(position.siblingAtColumn(1).data(Qt::DisplayRole).toString())
        .toUtf8();
    const auto values = QJsonDocument::fromJson(encoded).array();
    if (values.isEmpty() || !values.at(0).isObject())
    {
        return {1000000.0F, 1000000.0F, 1000000.0F};
    }
    const auto value = values.at(0).toObject();
    return {
        static_cast<float>(value.value(QStringLiteral("x")).toDouble()),
        static_cast<float>(value.value(QStringLiteral("y")).toDouble()),
        static_cast<float>(value.value(QStringLiteral("z")).toDouble()),
    };
}

bool near(float left, float right, float epsilon = 0.015F)
{
    return std::abs(left - right) <= epsilon;
}

bool copy_directory_tree(const QString& source_root, const QString& destination_root)
{
    if (!QDir{source_root}.exists() || !QDir{}.mkpath(destination_root))
    {
        return false;
    }

    QDirIterator iterator{
        source_root,
        QDir::AllEntries | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories};
    const QDir source{source_root};
    const QDir destination{destination_root};
    while (iterator.hasNext())
    {
        const auto source_path = iterator.next();
        const auto destination_path = destination.filePath(source.relativeFilePath(source_path));
        const QFileInfo source_info{source_path};
        if (source_info.isDir())
        {
            if (!QDir{}.mkpath(destination_path))
            {
                return false;
            }
            continue;
        }
        if (!QDir{}.mkpath(QFileInfo{destination_path}.absolutePath())
            || !QFile::copy(source_path, destination_path))
        {
            return false;
        }
    }
    return true;
}

QByteArray read_bytes(const QString& path)
{
    QFile file{path};
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

QString create_tiled_map_fixture(const QString& root)
{
    if (!QDir{}.mkpath(root))
    {
        return {};
    }
    QImage atlas{4, 4, QImage::Format_RGBA8888};
    atlas.fill(QColor{55, 175, 95});
    if (!atlas.save(QDir{root}.filePath(QStringLiteral("atlas.png")), "PNG"))
    {
        return {};
    }
    const QJsonObject map{
        {QStringLiteral("type"), QStringLiteral("map")},
        {QStringLiteral("orientation"), QStringLiteral("orthogonal")},
        {QStringLiteral("infinite"), false},
        {QStringLiteral("tilewidth"), 2},
        {QStringLiteral("tileheight"), 2},
        {QStringLiteral("width"), 2},
        {QStringLiteral("height"), 1},
        {QStringLiteral("tilesets"), QJsonArray{QJsonObject{
            {QStringLiteral("firstgid"), 1},
            {QStringLiteral("name"), QStringLiteral("Terrain")},
            {QStringLiteral("tilewidth"), 2},
            {QStringLiteral("tileheight"), 2},
            {QStringLiteral("tilecount"), 4},
            {QStringLiteral("columns"), 2},
            {QStringLiteral("image"), QStringLiteral("atlas.png")},
            {QStringLiteral("imagewidth"), 4},
            {QStringLiteral("imageheight"), 4}}}},
        {QStringLiteral("layers"), QJsonArray{QJsonObject{
            {QStringLiteral("id"), 1},
            {QStringLiteral("name"), QStringLiteral("Ground")},
            {QStringLiteral("type"), QStringLiteral("tilelayer")},
            {QStringLiteral("visible"), true},
            {QStringLiteral("width"), 2},
            {QStringLiteral("height"), 1},
            {QStringLiteral("data"), QJsonArray{1, 4}}}}},
    };
    const auto path = QDir{root}.filePath(QStringLiteral("TiledImportLevel.tmj"));
    QFile output{path};
    const auto bytes = QJsonDocument{map}.toJson(QJsonDocument::Indented);
    return output.open(QIODevice::WriteOnly | QIODevice::Truncate)
            && output.write(bytes) == bytes.size()
        ? path : QString{};
}
}

class EditorInteractionTests final : public QObject
{
    Q_OBJECT

private slots:
    void launch_without_document_opens_accessible_project_hub()
    {
        EditorWindow window{QString{}};
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        auto* hub = window.findChild<QDockWidget*>(QStringLiteral("Dock.ProjectHub"));
        auto* create = window.findChild<QPushButton*>(QStringLiteral("ProjectHubNewProject"));
        auto* open = window.findChild<QPushButton*>(QStringLiteral("ProjectHubOpenProject"));
        auto* recent = window.findChild<QListWidget*>(QStringLiteral("ProjectHubRecentProjects"));
        auto* new_project = window.findChild<QAction*>(QStringLiteral("NewProjectAction"));
        auto* new_scene = window.findChild<QAction*>(QStringLiteral("NewSceneAction"));
        auto* save_as = window.findChild<QAction*>(QStringLiteral("SaveSceneAsAction"));
        auto* import_tiled = window.findChild<QAction*>(QStringLiteral("ImportTiledTilemapAction"));

        QVERIFY(hub != nullptr && hub->isVisible());
        QVERIFY(hub->widget() != nullptr);
        QCOMPARE(hub->widget()->accessibleName(), QStringLiteral("Dragon Pixel Project Hub"));
        QVERIFY(create != nullptr && create->isEnabled());
        QVERIFY(open != nullptr && open->isEnabled());
        QVERIFY(recent != nullptr && !recent->accessibleName().isEmpty());
        QVERIFY(new_project != nullptr && new_project->isEnabled());
        QVERIFY(new_scene != nullptr && !new_scene->isEnabled());
        QVERIFY(save_as != nullptr && !save_as->isEnabled());
        QVERIFY(import_tiled != nullptr && !import_tiled->isEnabled());
        QVERIFY(!window.scene_.has_value());
        QVERIFY(window.project_manifest_path_.isEmpty());
    }

    void dock_workspace_uses_the_full_nested_grid_and_round_trips_layout()
    {
        EditorWindow window{QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT)};
        window.set_unsaved_prompt([](const QString&) {
            return EditorWindow::UnsavedDecision::discard;
        });
        window.reset_workspace();
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        QVERIFY(window.centralWidget() == nullptr);
        const auto options = window.dockOptions();
        QVERIFY(options.testFlag(QMainWindow::AnimatedDocks));
        QVERIFY(options.testFlag(QMainWindow::AllowNestedDocks));
        QVERIFY(options.testFlag(QMainWindow::AllowTabbedDocks));
        QVERIFY(options.testFlag(QMainWindow::GroupedDragging));

        auto* scene = window.findChild<QDockWidget*>(QStringLiteral("Dock.SceneView"));
        auto* game = window.findChild<QDockWidget*>(QStringLiteral("Dock.GameView"));
        auto* hierarchy = window.findChild<QDockWidget*>(QStringLiteral("Dock.Hierarchy"));
        auto* assets = window.findChild<QDockWidget*>(QStringLiteral("Dock.ProjectExplorer"));
        auto* inspector = window.findChild<QDockWidget*>(QStringLiteral("Dock.Inspector"));
        auto* console = window.findChild<QDockWidget*>(QStringLiteral("Dock.Console"));
        auto* tile_palette = window.findChild<QDockWidget*>(QStringLiteral("Dock.TilePalette"));
        auto* onboarding = window.findChild<QDockWidget*>(QStringLiteral("Dock.GettingStarted"));
        auto* project_hub = window.findChild<QDockWidget*>(QStringLiteral("Dock.ProjectHub"));
        QVERIFY(scene != nullptr && game != nullptr && hierarchy != nullptr && assets != nullptr);
        QVERIFY(inspector != nullptr && console != nullptr && tile_palette != nullptr && onboarding != nullptr);
        QVERIFY(project_hub != nullptr);
        auto* import_tiled = window.findChild<QAction*>(QStringLiteral("ImportTiledTilemapAction"));
        QVERIFY(import_tiled != nullptr && import_tiled->isEnabled());
        QCOMPARE(import_tiled->shortcut(), QKeySequence{QStringLiteral("Ctrl+Alt+T")});

        for (auto* dock : {scene, game, hierarchy, assets, inspector, console, tile_palette, onboarding, project_hub})
        {
            QCOMPARE(dock->allowedAreas(), Qt::AllDockWidgetAreas);
            QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetMovable));
            QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetFloatable));
            QVERIFY(!dock->isFloating());
        }
        QVERIFY(window.tabifiedDockWidgets(scene).contains(game));
        QVERIFY(window.tabifiedDockWidgets(console).contains(tile_palette));

        constexpr int state_version = 4;
        const auto default_state = window.saveState(state_version);
        QVERIFY(!default_state.isEmpty());

        window.splitDockWidget(scene, game, Qt::Horizontal);
        window.splitDockWidget(game, inspector, Qt::Vertical);
        QCoreApplication::processEvents();
        QVERIFY(!window.tabifiedDockWidgets(scene).contains(game));
        QVERIFY(window.saveState(state_version) != default_state);

        QVERIFY(window.restoreState(default_state, state_version));
        QCoreApplication::processEvents();
        QVERIFY(window.tabifiedDockWidgets(scene).contains(game));
        QVERIFY(window.tabifiedDockWidgets(scene).contains(onboarding));
        QVERIFY(window.tabifiedDockWidgets(console).contains(tile_palette));
        const auto restored_state = window.saveState(state_version);
        QVERIFY(!restored_state.isEmpty());
        QVERIFY(window.restoreState(restored_state, state_version));

        game->setFloating(true);
        QCoreApplication::processEvents();
        QVERIFY(game->isFloating());
        game->setFloating(false);
        window.addDockWidget(Qt::BottomDockWidgetArea, game);
        QCoreApplication::processEvents();
        QVERIFY(!game->isFloating());
        QCOMPARE(window.dockWidgetArea(game), Qt::BottomDockWidgetArea);

        window.reset_workspace();
        QCoreApplication::processEvents();
        QVERIFY(window.tabifiedDockWidgets(scene).contains(game));
        QVERIFY(window.tabifiedDockWidgets(scene).contains(onboarding));
        QVERIFY(window.tabifiedDockWidgets(console).contains(tile_palette));
    }

    void tiled_import_action_publishes_and_opens_the_palette_without_scene_mutation()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto sample_root = QFileInfo{QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT)}.absolutePath();
        const auto project_root = temporary.filePath(QStringLiteral("TiledImportProject"));
        QVERIFY(copy_directory_tree(sample_root, project_root));
        const auto manifest = QDir{project_root}.filePath(QStringLiteral("DragonPixelProject.json"));
        const auto source = create_tiled_map_fixture(
            temporary.filePath(QStringLiteral("External Tiled Source")));
        QVERIFY(!source.isEmpty());

        EditorWindow window{manifest};
        window.set_unsaved_prompt([](const QString&) {
            return EditorWindow::UnsavedDecision::discard;
        });
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto entity_count = window.scene_->entities().size();
        const auto scene_path = window.scene_path_;

        QVERIFY(window.perform_tiled_tilemap_import(source, 2.0));
        QCOMPARE(window.scene_->entities().size(), entity_count);
        QCOMPARE(window.scene_path_, scene_path);
        QVERIFY(window.tile_palette_dock_->isVisible());
        QVERIFY(window.tile_document_service_->tilemap() != nullptr);
        QCOMPARE(QFileInfo{window.tile_document_service_->tilemap_path()}.fileName(),
            QStringLiteral("TiledImportLevel.dpetilemap"));
        QVERIFY(window.project_index_.succeeded());
        const auto tilemap = std::find_if(window.project_index_.candidate->entries.cbegin(),
            window.project_index_.candidate->entries.cend(), [](const auto& entry) {
                return entry.asset_type == QStringLiteral("tilemap")
                    && QFileInfo{entry.resolved_source_path}.fileName()
                        == QStringLiteral("TiledImportLevel.dpetilemap");
            });
        QVERIFY(tilemap != window.project_index_.candidate->entries.cend());
    }

    void worker_client_rejects_downgraded_regressing_and_future_correlated_frames()
    {
        WorkerClient client{QStringLiteral("play")};
        client.negotiated_protocol_version_ = 2;
        client.input_revision_ = 5;
        client.last_frame_revision_ = 10;
        client.last_frame_input_revision_ = 3;

        QVERIFY(!client.accepts_frame_metadata(1, 11, 3));
        QVERIFY(!client.accepts_frame_metadata(2, 10, 3));
        QVERIFY(!client.accepts_frame_metadata(2, 9, 3));
        QVERIFY(!client.accepts_frame_metadata(2, 11, 2));
        QVERIFY(!client.accepts_frame_metadata(2, 11, 6));
        QVERIFY(client.accepts_frame_metadata(2, 11, 3));
        QVERIFY(client.accepts_frame_metadata(2, 11, 5));
    }

    void typed_string_and_enum_editors_round_trip_json_safely()
    {
        InspectorDelegate delegate;
        QStandardItemModel model{1, 1};
        const auto index = model.index(0, 0);
        model.setData(index, static_cast<int>(dragonpixel::metadata::value_type::string), EditorRoles::value_type);
        model.setData(index, QStringLiteral("\"original value\""), Qt::EditRole);

        QWidget parent;
        QStyleOptionViewItem option;
        auto* string_editor = qobject_cast<QLineEdit*>(delegate.createEditor(&parent, option, index));
        QVERIFY(string_editor != nullptr);
        delegate.setEditorData(string_editor, index);
        QCOMPARE(string_editor->text(), QStringLiteral("original value"));
        const auto expected = QStringLiteral("quoted \"value\" with \\ slash");
        string_editor->setText(expected);
        delegate.setModelData(string_editor, &model, index);
        const auto string_document = QJsonDocument::fromJson(
            QStringLiteral("[%1]").arg(model.data(index, Qt::EditRole).toString()).toUtf8());
        QVERIFY(string_document.isArray());
        QCOMPARE(string_document.array().at(0).toString(), expected);

        model.setData(index, QStringList{QStringLiteral("plain"), QStringLiteral("quoted \"choice\"")}, EditorRoles::enum_choices);
        model.setData(index, QStringLiteral("\"plain\""), Qt::EditRole);
        auto* enum_editor = qobject_cast<QComboBox*>(delegate.createEditor(&parent, option, index));
        QVERIFY(enum_editor != nullptr);
        delegate.setEditorData(enum_editor, index);
        enum_editor->setCurrentIndex(1);
        delegate.setModelData(enum_editor, &model, index);
        const auto enum_document = QJsonDocument::fromJson(
            QStringLiteral("[%1]").arg(model.data(index, Qt::EditRole).toString()).toUtf8());
        QVERIFY(enum_document.isArray());
        QCOMPARE(enum_document.array().at(0).toString(), QStringLiteral("quoted \"choice\""));
    }

    void presets_search_undo_redo_and_delete_use_real_widgets()
    {
        EditorWindow window{QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT)};
        window.set_unsaved_prompt([](const QString&) {
            return EditorWindow::UnsavedDecision::discard;
        });
        window.set_delete_prompt([](const QString&, int) { return true; });
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        auto* hierarchy = window.findChild<QTreeView*>(QStringLiteral("HierarchyView"));
        auto* hierarchy_search = window.findChild<QLineEdit*>(QStringLiteral("HierarchySearch"));
        auto* project = window.findChild<QTreeView*>(QStringLiteral("ProjectExplorerView"));
        auto* console = window.findChild<QTableView*>(QStringLiteral("ConsoleView"));
        auto* add_button = window.findChild<QToolButton*>(QStringLiteral("AddGameObjectButton"));
        QVERIFY(hierarchy != nullptr);
        QVERIFY(hierarchy_search != nullptr);
        QVERIFY(project != nullptr);
        QVERIFY(console != nullptr);
        QVERIFY(add_button != nullptr);
        QVERIFY(hierarchy->model()->rowCount() > 0);
        QVERIFY(project->model()->rowCount() > 0);
        QVERIFY(console->model()->rowCount() > 0);
        QCOMPARE(hierarchy->accessibleName(), QStringLiteral("Scene GameObject hierarchy"));

        const auto initial_count = recursive_rows(hierarchy->model());
        auto* menu = add_button->menu();
        QVERIFY(menu != nullptr);
        auto* empty_action = window.findChild<QAction*>(QStringLiteral("AddEmptyGameObjectAction"));
        QVERIFY(empty_action != nullptr);
        QTimer::singleShot(50, menu, [menu, empty_action] {
            QTest::mouseClick(menu, Qt::LeftButton, {}, menu->actionGeometry(empty_action).center());
        });
        QTest::mouseClick(add_button, Qt::LeftButton);
        QTRY_COMPARE(recursive_rows(hierarchy->model()), initial_count + 1);
        QVERIFY(window.windowTitle().endsWith(QStringLiteral("*")));

        QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE(recursive_rows(hierarchy->model()), initial_count);
        QTest::keyClick(&window, Qt::Key_Y, Qt::ControlModifier);
        QTRY_COMPARE(recursive_rows(hierarchy->model()), initial_count + 1);

        hierarchy_search->setFocus();
        QTest::keyClicks(hierarchy_search, QStringLiteral("GameObject"));
        QTRY_VERIFY(hierarchy->model()->rowCount() > 0);
        QTest::keyClick(hierarchy_search, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(hierarchy_search, Qt::Key_Backspace);

        const auto created = find_text(hierarchy->model(), QStringLiteral("GameObject"));
        QVERIFY(created.isValid());
        hierarchy->selectionModel()->clearSelection();
        hierarchy->setCurrentIndex(created);
        hierarchy->selectionModel()->select(created, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        hierarchy->setFocus();
        QTest::keyClick(hierarchy, Qt::Key_Delete);
        QTRY_COMPARE(recursive_rows(hierarchy->model()), initial_count);

        auto* onboarding = window.findChild<QDockWidget*>(QStringLiteral("Dock.GettingStarted"));
        auto* add_square = window.findChild<QPushButton*>(QStringLiteral("OnboardingAddSquare"));
        auto* add_circle = window.findChild<QPushButton*>(QStringLiteral("OnboardingAddCircle"));
        auto* dismiss_onboarding = window.findChild<QPushButton*>(QStringLiteral("OnboardingDismiss"));
        auto* inspector = window.findChild<QTreeView*>(QStringLiteral("InspectorView"));
        auto* inspector_enabled = window.findChild<QCheckBox*>(QStringLiteral("InspectorGameObjectEnabled"));
        QVERIFY(onboarding != nullptr && add_square != nullptr && add_circle != nullptr);
        QVERIFY(dismiss_onboarding != nullptr && inspector != nullptr && inspector_enabled != nullptr);
        QVERIFY(inspector->styleSheet().contains(QStringLiteral("QTreeView::indicator:checked")));
        QVERIFY(inspector_enabled->styleSheet().contains(QStringLiteral("QCheckBox::indicator:checked")));
        QVERIFY(!QPixmap{QStringLiteral(":/dragonpixel/icons/inspector-check.xpm")}.isNull());
        QVERIFY(!QPixmap{QStringLiteral(":/dragonpixel/icons/inspector-partial.xpm")}.isNull());
        QVERIFY(inspector_enabled->minimumWidth() >= 24 && inspector_enabled->minimumHeight() >= 24);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("AddSquareSpriteAction")) != nullptr);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("AddCircleSpriteAction")) != nullptr);
        onboarding->show();
        onboarding->raise();
        QCoreApplication::processEvents();
        QTest::mouseClick(add_square, Qt::LeftButton);
        QTRY_COMPARE(recursive_rows(hierarchy->model()), initial_count + 1);
        QVERIFY(find_text(hierarchy->model(), QStringLiteral("Square")).isValid());
        QTRY_COMPARE(
            find_text(inspector->model(), QStringLiteral("Texture"))
                .siblingAtColumn(1).data(Qt::DisplayRole).toString(),
            QStringLiteral("\"builtin://square\""));
        QTest::mouseClick(add_circle, Qt::LeftButton);
        QTRY_COMPARE(recursive_rows(hierarchy->model()), initial_count + 2);
        QVERIFY(find_text(hierarchy->model(), QStringLiteral("Circle")).isValid());
        QTRY_COMPARE(
            find_text(inspector->model(), QStringLiteral("Texture"))
                .siblingAtColumn(1).data(Qt::DisplayRole).toString(),
            QStringLiteral("\"builtin://circle\""));
        QTest::mouseClick(dismiss_onboarding, Qt::LeftButton);
        QTRY_VERIFY(!onboarding->isVisible());
        QTRY_VERIFY(window.findChild<QDockWidget*>(QStringLiteral("Dock.SceneView"))->isVisible());

        auto* undo = window.findChild<QAction*>(QStringLiteral("UndoAction"));
        auto* redo = window.findChild<QAction*>(QStringLiteral("RedoAction"));
        auto* save = window.findChild<QAction*>(QStringLiteral("SaveSceneAction"));
        QVERIFY(undo != nullptr && undo->isEnabled());
        QVERIFY(redo != nullptr);
        QVERIFY(save != nullptr && save->isEnabled());
        QVERIFY(window.findChild<QAction*>(QStringLiteral("Workspace2DAction")) != nullptr);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("Workspace3DAction")) != nullptr);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("WorkspaceDebugAction")) != nullptr);
        auto* simulate = window.findChild<QAction*>(QStringLiteral("SimulatePreviewAction"));
        QVERIFY(simulate != nullptr);
        QVERIFY(simulate->isCheckable());
        QVERIFY(simulate->isEnabled());
        simulate->trigger();
        QVERIFY(simulate->isChecked());
        simulate->trigger();
        QVERIFY(!simulate->isChecked());
    }

    void project_previews_filters_console_and_command_validation_are_live()
    {
        EditorWindow window{QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT)};
        window.set_unsaved_prompt([](const QString&) {
            return EditorWindow::UnsavedDecision::discard;
        });
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        auto* hierarchy = window.findChild<QTreeView*>(QStringLiteral("HierarchyView"));
        auto* inspector = window.findChild<QTreeView*>(QStringLiteral("InspectorView"));
        auto* project = window.findChild<QTreeView*>(QStringLiteral("ProjectExplorerView"));
        auto* project_type = window.findChild<QComboBox*>(QStringLiteral("ProjectTypeFilter"));
        auto* project_status = window.findChild<QComboBox*>(QStringLiteral("ProjectStatusFilter"));
        auto* console = window.findChild<QTableView*>(QStringLiteral("ConsoleView"));
        auto* undo = window.findChild<QAction*>(QStringLiteral("UndoAction"));
        QVERIFY(hierarchy != nullptr && inspector != nullptr && project != nullptr);
        QVERIFY(project_type != nullptr && project_status != nullptr && console != nullptr && undo != nullptr);
        QVERIFY(window.findChild<QPushButton*>(QStringLiteral("RefreshProjectAction")) != nullptr);
        QVERIFY(window.findChild<QPushButton*>(QStringLiteral("ClearConsoleAction")) != nullptr);
        QVERIFY(window.findChild<QPushButton*>(QStringLiteral("CopyConsoleAction")) != nullptr);
        QVERIFY(window.findChild<QPushButton*>(QStringLiteral("ExportConsoleAction")) != nullptr);

        const auto sprite_asset_id = QStringLiteral("dd02cd2a-8a7e-4b27-9d26-06331093885a");
        auto sprite_asset = find_role(project->model(), EditorRoles::asset_id, sprite_asset_id);
        QVERIFY(sprite_asset.isValid());
        QTRY_VERIFY_WITH_TIMEOUT(
            !qvariant_cast<QIcon>(sprite_asset.data(Qt::DecorationRole)).isNull(),
            5000);
        project_type->setCurrentIndex(project_type->findData(QStringLiteral("asset")));
        QTRY_VERIFY(find_role(project->model(), EditorRoles::asset_id, sprite_asset_id).isValid());
        QVERIFY(!find_text(project->model(), QStringLiteral("StarterCube")).isValid());
        project_status->setCurrentIndex(project_status->findData(QStringLiteral("ready")));
        QTRY_VERIFY(find_role(project->model(), EditorRoles::asset_id, sprite_asset_id).isValid());
        project_type->setCurrentIndex(0);
        project_status->setCurrentIndex(0);

        const auto cube = find_text(hierarchy->model(), QStringLiteral("Lit Cube (3D)"));
        QVERIFY(cube.isValid());
        hierarchy->selectionModel()->select(
            cube, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        hierarchy->setCurrentIndex(cube);
        QCoreApplication::processEvents();
        const auto roughness = find_text(inspector->model(), QStringLiteral("Roughness"));
        QVERIFY(roughness.isValid());
        const auto roughness_value = roughness.siblingAtColumn(1);
        QCOMPARE(roughness_value.data(Qt::DisplayRole).toString(), QStringLiteral("0.55"));
        QVERIFY(!undo->isEnabled());
        QVERIFY(!window.windowTitle().endsWith(QStringLiteral("*")));

        QVERIFY(inspector->model()->setData(roughness_value, QStringLiteral("2.0"), Qt::EditRole));
        QTRY_COMPARE(
            find_text(inspector->model(), QStringLiteral("Roughness"))
                .siblingAtColumn(1).data(Qt::DisplayRole).toString(),
            QStringLiteral("0.55"));
        QVERIFY(!undo->isEnabled());
        QVERIFY(!window.windowTitle().endsWith(QStringLiteral("*")));
        QTRY_VERIFY(model_contains(console->model(), QStringLiteral("DPE.COMMAND.PROPERTY_OUT_OF_RANGE")));

        const auto diagnostic = find_table_text(
            console->model(), QStringLiteral("DPE.COMMAND.PROPERTY_OUT_OF_RANGE"));
        QVERIFY(diagnostic.isValid());
        const auto diagnostic_row = diagnostic.siblingAtColumn(0);
        auto* console_dock = window.findChild<QDockWidget*>(QStringLiteral("Dock.Console"));
        QVERIFY(console_dock != nullptr);
        console_dock->show();
        console_dock->raise();
        QCoreApplication::processEvents();
        QTRY_VERIFY(console->isVisible());
        console->selectionModel()->select(
            diagnostic_row, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        console->setCurrentIndex(diagnostic_row);
        auto* copy_console = window.findChild<QPushButton*>(QStringLiteral("CopyConsoleAction"));
        QVERIFY(copy_console != nullptr);
        QTest::mouseClick(copy_console, Qt::LeftButton);
        QVERIFY(QApplication::clipboard()->text().contains(
            QStringLiteral("DPE.COMMAND.PROPERTY_OUT_OF_RANGE")));
        hierarchy->clearSelection();
        console->scrollTo(diagnostic_row);
        QTRY_VERIFY(console->visualRect(diagnostic_row).isValid());
        QTest::mouseClick(
            console->viewport(), Qt::LeftButton, Qt::NoModifier, console->visualRect(diagnostic_row).center());
        QTest::mouseDClick(
            console->viewport(), Qt::LeftButton, Qt::NoModifier, console->visualRect(diagnostic_row).center());
        QTRY_COMPARE(hierarchy->currentIndex().data().toString(), QStringLiteral("Lit Cube (3D)"));
        QTRY_COMPARE(hierarchy->selectionModel()->selectedRows().size(), 1);
        QTRY_VERIFY(find_text(inspector->model(), QStringLiteral("Roughness")).isValid());

        const auto valid_roughness = find_text(inspector->model(), QStringLiteral("Roughness")).siblingAtColumn(1);
        QVERIFY(inspector->model()->setData(valid_roughness, QStringLiteral("0.25"), Qt::EditRole));
        QTRY_COMPARE(
            find_text(inspector->model(), QStringLiteral("Roughness"))
                .siblingAtColumn(1).data(Qt::DisplayRole).toString(),
            QStringLiteral("0.25"));
        QTRY_VERIFY(undo->isEnabled());
        QTRY_VERIFY(window.windowTitle().endsWith(QStringLiteral("*")));

        auto* clear_console = window.findChild<QPushButton*>(QStringLiteral("ClearConsoleAction"));
        QVERIFY(clear_console != nullptr);
        QTest::mouseClick(clear_console, Qt::LeftButton);
        QTRY_COMPARE(console->model()->rowCount(), 0);
    }

    void gizmo_preview_cancel_commit_and_multiselection_are_transactional()
    {
        EditorWindow window{QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT)};
        window.set_unsaved_prompt([](const QString&) {
            return EditorWindow::UnsavedDecision::discard;
        });
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        auto* hierarchy = window.findChild<QTreeView*>(QStringLiteral("HierarchyView"));
        auto* inspector = window.findChild<QTreeView*>(QStringLiteral("InspectorView"));
        auto* viewport = window.findChild<AuthoringViewport*>(QStringLiteral("SceneViewport"));
        auto* undo = window.findChild<QAction*>(QStringLiteral("UndoAction"));
        QVERIFY(hierarchy != nullptr && inspector != nullptr && viewport != nullptr && undo != nullptr);

        const auto select_one = [&](const QString& name) {
            const auto index = find_text(hierarchy->model(), name);
            if (!index.isValid())
            {
                return false;
            }
            hierarchy->selectionModel()->select(
                index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            hierarchy->setCurrentIndex(index);
            QCoreApplication::processEvents();
            return true;
        };
        QVERIFY(select_one(QStringLiteral("Dragon Sprite (2D)")));
        QTRY_VERIFY(viewport->has_selection_geometry());
        const auto initial_anchor = viewport->gizmo_anchor_widget_position();
        QVERIFY(viewport->rect().contains(initial_anchor));
        QVERIFY(initial_anchor != viewport->rect().center());
        const auto original_position = inspector_position(inspector);
        QVERIFY(near(original_position.x(), -2.0F) && near(original_position.y(), 0.0F));
        QVERIFY(!undo->isEnabled());
        QVERIFY(!window.windowTitle().endsWith(QStringLiteral("*")));

        QSignalSpy preview_spy{&window, &EditorWindow::gizmo_preview_scene_changed};
        QSignalSpy restored_spy{&window, &EditorWindow::gizmo_preview_scene_restored};
        QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, initial_anchor);
        QTest::mouseMove(viewport, initial_anchor + QPoint{30, 0}, 20);
        QTRY_VERIFY(!preview_spy.isEmpty());
        const auto preview_position = preview_spy.back().at(1).value<QVector3D>();
        QVERIFY(near(preview_position.x(), -1.4F));
        QCOMPARE(inspector_position(inspector), original_position);
        QVERIFY(!undo->isEnabled());
        QVERIFY(!window.windowTitle().endsWith(QStringLiteral("*")));

        QTest::keyClick(viewport, Qt::Key_Escape);
        QTRY_COMPARE(restored_spy.size(), 1);
        QCOMPARE(inspector_position(inspector), original_position);
        QVERIFY(!undo->isEnabled());
        QVERIFY(!window.windowTitle().endsWith(QStringLiteral("*")));

        const auto sprite = find_text(hierarchy->model(), QStringLiteral("Dragon Sprite (2D)"));
        const auto cube = find_text(hierarchy->model(), QStringLiteral("Lit Cube (3D)"));
        QVERIFY(sprite.isValid() && cube.isValid());
        hierarchy->setCurrentIndex(sprite);
        hierarchy->selectionModel()->select(
            sprite, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        hierarchy->selectionModel()->select(cube, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        QCoreApplication::processEvents();
        QTRY_VERIFY(viewport->has_selection_geometry());
        const auto multi_anchor = viewport->gizmo_anchor_widget_position();
        QVERIFY(viewport->rect().contains(multi_anchor));

        QSignalSpy committed_spy{&window, &EditorWindow::gizmo_transaction_committed};
        preview_spy.clear();
        QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, multi_anchor);
        QTest::mouseMove(viewport, multi_anchor + QPoint{25, 0}, 20);
        QTRY_VERIFY(!preview_spy.isEmpty());
        QCOMPARE(preview_spy.back().at(0).toStringList().size(), 2);
        QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, multi_anchor + QPoint{25, 0});
        QTRY_COMPARE(committed_spy.size(), 1);
        QCOMPARE(committed_spy.front().front().toInt(), 2);
        QVERIFY(undo->isEnabled());
        QVERIFY(window.windowTitle().endsWith(QStringLiteral("*")));

        QVERIFY(select_one(QStringLiteral("Dragon Sprite (2D)")));
        auto position = inspector_position(inspector);
        QVERIFY(near(position.x(), -1.5F) && near(position.y(), 0.0F));
        QVERIFY(select_one(QStringLiteral("Lit Cube (3D)")));
        position = inspector_position(inspector);
        QVERIFY(near(position.x(), 2.0F) && near(position.y(), 0.0F));

        undo->trigger();
        QTRY_VERIFY(!undo->isEnabled());
        QVERIFY(!window.windowTitle().endsWith(QStringLiteral("*")));
        QVERIFY(select_one(QStringLiteral("Dragon Sprite (2D)")));
        position = inspector_position(inspector);
        QVERIFY(near(position.x(), -2.0F) && near(position.y(), 0.0F));
        QVERIFY(select_one(QStringLiteral("Lit Cube (3D)")));
        position = inspector_position(inspector);
        QVERIFY(near(position.x(), 1.5F) && near(position.y(), 0.0F));
    }

    void gizmo_local_global_snapping_and_focus_use_real_input()
    {
        EditorWindow window{QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT)};
        window.set_unsaved_prompt([](const QString&) {
            return EditorWindow::UnsavedDecision::discard;
        });
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        auto* hierarchy = window.findChild<QTreeView*>(QStringLiteral("HierarchyView"));
        auto* inspector = window.findChild<QTreeView*>(QStringLiteral("InspectorView"));
        auto* viewport = window.findChild<AuthoringViewport*>(QStringLiteral("SceneViewport"));
        auto* undo = window.findChild<QAction*>(QStringLiteral("UndoAction"));
        QVERIFY(hierarchy != nullptr && inspector != nullptr && viewport != nullptr && undo != nullptr);

        const auto select_one = [&](const QString& name) {
            const auto index = find_text(hierarchy->model(), name);
            if (!index.isValid())
            {
                return false;
            }
            hierarchy->selectionModel()->select(
                index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            hierarchy->setCurrentIndex(index);
            QCoreApplication::processEvents();
            return true;
        };
        QVERIFY(select_one(QStringLiteral("Dragon Sprite (2D)")));
        QTRY_VERIFY(viewport->has_selection_geometry());

        QSignalSpy camera_spy{viewport, &AuthoringViewport::camera_changed};
        viewport->setFocus();
        QTest::keyClick(viewport, Qt::Key_F);
        QTRY_VERIFY(!camera_spy.isEmpty());
        auto focus_target = camera_spy.back().at(2).value<QVector3D>();
        QVERIFY(near(focus_target.x(), -2.0F)
                && near(focus_target.y(), 0.0F)
                && near(focus_target.z(), 0.0F));

        QTest::keyClick(viewport, Qt::Key_S, Qt::ShiftModifier);
        QVERIFY(viewport->snapping_enabled());
        QTest::keyClick(viewport, Qt::Key_E);
        QCOMPARE(viewport->gizmo_tool(), AuthoringViewport::GizmoTool::rotate);
        auto anchor = viewport->gizmo_anchor_widget_position();
        QVERIFY(viewport->rect().contains(anchor));
        QSignalSpy committed_spy{&window, &EditorWindow::gizmo_transaction_committed};
        QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, anchor);
        QTest::mouseMove(viewport, anchor + QPoint{180, 0}, 20);
        QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, anchor + QPoint{180, 0});
        QTRY_COMPARE(committed_spy.size(), 1);

        QTest::keyClick(viewport, Qt::Key_W);
        QCOMPARE(viewport->gizmo_tool(), AuthoringViewport::GizmoTool::move);
        QCOMPARE(viewport->gizmo_orientation(), AuthoringViewport::GizmoOrientation::global);
        QSignalSpy preview_spy{&window, &EditorWindow::gizmo_preview_scene_changed};
        anchor = viewport->gizmo_anchor_widget_position();
        QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, anchor);
        QTest::mouseMove(viewport, anchor + QPoint{13, 0}, 20);
        QTRY_VERIFY(!preview_spy.isEmpty());
        auto preview_position = preview_spy.back().at(1).value<QVector3D>();
        QVERIFY(near(preview_position.x(), -1.5F) && near(preview_position.y(), 0.0F));
        QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, anchor + QPoint{13, 0});
        QTRY_COMPARE(committed_spy.size(), 2);
        auto position = inspector_position(inspector);
        QVERIFY(near(position.x(), -1.5F) && near(position.y(), 0.0F));

        undo->trigger();
        QTRY_VERIFY(near(inspector_position(inspector).x(), -2.0F));
        QTest::keyClick(viewport, Qt::Key_X);
        QCOMPARE(viewport->gizmo_orientation(), AuthoringViewport::GizmoOrientation::local);
        preview_spy.clear();
        anchor = viewport->gizmo_anchor_widget_position();
        QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, anchor);
        QTest::mouseMove(viewport, anchor + QPoint{13, 0}, 20);
        QTRY_VERIFY(!preview_spy.isEmpty());
        preview_position = preview_spy.back().at(1).value<QVector3D>();
        QVERIFY(near(preview_position.x(), -2.0F) && near(preview_position.y(), 0.5F));
        QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, anchor + QPoint{13, 0});
        QTRY_COMPARE(committed_spy.size(), 3);
        position = inspector_position(inspector);
        QVERIFY(near(position.x(), -2.0F) && near(position.y(), 0.5F));

        undo->trigger();
        QTRY_VERIFY(near(inspector_position(inspector).y(), 0.0F));
        const auto sprite = find_text(hierarchy->model(), QStringLiteral("Dragon Sprite (2D)"));
        const auto cube = find_text(hierarchy->model(), QStringLiteral("Lit Cube (3D)"));
        QVERIFY(sprite.isValid() && cube.isValid());
        hierarchy->setCurrentIndex(sprite);
        hierarchy->selectionModel()->select(
            sprite, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        hierarchy->selectionModel()->select(cube, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        QCoreApplication::processEvents();
        camera_spy.clear();
        viewport->setFocus();
        QTest::keyClick(viewport, Qt::Key_F);
        QTRY_VERIFY(!camera_spy.isEmpty());
        focus_target = camera_spy.back().at(2).value<QVector3D>();
        QVERIFY(near(focus_target.x(), -0.25F)
                && near(focus_target.y(), 0.0F)
                && near(focus_target.z(), 0.0F));
    }

    void linked_prefab_activation_edit_revert_unpack_and_undo_use_real_widgets()
    {
        EditorWindow window{QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT)};
        window.set_unsaved_prompt([](const QString&) {
            return EditorWindow::UnsavedDecision::discard;
        });
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        auto* hierarchy = window.findChild<QTreeView*>(QStringLiteral("HierarchyView"));
        auto* project = window.findChild<QTreeView*>(QStringLiteral("ProjectExplorerView"));
        auto* apply = window.findChild<QAction*>(QStringLiteral("ApplyPrefabAction"));
        auto* revert_selected = window.findChild<QAction*>(QStringLiteral("RevertSelectedPrefabAction"));
        auto* unpack_completely = window.findChild<QAction*>(QStringLiteral("UnpackCompletelyPrefabAction"));
        QVERIFY(hierarchy != nullptr && project != nullptr);
        QVERIFY(apply != nullptr && revert_selected != nullptr && unpack_completely != nullptr);
        project->expandAll();

        const auto initial_count = recursive_rows(hierarchy->model());
        const auto prefab = find_text(project->model(), QStringLiteral("StarterCube"));
        QVERIFY2(prefab.isValid(), "Sample linked prefab was not indexed by the Project Explorer.");
        project->scrollTo(prefab);
        QTest::mouseClick(project->viewport(), Qt::LeftButton, {}, project->visualRect(prefab).center());
        project->setCurrentIndex(prefab);
        project->setFocus();
        QTest::keyClick(project, Qt::Key_Return);
        QTRY_COMPARE(recursive_rows(hierarchy->model()), initial_count + 1);

        const auto linked = find_text(hierarchy->model(), QStringLiteral("Starter Cube Prefab"));
        QVERIFY(linked.isValid());
        hierarchy->selectionModel()->clearSelection();
        hierarchy->setCurrentIndex(linked);
        hierarchy->selectionModel()->select(
            linked, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        QTRY_VERIFY(unpack_completely->isEnabled());
        QVERIFY(!apply->isEnabled());

        hierarchy->setFocus();
        QTest::keyClick(hierarchy, Qt::Key_F2);
        auto* rename_editor = hierarchy->findChild<QLineEdit*>();
        QVERIFY(rename_editor != nullptr);
        rename_editor->selectAll();
        QTest::keyClicks(rename_editor, QStringLiteral("Prefab Override"));
        QTest::keyClick(rename_editor, Qt::Key_Return);
        QTRY_VERIFY(find_text(hierarchy->model(), QStringLiteral("Prefab Override")).isValid());
        QTRY_VERIFY(apply->isEnabled());
        QTRY_VERIFY(revert_selected->isEnabled());

        revert_selected->trigger();
        QTRY_VERIFY(find_text(hierarchy->model(), QStringLiteral("Starter Cube Prefab")).isValid());
        const auto restored = find_text(hierarchy->model(), QStringLiteral("Starter Cube Prefab"));
        hierarchy->setCurrentIndex(restored);
        hierarchy->selectionModel()->select(
            restored, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        unpack_completely->trigger();
        QTRY_COMPARE(recursive_rows(hierarchy->model()), initial_count + 1);
        QTRY_VERIFY(!unpack_completely->isEnabled());

        auto* undo = window.findChild<QAction*>(QStringLiteral("UndoAction"));
        QVERIFY(undo != nullptr && undo->isEnabled());
        undo->trigger();
        QTRY_VERIFY(unpack_completely->isEnabled());
    }

    void multiple_inspectors_lock_independent_targets_and_restore_mixed_before_images()
    {
        EditorWindow window{QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT)};
        window.set_unsaved_prompt([](const QString&) {
            return EditorWindow::UnsavedDecision::discard;
        });
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        auto* hierarchy = window.findChild<QTreeView*>(QStringLiteral("HierarchyView"));
        auto* primary_name = window.findChild<QLineEdit*>(QStringLiteral("InspectorGameObjectName"));
        auto* primary_lock = window.findChild<QToolButton*>(QStringLiteral("InspectorLock"));
        auto* create_inspector = window.findChild<QAction*>(QStringLiteral("NewInspectorAction"));
        QVERIFY(hierarchy != nullptr && primary_name != nullptr && primary_lock != nullptr);
        QVERIFY(create_inspector != nullptr && create_inspector->isEnabled());
        QVERIFY(hierarchy->model()->rowCount() >= 3);

        const auto first = hierarchy->model()->index(0, 0);
        const auto second = hierarchy->model()->index(1, 0);
        const auto third = hierarchy->model()->index(2, 0);
        const auto parse_id = [](const QModelIndex& index) {
            return dragonpixel::core::uuid::parse(
                index.data(EditorRoles::entity_id).toString().toStdString());
        };
        const auto first_id = parse_id(first);
        const auto second_id = parse_id(second);
        const auto third_id = parse_id(third);
        QVERIFY(first_id && second_id && third_id);

        hierarchy->selectionModel()->select(first,
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        hierarchy->setCurrentIndex(first);
        const auto first_name = QString::fromStdString(window.scene_->find_entity(*first_id)->name);
        QTRY_COMPARE(primary_name->text(), first_name);
        primary_lock->setChecked(true);
        hierarchy->selectionModel()->select(third,
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        hierarchy->setCurrentIndex(third);
        QTRY_COMPARE(primary_name->text(), first_name);

        primary_lock->setChecked(false);
        create_inspector->trigger();
        auto* extra_dock = window.findChild<QDockWidget*>(QStringLiteral("Dock.Inspector.2"));
        auto* extra_view = window.findChild<QTreeView*>(QStringLiteral("InspectorView.2"));
        auto* extra_lock = window.findChild<QToolButton*>(QStringLiteral("InspectorLock.2"));
        auto* extra_enabled = window.findChild<QCheckBox*>(QStringLiteral("InspectorGameObjectEnabled.2"));
        auto* extra_name = window.findChild<QLineEdit*>(QStringLiteral("InspectorGameObjectName.2"));
        auto* extra_identity = window.findChild<QLabel*>(QStringLiteral("InspectorGameObjectIdentity.2"));
        QVERIFY(extra_dock != nullptr && extra_view != nullptr && extra_lock != nullptr);
        QVERIFY(extra_enabled != nullptr && extra_name != nullptr && extra_identity != nullptr);
        QCOMPARE(extra_dock->accessibleName(), QStringLiteral("Inspector 2"));

        hierarchy->selectionModel()->clearSelection();
        hierarchy->setCurrentIndex(first);
        QItemSelection pair_selection;
        pair_selection.select(first, first);
        pair_selection.select(second, second);
        hierarchy->selectionModel()->select(pair_selection,
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        QTRY_COMPARE(window.selection_service_.snapshot().ordered_entity_ids.size(), 2);
        QTRY_VERIFY(extra_name->text().contains(QStringLiteral("2 GameObjects")));
        extra_lock->setChecked(true);
        hierarchy->selectionModel()->select(third,
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        hierarchy->setCurrentIndex(third);
        QTRY_VERIFY(extra_name->text().contains(QStringLiteral("2 GameObjects")));
        QVERIFY(window.selection_service_.snapshot().ordered_entity_ids.contains(*third_id));

        const auto transform_position = [](const dragonpixel::scene::entity* entity) {
            const auto component = std::find_if(entity->components.cbegin(), entity->components.cend(),
                [](const auto& candidate) {
                    return candidate.type_id == dragonpixel::metadata::builtin_component_ids::transform;
                });
            return component->properties.at("dpe.transform.position");
        };
        const auto first_position_before = transform_position(window.scene_->find_entity(*first_id));
        const auto second_position_before = transform_position(window.scene_->find_entity(*second_id));
        QVERIFY(first_position_before != second_position_before);
        auto mixed_position = find_text(extra_view->model(), QStringLiteral("Position"));
        QVERIFY(mixed_position.isValid());
        mixed_position = mixed_position.siblingAtColumn(1);
        QCOMPARE(mixed_position.data(Qt::DisplayRole).toString(), QStringLiteral("<mixed>"));
        extra_dock->raise();
        extra_view->scrollTo(mixed_position);
        QTest::mouseDClick(extra_view->viewport(), Qt::LeftButton, Qt::NoModifier,
            extra_view->visualRect(mixed_position).center());
        QTest::keyClick(extra_view, Qt::Key_Escape);
        QCOMPARE(transform_position(window.scene_->find_entity(*first_id)), first_position_before);
        QCOMPARE(transform_position(window.scene_->find_entity(*second_id)), second_position_before);

        const auto shared_position = QStringLiteral("{\"x\":3.0,\"y\":4.0,\"z\":0.0}");
        QVERIFY(extra_view->model()->setData(mixed_position, shared_position, Qt::EditRole));
        QTRY_COMPARE(transform_position(window.scene_->find_entity(*first_id)).at("x").get<double>(), 3.0);
        QTRY_COMPARE(transform_position(window.scene_->find_entity(*second_id)).at("y").get<double>(), 4.0);
        auto* undo = window.findChild<QAction*>(QStringLiteral("UndoAction"));
        QVERIFY(undo != nullptr);
        QTRY_VERIFY(undo->isEnabled());
        undo->trigger();
        QTRY_COMPARE(transform_position(window.scene_->find_entity(*first_id)), first_position_before);
        QTRY_COMPARE(transform_position(window.scene_->find_entity(*second_id)), second_position_before);

        const auto first_before = window.scene_->find_entity(*first_id)->enabled;
        const auto second_before = window.scene_->find_entity(*second_id)->enabled;
        const auto third_before = window.scene_->find_entity(*third_id)->enabled;
        extra_enabled->setCheckState(Qt::Unchecked);
        QTRY_VERIFY(!window.scene_->find_entity(*first_id)->enabled);
        QTRY_VERIFY(!window.scene_->find_entity(*second_id)->enabled);
        QCOMPARE(window.scene_->find_entity(*third_id)->enabled, third_before);

        QVERIFY(undo != nullptr && undo->isEnabled());
        undo->trigger();
        QTRY_COMPARE(window.scene_->find_entity(*first_id)->enabled, first_before);
        QTRY_COMPARE(window.scene_->find_entity(*second_id)->enabled, second_before);
        QCOMPARE(window.scene_->find_entity(*third_id)->enabled, third_before);
        QVERIFY(extra_lock->isChecked());
        QVERIFY(extra_view->model()->rowCount() > 0);

        window.set_delete_prompt([](const QString&, int) { return true; });
        const auto restored_first = find_role(hierarchy->model(), EditorRoles::entity_id,
            QString::fromStdString(first_id->to_string()));
        QVERIFY(restored_first.isValid());
        hierarchy->setCurrentIndex(restored_first);
        hierarchy->selectionModel()->select(restored_first,
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        auto* remove = window.findChild<QAction*>(QStringLiteral("DeleteGameObjectAction"));
        QVERIFY(remove != nullptr && remove->isEnabled());
        remove->trigger();
        QTRY_VERIFY(window.scene_->find_entity(*first_id) == nullptr);
        QTRY_VERIFY(extra_identity->text().contains(QStringLiteral("unavailable"), Qt::CaseInsensitive));
        QVERIFY(!extra_enabled->isEnabled());
        undo->trigger();
        QTRY_VERIFY(window.scene_->find_entity(*first_id) != nullptr);
        QTRY_VERIFY(extra_view->model()->rowCount() > 0);
        QVERIFY(extra_lock->isChecked());

        QVERIFY(window.close());
        EditorWindow reopened{QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT)};
        reopened.set_unsaved_prompt([](const QString&) {
            return EditorWindow::UnsavedDecision::discard;
        });
        reopened.show();
        QVERIFY(QTest::qWaitForWindowExposed(&reopened));
        auto* restored_dock = reopened.findChild<QDockWidget*>(QStringLiteral("Dock.Inspector.2"));
        auto* restored_lock = reopened.findChild<QToolButton*>(QStringLiteral("InspectorLock.2"));
        QVERIFY(restored_dock != nullptr && restored_lock != nullptr);
        QVERIFY(!restored_lock->isChecked());
    }

    void hierarchy_grouped_operations_are_atomic_and_reject_cycles_without_partial_movement()
    {
        EditorWindow window{QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT)};
        window.set_unsaved_prompt([](const QString&) {
            return EditorWindow::UnsavedDecision::discard;
        });
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* hierarchy = window.findChild<QTreeView*>(QStringLiteral("HierarchyView"));
        QVERIFY(hierarchy != nullptr);
        const auto sprite = find_text(hierarchy->model(), QStringLiteral("Dragon Sprite (2D)"));
        const auto cube = find_text(hierarchy->model(), QStringLiteral("Lit Cube (3D)"));
        const auto light = find_text(hierarchy->model(), QStringLiteral("Key Light"));
        QVERIFY(sprite.isValid() && cube.isValid() && light.isValid());
        const auto parse_id = [](const QModelIndex& index) {
            return dragonpixel::core::uuid::parse(
                index.data(EditorRoles::entity_id).toString().toStdString());
        };
        const auto sprite_id = parse_id(sprite);
        const auto cube_id = parse_id(cube);
        const auto light_id = parse_id(light);
        QVERIFY(sprite_id && cube_id && light_id);

        hierarchy->setCurrentIndex(sprite);
        QItemSelection selected_roots;
        selected_roots.select(sprite, sprite);
        selected_roots.select(cube, cube);
        hierarchy->selectionModel()->select(selected_roots,
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        const auto before_count = window.scene_->entities().size();
        window.group_selected();
        QCOMPARE(window.scene_->entities().size(), before_count + 1);
        const auto group_id = window.selected_entity_id();
        QVERIFY(group_id.has_value());
        QCOMPARE(window.scene_->find_entity(*sprite_id)->parent_id, group_id);
        QCOMPARE(window.scene_->find_entity(*cube_id)->parent_id, group_id);

        const auto light_parent_before = window.scene_->find_entity(*light_id)->parent_id;
        QVERIFY(!window.drag_reparent_entities(
            {*group_id, *light_id}, std::optional<dragonpixel::core::uuid>{*sprite_id}, std::nullopt));
        QCoreApplication::processEvents();
        QCOMPARE(window.scene_->find_entity(*group_id)->parent_id,
            std::optional<dragonpixel::core::uuid>{});
        QCOMPARE(window.scene_->find_entity(*light_id)->parent_id, light_parent_before);

        QVERIFY(window.drag_reparent_entities(
            {*sprite_id, *cube_id}, std::optional<dragonpixel::core::uuid>{*light_id}, 0));
        QCoreApplication::processEvents();
        QCOMPARE(window.scene_->find_entity(*sprite_id)->parent_id,
            std::optional<dragonpixel::core::uuid>{*light_id});
        QCOMPARE(window.scene_->find_entity(*cube_id)->parent_id,
            std::optional<dragonpixel::core::uuid>{*light_id});
        window.undo();
        QCOMPARE(window.scene_->find_entity(*sprite_id)->parent_id,
            std::optional<dragonpixel::core::uuid>{*group_id});
        QCOMPARE(window.scene_->find_entity(*cube_id)->parent_id,
            std::optional<dragonpixel::core::uuid>{*group_id});
    }

    void project_and_hierarchy_domain_drops_preserve_ids_and_create_linked_prefabs()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto sample_root = QFileInfo{QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT)}.absolutePath();
        const auto project_root = temporary.filePath(QStringLiteral("DropProject"));
        QVERIFY(copy_directory_tree(sample_root, project_root));
        const auto manifest = QDir{project_root}.filePath(QStringLiteral("DragonPixelProject.json"));

        AssetService assets;
        QVERIFY(assets.create_folder(manifest, QStringLiteral("Assets/Moved")).succeeded);
        QVERIFY(assets.create_folder(manifest, QStringLiteral("Assets/FolderSource")).succeeded);
        QVERIFY(assets.create_folder(manifest, QStringLiteral("Assets/FolderSource/Empty")).succeeded);
        const auto external_png = temporary.filePath(QStringLiteral("drop-source.png"));
        QImage image{4, 4, QImage::Format_RGBA8888};
        image.fill(QColor{35, 180, 240, 255});
        QVERIFY(image.save(external_png, "PNG"));
        const auto imported = assets.import_files({
            manifest, {external_png}, QStringLiteral("Assets"),
            AssetImportOwnership::copy_into_project});
        QVERIFY(imported.succeeded);
        QCOMPARE(imported.asset_ids.size(), 1);
        const auto stable_asset_id = imported.asset_ids.front();

        EditorWindow window{manifest};
        window.set_unsaved_prompt([](const QString&) {
            return EditorWindow::UnsavedDecision::discard;
        });
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.findChild<QTreeView*>(QStringLiteral("ProjectFolderTree")) != nullptr);
        QVERIFY(window.findChild<QListView*>(QStringLiteral("ProjectThumbnailView")) != nullptr);
        QVERIFY(window.findChild<QLabel*>(QStringLiteral("ProjectBreadcrumb")) != nullptr);
        QVERIFY(window.findChild<QLabel*>(QStringLiteral("ProjectDetailsPane")) != nullptr);

        const auto asset_source = find_role(window.project_model_, EditorRoles::asset_id, stable_asset_id);
        const auto moved_folder = find_role(window.project_model_, EditorRoles::project_logical_path,
            QStringLiteral("Assets/Moved"));
        QVERIFY(asset_source.isValid() && moved_folder.isValid());
        std::unique_ptr<QMimeData> asset_mime{window.project_model_->mimeData({asset_source})};
        QVERIFY(window.handle_project_browser_drop(asset_mime.get(), moved_folder));
        const auto moved_index = ProjectIndexService{}.build_candidate(manifest);
        QVERIFY(moved_index.succeeded());
        const auto moved_entry = std::find_if(
            moved_index.candidate->entries.begin(), moved_index.candidate->entries.end(),
            [&](const auto& entry) { return entry.id == stable_asset_id; });
        QVERIFY(moved_entry != moved_index.candidate->entries.end());
        QCOMPARE(QFileInfo{moved_entry->resolved_source_path}.absolutePath(),
            QDir::cleanPath(QDir{project_root}.filePath(QStringLiteral("Assets/Moved"))));

        const auto folder_source = find_role(window.project_model_, EditorRoles::project_logical_path,
            QStringLiteral("Assets/FolderSource"));
        const auto folder_destination = find_role(window.project_model_, EditorRoles::project_logical_path,
            QStringLiteral("Assets/Moved"));
        QVERIFY(folder_source.isValid() && folder_destination.isValid());
        std::unique_ptr<QMimeData> folder_mime{window.project_model_->mimeData({folder_source})};
        QVERIFY(window.handle_project_browser_drop(folder_mime.get(), folder_destination));
        QVERIFY(!QFileInfo::exists(QDir{project_root}.filePath(QStringLiteral("Assets/FolderSource"))));
        QVERIFY(QFileInfo{QDir{project_root}.filePath(
            QStringLiteral("Assets/Moved/FolderSource/Empty"))}.isDir());

        const auto scene_count_before_drop = window.scene_->entities().size();
        window.viewport_->project_item_dropped(
            window.project_index_.candidate->project_id,
            static_cast<qint64>(window.project_model_->drag_revision()),
            moved_entry->absolute_path,
            QStringLiteral("asset"), QStringLiteral("sprite"), stable_asset_id);
        QCOMPARE(window.scene_->entities().size(), scene_count_before_drop + 1);
        const auto dropped_id = window.selected_entity_id();
        QVERIFY(dropped_id.has_value());
        const auto* dropped = window.scene_->find_entity(*dropped_id);
        QVERIFY(dropped != nullptr && !dropped->parent_id.has_value());
        const auto dropped_transform = std::find_if(dropped->components.cbegin(), dropped->components.cend(),
            [](const auto& component) {
                return component.type_id == dragonpixel::metadata::builtin_component_ids::transform;
            });
        const auto dropped_sprite = std::find_if(dropped->components.cbegin(), dropped->components.cend(),
            [](const auto& component) {
                return component.type_id == dragonpixel::metadata::builtin_component_ids::sprite;
            });
        QVERIFY(dropped_transform != dropped->components.cend()
            && dropped_sprite != dropped->components.cend());
        const auto& dropped_position = dropped_transform->properties.at("dpe.transform.position");
        QCOMPARE(dropped_position.at("x").get<double>(), 0.0);
        QCOMPARE(dropped_position.at("y").get<double>(), 0.0);
        QCOMPARE(dropped_position.at("z").get<double>(), 0.0);
        QCOMPARE(QString::fromStdString(
            dropped_sprite->properties.at("dpe.sprite.asset").get<std::string>()), stable_asset_id);

        const auto sprite_proxy = find_text(window.hierarchy_->model(), QStringLiteral("Dragon Sprite (2D)"));
        QVERIFY(sprite_proxy.isValid());
        window.hierarchy_->setCurrentIndex(sprite_proxy);
        window.hierarchy_->selectionModel()->select(sprite_proxy,
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        const auto texture_field = find_text(window.inspector_->model(), QStringLiteral("Texture"));
        const auto moved_asset_source = find_role(window.project_model_, EditorRoles::asset_id, stable_asset_id);
        QVERIFY(texture_field.isValid() && moved_asset_source.isValid());
        std::unique_ptr<QMimeData> inspector_asset_mime{
            window.project_model_->mimeData({moved_asset_source})};
        QVERIFY(window.assign_inspector_asset_drop(window.inspector_, texture_field, inspector_asset_mime.get()));
        const auto sprite_id = dragonpixel::core::uuid::parse(
            sprite_proxy.data(EditorRoles::entity_id).toString().toStdString());
        QVERIFY(sprite_id.has_value());
        const auto* sprite_entity = window.scene_->find_entity(*sprite_id);
        const auto sprite_component = std::find_if(sprite_entity->components.begin(), sprite_entity->components.end(),
            [](const auto& component) {
                return component.type_id == dragonpixel::metadata::builtin_component_ids::sprite;
            });
        QVERIFY(sprite_component != sprite_entity->components.end());
        QCOMPARE(QString::fromStdString(sprite_component->properties.at("dpe.sprite.asset").get<std::string>()),
            stable_asset_id);

        const auto tileset_entry = std::find_if(
            window.project_index_.candidate->entries.cbegin(),
            window.project_index_.candidate->entries.cend(),
            [](const auto& entry) {
                return entry.kind == ProjectIndexEntryKind::asset
                    && entry.asset_type.contains(QStringLiteral("tileset"), Qt::CaseInsensitive);
            });
        QVERIFY(tileset_entry != window.project_index_.candidate->entries.cend());
        const auto tileset_id = tileset_entry->id;
        QVERIFY(window.create_tilemap_from_tileset(
            tileset_id, QStringLiteral("Interaction Map")));
        QVERIFY(window.create_tilemap_from_tileset(
            tileset_id, QStringLiteral("Alternate Interaction Map")));
        const auto created_maps = [&window] {
            QStringList ids;
            for (const auto& entry : window.project_index_.candidate->entries)
            {
                if (entry.kind == ProjectIndexEntryKind::asset
                    && entry.asset_type.contains(QStringLiteral("tilemap"), Qt::CaseInsensitive)
                    && (QFileInfo{entry.resolved_source_path}.completeBaseName()
                            == QStringLiteral("Interaction Map")
                        || QFileInfo{entry.resolved_source_path}.completeBaseName()
                            == QStringLiteral("Alternate Interaction Map")))
                {
                    ids.push_back(entry.id);
                }
            }
            ids.sort();
            return ids;
        }();
        QCOMPARE(created_maps.size(), 2);
        const auto primary_map_id = created_maps.front();
        const auto alternate_map_id = created_maps.back();
        const auto primary_map_source = find_role(
            window.project_model_, EditorRoles::asset_id, primary_map_id);
        const auto alternate_map_source = find_role(
            window.project_model_, EditorRoles::asset_id, alternate_map_id);
        QVERIFY(primary_map_source.isValid() && alternate_map_source.isValid());

        const auto tilemap_count_before = window.scene_->entities().size();
        const auto* primary_map_entry = window.project_index_.candidate->find_by_id(primary_map_id);
        QVERIFY(primary_map_entry != nullptr);
        window.viewport_->project_item_dropped(
            window.project_index_.candidate->project_id,
            static_cast<qint64>(window.project_model_->drag_revision()),
            primary_map_entry->absolute_path,
            QStringLiteral("asset"), QStringLiteral("tilemap"), primary_map_id);
        QCOMPARE(window.scene_->entities().size(), tilemap_count_before + 1);
        const auto tilemap_entity_id = window.selected_entity_id();
        QVERIFY(tilemap_entity_id.has_value());
        const auto* tilemap_entity = window.scene_->find_entity(*tilemap_entity_id);
        QVERIFY(tilemap_entity != nullptr);
        const auto tilemap_component = std::find_if(
            tilemap_entity->components.cbegin(), tilemap_entity->components.cend(),
            [](const auto& component) {
                return component.type_id
                    == dragonpixel::metadata::builtin_component_ids::tilemap_2d;
            });
        QVERIFY(tilemap_component != tilemap_entity->components.cend());
        QCOMPARE(QString::fromStdString(tilemap_component->properties
            .at("dpe.tilemap.asset").get<std::string>()), primary_map_id);

        const auto tilemap_field = find_text(
            window.inspector_->model(), QStringLiteral("Tilemap"));
        QVERIFY(tilemap_field.isValid());
        std::unique_ptr<QMimeData> alternate_map_mime{
            window.project_model_->mimeData({alternate_map_source})};
        QVERIFY(window.assign_inspector_asset_drop(
            window.inspector_, tilemap_field, alternate_map_mime.get()));
        tilemap_entity = window.scene_->find_entity(*tilemap_entity_id);
        const auto reassigned_component = std::find_if(
            tilemap_entity->components.cbegin(), tilemap_entity->components.cend(),
            [](const auto& component) {
                return component.type_id
                    == dragonpixel::metadata::builtin_component_ids::tilemap_2d;
            });
        QCOMPARE(QString::fromStdString(reassigned_component->properties
            .at("dpe.tilemap.asset").get<std::string>()), alternate_map_id);

        std::unique_ptr<QMimeData> hierarchy_map_mime{
            window.project_model_->mimeData({primary_map_source})};
        const auto hierarchy_parent_proxy = find_text(
            window.hierarchy_->model(), QStringLiteral("Dragon Sprite (2D)"));
        QVERIFY(hierarchy_parent_proxy.isValid());
        const auto hierarchy_parent_source = window.hierarchy_filter_->mapToSource(
            hierarchy_parent_proxy);
        const auto before_hierarchy_drop = window.scene_->entities().size();
        QVERIFY(window.hierarchy_model_->dropMimeData(
            hierarchy_map_mime.get(), Qt::CopyAction, -1, 0, hierarchy_parent_source));
        QCOMPARE(window.scene_->entities().size(), before_hierarchy_drop + 1);
        const auto parent_entity = std::find_if(
            window.scene_->entities().begin(), window.scene_->entities().end(),
            [](const auto& entity) {
                return entity.name == "Dragon Sprite (2D)";
            });
        QVERIFY(parent_entity != window.scene_->entities().end());
        const auto parent_id = std::optional<dragonpixel::core::uuid>{parent_entity->id};
        const auto child_tilemap = std::find_if(
            window.scene_->entities().begin(), window.scene_->entities().end(),
            [&](const auto& entity) {
                return entity.parent_id == parent_id
                    && std::any_of(entity.components.begin(), entity.components.end(),
                        [](const auto& component) {
                            return component.type_id
                                == dragonpixel::metadata::builtin_component_ids::tilemap_2d;
                        });
            });
        QVERIFY(child_tilemap != window.scene_->entities().end());
        QCOMPARE(child_tilemap->parent_id,
            std::optional<dragonpixel::core::uuid>{*parent_id});

        const auto prefab_folder = find_role(window.project_model_, EditorRoles::project_logical_path,
            QStringLiteral("Prefabs"));
        QVERIFY(prefab_folder.isValid());
        const auto hierarchy_proxy = find_text(window.hierarchy_->model(), QStringLiteral("Dragon Sprite (2D)"));
        QVERIFY(hierarchy_proxy.isValid());
        const auto hierarchy_source = window.hierarchy_filter_->mapToSource(hierarchy_proxy);
        const auto source_id = dragonpixel::core::uuid::parse(
            hierarchy_source.data(EditorRoles::entity_id).toString().toStdString());
        QVERIFY(source_id.has_value());
        const auto* source_entity = window.scene_->find_entity(*source_id);
        QVERIFY(source_entity != nullptr);
        auto safe_name = QString::fromStdString(source_entity->name);
        safe_name.replace(QRegularExpression{QStringLiteral("[^A-Za-z0-9_-]+")}, QStringLiteral("_"));
        const auto expected_prefab = QDir{project_root}.filePath(
            QStringLiteral("Prefabs/%1.dpeprefab").arg(safe_name));
        QVERIFY(!QFileInfo::exists(expected_prefab));
        std::unique_ptr<QMimeData> hierarchy_mime{
            window.hierarchy_model_->mimeData({hierarchy_source})};
        QVERIFY(window.handle_project_browser_drop(hierarchy_mime.get(), prefab_folder));
        QVERIFY(QFileInfo::exists(expected_prefab));

        const auto second_proxy = find_text(window.hierarchy_->model(), QStringLiteral("Lit Cube (3D)"));
        QVERIFY(second_proxy.isValid());
        const auto second_source = window.hierarchy_filter_->mapToSource(second_proxy);
        QVERIFY(second_source.isValid());
        std::unique_ptr<QMimeData> cross_project_mime{
            window.hierarchy_model_->mimeData({second_source})};
        auto payload = QJsonDocument::fromJson(cross_project_mime->data(
            QStringLiteral("application/x-dragonpixel-entity"))).object();
        payload.insert(QStringLiteral("projectId"), QStringLiteral("different-project"));
        cross_project_mime->setData(QStringLiteral("application/x-dragonpixel-entity"),
            QJsonDocument{payload}.toJson(QJsonDocument::Compact));
        const auto refreshed_prefab_folder = find_role(window.project_model_,
            EditorRoles::project_logical_path, QStringLiteral("Prefabs"));
        QVERIFY(refreshed_prefab_folder.isValid());
        QVERIFY(!window.handle_project_browser_drop(cross_project_mime.get(), refreshed_prefab_folder));
    }

    void metadata_v4_polymorphic_inspector_and_tile_transactions_are_functional()
    {
        EditorWindow window{QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT)};
        window.set_unsaved_prompt([](const QString&) {
            return EditorWindow::UnsavedDecision::discard;
        });
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        auto* hierarchy = window.findChild<QTreeView*>(QStringLiteral("HierarchyView"));
        auto* inspector = window.findChild<QTreeView*>(QStringLiteral("InspectorView"));
        auto* add_component = window.findChild<QPushButton*>(QStringLiteral("InspectorAddComponent"));
        QVERIFY(hierarchy != nullptr && inspector != nullptr && add_component != nullptr);
        const auto selected = hierarchy->model()->index(0, 0);
        QVERIFY(selected.isValid());
        hierarchy->selectionModel()->select(selected, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        hierarchy->setCurrentIndex(selected);

        QTimer::singleShot(0, &window, [] {
            auto* dialog = QApplication::activeModalWidget();
            QVERIFY(dialog != nullptr);
            auto* search = dialog->findChild<QLineEdit*>(QStringLiteral("AddComponentSearch"));
            auto* list = dialog->findChild<QListWidget*>(QStringLiteral("AddComponentList"));
            auto* confirm = dialog->findChild<QPushButton*>(QStringLiteral("ConfirmAddComponent"));
            auto* create_csharp = dialog->findChild<QPushButton*>(QStringLiteral("CreateCSharpScriptFromInspector"));
            auto* create_cpp = dialog->findChild<QPushButton*>(QStringLiteral("CreateCppComponentFromInspector"));
            QVERIFY(search != nullptr && list != nullptr && confirm != nullptr);
            QVERIFY(create_csharp != nullptr && create_cpp != nullptr);
            QTest::keyClicks(search, QStringLiteral("Managed Mover"));
            int visible_row = -1;
            for (int row = 0; row < list->count(); ++row)
            {
                if (!list->item(row)->isHidden())
                {
                    visible_row = row;
                    break;
                }
            }
            QVERIFY(visible_row >= 0);
            list->setCurrentRow(visible_row);
            QTest::mouseClick(confirm, Qt::LeftButton);
        });
        QTest::mouseClick(add_component, Qt::LeftButton);
        QTRY_VERIFY(find_text(inspector->model(), QStringLiteral("Managed Mover")).isValid());
        QVERIFY(find_text(inspector->model(), QStringLiteral("Script Source")).isValid());
        QVERIFY(!find_text(inspector->model(), QStringLiteral("Lifecycle")).isValid());

        auto profile = find_text(inspector->model(), QStringLiteral("Movement Profile"));
        QVERIFY(profile.isValid());
        const auto profile_value = profile.siblingAtColumn(1);
        inspector->scrollTo(profile_value);
        inspector->setCurrentIndex(profile_value);
        inspector->edit(profile_value);
        QApplication::processEvents();
        auto* type_editor = inspector->findChild<QComboBox*>(QStringLiteral("InspectorPolymorphicEditor"));
        QVERIFY(type_editor != nullptr);
        QCOMPARE(type_editor->findText(QStringLiteral("Platform Movement")) >= 0, true);
        type_editor->setCurrentIndex(type_editor->findText(QStringLiteral("Platform Movement")));
        type_editor->setProperty("dpeDirty", true);
        QTest::keyClick(type_editor, Qt::Key_Return);
        QTRY_VERIFY(find_text(inspector->model(), QStringLiteral("Speed")).isValid());

        auto speed = find_text(inspector->model(), QStringLiteral("Speed"));
        const auto speed_value = speed.siblingAtColumn(1);
        inspector->scrollTo(speed_value);
        inspector->setCurrentIndex(speed_value);
        inspector->edit(speed_value);
        QApplication::processEvents();
        auto* number_editor = inspector->findChild<QDoubleSpinBox*>(QStringLiteral("InspectorNumberEditor"));
        QVERIFY(number_editor != nullptr);
        number_editor->setValue(12.5);
        QTest::keyClick(number_editor, Qt::Key_Return);
        QTRY_VERIFY(find_text(inspector->model(), QStringLiteral("Speed")).siblingAtColumn(1).data().toString().contains(QStringLiteral("12.5")));

        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto project_root = QFileInfo{QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT)}.absolutePath();
        const auto source_map = QDir{project_root}.filePath(QStringLiteral("Assets/Tiles/StarterMap.dpetilemap"));
        const auto source_set = QDir{project_root}.filePath(QStringLiteral("Assets/Tiles/StarterTiles.dpetileset"));
        const auto map_copy = QDir{temporary.path()}.filePath(QStringLiteral("Map.dpetilemap"));
        const auto set_copy = QDir{temporary.path()}.filePath(QStringLiteral("Tiles.dpetileset"));
        QVERIFY(QFile::copy(source_map, map_copy));
        QVERIFY(QFile::copy(source_set, set_copy));

        TileDocumentService tiles;
        QVERIFY(tiles.load(map_copy, set_copy));
        const auto tile_id = tiles.tileset()->tiles.front().tile_id;
        tiles.begin_stroke();
        QVERIFY(tiles.paint_cell(0, 32, 0, tile_id));
        QVERIFY(tiles.paint_cell(0, -1, -1, tile_id));
        tiles.commit_stroke();
        QVERIFY(tiles.is_dirty());
        QVERIFY(tiles.tile_at(0, 32, 0).has_value());
        QVERIFY(tiles.tile_at(0, -1, -1).has_value());
        QVERIFY(tiles.undo());
        QVERIFY(!tiles.tile_at(0, 32, 0).has_value());
        QVERIFY(tiles.redo());
        QVERIFY(tiles.tile_at(0, 32, 0).has_value());
        tiles.begin_stroke();
        QVERIFY(tiles.preview_rectangle(0, 3, 3, 4, 4, tile_id, false));
        tiles.cancel_stroke();
        QVERIFY(!tiles.tile_at(0, 4, 4).has_value());
        QVERIFY(tiles.save());
        QVERIFY(!tiles.is_dirty());
        QFile saved{map_copy};
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QVERIFY(dragonpixel::tiles::read_tilemap(saved.readAll().toStdString()).succeeded());
    }

    void worker_transport_files_are_cleaned_up()
    {
        const auto pattern = QStringLiteral("dpe-s1-%1-*.frame").arg(QApplication::applicationPid());
        QTRY_VERIFY_WITH_TIMEOUT(
            QDir{QDir::tempPath()}.entryList({pattern}, QDir::Files).isEmpty(),
            3000);
    }

    void png_tileset_creation_is_contained_deterministic_and_non_overwriting()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QVERIFY(QDir{temporary.path()}.mkpath(QStringLiteral("Assets")));
        const auto outside_png = QDir{QDir::tempPath()}.filePath(
            QStringLiteral("dpe-tileset-test-%1.png").arg(QApplication::applicationPid()));
        QImage source{64, 32, QImage::Format_ARGB32};
        source.fill(QColor{20, 40, 80});
        for (int y = 0; y < 32; ++y)
            for (int x = 32; x < 64; ++x) source.setPixelColor(x, y, QColor{220, 120, 30});
        QVERIFY(source.save(outside_png, "PNG"));

        TileSetCreationRequest request;
        request.project_root = temporary.path();
        request.source_png = outside_png;
        request.name = QStringLiteral("Interaction Tiles");
        request.cell_width = 32;
        request.cell_height = 32;
        request.pixels_per_unit = 32.0;
        const auto created = TileSetCreationService::create(request);
        QVERIFY2(created.succeeded, qPrintable(created.error));
        QCOMPARE(created.tile_count, 2);
        QVERIFY(QFileInfo{created.texture_path}.absoluteFilePath().startsWith(
            QFileInfo{temporary.path()}.absoluteFilePath()));
        QFile tiles{created.tile_set_path};
        QVERIFY(tiles.open(QIODevice::ReadOnly));
        const auto parsed = dragonpixel::tiles::read_tile_set(tiles.readAll().toStdString());
        QVERIFY(parsed.succeeded());
        QCOMPARE(parsed.document->tiles.size(), std::size_t{2});
        QSet<int> source_x;
        for (const auto& tile : parsed.document->tiles) source_x.insert(tile.source.x);
        QCOMPARE(source_x, QSet<int>({0, 32}));
        QVERIFY(parsed.document->tiles.at(0).tile_id != parsed.document->tiles.at(1).tile_id);

        const auto duplicate = TileSetCreationService::create(request);
        QVERIFY(!duplicate.succeeded);
        QVERIFY(duplicate.error.contains(QStringLiteral("already exists")));
        QVERIFY(QFile::remove(outside_png));
    }

    void generated_csharp_script_attaches_to_the_selected_gameobject()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto source_root = QFileInfo{QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT)}.absolutePath();
        const auto project_root = QDir{temporary.path()}.filePath(QStringLiteral("ScriptProject"));
        QVERIFY(copy_directory_tree(source_root, project_root));
        const auto project_path = QDir{project_root}.filePath(QStringLiteral("DragonPixelProject.json"));
        const auto fake_rider = QDir{project_root}.filePath(QStringLiteral("Tools/rider64.exe"));
        QVERIFY(QDir{}.mkpath(QFileInfo{fake_rider}.absolutePath()));
        QFile fake_rider_file{fake_rider};
        QVERIFY(fake_rider_file.open(QIODevice::WriteOnly));
        QCOMPARE(fake_rider_file.write(QByteArrayLiteral("rider probe")), 11);
        fake_rider_file.close();

        EditorWindow window{project_path};
        QVector<ScriptEditorInvocation> rider_invocations;
        window.script_editor_service_ = ScriptEditorService{
            [&rider_invocations](const QString& program, const QStringList& arguments,
                const QString& working_directory, QString&) {
                rider_invocations.push_back({program, arguments, working_directory});
                return true;
            }};
        window.set_unsaved_prompt([](const QString&) {
            return EditorWindow::UnsavedDecision::discard;
        });
        window.set_component_name_prompt([](ProjectComponentLanguage language) {
            return language == ProjectComponentLanguage::csharp
                ? std::optional<QString>{QStringLiteral("PlayerLifecycleScript")}
                : std::nullopt;
        });
        QVERIFY(window.auto_build_project_scripts_);
        window.auto_build_project_scripts_ = false;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        auto* hierarchy = window.findChild<QTreeView*>(QStringLiteral("HierarchyView"));
        auto* inspector = window.findChild<QTreeView*>(QStringLiteral("InspectorView"));
        QVERIFY(hierarchy != nullptr && inspector != nullptr);
        const auto selected_index = hierarchy->model()->index(0, 0);
        QVERIFY(selected_index.isValid());
        hierarchy->selectionModel()->select(
            selected_index,
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        hierarchy->setCurrentIndex(selected_index);
        QCoreApplication::processEvents();

        const auto selected_id = window.selected_entity_id();
        QVERIFY(selected_id.has_value());
        const auto* before = window.scene_->find_entity(*selected_id);
        QVERIFY(before != nullptr);
        const auto initial_component_count = before->components.size();

        window.create_project_component(ProjectComponentLanguage::csharp);

        const auto* after = window.scene_->find_entity(*selected_id);
        QVERIFY(after != nullptr);
        QCOMPARE(after->components.size(), initial_component_count + 1);
        const auto attached = std::find_if(
            after->components.begin(),
            after->components.end(),
            [](const auto& component) {
                return component.qualified_name == "DragonPixel.ProjectComponents.PlayerLifecycleScript";
            });
        QVERIFY(attached != after->components.end());
        QVERIFY(attached->owner == dragonpixel::metadata::runtime_owner::managed);
        QVERIFY(attached->enabled);
        QTRY_VERIFY(find_text(inspector->model(), QStringLiteral("PlayerLifecycleScript")).isValid());
        QTRY_VERIFY(find_text(inspector->model(), QStringLiteral("Horizontal Action")).isValid());
        QTRY_VERIFY(find_text(inspector->model(), QStringLiteral("Vertical Action")).isValid());
        QTRY_VERIFY(find_text(inspector->model(), QStringLiteral("Speed")).isValid());
        QVERIFY(QFileInfo::exists(
            QDir{project_root}.filePath(QStringLiteral("Components/CSharp/PlayerLifecycleScript.cs"))));
        QVERIFY(!window.component_modules_available_);

        auto* project = window.findChild<QTreeView*>(QStringLiteral("ProjectExplorerView"));
        QVERIFY(project != nullptr);
        const auto source_item = find_text(
            project->model(), QStringLiteral("PlayerLifecycleScript.cs"));
        QVERIFY(source_item.isValid());
        QCOMPARE(
            source_item.data(EditorRoles::project_kind).toInt(),
            static_cast<int>(ProjectItemKind::component_source));

        ScriptEditorRequest probe;
        probe.project_root = project_root;
        probe.selected_source_path = source_item.data(EditorRoles::project_path).toString();
        probe.contracts_assembly_path = QString::fromUtf8(DPE_CONTRACTS_ASSEMBLY);
        probe.rider_executable = fake_rider;
        for (const auto& entry : window.project_index_.candidate->entries)
        {
            if (entry.kind == ProjectIndexEntryKind::component_source)
                probe.component_source_paths.push_back(entry.absolute_path);
            else if (entry.kind == ProjectIndexEntryKind::component_manifest)
                probe.component_manifest_paths.push_back(entry.absolute_path);
        }
        const auto prepared = window.script_editor_service_.open_in_rider(probe);
        QVERIFY2(prepared.succeeded, qPrintable(prepared.error));
        QCOMPARE(rider_invocations.size(), 2);
        QVERIFY(QFileInfo::exists(prepared.solution_path));
        QVERIFY(read_bytes(prepared.project_path).contains(QStringLiteral("ManagedMover.cs").toUtf8()));
        QVERIFY(read_bytes(prepared.project_path).contains(
            QStringLiteral("PlayerLifecycleScript.cs").toUtf8()));
        rider_invocations.clear();

        const auto previous_rider = qgetenv("DPE_RIDER_EXECUTABLE");
        QVERIFY(qputenv("DPE_RIDER_EXECUTABLE", fake_rider.toUtf8()));
        window.activate_project_item(source_item);
        QCOMPARE(rider_invocations.size(), 2);
        QCOMPARE(rider_invocations.at(0).arguments.size(), 1);
        QVERIFY(rider_invocations.at(0).arguments.constFirst().endsWith(
            QStringLiteral("DragonPixel.ProjectComponents.sln")));
        QCOMPARE(
            rider_invocations.at(1).arguments.mid(0, 2),
            QStringList({QStringLiteral("--line"), QStringLiteral("1")}));
        rider_invocations.clear();

        const auto component_row = find_text(
            inspector->model(), QStringLiteral("PlayerLifecycleScript"));
        QVERIFY(component_row.isValid());
        inspector->scrollTo(component_row);
        inspector->setCurrentIndex(component_row);
        QTimer::singleShot(0, &window, [] {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            QVERIFY(menu != nullptr);
            auto* edit = menu->findChild<QAction*>(
                QStringLiteral("InspectorEditScriptInRider"));
            QVERIFY(edit != nullptr);
            QCOMPARE(edit->text(), QStringLiteral("Edit Script in Rider"));
            QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                menu->actionGeometry(edit).center());
        });
        window.show_inspector_context_menu(inspector->visualRect(component_row).center());
        QCOMPARE(rider_invocations.size(), 2);
        QCOMPARE(
            QFileInfo{rider_invocations.at(1).arguments.constLast()}.fileName(),
            QStringLiteral("PlayerLifecycleScript.cs"));
        if (previous_rider.isNull())
            qunsetenv("DPE_RIDER_EXECUTABLE");
        else
            QVERIFY(qputenv("DPE_RIDER_EXECUTABLE", previous_rider));
    }

    void project_input_map_is_indexed_loaded_and_opens_the_rebinding_editor()
    {
        EditorWindow window{QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT)};
        window.set_unsaved_prompt([](const QString&) {
            return EditorWindow::UnsavedDecision::discard;
        });
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        auto* action = window.findChild<QAction*>(QStringLiteral("InputMapAction"));
        auto* settings_action = window.findChild<QAction*>(QStringLiteral("InputSettingsAction"));
        auto* settings_menu = window.findChild<QMenu*>(QStringLiteral("ProjectSettingsMenu"));
        auto* project = window.findChild<QTreeView*>(QStringLiteral("ProjectExplorerView"));
        QVERIFY(action != nullptr);
        QVERIFY(settings_action != nullptr && settings_menu != nullptr);
        QVERIFY(project != nullptr);
        QVERIFY(action->isEnabled());
        QVERIFY(settings_action->isEnabled());
        QVERIFY(window.project_input_map_.has_value());
        QVERIFY(QFileInfo::exists(window.input_map_source_path_));
        QCOMPARE(window.project_input_map_->name, QStringLiteral("Default Input"));

        InputMapService service;
        const auto keyboard = service.evaluate(*window.project_input_map_, {
            {QStringLiteral("keyboard/d"), 1.0},
            {QStringLiteral("keyboard/w"), 1.0},
        });
        QCOMPARE(keyboard.value(QStringLiteral("move.x")).value, 1.0);
        QCOMPARE(keyboard.value(QStringLiteral("move.y")).value, 1.0);
        const auto gamepad = service.evaluate(*window.project_input_map_, {
            {QStringLiteral("gamepad/left-x"), -1.0},
            {QStringLiteral("gamepad/dpad-up"), 1.0},
        });
        QCOMPARE(gamepad.value(QStringLiteral("move.x")).value, -1.0);
        QCOMPARE(gamepad.value(QStringLiteral("move.y")).value, 1.0);

        bool dialog_opened = false;
        QTimer::singleShot(0, &window, [&window, &dialog_opened] {
            auto* dialog = window.findChild<QDialog*>(QStringLiteral("InputMapEditorDialog"));
            if (dialog != nullptr)
            {
                dialog_opened = true;
                QVERIFY(dialog->findChild<QComboBox*>(
                    QStringLiteral("InputControlMapSelector")) != nullptr);
                QVERIFY(dialog->findChild<QTreeWidget*>(
                    QStringLiteral("InputBindingTree")) != nullptr);
                QVERIFY(dialog->findChild<QPushButton*>(
                    QStringLiteral("RebindInputBindingButton")) != nullptr);
                QVERIFY(dialog->findChild<QPushButton*>(
                    QStringLiteral("RenameInputActionButton")) != nullptr);
                QVERIFY(dialog->findChild<QCheckBox*>(
                    QStringLiteral("InputControlMapEnabled")) != nullptr);
                QCOMPARE(dialog->windowTitle(), QStringLiteral("Input Settings"));
                dialog->reject();
            }
        });
        action->trigger();
        QVERIFY(dialog_opened);

        dialog_opened = false;
        QTimer::singleShot(0, &window, [&window, &dialog_opened] {
            auto* dialog = window.findChild<QDialog*>(QStringLiteral("InputMapEditorDialog"));
            if (dialog != nullptr)
            {
                dialog_opened = true;
                dialog->reject();
            }
        });
        settings_action->trigger();
        QVERIFY(dialog_opened);
    }

    void failed_candidate_scene_validation_preserves_active_project_and_workers()
    {
        EditorWindow window{QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT)};
        int prompt_count{};
        window.set_unsaved_prompt([&prompt_count](const QString&) {
            ++prompt_count;
            return EditorWindow::UnsavedDecision::cancel;
        });
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY_WITH_TIMEOUT(window.preview_worker_->has_frame(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(window.game_preview_worker_->has_frame(), 15000);

        const auto active_module_manifest = QStringLiteral("active-runtime-modules.json");
        window.apply_component_module_manifest(active_module_manifest);
        QVERIFY(window.project_index_.candidate.has_value());
        QVERIFY(window.scene_.has_value());
        const auto active_project_manifest = window.project_manifest_path_;
        const auto active_project_root = window.project_root_;
        const auto active_scene_path = window.scene_path_;
        const auto active_project_id = window.project_index_.candidate->project_id;
        const auto active_metadata_size = window.metadata_.size();
        const auto active_scene_json = dragonpixel::serialization::write_scene_json(*window.scene_);
        const auto preview_process_id = window.preview_worker_->process_id();
        const auto game_preview_process_id = window.game_preview_worker_->process_id();
        QVERIFY(preview_process_id > 0);
        QVERIFY(game_preview_process_id > 0);

        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto source_root = QFileInfo{QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT)}.absolutePath();
        const auto candidate_root = QDir{temporary.path()}.filePath(QStringLiteral("Candidate"));
        QVERIFY(copy_directory_tree(source_root, candidate_root));
        const auto candidate_scene_path = QDir{candidate_root}.filePath(QStringLiteral("Scenes/Main.dpescene"));
        QFile candidate_scene{candidate_scene_path};
        QVERIFY(candidate_scene.open(QIODevice::ReadOnly));
        auto candidate_document = QJsonDocument::fromJson(candidate_scene.readAll());
        candidate_scene.close();
        QVERIFY(candidate_document.isObject());
        auto candidate_scene_root = candidate_document.object();
        auto entities = candidate_scene_root.value(QStringLiteral("entities")).toArray();
        QVERIFY(!entities.isEmpty());
        auto invalid_entity = entities.at(0).toObject();
        invalid_entity.insert(QStringLiteral("id"), QStringLiteral("not-a-uuid"));
        entities.replace(0, invalid_entity);
        candidate_scene_root.insert(QStringLiteral("entities"), entities);
        candidate_document.setObject(candidate_scene_root);
        const auto invalid_scene_json = candidate_document.toJson(QJsonDocument::Indented);
        QVERIFY(candidate_scene.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(candidate_scene.write(invalid_scene_json), invalid_scene_json.size());
        candidate_scene.close();

        const auto candidate_manifest = QDir{candidate_root}.filePath(QStringLiteral("DragonPixelProject.json"));
        QVERIFY(!window.load_project(candidate_manifest));

        QCOMPARE(prompt_count, 0);
        QCOMPARE(window.project_manifest_path_, active_project_manifest);
        QCOMPARE(window.project_root_, active_project_root);
        QCOMPARE(window.scene_path_, active_scene_path);
        QVERIFY(window.project_index_.candidate.has_value());
        QCOMPARE(window.project_index_.candidate->project_id, active_project_id);
        QCOMPARE(window.metadata_.size(), active_metadata_size);
        QVERIFY(window.scene_.has_value());
        QCOMPARE(dragonpixel::serialization::write_scene_json(*window.scene_), active_scene_json);
        QCOMPARE(window.component_module_manifest_, active_module_manifest);
        QVERIFY(window.component_modules_available_);
        QCOMPARE(window.preview_worker_->process_id(), preview_process_id);
        QCOMPARE(window.game_preview_worker_->process_id(), game_preview_process_id);
    }

    void project_v2_open_uses_the_staged_v4_component_root_migration()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto source_root = QFileInfo{QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT)}.absolutePath();
        const auto project_root = QDir{temporary.path()}.filePath(QStringLiteral("MigratedProject"));
        QVERIFY(copy_directory_tree(source_root, project_root));
        const auto manifest_path = QDir{project_root}.filePath(QStringLiteral("DragonPixelProject.json"));

        QFile manifest{manifest_path};
        QVERIFY(manifest.open(QIODevice::ReadOnly));
        auto document = QJsonDocument::fromJson(manifest.readAll());
        manifest.close();
        QVERIFY(document.isObject());
        auto root = document.object();
        root.insert(QStringLiteral("$schema"),
            QStringLiteral("https://dragonpixel.dev/schemas/v2/project.schema.json"));
        root.insert(QStringLiteral("formatVersion"), 2);
        root.remove(QStringLiteral("componentRoots"));
        document.setObject(root);
        const auto version_two_bytes = document.toJson(QJsonDocument::Indented);
        QVERIFY(manifest.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(manifest.write(version_two_bytes), version_two_bytes.size());
        manifest.close();

        EditorWindow window{manifest_path};
        window.set_unsaved_prompt([](const QString&) {
            return EditorWindow::UnsavedDecision::discard;
        });
        QVERIFY(window.scene_.has_value());
        QVERIFY(window.project_index_.candidate.has_value());
        QCOMPARE(window.project_index_.candidate->source_format_version, 2);
        QCOMPARE(window.project_index_.candidate->format_version, 4);
        QCOMPARE(window.project_index_.candidate->migrations.size(), 2);
        QCOMPARE(window.project_index_.candidate->migrations.at(0).source_format_version, 2);
        QCOMPARE(window.project_index_.candidate->migrations.at(0).target_format_version, 3);
        QCOMPARE(window.project_index_.candidate->migrations.at(1).source_format_version, 3);
        QCOMPARE(window.project_index_.candidate->migrations.at(1).target_format_version, 4);
        QVERIFY(window.metadata_.find("ee23acae-dd2f-449d-a308-19ea39c15106") != nullptr);
        QCOMPARE(read_bytes(manifest_path), version_two_bytes);
    }

    void cancelled_candidate_project_open_preserves_active_project_and_workers()
    {
        EditorWindow window{QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT)};
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY_WITH_TIMEOUT(window.preview_worker_->has_frame(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(window.game_preview_worker_->has_frame(), 15000);

        const auto active_module_manifest = QStringLiteral("active-runtime-modules.json");
        window.apply_component_module_manifest(active_module_manifest);
        auto* add_empty = window.findChild<QAction*>(QStringLiteral("AddEmptyGameObjectAction"));
        QVERIFY(add_empty != nullptr);
        add_empty->trigger();
        QTRY_VERIFY(window.windowTitle().endsWith(QStringLiteral("*")));
        QVERIFY(window.project_index_.candidate.has_value());
        QVERIFY(window.scene_.has_value());

        const auto active_project_manifest = window.project_manifest_path_;
        const auto active_project_root = window.project_root_;
        const auto active_scene_path = window.scene_path_;
        const auto active_project_id = window.project_index_.candidate->project_id;
        const auto active_metadata_size = window.metadata_.size();
        const auto active_scene_json = dragonpixel::serialization::write_scene_json(*window.scene_);
        const auto preview_process_id = window.preview_worker_->process_id();
        const auto game_preview_process_id = window.game_preview_worker_->process_id();
        QVERIFY(preview_process_id > 0);
        QVERIFY(game_preview_process_id > 0);

        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto source_root = QFileInfo{QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT)}.absolutePath();
        const auto candidate_root = QDir{temporary.path()}.filePath(QStringLiteral("Candidate"));
        QVERIFY(copy_directory_tree(source_root, candidate_root));
        const auto candidate_manifest = QDir{candidate_root}.filePath(QStringLiteral("DragonPixelProject.json"));
        int prompt_count{};
        window.set_unsaved_prompt([&prompt_count](const QString&) {
            ++prompt_count;
            return EditorWindow::UnsavedDecision::cancel;
        });

        QVERIFY(!window.load_project(candidate_manifest));

        QCOMPARE(prompt_count, 1);
        QCOMPARE(window.project_manifest_path_, active_project_manifest);
        QCOMPARE(window.project_root_, active_project_root);
        QCOMPARE(window.scene_path_, active_scene_path);
        QVERIFY(window.project_index_.candidate.has_value());
        QCOMPARE(window.project_index_.candidate->project_id, active_project_id);
        QCOMPARE(window.metadata_.size(), active_metadata_size);
        QVERIFY(window.scene_.has_value());
        QCOMPARE(dragonpixel::serialization::write_scene_json(*window.scene_), active_scene_json);
        QVERIFY(window.scene_->is_dirty());
        QVERIFY(window.windowTitle().endsWith(QStringLiteral("*")));
        QCOMPARE(window.component_module_manifest_, active_module_manifest);
        QVERIFY(window.component_modules_available_);
        QCOMPARE(window.preview_worker_->process_id(), preview_process_id);
        QCOMPARE(window.game_preview_worker_->process_id(), game_preview_process_id);
    }

    void linked_project_locations_cannot_trigger_recovery_writes()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto source_root = QFileInfo{QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT)}.absolutePath();
        EditorWindow window{QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT)};
        bool exercised{};

        const auto linked_root_target = QDir{temporary.path()}.filePath(
            QStringLiteral("LinkedRootTarget"));
        QVERIFY(copy_directory_tree(source_root, linked_root_target));
        const auto linked_root_manifest = QDir{linked_root_target}.filePath(
            QStringLiteral("DragonPixelProject.json"));
        const auto linked_root_scene = QDir{linked_root_target}.filePath(
            QStringLiteral("Scenes/Main.dpescene"));
        const std::vector<dragonpixel::serialization::utf8_transaction_write> linked_root_writes{
            {filesystem_path(linked_root_manifest), "{ interrupted linked-root manifest\n"},
            {filesystem_path(linked_root_scene), "{ interrupted linked-root scene\n"},
        };
        const auto linked_root_interruption = dragonpixel::serialization::save_utf8_transaction(
            linked_root_writes,
            filesystem_path(linked_root_target),
            dragonpixel::serialization::transaction_save_fault::leave_interrupted_after_first_replace);
        QVERIFY(!linked_root_interruption.succeeded);
        const auto linked_root_interrupted_bytes = read_bytes(linked_root_manifest);
        const auto linked_root_alias = QDir{temporary.path()}.filePath(
            QStringLiteral("LinkedRootAlias"));
        if (create_directory_symlink(linked_root_target, linked_root_alias))
        {
            exercised = true;
            QVERIFY(!window.load_project(QDir{linked_root_alias}.filePath(
                QStringLiteral("DragonPixelProject.json"))));
            QCOMPARE(read_bytes(linked_root_manifest), linked_root_interrupted_bytes);
            const auto recovery_root = QDir{linked_root_target}.filePath(
                QStringLiteral(".dragonpixel/Recovery/Transactions"));
            QVERIFY(QDir{recovery_root}.exists());
            QVERIFY(!QDir{recovery_root}.entryList(
                QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
            QVERIFY(remove_filesystem_link(linked_root_alias));
        }

        const auto linked_manifest_target = QDir{temporary.path()}.filePath(
            QStringLiteral("LinkedManifestTarget"));
        QVERIFY(copy_directory_tree(source_root, linked_manifest_target));
        const auto linked_manifest = QDir{linked_manifest_target}.filePath(
            QStringLiteral("DragonPixelProject.json"));
        const auto linked_manifest_scene = QDir{linked_manifest_target}.filePath(
            QStringLiteral("Scenes/Main.dpescene"));
        const std::vector<dragonpixel::serialization::utf8_transaction_write> linked_manifest_writes{
            {filesystem_path(linked_manifest), "{ interrupted linked-manifest target\n"},
            {filesystem_path(linked_manifest_scene), "{ interrupted linked-manifest scene\n"},
        };
        const auto linked_manifest_interruption = dragonpixel::serialization::save_utf8_transaction(
            linked_manifest_writes,
            filesystem_path(linked_manifest_target),
            dragonpixel::serialization::transaction_save_fault::leave_interrupted_after_first_replace);
        QVERIFY(!linked_manifest_interruption.succeeded);
        const auto linked_manifest_interrupted_bytes = read_bytes(linked_manifest);
        const QDir selected_root{QDir{temporary.path()}.filePath(
            QStringLiteral("SelectedRoot"))};
        QVERIFY(QDir{}.mkpath(selected_root.path()));
        const auto selected_manifest = selected_root.filePath(
            QStringLiteral("DragonPixelProject.json"));
        if (create_file_symlink(linked_manifest, selected_manifest))
        {
            exercised = true;
            QVERIFY(!window.load_project(selected_manifest));
            QCOMPARE(read_bytes(linked_manifest), linked_manifest_interrupted_bytes);
            const auto recovery_root = QDir{linked_manifest_target}.filePath(
                QStringLiteral(".dragonpixel/Recovery/Transactions"));
            QVERIFY(QDir{recovery_root}.exists());
            QVERIFY(!QDir{recovery_root}.entryList(
                QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
            QVERIFY(remove_filesystem_link(selected_manifest));
        }

        if (!exercised)
        {
            QSKIP("This host does not permit creation of file or directory symbolic links.");
        }
    }

    void scene_and_tile_save_is_one_recoverable_transaction()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto source_root = QFileInfo{QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT)}.absolutePath();
        const auto project_root = QDir{temporary.path()}.filePath(QStringLiteral("TransactionalSave"));
        QVERIFY(copy_directory_tree(source_root, project_root));

        const auto manifest_path = QDir{project_root}.filePath(QStringLiteral("DragonPixelProject.json"));
        const auto scene_path = QDir{project_root}.filePath(QStringLiteral("Scenes/Main.dpescene"));
        const auto tilemap_path = QDir{project_root}.filePath(QStringLiteral("Assets/Tiles/StarterMap.dpetilemap"));
        const auto tileset_path = QDir{project_root}.filePath(QStringLiteral("Assets/Tiles/StarterTiles.dpetileset"));
        const auto manifest_before = read_bytes(manifest_path);
        const auto scene_before = read_bytes(scene_path);
        const auto tilemap_before = read_bytes(tilemap_path);
        QVERIFY(!manifest_before.isEmpty());
        QVERIFY(!scene_before.isEmpty());
        QVERIFY(!tilemap_before.isEmpty());

        const std::vector<dragonpixel::serialization::utf8_transaction_write> interrupted_open{
            {filesystem_path(manifest_path), "{ interrupted manifest\n"},
            {filesystem_path(scene_path), "{ interrupted scene\n"},
        };
        const auto interrupted_open_result = dragonpixel::serialization::save_utf8_transaction(
            interrupted_open,
            filesystem_path(project_root),
            dragonpixel::serialization::transaction_save_fault::leave_interrupted_after_first_replace);
        QVERIFY(!interrupted_open_result.succeeded);
        QCOMPARE(read_bytes(manifest_path), QByteArrayLiteral("{ interrupted manifest\n"));
        QCOMPARE(read_bytes(scene_path), scene_before);
        {
            EditorWindow recovered_before_index{manifest_path};
            QVERIFY(recovered_before_index.scene_.has_value());
            QCOMPARE(read_bytes(manifest_path), manifest_before);
            QCOMPARE(read_bytes(scene_path), scene_before);
        }

        EditorWindow window{manifest_path};
        window.set_unsaved_prompt([](const QString&) {
            return EditorWindow::UnsavedDecision::discard;
        });
        QVERIFY(window.scene_.has_value());
        auto* add_empty = window.findChild<QAction*>(QStringLiteral("AddEmptyGameObjectAction"));
        QVERIFY(add_empty != nullptr);
        add_empty->trigger();
        QVERIFY(window.scene_->is_dirty());

        QVERIFY(window.tile_document_service_->load(tilemap_path, tileset_path));
        const auto tile_id = window.tile_document_service_->tileset()->tiles.front().tile_id;
        window.tile_document_service_->begin_stroke();
        QVERIFY(window.tile_document_service_->paint_cell(0, 96, 64, tile_id));
        window.tile_document_service_->commit_stroke();
        QVERIFY(window.tile_document_service_->is_dirty());

        window.save_fault_for_test_ =
            dragonpixel::serialization::transaction_save_fault::after_first_replace;
        QTimer::singleShot(0, [] {
            if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            {
                message->accept();
            }
        });
        QVERIFY(!window.save_scene());
        QCOMPARE(read_bytes(scene_path), scene_before);
        QCOMPARE(read_bytes(tilemap_path), tilemap_before);
        QVERIFY(window.scene_->is_dirty());
        QVERIFY(window.tile_document_service_->is_dirty());
        QCOMPARE(window.save_fault_for_test_,
            dragonpixel::serialization::transaction_save_fault::none);

        window.save_fault_for_test_ =
            dragonpixel::serialization::transaction_save_fault::leave_interrupted_after_first_replace;
        QTimer::singleShot(0, [] {
            if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            {
                message->accept();
            }
        });
        QVERIFY(!window.save_scene());
        QVERIFY(read_bytes(scene_path) != scene_before);
        QCOMPARE(read_bytes(tilemap_path), tilemap_before);
        QVERIFY(window.scene_->is_dirty());
        QVERIFY(window.tile_document_service_->is_dirty());

        {
            EditorWindow recovered{manifest_path};
            recovered.set_unsaved_prompt([](const QString&) {
                return EditorWindow::UnsavedDecision::discard;
            });
            QVERIFY(recovered.scene_.has_value());
            QCOMPARE(read_bytes(scene_path), scene_before);
            QCOMPARE(read_bytes(tilemap_path), tilemap_before);
            QVERIFY(!recovered.scene_->is_dirty());
            const auto transaction_root = QDir{project_root}.filePath(
                QStringLiteral(".dragonpixel/Recovery/Transactions"));
            QVERIFY(!QDir{transaction_root}.exists()
                || QDir{transaction_root}.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
        }

        QVERIFY(window.save_scene());
        QVERIFY(!window.scene_->is_dirty());
        QVERIFY(!window.tile_document_service_->is_dirty());
        QVERIFY(read_bytes(scene_path) != scene_before);
        QVERIFY(read_bytes(tilemap_path) != tilemap_before);
    }

    void poc_j_qt_input_latency_data()
    {
        QTest::addColumn<QString>("adapter");
        if (!qEnvironmentVariableIsSet("DPE_RUN_POC_J"))
        {
            QTest::newRow("dedicated-ctest-only") << QString{};
            return;
        }
        QTest::newRow("monogame") << QStringLiteral("monogame");
        QTest::newRow("kni") << QStringLiteral("kni");
    }

    void poc_j_qt_input_latency()
    {
        QFETCH(QString, adapter);
        if (adapter.isEmpty())
        {
            QSKIP("POC J runs only through the dedicated poc_j.qt_input_latency CTest entry.");
        }

        constexpr int active_press_count = 21;
        constexpr int active_press_pacing_milliseconds = 110;
        constexpr int minimum_warmup_frames = 30;
        constexpr qint64 minimum_steady_paint_window_nanoseconds = 2'000'000'000LL;
        constexpr qint64 maximum_p50_latency_nanoseconds = 100'000'000LL;
        const QSize required_worker_size{1280, 720};

        EditorWindow window{QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT)};
        window.set_unsaved_prompt([](const QString&) {
            return EditorWindow::UnsavedDecision::discard;
        });
        auto* adapter_selector = window.findChild<QComboBox*>(QStringLiteral("RuntimeAdapterCombo"));
        auto* play = window.findChild<QAction*>(QStringLiteral("PlayAction"));
        auto* stop = window.findChild<QAction*>(QStringLiteral("StopAction"));
        auto* game = window.findChild<GameViewport*>(QStringLiteral("GameViewport"));
        auto* scene_view = window.findChild<AuthoringViewport*>(QStringLiteral("SceneViewport"));
        auto* move_gizmo = window.findChild<QAction*>(QStringLiteral("MoveGizmoAction"));
        QVERIFY(adapter_selector != nullptr && play != nullptr && stop != nullptr && game != nullptr
            && scene_view != nullptr && move_gizmo != nullptr);
        const auto adapter_index = adapter_selector->findData(adapter);
        QVERIFY2(adapter_index >= 0, qPrintable(QStringLiteral("Missing runtime adapter %1").arg(adapter)));
        adapter_selector->setCurrentIndex(adapter_index);
        QCOMPARE(adapter_selector->currentData().toString(), adapter);

        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.scene_.has_value());
        const auto authoring_before = dragonpixel::serialization::write_scene_json(*window.scene_);
        const auto authoring_scene_path = window.authoring_scene_path();
        QVERIFY(!authoring_scene_path.isEmpty());
        const auto authoring_file_before = read_bytes(authoring_scene_path);
        QVERIFY(!authoring_file_before.isEmpty());
        const auto authoring_dirty_before = window.scene_->is_dirty();

        struct WorkerFrameEvidence final
        {
            quint64 input_revision{};
            QSize image_size;
            int occurrences{};
        };
        QSize last_worker_frame_size;
        QHash<quint64, WorkerFrameEvidence> worker_frame_evidence;
        QObject worker_frame_evidence_context;
        connect(window.play_worker_, &WorkerClient::frame_ready_correlated,
            &worker_frame_evidence_context,
            [&last_worker_frame_size, &worker_frame_evidence](
                const QImage& image,
                quint64 input_revision,
                quint64 frame_revision) {
                last_worker_frame_size = image.size();
                auto& evidence = worker_frame_evidence[frame_revision];
                evidence.input_revision = input_revision;
                evidence.image_size = image.size();
                ++evidence.occurrences;
            });
        QSignalSpy input_ready_spy{window.play_worker_, &WorkerClient::runtime_input_ready};
        QSignalSpy input_actions_spy{game, &GameViewport::correlated_input_actions_changed};
        QSignalSpy presented_input_spy{game, &GameViewport::input_presented};
        QSignalSpy joined_paint_spy{game, &GameViewport::input_frame_painted};
        QSignalSpy dropped_spy{game, &GameViewport::input_correlation_dropped};
        QSignalSpy expired_spy{game, &GameViewport::input_correlation_expired};
        QSignalSpy presented_frame_spy{game, &GameViewport::play_frame_presented};
        QSignalSpy skipped_paint_spy{game, &GameViewport::presentation_frames_skipped};
        QSignalSpy runtime_stopped_spy{window.play_worker_, &WorkerClient::runtime_stopped};
        QSignalSpy move_gizmo_spy{move_gizmo, &QAction::triggered};
        QVERIFY(input_ready_spy.isValid() && input_actions_spy.isValid()
            && presented_input_spy.isValid()
            && joined_paint_spy.isValid()
            && dropped_spy.isValid() && expired_spy.isValid()
            && presented_frame_spy.isValid() && skipped_paint_spy.isValid()
            && runtime_stopped_spy.isValid() && move_gizmo_spy.isValid());

        scene_view->set_gizmo_tool(AuthoringViewport::GizmoTool::rotate);
        QCOMPARE(scene_view->gizmo_tool(), AuthoringViewport::GizmoTool::rotate);

        window.play_worker_->resize_viewport(required_worker_size);
        QVERIFY(play->isEnabled());
        play->trigger();
        QVERIFY(game->is_play_mode());
        QCOMPARE(window.play_worker_->adapter_name(), adapter);
        window.play_worker_->resize_viewport(required_worker_size);
        QTRY_VERIFY_WITH_TIMEOUT(!input_ready_spy.isEmpty(), 20'000);
        QTRY_VERIFY_WITH_TIMEOUT(game->is_input_enabled(), 20'000);
        QTRY_COMPARE_WITH_TIMEOUT(last_worker_frame_size, required_worker_size, 20'000);

        game->setFocus(Qt::OtherFocusReason);
        QTRY_VERIFY_WITH_TIMEOUT(game->hasFocus(), 5'000);
        presented_frame_spy.clear();
        QTRY_VERIFY_WITH_TIMEOUT(presented_frame_spy.size() >= minimum_warmup_frames, 20'000);
        QTRY_VERIFY_WITH_TIMEOUT(!window.play_worker_->negotiated_adapter().isEmpty(), 20'000);
        QTRY_VERIFY_WITH_TIMEOUT(!window.play_worker_->runtime_backend().isEmpty(), 20'000);
        QTRY_VERIFY_WITH_TIMEOUT(
            !window.play_worker_->runtime_device().isEmpty()
                && window.play_worker_->runtime_device().compare(
                       QStringLiteral("not initialized"), Qt::CaseInsensitive) != 0,
            20'000);
        const auto negotiated_adapter = window.play_worker_->negotiated_adapter();
        const auto runtime_backend = window.play_worker_->runtime_backend();
        const auto runtime_device = window.play_worker_->runtime_device();
        const auto runtime_process_id = window.play_worker_->process_id();
        const auto recovery_count = window.play_worker_->recovery_count();
        QCOMPARE(negotiated_adapter.compare(adapter, Qt::CaseInsensitive), 0);
        QVERIFY(runtime_backend.contains(adapter, Qt::CaseInsensitive));
        QVERIFY(runtime_device.compare(QStringLiteral("not initialized"), Qt::CaseInsensitive) != 0);
        QVERIFY(runtime_process_id > 0);

        const auto wait_for_condition = [](const auto& condition, int timeout_milliseconds) {
            if (condition()) return true;
            QEventLoop loop;
            QTimer condition_timer;
            condition_timer.setInterval(1);
            condition_timer.setTimerType(Qt::PreciseTimer);
            QTimer deadline_timer;
            deadline_timer.setSingleShot(true);
            QObject::connect(&condition_timer, &QTimer::timeout, &loop, [&] {
                if (condition()) loop.quit();
            });
            QObject::connect(&deadline_timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            condition_timer.start();
            deadline_timer.start(timeout_milliseconds);
            loop.exec(QEventLoop::AllEvents);
            return condition();
        };

        // Prove the literal editor/gameplay conflict from ADR-0015: W reaches the
        // captured Game input path without triggering the real Move gizmo action.
        const auto move_gizmo_enabled_during_play = move_gizmo->isEnabled();
        const auto move_gizmo_uses_w = move_gizmo->shortcut() == QKeySequence{Qt::Key_W};
        const auto move_gizmo_is_window_shortcut =
            move_gizmo->shortcutContext() == Qt::WindowShortcut;
        input_actions_spy.clear();
        presented_input_spy.clear();
        joined_paint_spy.clear();
        dropped_spy.clear();
        expired_spy.clear();
        QTest::keyPress(game, Qt::Key_W);
        const auto shortcut_active_action_arrived = wait_for_condition(
            [&input_actions_spy] { return !input_actions_spy.isEmpty(); }, 1'000);
        quint64 shortcut_active_revision{};
        bool shortcut_active_action_valid{};
        if (shortcut_active_action_arrived && input_actions_spy.size() == 1
            && input_actions_spy.first().size() == 2)
        {
            const auto shortcut_active_state = input_actions_spy.first().at(0).toJsonObject();
            shortcut_active_revision = input_actions_spy.first().at(1).toULongLong();
            shortcut_active_action_valid = shortcut_active_revision > 0
                && shortcut_active_state.value(QStringLiteral("actions")).toObject()
                       .value(QStringLiteral("move.y")).toObject()
                       .value(QStringLiteral("value")).toDouble() == 1.0;
        }
        const auto shortcut_active_presented = shortcut_active_revision > 0
            && wait_for_condition(
                [&presented_input_spy, shortcut_active_revision] {
                    return std::any_of(
                        presented_input_spy.cbegin(), presented_input_spy.cend(),
                        [shortcut_active_revision](const QList<QVariant>& values) {
                            return !values.isEmpty()
                                && values.at(0).toULongLong() == shortcut_active_revision;
                        });
                },
                6'000);

        input_actions_spy.clear();
        // Release is unconditional so a failed shortcut proof cannot leak W into
        // the latency distribution or suppress its complete 21-attempt report.
        QTest::keyRelease(game, Qt::Key_W);
        const auto shortcut_neutral_action_arrived = wait_for_condition(
            [&input_actions_spy] { return !input_actions_spy.isEmpty(); }, 1'000);
        quint64 shortcut_neutral_revision{};
        bool shortcut_neutral_action_valid{};
        if (shortcut_neutral_action_arrived && input_actions_spy.size() == 1
            && input_actions_spy.first().size() == 2)
        {
            const auto shortcut_neutral_state = input_actions_spy.first().at(0).toJsonObject();
            shortcut_neutral_revision = input_actions_spy.first().at(1).toULongLong();
            shortcut_neutral_action_valid = shortcut_neutral_revision > shortcut_active_revision
                && shortcut_neutral_state.value(QStringLiteral("actions")).toObject()
                       .value(QStringLiteral("move.y")).toObject()
                       .value(QStringLiteral("value")).toDouble() == 0.0;
        }
        const auto shortcut_neutral_presented = shortcut_neutral_revision > 0
            && wait_for_condition(
                [&presented_input_spy, shortcut_neutral_revision] {
                    return std::any_of(
                        presented_input_spy.cbegin(), presented_input_spy.cend(),
                        [shortcut_neutral_revision](const QList<QVariant>& values) {
                            return !values.isEmpty()
                                && values.at(0).toULongLong() == shortcut_neutral_revision;
                        });
                },
                6'000);
        const auto play_shortcut_suppressed = move_gizmo_enabled_during_play
            && move_gizmo_uses_w && move_gizmo_is_window_shortcut
            && shortcut_active_action_valid && shortcut_active_presented
            && shortcut_neutral_action_valid && shortcut_neutral_presented
            && move_gizmo_spy.isEmpty()
            && scene_view->gizmo_tool() == AuthoringViewport::GizmoTool::rotate
            && dropped_spy.isEmpty() && expired_spy.isEmpty();
        const auto play_move_gizmo_trigger_count = move_gizmo_spy.size();
        move_gizmo_spy.clear();

        worker_frame_evidence.clear();
        input_actions_spy.clear();
        presented_input_spy.clear();
        joined_paint_spy.clear();
        dropped_spy.clear();
        expired_spy.clear();
        presented_frame_spy.clear();
        skipped_paint_spy.clear();

        const auto joined_paint_for = [&joined_paint_spy](quint64 input_revision) {
            for (const auto& values : joined_paint_spy)
            {
                if (!values.isEmpty() && values.at(0).toULongLong() == input_revision)
                {
                    return values;
                }
            }
            return QList<QVariant>{};
        };
        const auto joined_paint_count = [&joined_paint_spy](quint64 input_revision) {
            return static_cast<int>(std::count_if(
                joined_paint_spy.cbegin(), joined_paint_spy.cend(),
                [input_revision](const QList<QVariant>& values) {
                    return !values.isEmpty()
                        && values.at(0).toULongLong() == input_revision;
                }));
        };
        const auto presented_input_for = [&presented_input_spy](quint64 input_revision) {
            for (const auto& values : presented_input_spy)
            {
                if (!values.isEmpty() && values.at(0).toULongLong() == input_revision)
                {
                    return values;
                }
            }
            return QList<QVariant>{};
        };
        const auto presented_input_count = [&presented_input_spy](quint64 input_revision) {
            return static_cast<int>(std::count_if(
                presented_input_spy.cbegin(), presented_input_spy.cend(),
                [input_revision](const QList<QVariant>& values) {
                    return !values.isEmpty()
                        && values.at(0).toULongLong() == input_revision;
                }));
        };
        const auto correlation_count = [](const QSignalSpy& spy, quint64 input_revision) {
            return static_cast<int>(std::count_if(
                spy.cbegin(), spy.cend(),
                [input_revision](const QList<QVariant>& values) {
                    return !values.isEmpty()
                        && values.at(0).toULongLong() == input_revision;
                }));
        };
        const auto presented_frame_count = [&presented_frame_spy](
                                               quint64 frame_revision,
                                               quint64 input_revision,
                                               qint64 paint_completed_nanoseconds) {
            return static_cast<int>(std::count_if(
                presented_frame_spy.cbegin(), presented_frame_spy.cend(),
                [frame_revision, input_revision, paint_completed_nanoseconds](
                    const QList<QVariant>& values) {
                    return values.size() == 3
                        && values.at(0).toULongLong() == frame_revision
                        && values.at(1).toULongLong() == input_revision
                        && values.at(2).toLongLong() == paint_completed_nanoseconds;
                }));
        };

        constexpr int input_action_timeout_milliseconds = 1'000;
        constexpr int correlation_outcome_timeout_milliseconds = 6'000;
        QList<qint64> active_press_latencies;
        QSet<quint64> active_press_revisions;
        QSet<quint64> neutral_revisions;
        int active_successes{};
        int active_exact_tuples{};
        int active_timeouts{};
        int active_invalid_actions{};
        int active_invalid_tuples{};
        int neutral_attempts{};
        int neutral_successes{};
        int neutral_timeouts{};
        int neutral_invalid_actions{};
        int neutral_invalid_presentations{};
        qint64 previous_active_paint_completion{};
        QElapsedTimer press_pacer;
        press_pacer.start();
        for (int sample = 0; sample < active_press_count; ++sample)
        {
            const auto scheduled_milliseconds = static_cast<qint64>(sample)
                * active_press_pacing_milliseconds;
            if (press_pacer.elapsed() < scheduled_milliseconds)
            {
                const auto remaining_milliseconds = static_cast<int>(
                    scheduled_milliseconds - press_pacer.elapsed());
                (void)wait_for_condition(
                    [&] { return press_pacer.elapsed() >= scheduled_milliseconds; },
                    remaining_milliseconds + 20);
            }

            const auto key = sample % 2 == 0 ? Qt::Key_A : Qt::Key_D;
            const auto expected_axis = key == Qt::Key_A ? -1.0 : 1.0;
            input_actions_spy.clear();
            QTest::keyPress(game, key);
            const auto pressed_action_arrived = wait_for_condition(
                [&input_actions_spy] { return !input_actions_spy.isEmpty(); },
                input_action_timeout_milliseconds);

            quint64 pressed_revision{};
            bool pressed_action_valid{};
            if (!pressed_action_arrived)
            {
                ++active_timeouts;
            }
            else
            {
                const auto pressed_values = input_actions_spy.last();
                if (pressed_values.size() == 2)
                {
                    pressed_revision = pressed_values.at(1).toULongLong();
                    const auto pressed_state = pressed_values.at(0).toJsonObject();
                    const auto pressed_axis = pressed_state.value(QStringLiteral("actions")).toObject()
                        .value(QStringLiteral("move.x")).toObject()
                        .value(QStringLiteral("value")).toDouble();
                    pressed_action_valid = input_actions_spy.size() == 1
                        && pressed_revision > 0
                        && !active_press_revisions.contains(pressed_revision)
                        && pressed_axis == expected_axis;
                }
                if (!pressed_action_valid) ++active_invalid_actions;
            }

            const auto pressed_revision_usable = pressed_revision > 0
                && !active_press_revisions.contains(pressed_revision);
            if (pressed_revision_usable)
            {
                active_press_revisions.insert(pressed_revision);
                const auto active_outcome_arrived = wait_for_condition(
                    [&] {
                        return joined_paint_count(pressed_revision) > 0
                            || correlation_count(dropped_spy, pressed_revision) > 0
                            || correlation_count(expired_spy, pressed_revision) > 0;
                    },
                    correlation_outcome_timeout_milliseconds);
                if (!active_outcome_arrived)
                {
                    ++active_timeouts;
                }
                else if (joined_paint_count(pressed_revision) > 0)
                {
                    const auto pressed_paint = joined_paint_for(pressed_revision);
                    bool exact_tuple = joined_paint_count(pressed_revision) == 1
                        && pressed_paint.size() == 4;
                    quint64 pressed_frame_revision{};
                    qint64 latency_nanoseconds{-1};
                    qint64 paint_completion_nanoseconds{};
                    if (exact_tuple)
                    {
                        pressed_frame_revision = pressed_paint.at(1).toULongLong();
                        latency_nanoseconds = pressed_paint.at(2).toLongLong();
                        paint_completion_nanoseconds = pressed_paint.at(3).toLongLong();
                        exact_tuple = pressed_frame_revision > 0
                            && latency_nanoseconds >= 0
                            && paint_completion_nanoseconds > previous_active_paint_completion;
                    }
                    const auto worker_frame = worker_frame_evidence.constFind(
                        pressed_frame_revision);
                    exact_tuple = exact_tuple
                        && worker_frame != worker_frame_evidence.cend()
                        && worker_frame->input_revision == pressed_revision
                        && worker_frame->occurrences == 1
                        && worker_frame->image_size == required_worker_size
                        && presented_frame_count(
                               pressed_frame_revision,
                               pressed_revision,
                               paint_completion_nanoseconds) == 1;
                    if (exact_tuple)
                    {
                        ++active_exact_tuples;
                        previous_active_paint_completion = paint_completion_nanoseconds;
                        if (pressed_action_valid
                            && correlation_count(dropped_spy, pressed_revision) == 0
                            && correlation_count(expired_spy, pressed_revision) == 0)
                        {
                            ++active_successes;
                            active_press_latencies.push_back(latency_nanoseconds);
                        }
                    }
                    else
                    {
                        ++active_invalid_tuples;
                    }
                }
            }

            // Release is unconditional so even a failed active attempt cannot leak input state
            // into the next sample or leave the worker running with a held action.
            ++neutral_attempts;
            input_actions_spy.clear();
            QTest::keyRelease(game, key);
            const auto neutral_action_arrived = wait_for_condition(
                [&input_actions_spy] { return !input_actions_spy.isEmpty(); },
                input_action_timeout_milliseconds);
            if (!neutral_action_arrived)
            {
                ++neutral_timeouts;
                continue;
            }

            const auto neutral_values = input_actions_spy.last();
            quint64 neutral_revision{};
            bool neutral_action_valid{};
            if (neutral_values.size() == 2)
            {
                neutral_revision = neutral_values.at(1).toULongLong();
                const auto neutral_state = neutral_values.at(0).toJsonObject();
                const auto neutral_axis = neutral_state.value(QStringLiteral("actions")).toObject()
                    .value(QStringLiteral("move.x")).toObject()
                    .value(QStringLiteral("value")).toDouble();
                neutral_action_valid = input_actions_spy.size() == 1
                    && neutral_revision > 0
                    && !neutral_revisions.contains(neutral_revision)
                    && (!pressed_revision_usable || neutral_revision > pressed_revision)
                    && neutral_axis == 0.0;
            }
            if (!neutral_action_valid) ++neutral_invalid_actions;
            const auto neutral_revision_usable = neutral_revision > 0
                && !neutral_revisions.contains(neutral_revision);
            if (!neutral_revision_usable) continue;

            neutral_revisions.insert(neutral_revision);
            const auto neutral_outcome_arrived = wait_for_condition(
                [&] {
                    return presented_input_count(neutral_revision) > 0
                        || correlation_count(dropped_spy, neutral_revision) > 0
                        || correlation_count(expired_spy, neutral_revision) > 0;
                },
                correlation_outcome_timeout_milliseconds);
            if (!neutral_outcome_arrived)
            {
                ++neutral_timeouts;
                continue;
            }
            if (presented_input_count(neutral_revision) > 0)
            {
                const auto neutral_presentation = presented_input_for(neutral_revision);
                const auto exact_neutral_presentation = presented_input_count(neutral_revision) == 1
                    && neutral_presentation.size() == 2
                    && neutral_presentation.at(1).toLongLong() >= 0;
                if (!exact_neutral_presentation)
                {
                    ++neutral_invalid_presentations;
                }
                else if (neutral_action_valid
                    && correlation_count(dropped_spy, neutral_revision) == 0
                    && correlation_count(expired_spy, neutral_revision) == 0)
                {
                    ++neutral_successes;
                }
            }
        }

        const auto steady_paint_span = [&presented_frame_spy]() -> qint64 {
            if (presented_frame_spy.size() < 2
                || presented_frame_spy.first().size() != 3
                || presented_frame_spy.last().size() != 3)
            {
                return 0;
            }
            return presented_frame_spy.last().at(2).toLongLong()
                - presented_frame_spy.first().at(2).toLongLong();
        };
        const auto steady_window_reached = wait_for_condition(
            [&] { return steady_paint_span() >= minimum_steady_paint_window_nanoseconds; },
            10'000);

        QList<qint64> paint_completion_times;
        paint_completion_times.reserve(presented_frame_spy.size());
        int invalid_paint_records{};
        qint64 previous_paint_completion{};
        for (const auto& values : presented_frame_spy)
        {
            if (values.size() != 3)
            {
                ++invalid_paint_records;
                continue;
            }
            const auto paint_completion = values.at(2).toLongLong();
            if (paint_completion <= previous_paint_completion)
            {
                ++invalid_paint_records;
                continue;
            }
            previous_paint_completion = paint_completion;
            paint_completion_times.push_back(paint_completion);
        }
        const auto paint_window_nanoseconds = paint_completion_times.size() >= 2
            ? paint_completion_times.back() - paint_completion_times.front()
            : 0;
        const auto painted_fps = paint_window_nanoseconds > 0
            ? static_cast<double>(paint_completion_times.size() - 1)
                * 1'000'000'000.0 / static_cast<double>(paint_window_nanoseconds)
            : 0.0;

        auto sorted_latencies = active_press_latencies;
        std::sort(sorted_latencies.begin(), sorted_latencies.end());
        qint64 p50_nanoseconds{-1};
        qint64 p95_nanoseconds{-1};
        qint64 maximum_nanoseconds{-1};
        if (!sorted_latencies.isEmpty())
        {
            p50_nanoseconds = sorted_latencies.at(sorted_latencies.size() / 2);
            const auto p95_index = static_cast<qsizetype>(std::ceil(
                0.95 * static_cast<double>(sorted_latencies.size()))) - 1;
            p95_nanoseconds = sorted_latencies.at(p95_index);
            maximum_nanoseconds = sorted_latencies.back();
        }

        const auto signal_count_for = [](const QSignalSpy& spy, const QSet<quint64>& revisions) {
            return static_cast<int>(std::count_if(
                spy.cbegin(), spy.cend(),
                [&revisions](const QList<QVariant>& values) {
                    return !values.isEmpty()
                        && revisions.contains(values.at(0).toULongLong());
                }));
        };
        const auto active_drops = signal_count_for(dropped_spy, active_press_revisions);
        const auto active_expirations = signal_count_for(expired_spy, active_press_revisions);
        const auto neutral_drops = signal_count_for(dropped_spy, neutral_revisions);
        const auto neutral_expirations = signal_count_for(expired_spy, neutral_revisions);
        quint64 skipped_paints{};
        for (const auto& values : skipped_paint_spy)
        {
            if (!values.isEmpty()) skipped_paints += values.at(0).toULongLong();
        }

        const auto authoring_before_stop = dragonpixel::serialization::write_scene_json(*window.scene_);
        const auto authoring_file_before_stop = read_bytes(authoring_scene_path);
        const auto authoring_dirty_before_stop = window.scene_->is_dirty();
        const auto identity_stable = window.play_worker_->process_id() == runtime_process_id
            && window.play_worker_->recovery_count() == recovery_count
            && window.play_worker_->negotiated_adapter() == negotiated_adapter
            && window.play_worker_->runtime_backend() == runtime_backend
            && window.play_worker_->runtime_device() == runtime_device;
        stop->trigger();
        const auto stop_completed = wait_for_condition(
            [game, &runtime_stopped_spy] {
                return !game->is_play_mode() && !runtime_stopped_spy.isEmpty();
            },
            5'000);
        const auto authoring_after_stop = dragonpixel::serialization::write_scene_json(*window.scene_);
        const auto authoring_file_after_stop = read_bytes(authoring_scene_path);
        const auto authoring_dirty_after_stop = window.scene_->is_dirty();
        scene_view->setFocus(Qt::OtherFocusReason);
        const auto edit_control_focused = wait_for_condition(
            [scene_view] { return scene_view->hasFocus(); }, 5'000);
        QTest::keyClick(scene_view, Qt::Key_W);
        const auto edit_control_action_arrived = wait_for_condition(
            [&move_gizmo_spy] { return !move_gizmo_spy.isEmpty(); }, 5'000);
        const auto edit_shortcut_control_triggered = edit_control_focused
            && edit_control_action_arrived && move_gizmo_spy.size() == 1
            && scene_view->gizmo_tool() == AuthoringViewport::GizmoTool::move;

        const QStringList metric_fields{
            QStringLiteral("activeAttempts=%1").arg(active_press_count),
            QStringLiteral("sourceWidth=%1").arg(required_worker_size.width()),
            QStringLiteral("sourceHeight=%1").arg(required_worker_size.height()),
            QStringLiteral("warmupMinimumFrames=%1").arg(minimum_warmup_frames),
            QStringLiteral("activeSuccesses=%1").arg(active_successes),
            QStringLiteral("activeExactTuples=%1").arg(active_exact_tuples),
            QStringLiteral("activeDrops=%1").arg(active_drops),
            QStringLiteral("activeExpirations=%1").arg(active_expirations),
            QStringLiteral("activeTimeouts=%1").arg(active_timeouts),
            QStringLiteral("activeInvalidActions=%1").arg(active_invalid_actions),
            QStringLiteral("activeInvalidTuples=%1").arg(active_invalid_tuples),
            QStringLiteral("neutralAttempts=%1").arg(neutral_attempts),
            QStringLiteral("neutralSuccesses=%1").arg(neutral_successes),
            QStringLiteral("neutralDrops=%1").arg(neutral_drops),
            QStringLiteral("neutralExpirations=%1").arg(neutral_expirations),
            QStringLiteral("neutralTimeouts=%1").arg(neutral_timeouts),
            QStringLiteral("neutralInvalidActions=%1").arg(neutral_invalid_actions),
            QStringLiteral("neutralInvalidPresentations=%1").arg(neutral_invalid_presentations),
            QStringLiteral("joinedPaintSignals=%1").arg(joined_paint_spy.size()),
            QStringLiteral("totalDrops=%1").arg(dropped_spy.size()),
            QStringLiteral("totalExpirations=%1").arg(expired_spy.size()),
            QStringLiteral("skippedPaints=%1").arg(skipped_paints),
            QStringLiteral("invalidPaintRecords=%1").arg(invalid_paint_records),
            QStringLiteral("p50Ms=%1").arg(
                p50_nanoseconds < 0 ? -1.0 : static_cast<double>(p50_nanoseconds) / 1'000'000.0,
                0, 'f', 3),
            QStringLiteral("p95Ms=%1").arg(
                p95_nanoseconds < 0 ? -1.0 : static_cast<double>(p95_nanoseconds) / 1'000'000.0,
                0, 'f', 3),
            QStringLiteral("maxMs=%1").arg(
                maximum_nanoseconds < 0 ? -1.0 : static_cast<double>(maximum_nanoseconds) / 1'000'000.0,
                0, 'f', 3),
            QStringLiteral("paintedFps=%1").arg(painted_fps, 0, 'f', 3),
            QStringLiteral("paintWindowSeconds=%1").arg(
                static_cast<double>(paint_window_nanoseconds) / 1'000'000'000.0,
                0, 'f', 3),
            QStringLiteral("steadyWindowReached=%1").arg(steady_window_reached ? 1 : 0),
            QStringLiteral("negotiatedAdapter=\"%1\"").arg(negotiated_adapter),
            QStringLiteral("runtimeBackend=\"%1\"").arg(runtime_backend),
            QStringLiteral("runtimeDevice=\"%1\"").arg(runtime_device),
            QStringLiteral("runtimeProcessId=%1").arg(runtime_process_id),
            QStringLiteral("recoveryCount=%1").arg(recovery_count),
            QStringLiteral("identityStable=%1").arg(identity_stable ? 1 : 0),
            QStringLiteral("runtimeStoppedSignals=%1").arg(runtime_stopped_spy.size()),
            QStringLiteral("moveGizmoEnabledDuringPlay=%1").arg(
                move_gizmo_enabled_during_play ? 1 : 0),
            QStringLiteral("moveGizmoUsesW=%1").arg(move_gizmo_uses_w ? 1 : 0),
            QStringLiteral("moveGizmoWindowShortcut=%1").arg(
                move_gizmo_is_window_shortcut ? 1 : 0),
            QStringLiteral("shortcutActiveActionValid=%1").arg(
                shortcut_active_action_valid ? 1 : 0),
            QStringLiteral("shortcutActivePresented=%1").arg(shortcut_active_presented ? 1 : 0),
            QStringLiteral("shortcutNeutralActionValid=%1").arg(
                shortcut_neutral_action_valid ? 1 : 0),
            QStringLiteral("shortcutNeutralPresented=%1").arg(
                shortcut_neutral_presented ? 1 : 0),
            QStringLiteral("playMoveGizmoTriggers=%1").arg(play_move_gizmo_trigger_count),
            QStringLiteral("playShortcutSuppressed=%1").arg(play_shortcut_suppressed ? 1 : 0),
            QStringLiteral("editShortcutControlTriggered=%1").arg(
                edit_shortcut_control_triggered ? 1 : 0),
            QStringLiteral("dirtyBefore=%1").arg(authoring_dirty_before ? 1 : 0),
            QStringLiteral("dirtyBeforeStop=%1").arg(authoring_dirty_before_stop ? 1 : 0),
            QStringLiteral("dirtyAfterStop=%1").arg(authoring_dirty_after_stop ? 1 : 0),
            QStringLiteral("stopCompleted=%1").arg(stop_completed ? 1 : 0),
        };
        const auto metric_line = QStringLiteral("POC J %1: %2")
            .arg(adapter, metric_fields.join(QLatin1Char{' '}));
        std::cout << metric_line.toStdString() << '\n' << std::flush;

        QCOMPARE(active_successes, active_press_count);
        QCOMPARE(active_exact_tuples, active_press_count);
        QCOMPARE(active_press_latencies.size(), qsizetype{active_press_count});
        QCOMPARE(active_press_revisions.size(), qsizetype{active_press_count});
        QCOMPARE(active_drops, 0);
        QCOMPARE(active_expirations, 0);
        QCOMPARE(active_timeouts, 0);
        QCOMPARE(active_invalid_actions, 0);
        QCOMPARE(active_invalid_tuples, 0);
        QCOMPARE(neutral_attempts, active_press_count);
        QCOMPARE(neutral_successes, active_press_count);
        QCOMPARE(neutral_revisions.size(), qsizetype{active_press_count});
        QCOMPARE(neutral_drops, 0);
        QCOMPARE(neutral_expirations, 0);
        QCOMPARE(neutral_timeouts, 0);
        QCOMPARE(neutral_invalid_actions, 0);
        QCOMPARE(neutral_invalid_presentations, 0);
        QCOMPARE(joined_paint_spy.size(), qsizetype{active_press_count});
        QCOMPARE(dropped_spy.size(), qsizetype{0});
        QCOMPARE(expired_spy.size(), qsizetype{0});
        QCOMPARE(invalid_paint_records, 0);
        QVERIFY(steady_window_reached);
        QVERIFY(paint_window_nanoseconds >= minimum_steady_paint_window_nanoseconds);
        QVERIFY(identity_stable);
        QCOMPARE(recovery_count, 0);
        QCOMPARE(runtime_stopped_spy.size(), qsizetype{1});
        QVERIFY(stop_completed);
        QVERIFY(play_shortcut_suppressed);
        QVERIFY(edit_shortcut_control_triggered);
        QCOMPARE(authoring_before_stop, authoring_before);
        QCOMPARE(authoring_after_stop, authoring_before);
        QCOMPARE(authoring_file_before_stop, authoring_file_before);
        QCOMPARE(authoring_file_after_stop, authoring_file_before);
        QCOMPARE(authoring_dirty_before_stop, authoring_dirty_before);
        QCOMPARE(authoring_dirty_after_stop, authoring_dirty_before);
        QVERIFY2(p50_nanoseconds >= 0
                && p50_nanoseconds < maximum_p50_latency_nanoseconds,
            qPrintable(QStringLiteral("%1 POC J p50 latency was %2 ms")
                .arg(adapter)
                .arg(static_cast<double>(p50_nanoseconds) / 1'000'000.0, 0, 'f', 3)));
        QVERIFY2(painted_fps >= 30.0,
            qPrintable(QStringLiteral("%1 POC J Qt-painted FPS was %2")
                .arg(adapter).arg(painted_fps, 0, 'f', 3)));
    }

    void play_input_queued_before_handshake_survives_startup_and_crash_neutralizes()
    {
        EditorWindow window{QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT)};
        window.set_unsaved_prompt([](const QString&) {
            return EditorWindow::UnsavedDecision::discard;
        });
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        auto* play = window.findChild<QAction*>(QStringLiteral("PlayAction"));
        auto* stop = window.findChild<QAction*>(QStringLiteral("StopAction"));
        auto* game = window.findChild<GameViewport*>(QStringLiteral("GameViewport"));
        QVERIFY(play != nullptr && play->isEnabled());
        QVERIFY(stop != nullptr);
        QVERIFY(game != nullptr);
        QVERIFY(window.scene_.has_value());
        const auto authoring_before = dragonpixel::serialization::write_scene_json(*window.scene_);
        QSignalSpy actions_spy{game, &GameViewport::input_actions_changed};
        QSignalSpy correlated_actions_spy{game, &GameViewport::correlated_input_actions_changed};
        QSignalSpy capture_spy{game, &GameViewport::input_capture_changed};
        QSignalSpy frame_spy{window.play_worker_, &WorkerClient::frame_ready};
        QSignalSpy presented_input_spy{game, &GameViewport::input_presented};
        QSignalSpy presented_frame_spy{game, &GameViewport::play_frame_presented};
        QSignalSpy retired_frame_spy{game, &GameViewport::play_frame_retired};
        QSignalSpy worker_status_spy{window.play_worker_, &WorkerClient::status_message};

        play->trigger();
        QVERIFY(game->is_play_mode());
        game->setFocus(Qt::OtherFocusReason);
        QTRY_VERIFY(game->hasFocus());
        QTest::keyPress(game, Qt::Key_W);
        QVERIFY(!actions_spy.isEmpty());
        QVERIFY(!correlated_actions_spy.isEmpty());
        const auto pressed_input_state = actions_spy.last().at(0).toJsonObject();
        QVERIFY(pressed_input_state.value(QStringLiteral("focused")).toBool());
        QVERIFY(pressed_input_state.value(QStringLiteral("captured")).toBool());
        const auto pressed_actions = pressed_input_state.value(QStringLiteral("actions")).toObject();
        QCOMPARE(pressed_actions.value(QStringLiteral("move.y")).toObject()
                     .value(QStringLiteral("value")).toDouble(), 1.0);
        QCOMPARE(pressed_actions.value(QStringLiteral("move.y")).toObject()
                     .value(QStringLiteral("pressCount")).toInteger(), qint64{1});
        const auto pressed_revision = correlated_actions_spy.last().at(1).toULongLong();
        QCOMPARE(pressed_revision, quint64{1});
        QVERIFY(!capture_spy.isEmpty() && capture_spy.last().at(0).toBool());

        QTRY_VERIFY_WITH_TIMEOUT(window.play_worker_->has_frame() && !frame_spy.isEmpty(), 15000);
        // The first device frame can arrive before the input submitted during the
        // control-channel handshake has produced a distinct device-pixel result.
        // Keep the startup/recovery proof aligned with the worker startup budget;
        // POC J owns the steady-state input-to-paint latency threshold.
        QTRY_VERIFY_WITH_TIMEOUT(!presented_input_spy.isEmpty(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!presented_frame_spy.isEmpty(), 5000);
        QCOMPARE(presented_input_spy.first().at(0).toULongLong(), pressed_revision);
        QVERIFY(presented_input_spy.first().at(1).toLongLong() >= 0);
        QTRY_COMPARE_WITH_TIMEOUT(
            presented_frame_spy.last().at(1).toULongLong(), pressed_revision, 5000);
        const auto first_process = window.play_worker_->process_id();
        const auto frames_before_crash = frame_spy.size();
        QVERIFY(first_process > 0);
        window.play_worker_->send_correlated_input_actions(
            QJsonObject{
                {QStringLiteral("focused"), true},
                {QStringLiteral("captured"), true},
                {QStringLiteral("actions"), QJsonObject{
                    {QStringLiteral("move.y"), 1.0},
                }},
            },
            pressed_revision + 1);

        QTRY_VERIFY_WITH_TIMEOUT(!retired_frame_spy.isEmpty(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(std::any_of(
            worker_status_spy.cbegin(), worker_status_spy.cend(), [](const QList<QVariant>& values) {
                return values.at(0).toString().contains(
                    QStringLiteral("Worker error for runtimeInput"));
            }), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(
            !actions_spy.isEmpty()
                && !actions_spy.last().at(0).toJsonObject()
                        .value(QStringLiteral("captured")).toBool()
                && actions_spy.last().at(0).toJsonObject()
                        .value(QStringLiteral("actions")).toObject()
                        .value(QStringLiteral("move.y")).toObject()
                        .value(QStringLiteral("value")).toDouble() == 0.0,
            5000);
        QTRY_VERIFY_WITH_TIMEOUT(
            !capture_spy.isEmpty() && !capture_spy.last().at(0).toBool(),
            5000);
        QTRY_VERIFY_WITH_TIMEOUT(!game->is_input_enabled(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(window.play_worker_->recovered_after_crash(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(game->is_input_enabled(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(frame_spy.size() > frames_before_crash, 15000);
        QVERIFY(window.play_worker_->process_id() > 0);
        QVERIFY(window.play_worker_->process_id() != first_process);
        QCOMPARE(dragonpixel::serialization::write_scene_json(*window.scene_), authoring_before);

        stop->trigger();
        QVERIFY(!game->is_play_mode());
        QCOMPARE(dragonpixel::serialization::write_scene_json(*window.scene_), authoring_before);
    }

    void play_pause_intent_survives_handshake_and_worker_recovery()
    {
        EditorWindow window{QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT)};
        window.set_unsaved_prompt([](const QString&) {
            return EditorWindow::UnsavedDecision::discard;
        });
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        auto* play = window.findChild<QAction*>(QStringLiteral("PlayAction"));
        auto* pause = window.findChild<QAction*>(QStringLiteral("PauseAction"));
        auto* resume = window.findChild<QAction*>(QStringLiteral("ResumeAction"));
        auto* stop = window.findChild<QAction*>(QStringLiteral("StopAction"));
        auto* game = window.findChild<GameViewport*>(QStringLiteral("GameViewport"));
        QVERIFY(play != nullptr && play->isEnabled());
        QVERIFY(pause != nullptr && resume != nullptr && stop != nullptr && game != nullptr);
        QSignalSpy pause_spy{window.play_worker_, &WorkerClient::runtime_pause_changed};

        play->trigger();
        pause->trigger();
        QVERIFY(window.play_paused_);
        QVERIFY(!game->is_input_enabled());
        QTRY_VERIFY_WITH_TIMEOUT(!pause_spy.isEmpty(), 15000);
        QCOMPARE(pause_spy.last().at(0).toBool(), true);

        const auto first_process = window.play_worker_->process_id();
        QVERIFY(first_process > 0);
        pause_spy.clear();
        window.play_worker_->force_crash();
        QTRY_VERIFY_WITH_TIMEOUT(window.play_worker_->recovered_after_crash(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!pause_spy.isEmpty(), 15000);
        QCOMPARE(pause_spy.last().at(0).toBool(), true);
        QVERIFY(window.play_paused_);
        QVERIFY(!game->is_input_enabled());
        QVERIFY(window.play_worker_->process_id() > 0);
        QVERIFY(window.play_worker_->process_id() != first_process);

        pause_spy.clear();
        resume->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!pause_spy.isEmpty(), 5000);
        QCOMPARE(pause_spy.last().at(0).toBool(), false);
        QTRY_VERIFY_WITH_TIMEOUT(game->is_input_enabled(), 5000);
        QVERIFY(!window.play_paused_);

        stop->trigger();
        QVERIFY(!game->is_play_mode());
    }

    void stopping_during_startup_cannot_reenable_or_restart_play_input()
    {
        EditorWindow window{QString::fromUtf8(DPE_DEFAULT_SAMPLE_PROJECT)};
        window.set_unsaved_prompt([](const QString&) {
            return EditorWindow::UnsavedDecision::discard;
        });
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        auto* play = window.findChild<QAction*>(QStringLiteral("PlayAction"));
        auto* stop = window.findChild<QAction*>(QStringLiteral("StopAction"));
        auto* game = window.findChild<GameViewport*>(QStringLiteral("GameViewport"));
        QVERIFY(play != nullptr && play->isEnabled());
        QVERIFY(stop != nullptr && game != nullptr);
        QSignalSpy input_ready_spy{window.play_worker_, &WorkerClient::runtime_input_ready};
        QSignalSpy frame_spy{window.play_worker_, &WorkerClient::frame_ready};

        play->trigger();
        QVERIFY(window.play_running_);
        stop->trigger();
        QVERIFY(!window.play_running_);
        QVERIFY(!window.play_paused_);
        QVERIFY(!game->is_play_mode());
        QVERIFY(!game->is_input_enabled());

        QTest::qWait(1000);
        QVERIFY(input_ready_spy.isEmpty());
        QVERIFY(frame_spy.isEmpty());
        QVERIFY(!window.play_running_);
        QVERIFY(!game->is_play_mode());
        QVERIFY(!game->is_input_enabled());
    }
};

QTEST_MAIN(EditorInteractionTests)

#include "EditorInteractionTests.moc"
