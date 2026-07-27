#include "GameViewport.h"

#include <QAction>
#include <QApplication>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMainWindow>
#include <QSignalSpy>
#include <QTest>
#include <QVBoxLayout>
#include <QWidget>

namespace
{
QJsonObject last_input_state(const QSignalSpy& spy)
{
    return spy.isEmpty() ? QJsonObject{} : spy.last().at(0).toJsonObject();
}

QJsonObject action_state(const QJsonObject& input_state, const QString& name)
{
    return input_state.value(QStringLiteral("actions")).toObject().value(name).toObject();
}

double action_value(const QJsonObject& input_state, const QString& name)
{
    return action_state(input_state, name).value(QStringLiteral("value")).toDouble();
}
}

class GameViewportTests final : public QObject
{
    Q_OBJECT

private slots:
    void play_input_maps_actions_suppresses_shortcuts_and_neutralizes()
    {
        QMainWindow window;
        auto* container = new QWidget(&window);
        auto* layout = new QVBoxLayout(container);
        auto* viewport = new GameViewport(container);
        auto* other_focus = new QLineEdit(container);
        layout->addWidget(viewport);
        layout->addWidget(other_focus);
        window.setCentralWidget(container);

        auto* save_action = new QAction(&window);
        save_action->setShortcut(QKeySequence::Save);
        save_action->setShortcutContext(Qt::WindowShortcut);
        window.addAction(save_action);
        QSignalSpy shortcut_spy{save_action, &QAction::triggered};
        QSignalSpy actions_spy{viewport, &GameViewport::input_actions_changed};
        QSignalSpy capture_spy{viewport, &GameViewport::input_capture_changed};

        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        viewport->setFocus(Qt::OtherFocusReason);
        QTRY_VERIFY(viewport->hasFocus());
        const auto save_combination = save_action->shortcut()[0];
        QTest::keyClick(
            viewport,
            save_combination.key(),
            save_combination.keyboardModifiers());
        QCOMPARE(shortcut_spy.size(), 1);

        viewport->set_play_mode(true);

        QTest::keyPress(viewport, Qt::Key_W);
        QTest::keyPress(viewport, Qt::Key_D);
        QTest::keyPress(viewport, Qt::Key_Space);
        QVERIFY(actions_spy.size() >= 3);
        auto input_state = last_input_state(actions_spy);
        QVERIFY(input_state.value(QStringLiteral("focused")).toBool());
        QVERIFY(input_state.value(QStringLiteral("captured")).toBool());
        QCOMPARE(action_value(input_state, QStringLiteral("move.x")), 1.0);
        QCOMPARE(action_value(input_state, QStringLiteral("move.y")), 1.0);
        QCOMPARE(action_value(input_state, QStringLiteral("jump")), 1.0);
        QCOMPARE(action_state(input_state, QStringLiteral("move.x"))
                     .value(QStringLiteral("pressCount")).toInteger(), qint64{1});
        QCOMPARE(action_state(input_state, QStringLiteral("jump"))
                     .value(QStringLiteral("pressCount")).toInteger(), qint64{1});
        QVERIFY(capture_spy.last().at(0).toBool());

        QTest::keyRelease(viewport, Qt::Key_W);
        QTest::keyRelease(viewport, Qt::Key_D);
        QTest::keyPress(viewport, Qt::Key_Right);
        QTest::keyPress(viewport, Qt::Key_Up);
        input_state = last_input_state(actions_spy);
        QCOMPARE(action_value(input_state, QStringLiteral("move.x")), 1.0);
        QCOMPARE(action_value(input_state, QStringLiteral("move.y")), 1.0);
        QVERIFY(!input_state.value(QStringLiteral("actions")).toObject()
                     .contains(QStringLiteral("look.x")));
        QVERIFY(!input_state.value(QStringLiteral("actions")).toObject()
                     .contains(QStringLiteral("look.y")));

        QTest::keyRelease(viewport, Qt::Key_Right);
        QTest::keyRelease(viewport, Qt::Key_Up);
        QTest::keyPress(viewport, Qt::Key_A);
        QTest::keyPress(viewport, Qt::Key_S);
        input_state = last_input_state(actions_spy);
        QCOMPARE(action_value(input_state, QStringLiteral("move.x")), -1.0);
        QCOMPARE(action_value(input_state, QStringLiteral("move.y")), -1.0);
        QTest::keyPress(viewport, Qt::Key_D);
        QTest::keyPress(viewport, Qt::Key_W);
        input_state = last_input_state(actions_spy);
        QCOMPARE(action_value(input_state, QStringLiteral("move.x")), 0.0);
        QCOMPARE(action_value(input_state, QStringLiteral("move.y")), 0.0);
        const auto before_auto_repeat = actions_spy.size();
        QKeyEvent auto_repeat{
            QEvent::KeyPress, Qt::Key_W, Qt::NoModifier, QString{}, true, 2};
        QApplication::sendEvent(viewport, &auto_repeat);
        QCOMPARE(actions_spy.size(), before_auto_repeat);
        QTest::keyRelease(viewport, Qt::Key_A);
        QTest::keyRelease(viewport, Qt::Key_S);
        QTest::keyRelease(viewport, Qt::Key_D);
        QTest::keyRelease(viewport, Qt::Key_W);

        const auto before_shortcut = actions_spy.size();
        QTest::keyClick(
            viewport,
            save_combination.key(),
            save_combination.keyboardModifiers());
        QCOMPARE(shortcut_spy.size(), 1);
        QVERIFY(actions_spy.size() > before_shortcut);

        const auto before_mouse = actions_spy.size();
        QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, viewport->rect().center());
        QCOMPARE(actions_spy.size(), before_mouse);
        QVERIFY(!last_input_state(actions_spy).value(QStringLiteral("actions")).toObject()
                     .contains(QStringLiteral("primary")));
        QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, viewport->rect().center());

        QTest::keyPress(viewport, Qt::Key_W);
        QTest::keyClick(viewport, Qt::Key_Escape);
        QTRY_VERIFY(!viewport->hasFocus());
        input_state = last_input_state(actions_spy);
        QVERIFY(!input_state.value(QStringLiteral("focused")).toBool());
        QVERIFY(!input_state.value(QStringLiteral("captured")).toBool());
        QCOMPARE(action_value(input_state, QStringLiteral("move.x")), 0.0);
        QCOMPARE(action_value(input_state, QStringLiteral("move.y")), 0.0);
        QCOMPARE(action_value(input_state, QStringLiteral("jump")), 0.0);
        QVERIFY(!capture_spy.last().at(0).toBool());

        viewport->setFocus(Qt::OtherFocusReason);
        QTRY_VERIFY(viewport->hasFocus());
        QTest::keyPress(viewport, Qt::Key_W);
        other_focus->setFocus(Qt::OtherFocusReason);
        QTRY_VERIFY(other_focus->hasFocus());
        QTRY_VERIFY(!actions_spy.isEmpty()
            && !last_input_state(actions_spy).value(QStringLiteral("focused")).toBool()
            && !last_input_state(actions_spy).value(QStringLiteral("captured")).toBool()
            && action_value(last_input_state(actions_spy), QStringLiteral("move.y")) == 0.0);
        QVERIFY(!capture_spy.last().at(0).toBool());

        viewport->setFocus(Qt::OtherFocusReason);
        QTRY_VERIFY(viewport->hasFocus());
        viewport->set_input_enabled(false);
        const auto before_paused_key = actions_spy.size();
        QTest::keyClick(viewport, Qt::Key_W);
        QCOMPARE(actions_spy.size(), before_paused_key);
        QVERIFY(!viewport->is_input_enabled());
        viewport->set_input_enabled(true);
        QTest::keyPress(viewport, Qt::Key_W);
        const auto before_stop = actions_spy.size();
        viewport->set_play_mode(false);
        QCOMPARE(actions_spy.size(), before_stop + 1);
        input_state = last_input_state(actions_spy);
        QVERIFY(!input_state.value(QStringLiteral("captured")).toBool());
        QCOMPARE(action_value(input_state, QStringLiteral("move.y")), 0.0);
        QTest::keyClick(viewport, Qt::Key_W);
        QCOMPARE(actions_spy.size(), before_stop + 1);
    }

    void applies_rebound_keyboard_and_mouse_actions_from_input_map()
    {
        InputMapService service;
        auto input_map = service.compatibility_map();
        auto& gameplay = input_map.control_maps.front();
        gameplay.actions.front().bindings.front().path = QStringLiteral("keyboard/l");
        gameplay.actions.push_back(InputAction{
            QStringLiteral("0ccb0140-347f-4d52-86ad-95320ed480ef"),
            QStringLiteral("fire"), InputActionKind::button,
            {InputBinding{QStringLiteral("1b8b596d-4c47-430f-a51c-aa99b62e1333"),
                QStringLiteral("mouse/left"), 1.0, 0.0, {}}}, {}});
        gameplay.actions.push_back(InputAction{
            QStringLiteral("94374dbc-586b-43a6-90bf-e4172cc98a44"),
            QStringLiteral("look.x"), InputActionKind::axis1d,
            {InputBinding{QStringLiteral("48f30c24-0792-441f-b99e-80f18411e37e"),
                QStringLiteral("mouse/delta-x"), 0.1, 0.0, {}}}, {}});

        GameViewport viewport;
        viewport.resize(640, 360);
        viewport.set_input_map(input_map);
        QSignalSpy actions_spy{&viewport, &GameViewport::input_actions_changed};
        viewport.show();
        QVERIFY(QTest::qWaitForWindowExposed(&viewport));
        viewport.set_play_mode(true);
        viewport.setFocus(Qt::OtherFocusReason);
        QTRY_VERIFY(viewport.hasFocus());

        QTest::keyPress(&viewport, Qt::Key_D);
        QCOMPARE(actions_spy.size(), 0);
        QTest::keyPress(&viewport, Qt::Key_L);
        QCOMPARE(action_value(last_input_state(actions_spy), QStringLiteral("move.x")), 1.0);
        QTest::keyRelease(&viewport, Qt::Key_L);

        const auto center = viewport.rect().center();
        QTest::mousePress(&viewport, Qt::LeftButton, Qt::NoModifier, center);
        QCOMPARE(action_value(last_input_state(actions_spy), QStringLiteral("fire")), 1.0);
        QTest::mouseMove(&viewport, center + QPoint{5, 0});
        QCOMPARE(action_value(last_input_state(actions_spy), QStringLiteral("look.x")), 0.5);
        QTRY_COMPARE_WITH_TIMEOUT(
            action_value(last_input_state(actions_spy), QStringLiteral("look.x")), 0.0, 250);
        QTest::mouseRelease(&viewport, Qt::LeftButton, Qt::NoModifier, center + QPoint{5, 0});
        QCOMPARE(action_value(last_input_state(actions_spy), QStringLiteral("fire")), 0.0);
    }

    void correlates_real_input_to_completed_qt_paint_and_reports_skips()
    {
        GameViewport viewport;
        viewport.resize(640, 360);
        QSignalSpy correlated_spy{&viewport, &GameViewport::correlated_input_actions_changed};
        QSignalSpy presented_spy{&viewport, &GameViewport::input_presented};
        QSignalSpy input_frame_painted_spy{&viewport, &GameViewport::input_frame_painted};
        QSignalSpy dropped_spy{&viewport, &GameViewport::input_correlation_dropped};
        QSignalSpy frame_presented_spy{&viewport, &GameViewport::play_frame_presented};
        QSignalSpy frame_skipped_spy{&viewport, &GameViewport::presentation_frames_skipped};
        QSignalSpy frame_retired_spy{&viewport, &GameViewport::play_frame_retired};

        viewport.show();
        QVERIFY(QTest::qWaitForWindowExposed(&viewport));
        viewport.set_play_mode(true);
        viewport.setFocus(Qt::OtherFocusReason);
        QTRY_VERIFY(viewport.hasFocus());

        QTest::keyPress(&viewport, Qt::Key_D);
        QCOMPARE(correlated_spy.size(), 1);
        const auto first_revision = correlated_spy.last().at(1).toULongLong();
        QCOMPARE(first_revision, quint64{1});
        viewport.set_play_frame(
            QImage{1280, 720, QImage::Format_ARGB32}, first_revision + 1, 9);
        QTest::qWait(25);
        QCOMPARE(frame_presented_spy.size(), 0);
        viewport.set_play_frame(QImage{1280, 720, QImage::Format_ARGB32}, first_revision, 10);
        QTRY_COMPARE(presented_spy.size(), 1);
        QTRY_COMPARE(input_frame_painted_spy.size(), 1);
        QTRY_COMPARE(frame_presented_spy.size(), 1);
        QCOMPARE(frame_presented_spy.last().at(0).toULongLong(), quint64{10});
        QCOMPARE(frame_presented_spy.last().at(1).toULongLong(), first_revision);
        QCOMPARE(presented_spy.last().at(0).toULongLong(), first_revision);
        QVERIFY(presented_spy.last().at(1).toLongLong() >= 0);
        QCOMPARE(input_frame_painted_spy.last().at(0).toULongLong(), first_revision);
        QCOMPARE(input_frame_painted_spy.last().at(1).toULongLong(), quint64{10});
        QCOMPARE(
            input_frame_painted_spy.last().at(2).toLongLong(),
            presented_spy.last().at(1).toLongLong());
        QVERIFY(input_frame_painted_spy.last().at(2).toLongLong() >= 20'000'000LL);
        QVERIFY(input_frame_painted_spy.last().at(3).toLongLong()
            >= input_frame_painted_spy.last().at(2).toLongLong());

        QTest::keyRelease(&viewport, Qt::Key_D);
        QTest::keyPress(&viewport, Qt::Key_A);
        QCOMPARE(correlated_spy.size(), 3);
        const auto latest_revision = correlated_spy.last().at(1).toULongLong();
        viewport.set_play_frame(QImage{1280, 720, QImage::Format_ARGB32}, latest_revision, 13);
        QTRY_COMPARE(presented_spy.size(), 2);
        QTRY_COMPARE(input_frame_painted_spy.size(), 2);
        QTRY_COMPARE(dropped_spy.size(), 1);
        QTRY_COMPARE(frame_presented_spy.size(), 2);
        QTRY_COMPARE(frame_skipped_spy.size(), 1);
        QCOMPARE(frame_skipped_spy.last().at(0).toULongLong(), quint64{2});
        QCOMPARE(dropped_spy.last().at(0).toULongLong(), quint64{2});
        QCOMPARE(dropped_spy.last().at(1).toULongLong(), latest_revision);
        QCOMPARE(input_frame_painted_spy.last().at(0).toULongLong(), latest_revision);
        QCOMPARE(input_frame_painted_spy.last().at(1).toULongLong(), quint64{13});

        const auto presented_count = frame_presented_spy.size();
        viewport.set_play_frame(
            QImage{1280, 720, QImage::Format_ARGB32}, latest_revision - 1, 14);
        viewport.set_play_frame(
            QImage{1280, 720, QImage::Format_ARGB32}, latest_revision, 13);
        viewport.set_play_frame(
            QImage{1280, 720, QImage::Format_ARGB32}, latest_revision, 12);
        QTest::qWait(50);
        QCOMPARE(frame_presented_spy.size(), presented_count);

        viewport.retire_play_frame();
        QCOMPARE(frame_retired_spy.size(), 1);
        const auto before_repaint = frame_presented_spy.size();
        viewport.repaint();
        QCOMPARE(frame_presented_spy.size(), before_repaint);
    }

    void neutral_release_does_not_emit_active_pixel_latency()
    {
        GameViewport viewport;
        viewport.resize(640, 360);
        QSignalSpy correlated_spy{&viewport, &GameViewport::correlated_input_actions_changed};
        QSignalSpy presented_spy{&viewport, &GameViewport::input_presented};
        QSignalSpy input_frame_painted_spy{&viewport, &GameViewport::input_frame_painted};

        viewport.show();
        QVERIFY(QTest::qWaitForWindowExposed(&viewport));
        viewport.set_play_mode(true);
        viewport.setFocus(Qt::OtherFocusReason);
        QTRY_VERIFY(viewport.hasFocus());

        QTest::keyPress(&viewport, Qt::Key_D);
        QCOMPARE(correlated_spy.size(), 1);
        const auto active_revision = correlated_spy.last().at(1).toULongLong();
        viewport.set_play_frame(
            QImage{1280, 720, QImage::Format_ARGB32}, active_revision, 1);
        QTRY_COMPARE(presented_spy.size(), 1);
        QTRY_COMPARE(input_frame_painted_spy.size(), 1);

        QTest::keyRelease(&viewport, Qt::Key_D);
        QCOMPARE(correlated_spy.size(), 2);
        const auto neutral_revision = correlated_spy.last().at(1).toULongLong();
        viewport.set_play_frame(
            QImage{1280, 720, QImage::Format_ARGB32}, neutral_revision, 2);
        QTRY_COMPARE(presented_spy.size(), 2);
        QCOMPARE(presented_spy.last().at(0).toULongLong(), neutral_revision);
        QCOMPARE(input_frame_painted_spy.size(), 1);
    }

    void expires_first_non_applicable_input_while_presented_revision_remains_zero()
    {
        GameViewport viewport;
        viewport.resize(640, 360);
        QSignalSpy correlated_spy{&viewport, &GameViewport::correlated_input_actions_changed};
        QSignalSpy presented_spy{&viewport, &GameViewport::input_presented};
        QSignalSpy dropped_spy{&viewport, &GameViewport::input_correlation_dropped};
        QSignalSpy expired_spy{&viewport, &GameViewport::input_correlation_expired};

        viewport.show();
        QVERIFY(QTest::qWaitForWindowExposed(&viewport));
        viewport.set_play_mode(true);
        viewport.setFocus(Qt::OtherFocusReason);
        QTRY_VERIFY(viewport.hasFocus());

        QTest::keyPress(&viewport, Qt::Key_D);
        QCOMPARE(correlated_spy.size(), 1);
        const auto first_revision = correlated_spy.last().at(1).toULongLong();
        viewport.set_play_frame(QImage{1280, 720, QImage::Format_ARGB32}, 0, 1);

        QTRY_COMPARE_WITH_TIMEOUT(expired_spy.size(), 1, 6500);
        QCOMPARE(expired_spy.first().at(0).toULongLong(), first_revision);
        QCOMPARE(presented_spy.size(), 0);
        QCOMPARE(dropped_spy.size(), 0);
    }
};

QTEST_MAIN(GameViewportTests)
#include "GameViewportTests.moc"
