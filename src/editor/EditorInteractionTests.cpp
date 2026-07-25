#include "EditorWindow.h"

#include <QAction>
#include <QComboBox>
#include <QClipboard>
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QItemSelectionModel>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QtTest/QTest>

#include <cmath>

namespace
{
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
}

class EditorInteractionTests final : public QObject
{
    Q_OBJECT

private slots:
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

    void worker_transport_files_are_cleaned_up()
    {
        const auto pattern = QStringLiteral("dpe-s1-%1-*.frame").arg(QApplication::applicationPid());
        QTRY_VERIFY_WITH_TIMEOUT(
            QDir{QDir::tempPath()}.entryList({pattern}, QDir::Files).isEmpty(),
            3000);
    }
};

QTEST_MAIN(EditorInteractionTests)

#include "EditorInteractionTests.moc"
