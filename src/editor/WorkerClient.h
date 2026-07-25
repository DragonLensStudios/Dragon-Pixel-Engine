#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QProcess>
#include <QPoint>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <cstdint>

class WorkerClient final : public QObject
{
    Q_OBJECT

public:
    explicit WorkerClient(QString session_kind, QObject* parent = nullptr);
    ~WorkerClient() override;

    void start_session(const QString& adapter, const QString& snapshot);
    void reload_snapshot(const QString& snapshot);
    void resize_viewport(const QSize& size);
    void update_viewport(
        const QJsonObject& camera,
        const QStringList& selection,
        std::uint64_t camera_revision,
        std::uint64_t command_revision);
    void pick(const QPoint& frame_position);
    void pause();
    void resume();
    void set_preview_simulation(bool enabled);
    void stop_and_discard();
    void force_crash();

    [[nodiscard]] bool has_frame() const noexcept { return last_sequence_ > 0; }
    [[nodiscard]] const QString& adapter_name() const noexcept { return adapter_; }
    [[nodiscard]] qint64 process_id() const noexcept { return process_id_; }
    [[nodiscard]] int recovery_count() const noexcept { return recovery_count_; }
    [[nodiscard]] bool recovered_after_crash() const noexcept
    {
        return minimum_recovery_generation_ > 0 && frame_process_generation_ >= minimum_recovery_generation_;
    }

signals:
    void frame_ready(const QImage& image);
    void pick_ready(const QString& entity_id, std::uint64_t frame_revision);
    void status_message(const QString& message);
    void runtime_stopped();
    void preview_simulation_changed(bool enabled);

private:
    void launch();
    void connect_control();
    void send_request(const QString& method, const QJsonObject& parameters = {});
    void consume_messages();
    void handle_response(const QJsonObject& response);
    void poll_frame();
    void dispose_process(bool graceful);
    [[nodiscard]] QString worker_dll() const;

    QProcess* process_{};
    QLocalSocket* socket_{};
    QTimer frame_timer_;
    QTimer connect_timer_;
    QElapsedTimer connect_deadline_;
    QByteArray incoming_;
    QHash<int, QString> pending_;
    QString adapter_;
    QString snapshot_path_;
    QString frame_path_;
    QString control_endpoint_;
    QString capability_token_;
    QString session_kind_;
    std::uint64_t last_sequence_{};
    std::uint64_t snapshot_revision_{};
    std::uint64_t camera_revision_{};
    std::uint64_t command_revision_{};
    std::uint64_t input_revision_{};
    std::uint64_t last_frame_revision_{};
    std::uint64_t lifecycle_generation_{};
    std::uint64_t process_generation_{};
    std::uint64_t frame_process_generation_{};
    std::uint64_t minimum_recovery_generation_{};
    int next_request_id_{};
    int recovery_count_{};
    qint64 process_id_{};
    QSize viewport_size_{960, 540};
    QJsonObject cached_camera_;
    QStringList cached_selection_;
    bool desired_running_{};
    bool shutting_down_{};
    bool preview_simulation_enabled_{};
};
