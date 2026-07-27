#pragma once

#include "InputMapService.h"

#include <QElapsedTimer>
#include <QImage>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QTimer>
#include <QWidget>

class QPaintEvent;
class QResizeEvent;
class QFocusEvent;
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;
class GamepadInputSource;

class GameViewport final : public QWidget
{
    Q_OBJECT

public:
    explicit GameViewport(QWidget* parent = nullptr);

    void set_preview_frame(const QImage& image);
    void set_play_frame(const QImage& image);
    void set_play_frame(const QImage& image, quint64 input_revision);
    void set_play_frame(const QImage& image, quint64 input_revision, quint64 frame_revision);
    void retire_play_frame();
    void clear_preview_frame();
    void set_play_mode(bool enabled);
    void set_input_enabled(bool enabled);
    void set_runtime_input_ready(bool ready);
    void set_status(QString status);
    void set_aspect_ratio(double ratio);
    void set_input_map(const InputMapDocument& input_map);
    void release_input();

    [[nodiscard]] bool is_play_mode() const noexcept { return play_mode_; }
    [[nodiscard]] bool is_input_enabled() const noexcept { return input_enabled_; }

signals:
    void viewport_resized(const QSize& size);
    void input_actions_changed(const QJsonObject& actions);
    void correlated_input_actions_changed(const QJsonObject& actions, quint64 input_revision);
    void input_capture_changed(bool captured);
    void input_presented(quint64 input_revision, qint64 latency_nanoseconds);
    void input_frame_painted(
        quint64 input_revision,
        quint64 frame_revision,
        qint64 latency_ns,
        qint64 paint_completed_ns);
    void input_correlation_dropped(quint64 input_revision, quint64 presented_input_revision);
    void input_correlation_expired(quint64 input_revision);
    void play_frame_presented(
        quint64 frame_revision,
        quint64 input_revision,
        qint64 presentation_nanoseconds);
    void presentation_frames_skipped(quint64 count);
    void play_frame_retired();

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    struct PendingInputCorrelation
    {
        qint64 event_received_ns{};
        qint64 expiration_origin_ns{};
        bool has_active_action{};
    };

    [[nodiscard]] QRect presentation_rect() const;
    void clear_input(bool force_neutral, bool focused, qint64 event_received_ns = -1);
    void emit_input_actions(const QJsonObject& input_state, qint64 event_received_ns);
    [[nodiscard]] QJsonObject make_input_state(
        const QHash<QString, EvaluatedInputAction>& values,
        bool focused,
        bool captured);
    void publish_input(qint64 event_received_ns);
    void clear_transient_controls();
    void refresh_gamepad_activation();
    void record_presented_input(qint64 paint_completed_ns);
    void expire_input_correlations(qint64 now);
    void schedule_input_correlation_timeout();

    QImage preview_frame_;
    QImage play_frame_;
    QElapsedTimer presentation_clock_;
    QTimer input_correlation_timer_;
    QTimer transient_input_timer_;
    QMap<quint64, PendingInputCorrelation> pending_input_correlations_;
    QString status_{QStringLiteral("Live primary-camera preview")};
    quint64 next_input_revision_{};
    quint64 play_frame_input_revision_{};
    quint64 play_frame_revision_{};
    quint64 last_presented_frame_revision_{};
    bool play_mode_{};
    bool input_enabled_{};
    bool input_correlation_expiration_suspended_{};
    bool capture_active_{};
    double aspect_ratio_{};
    InputMapService input_map_service_;
    InputMapDocument input_map_;
    GamepadInputSource* gamepad_input_source_{};
    QHash<QString, double> controls_;
    QPointF last_mouse_position_;
    bool has_mouse_position_{};
    QMap<QString, double> last_action_values_;
    QMap<QString, quint64> action_press_counts_;
    QMap<QString, quint64> action_release_counts_;
};
