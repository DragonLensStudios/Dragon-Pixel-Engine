#include "GameViewport.h"
#include "GamepadInputSource.h"

#include <QPainter>
#include <QPaintEvent>
#include <QEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <utility>

namespace
{
constexpr qint64 input_correlation_timeout_nanoseconds = 5'000'000'000LL;
constexpr qsizetype maximum_pending_input_correlations = 256;

QString canonical_key_path(int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
    {
        return QStringLiteral("keyboard/")
            + QChar{static_cast<char16_t>(QLatin1Char('a').unicode() + key - Qt::Key_A)};
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
    {
        return QStringLiteral("keyboard/")
            + QChar{static_cast<char16_t>(QLatin1Char('0').unicode() + key - Qt::Key_0)};
    }
    switch (key)
    {
    case Qt::Key_Space: return QStringLiteral("keyboard/space");
    case Qt::Key_Up: return QStringLiteral("keyboard/up");
    case Qt::Key_Down: return QStringLiteral("keyboard/down");
    case Qt::Key_Left: return QStringLiteral("keyboard/left");
    case Qt::Key_Right: return QStringLiteral("keyboard/right");
    case Qt::Key_Return:
    case Qt::Key_Enter: return QStringLiteral("keyboard/enter");
    case Qt::Key_Tab: return QStringLiteral("keyboard/tab");
    case Qt::Key_Shift: return QStringLiteral("keyboard/shift");
    case Qt::Key_Control: return QStringLiteral("keyboard/control");
    case Qt::Key_Alt: return QStringLiteral("keyboard/alt");
    default: return {};
    }
}

QString canonical_mouse_path(Qt::MouseButton button)
{
    switch (button)
    {
    case Qt::LeftButton: return QStringLiteral("mouse/left");
    case Qt::RightButton: return QStringLiteral("mouse/right");
    case Qt::MiddleButton: return QStringLiteral("mouse/middle");
    case Qt::BackButton: return QStringLiteral("mouse/back");
    case Qt::ForwardButton: return QStringLiteral("mouse/forward");
    default: return {};
    }
}

bool has_active_action(const QJsonObject& input_state)
{
    const auto actions = input_state.value(QStringLiteral("actions")).toObject();
    for (auto iterator = actions.constBegin(); iterator != actions.constEnd(); ++iterator)
    {
        const auto value = iterator.value().toObject().value(QStringLiteral("value")).toDouble();
        if (qIsFinite(value) && !qFuzzyIsNull(value))
        {
            return true;
        }
    }
    return false;
}
}

GameViewport::GameViewport(QWidget* parent) : QWidget(parent)
{
    input_map_ = input_map_service_.compatibility_map();
    gamepad_input_source_ = new GamepadInputSource(this);
    connect(gamepad_input_source_, &GamepadInputSource::controls_changed, this,
        [this](const QHash<QString, double>& gamepad_controls) {
            for (auto iterator = controls_.begin(); iterator != controls_.end();)
            {
                if (iterator.key().startsWith(QStringLiteral("gamepad/")))
                {
                    iterator = controls_.erase(iterator);
                }
                else ++iterator;
            }
            for (auto iterator = gamepad_controls.constBegin();
                 iterator != gamepad_controls.constEnd(); ++iterator)
            {
                if (input_map_service_.uses_control_path(input_map_, iterator.key()))
                {
                    controls_.insert(iterator.key(), iterator.value());
                }
            }
            if (play_mode_ && input_enabled_ && hasFocus())
            {
                publish_input(presentation_clock_.nsecsElapsed());
            }
        });
    presentation_clock_.start();
    input_correlation_timer_.setSingleShot(true);
    input_correlation_timer_.setTimerType(Qt::PreciseTimer);
    connect(&input_correlation_timer_, &QTimer::timeout, this, [this] {
        expire_input_correlations(presentation_clock_.nsecsElapsed());
        schedule_input_correlation_timeout();
    });
    transient_input_timer_.setSingleShot(true);
    transient_input_timer_.setInterval(32);
    transient_input_timer_.setTimerType(Qt::PreciseTimer);
    connect(&transient_input_timer_, &QTimer::timeout, this, [this] {
        bool had_transient = controls_.remove(QStringLiteral("mouse/delta-x"));
        had_transient = controls_.remove(QStringLiteral("mouse/delta-y"))
            || had_transient;
        had_transient = controls_.remove(QStringLiteral("mouse/wheel-x"))
            || had_transient;
        had_transient = controls_.remove(QStringLiteral("mouse/wheel-y"))
            || had_transient;
        if (had_transient && play_mode_ && input_enabled_ && hasFocus())
        {
            publish_input(presentation_clock_.nsecsElapsed());
        }
    });
    setObjectName(QStringLiteral("GameViewport"));
    setAccessibleName(QStringLiteral("Game view"));
    setAccessibleDescription(QStringLiteral(
        "Primary camera output outside Play and isolated runtime output during Play."));
    setMinimumSize(320, 180);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAutoFillBackground(false);
}

bool GameViewport::event(QEvent* event)
{
    if (play_mode_ && input_enabled_ && event->type() == QEvent::ShortcutOverride)
    {
        event->accept();
        return true;
    }
    return QWidget::event(event);
}

void GameViewport::set_preview_frame(const QImage& image)
{
    preview_frame_ = image;
    update();
}

void GameViewport::set_play_frame(const QImage& image)
{
    set_play_frame(image, 0, 0);
}

void GameViewport::set_play_frame(const QImage& image, quint64 input_revision)
{
    set_play_frame(image, input_revision, 0);
}

void GameViewport::set_play_frame(
    const QImage& image,
    quint64 input_revision,
    quint64 frame_revision)
{
    if ((play_frame_revision_ != 0
            && (frame_revision == 0 || frame_revision <= play_frame_revision_))
        || input_revision < play_frame_input_revision_
        || input_revision > next_input_revision_)
    {
        return;
    }
    play_frame_ = image;
    play_frame_input_revision_ = input_revision;
    play_frame_revision_ = frame_revision;
    update();
}

void GameViewport::retire_play_frame()
{
    play_frame_ = {};
    play_frame_input_revision_ = 0;
    play_frame_revision_ = 0;
    last_presented_frame_revision_ = 0;
    emit play_frame_retired();
    update();
}

void GameViewport::clear_preview_frame()
{
    preview_frame_ = {};
    update();
}

void GameViewport::set_play_mode(bool enabled)
{
    if (play_mode_ == enabled)
    {
        return;
    }
    if (!enabled)
    {
        release_input();
    }
    play_mode_ = enabled;
    input_enabled_ = enabled;
    if (enabled)
    {
        play_frame_ = {};
        play_frame_input_revision_ = 0;
        play_frame_revision_ = 0;
        last_presented_frame_revision_ = 0;
        next_input_revision_ = 0;
        pending_input_correlations_.clear();
        input_correlation_timer_.stop();
        controls_.clear();
        has_mouse_position_ = false;
        capture_active_ = false;
        last_action_values_.clear();
        action_press_counts_.clear();
        action_release_counts_.clear();
        status_ = QStringLiteral("Play — isolated runtime world");
    }
    else
    {
        pending_input_correlations_.clear();
        input_correlation_timer_.stop();
        status_ = QStringLiteral("Live primary-camera preview");
    }
    refresh_gamepad_activation();
    update();
}

void GameViewport::set_input_enabled(bool enabled)
{
    if (input_enabled_ == enabled)
    {
        return;
    }
    if (!enabled)
    {
        clear_input(true, hasFocus() && play_mode_);
    }
    input_enabled_ = enabled;
    refresh_gamepad_activation();
}

void GameViewport::set_runtime_input_ready(bool ready)
{
    if (!ready)
    {
        input_correlation_expiration_suspended_ = true;
        input_correlation_timer_.stop();
        return;
    }
    if (!input_correlation_expiration_suspended_)
    {
        return;
    }
    input_correlation_expiration_suspended_ = false;
    const auto now = presentation_clock_.nsecsElapsed();
    for (auto iterator = pending_input_correlations_.begin();
         iterator != pending_input_correlations_.end(); ++iterator)
    {
        if (iterator->expiration_origin_ns < 0)
        {
            iterator->expiration_origin_ns = now;
        }
    }
    schedule_input_correlation_timeout();
}

void GameViewport::set_input_map(const InputMapDocument& input_map)
{
    if (play_mode_) clear_input(true, hasFocus() && input_enabled_);
    input_map_ = input_map;
    last_action_values_.clear();
    action_press_counts_.clear();
    action_release_counts_.clear();
}

void GameViewport::release_input()
{
    clear_input(true, hasFocus() && play_mode_);
}

void GameViewport::clear_input(bool force_neutral, bool focused, qint64 event_received_ns)
{
    if (event_received_ns < 0)
    {
        event_received_ns = presentation_clock_.nsecsElapsed();
    }
    const auto had_input = std::any_of(
        controls_.cbegin(), controls_.cend(), [](double value) { return !qFuzzyIsNull(value); });
    controls_.clear();
    transient_input_timer_.stop();
    has_mouse_position_ = false;
    const auto was_captured = capture_active_;
    capture_active_ = false;
    if (force_neutral || had_input || was_captured)
    {
        emit_input_actions(make_input_state(
            input_map_service_.evaluate(input_map_, controls_), focused, false),
            event_received_ns);
    }
    if (was_captured || force_neutral)
    {
        emit input_capture_changed(false);
    }
}

void GameViewport::emit_input_actions(const QJsonObject& input_state, qint64 event_received_ns)
{
    if (event_received_ns < 0)
    {
        event_received_ns = presentation_clock_.nsecsElapsed();
    }
    const auto revision = ++next_input_revision_;
    pending_input_correlations_.insert(
        revision,
        PendingInputCorrelation{
            event_received_ns,
            input_correlation_expiration_suspended_ ? -1 : event_received_ns,
            has_active_action(input_state)});
    while (pending_input_correlations_.size() > maximum_pending_input_correlations)
    {
        const auto expired = pending_input_correlations_.begin().key();
        pending_input_correlations_.erase(pending_input_correlations_.begin());
        emit input_correlation_expired(expired);
    }
    schedule_input_correlation_timeout();
    emit input_actions_changed(input_state);
    emit correlated_input_actions_changed(input_state, revision);
}

QJsonObject GameViewport::make_input_state(
    const QHash<QString, EvaluatedInputAction>& values,
    bool focused,
    bool captured)
{
    QJsonObject actions;
    auto names = values.keys();
    std::sort(names.begin(), names.end());
    for (const auto& name : names)
    {
        const auto evaluated = values.value(name);
        const auto value = evaluated.value;
        const auto previous = last_action_values_.value(name, 0.0);
        const auto previous_active = !qFuzzyIsNull(previous);
        const auto active = !qFuzzyIsNull(value);
        if (!previous_active && active)
        {
            ++action_press_counts_[name];
        }
        else if (previous_active && !active)
        {
            ++action_release_counts_[name];
        }
        else if (previous_active && active && previous * value < 0.0)
        {
            ++action_release_counts_[name];
            ++action_press_counts_[name];
        }
        last_action_values_.insert(name, value);
        actions.insert(name, QJsonObject{
            {QStringLiteral("kind"), InputMapService::action_kind_name(evaluated.kind)},
            {QStringLiteral("value"), value},
            {QStringLiteral("pressCount"), static_cast<qint64>(action_press_counts_.value(name))},
            {QStringLiteral("releaseCount"), static_cast<qint64>(action_release_counts_.value(name))},
        });
    }
    return QJsonObject{
        {QStringLiteral("focused"), focused},
        {QStringLiteral("captured"), captured},
        {QStringLiteral("actions"), actions},
    };
}

void GameViewport::publish_input(qint64 event_received_ns)
{
    if (!play_mode_ || !input_enabled_) return;
    if (!capture_active_)
    {
        capture_active_ = true;
        emit input_capture_changed(true);
    }
    emit_input_actions(make_input_state(
        input_map_service_.evaluate(input_map_, controls_), hasFocus(), true),
        event_received_ns);
    clear_transient_controls();
}

void GameViewport::keyPressEvent(QKeyEvent* event)
{
    const auto event_received_ns = presentation_clock_.nsecsElapsed();
    if (play_mode_ && input_enabled_ && hasFocus() && !event->isAutoRepeat())
    {
        if (event->key() == Qt::Key_Escape)
        {
            clear_input(true, false, event_received_ns);
            clearFocus();
            event->accept();
            return;
        }
        const auto path = canonical_key_path(event->key());
        if (!path.isEmpty() && input_map_service_.uses_control_path(input_map_, path))
        {
            if (qFuzzyIsNull(controls_.value(path)))
            {
                controls_.insert(path, 1.0);
                publish_input(event_received_ns);
            }
        }
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void GameViewport::keyReleaseEvent(QKeyEvent* event)
{
    const auto event_received_ns = presentation_clock_.nsecsElapsed();
    if (play_mode_ && input_enabled_ && hasFocus() && !event->isAutoRepeat())
    {
        if (event->key() == Qt::Key_Escape)
        {
            event->accept();
            return;
        }
        const auto path = canonical_key_path(event->key());
        if (!path.isEmpty() && input_map_service_.uses_control_path(input_map_, path))
        {
            if (!qFuzzyIsNull(controls_.value(path)))
            {
                controls_.insert(path, 0.0);
                publish_input(event_received_ns);
            }
        }
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void GameViewport::mousePressEvent(QMouseEvent* event)
{
    const auto event_received_ns = presentation_clock_.nsecsElapsed();
    if (play_mode_ && input_enabled_)
    {
        setFocus(Qt::MouseFocusReason);
        const auto path = canonical_mouse_path(event->button());
        const auto mapped = !path.isEmpty()
            && input_map_service_.uses_control_path(input_map_, path);
        if (mapped) controls_.insert(path, 1.0);
        last_mouse_position_ = event->position();
        has_mouse_position_ = true;
        refresh_gamepad_activation();
        if (mapped || !capture_active_) publish_input(event_received_ns);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void GameViewport::mouseReleaseEvent(QMouseEvent* event)
{
    if (play_mode_ && input_enabled_)
    {
        const auto path = canonical_mouse_path(event->button());
        if (!path.isEmpty() && input_map_service_.uses_control_path(input_map_, path))
        {
            controls_.insert(path, 0.0);
            publish_input(presentation_clock_.nsecsElapsed());
        }
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void GameViewport::mouseMoveEvent(QMouseEvent* event)
{
    if (play_mode_ && input_enabled_ && hasFocus())
    {
        const auto maps_x = input_map_service_.uses_control_path(
            input_map_, QStringLiteral("mouse/delta-x"));
        const auto maps_y = input_map_service_.uses_control_path(
            input_map_, QStringLiteral("mouse/delta-y"));
        if (has_mouse_position_ && (maps_x || maps_y))
        {
            const auto delta = event->position() - last_mouse_position_;
            if (maps_x) controls_.insert(QStringLiteral("mouse/delta-x"), delta.x());
            if (maps_y) controls_.insert(QStringLiteral("mouse/delta-y"), delta.y());
            if ((maps_x && !qFuzzyIsNull(delta.x()))
                || (maps_y && !qFuzzyIsNull(delta.y())))
            {
                publish_input(presentation_clock_.nsecsElapsed());
            }
        }
        last_mouse_position_ = event->position();
        has_mouse_position_ = true;
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void GameViewport::wheelEvent(QWheelEvent* event)
{
    if (play_mode_ && input_enabled_ && hasFocus())
    {
        const auto delta = event->angleDelta();
        const auto maps_x = input_map_service_.uses_control_path(
            input_map_, QStringLiteral("mouse/wheel-x"));
        const auto maps_y = input_map_service_.uses_control_path(
            input_map_, QStringLiteral("mouse/wheel-y"));
        if (maps_x) controls_.insert(QStringLiteral("mouse/wheel-x"), delta.x() / 120.0);
        if (maps_y) controls_.insert(QStringLiteral("mouse/wheel-y"), delta.y() / 120.0);
        if (maps_x || maps_y) publish_input(presentation_clock_.nsecsElapsed());
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

void GameViewport::focusInEvent(QFocusEvent* event)
{
    has_mouse_position_ = false;
    refresh_gamepad_activation();
    QWidget::focusInEvent(event);
}

void GameViewport::focusOutEvent(QFocusEvent* event)
{
    clear_input(false, false, presentation_clock_.nsecsElapsed());
    refresh_gamepad_activation();
    QWidget::focusOutEvent(event);
}

void GameViewport::clear_transient_controls()
{
    if (controls_.contains(QStringLiteral("mouse/delta-x"))
        || controls_.contains(QStringLiteral("mouse/delta-y"))
        || controls_.contains(QStringLiteral("mouse/wheel-x"))
        || controls_.contains(QStringLiteral("mouse/wheel-y")))
    {
        transient_input_timer_.start();
    }
}

void GameViewport::refresh_gamepad_activation()
{
    if (gamepad_input_source_ != nullptr)
    {
        gamepad_input_source_->set_active(play_mode_ && input_enabled_ && hasFocus());
    }
}

void GameViewport::set_status(QString status)
{
    status_ = std::move(status);
    update();
}

void GameViewport::set_aspect_ratio(double ratio)
{
    aspect_ratio_ = std::max(0.0, ratio);
    update();
}

QRect GameViewport::presentation_rect() const
{
    const auto available = rect().adjusted(8, 8, -8, -8);
    const auto& frame = play_mode_ ? play_frame_ : preview_frame_;
    double ratio = aspect_ratio_;
    if (ratio <= 0.0 && !frame.isNull() && frame.height() > 0)
    {
        ratio = static_cast<double>(frame.width()) / static_cast<double>(frame.height());
    }
    if (ratio <= 0.0)
    {
        return available;
    }
    auto width = available.width();
    auto height = qRound(static_cast<double>(width) / ratio);
    if (height > available.height())
    {
        height = available.height();
        width = qRound(static_cast<double>(height) * ratio);
    }
    return {available.center().x() - width / 2, available.center().y() - height / 2, width, height};
}

void GameViewport::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter{this};
    painter.fillRect(rect(), QColor{17, 19, 24});
    const auto target = presentation_rect();
    const auto& frame = play_mode_ ? play_frame_ : preview_frame_;
    if (frame.isNull())
    {
        painter.fillRect(target, QColor{29, 32, 39});
        painter.setPen(QColor{190, 195, 205});
        painter.drawText(target, Qt::AlignCenter,
            play_mode_ ? QStringLiteral("Waiting for Play frame…") : QStringLiteral("Waiting for primary-camera preview…"));
    }
    else
    {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawImage(target, frame);
    }
    painter.setPen(QColor{220, 224, 232});
    painter.fillRect(QRect{12, 12, std::min(width() - 24, 330), 25}, QColor{0, 0, 0, 145});
    painter.drawText(QRect{20, 12, std::max(0, width() - 40), 25}, Qt::AlignVCenter, status_);
    painter.end();
    const auto paint_completed_ns = presentation_clock_.nsecsElapsed();
    if (play_mode_ && !play_frame_.isNull() && play_frame_revision_ != 0
        && play_frame_revision_ != last_presented_frame_revision_)
    {
        if (last_presented_frame_revision_ != 0
            && play_frame_revision_ > last_presented_frame_revision_ + 1)
        {
            emit presentation_frames_skipped(
                play_frame_revision_ - last_presented_frame_revision_ - 1);
        }
        last_presented_frame_revision_ = play_frame_revision_;
        emit play_frame_presented(
            play_frame_revision_,
            play_frame_input_revision_,
            paint_completed_ns);
    }
    record_presented_input(paint_completed_ns);
}

void GameViewport::record_presented_input(qint64 paint_completed_ns)
{
    if (!play_mode_ || play_frame_.isNull() || pending_input_correlations_.isEmpty())
    {
        return;
    }
    expire_input_correlations(paint_completed_ns);
    if (play_frame_input_revision_ == 0 || pending_input_correlations_.isEmpty()) return;
    auto iterator = pending_input_correlations_.begin();
    while (iterator != pending_input_correlations_.end()
        && iterator.key() <= play_frame_input_revision_)
    {
        const auto revision = iterator.key();
        const auto correlation = iterator.value();
        iterator = pending_input_correlations_.erase(iterator);
        if (revision == play_frame_input_revision_)
        {
            const auto latency_ns = std::max<qint64>(
                0,
                paint_completed_ns - correlation.event_received_ns);
            emit input_presented(revision, latency_ns);
            if (correlation.has_active_action)
            {
                emit input_frame_painted(
                    revision,
                    play_frame_revision_,
                    latency_ns,
                    paint_completed_ns);
            }
        }
        else
        {
            emit input_correlation_dropped(revision, play_frame_input_revision_);
        }
    }
    schedule_input_correlation_timeout();
}

void GameViewport::expire_input_correlations(qint64 now)
{
    while (!pending_input_correlations_.isEmpty()
        && pending_input_correlations_.begin().value().expiration_origin_ns >= 0
        && now - pending_input_correlations_.begin().value().expiration_origin_ns
            >= input_correlation_timeout_nanoseconds)
    {
        const auto expired = pending_input_correlations_.begin().key();
        pending_input_correlations_.erase(pending_input_correlations_.begin());
        emit input_correlation_expired(expired);
    }
}

void GameViewport::schedule_input_correlation_timeout()
{
    if (pending_input_correlations_.isEmpty() || input_correlation_expiration_suspended_)
    {
        input_correlation_timer_.stop();
        return;
    }
    const auto elapsed = presentation_clock_.nsecsElapsed()
        - pending_input_correlations_.begin().value().expiration_origin_ns;
    const auto remaining = std::max<qint64>(
        0, input_correlation_timeout_nanoseconds - elapsed);
    const auto remaining_milliseconds = static_cast<int>(std::max<qint64>(
        1, (remaining + 999'999) / 1'000'000));
    input_correlation_timer_.start(remaining_milliseconds);
}

void GameViewport::resizeEvent(QResizeEvent* event)
{
    emit viewport_resized(event->size());
    QWidget::resizeEvent(event);
}
