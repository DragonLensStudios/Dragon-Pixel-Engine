#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QJsonArray>
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
#include <utility>

class EditorInteractionTests;

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
    void send_input_actions(const QJsonObject& actions);
    void send_correlated_input_actions(const QJsonObject& actions, std::uint64_t input_revision);
    void set_component_module_manifest(QString path) { component_module_manifest_ = std::move(path); }
    void propose_tile_brush(quint64 request_token, QJsonObject parameters);
    void pick(const QPoint& frame_position);
    void pause();
    void resume();
    void set_preview_simulation(bool enabled);
    void stop_and_discard();
    void force_crash();

    [[nodiscard]] bool has_frame() const noexcept { return last_sequence_ > 0; }
    [[nodiscard]] const QString& adapter_name() const noexcept { return adapter_; }
    [[nodiscard]] const QString& negotiated_adapter() const noexcept { return negotiated_adapter_; }
    [[nodiscard]] const QString& runtime_backend() const noexcept { return runtime_backend_; }
    [[nodiscard]] const QString& runtime_device() const noexcept { return runtime_device_; }
    [[nodiscard]] qint64 process_id() const noexcept { return process_id_; }
    [[nodiscard]] int recovery_count() const noexcept { return recovery_count_; }
    [[nodiscard]] bool recovered_after_crash() const noexcept
    {
        return minimum_recovery_generation_ > 0 && frame_process_generation_ >= minimum_recovery_generation_;
    }

signals:
    void frame_ready(const QImage& image);
    void frame_ready_correlated(const QImage& image, quint64 input_revision, quint64 frame_revision);
    void pick_ready(const QString& entity_id, std::uint64_t frame_revision);
    void status_message(const QString& message);
    void runtime_stopped();
    void runtime_input_reset();
    void runtime_input_ready();
    void runtime_pause_changed(bool paused);
    void preview_simulation_changed(bool enabled);
    void tile_brush_proposal_ready(quint64 request_token, const QJsonArray& commands);
    void tile_brush_proposal_failed(
        quint64 request_token,
        const QString& error_code,
        const QString& error_message);

private:
    friend class EditorInteractionTests;

    void launch();
    void connect_control();
    void fail_session(const QString& message);
    void recover_from_runtime_input_error(const QString& message);
    void neutralize_cached_input_actions();
    void fail_pending_tile_brush_proposals(
        const QString& error_code,
        const QString& error_message);
    void send_cached_input_actions();
    void send_request(const QString& method, const QJsonObject& parameters = {});
    void consume_messages();
    void handle_response(const QJsonObject& response);
    void poll_frame();
    void close_frame_mapping();
    void dispose_process(bool graceful);
    [[nodiscard]] QString worker_dll() const;
    [[nodiscard]] bool accepts_frame_metadata(
        std::uint32_t version,
        std::uint64_t frame_revision,
        std::uint64_t input_revision) const noexcept;

    QProcess* process_{};
    QLocalSocket* socket_{};
    QFile frame_file_;
    uchar* frame_mapping_{};
    qint64 frame_mapping_size_{};
    QTimer frame_timer_;
    QTimer connect_timer_;
    QElapsedTimer connect_deadline_;
    QByteArray incoming_;
    QHash<int, QString> pending_;
    QHash<int, quint64> pending_tile_brush_tokens_;
    QString adapter_;
    QString negotiated_adapter_;
    QString runtime_backend_;
    QString runtime_device_;
    QString snapshot_path_;
    QString frame_path_;
    QString control_endpoint_;
    QString capability_token_;
    QString session_kind_;
    QString view_id_;
    QString component_module_manifest_;
    std::uint64_t last_sequence_{};
    std::uint64_t snapshot_revision_{};
    std::uint64_t camera_revision_{};
    std::uint64_t command_revision_{};
    std::uint64_t viewport_input_revision_{};
    std::uint64_t input_revision_{};
    std::uint64_t last_frame_revision_{};
    std::uint64_t last_frame_input_revision_{};
    std::uint64_t lifecycle_generation_{};
    std::uint64_t process_generation_{};
    std::uint64_t frame_process_generation_{};
    std::uint64_t minimum_recovery_generation_{};
    int next_request_id_{};
    int negotiated_protocol_version_{};
    int recovery_count_{};
    qint64 process_id_{};
    QSize viewport_size_{960, 540};
    QJsonObject cached_camera_;
    QJsonObject cached_input_actions_;
    QStringList cached_selection_;
    bool desired_running_{};
    bool shutting_down_{};
    bool preview_simulation_enabled_{};
    bool has_cached_input_actions_{};
    bool runtime_ready_{};
    bool desired_paused_{};
    bool awaiting_neutral_input_ack_{};
    bool runtime_input_recovery_requested_{};
    bool runtime_identity_refresh_requested_{};
    bool tile_brush_proposals_available_{};
};
