#include "WorkerClient.h"

#include "LocalEndpoint.h"
#include "EditorRuntimePaths.h"

#include <QApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QtEndian>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <utility>

namespace
{
constexpr auto version_one_header_size = 64;
constexpr auto version_two_header_size = 96;
constexpr std::uint32_t frame_magic = 0x46504544U;

std::uint32_t read_u32(const unsigned char* bytes, std::size_t offset)
{
    return qFromLittleEndian<std::uint32_t>(bytes + offset);
}

std::uint64_t read_u64(const unsigned char* bytes, std::size_t offset)
{
    return qFromLittleEndian<std::uint64_t>(bytes + offset);
}

bool is_canonical_action_name(const QString& name)
{
    if (name.isEmpty() || name.size() > 128 || name.front() < QLatin1Char('a')
        || name.front() > QLatin1Char('z'))
    {
        return false;
    }
    return std::all_of(name.cbegin(), name.cend(), [](const QChar character) {
        return (character >= QLatin1Char('a') && character <= QLatin1Char('z'))
            || (character >= QLatin1Char('0') && character <= QLatin1Char('9'))
            || character == QLatin1Char('.') || character == QLatin1Char('_')
            || character == QLatin1Char('-');
    });
}
}

WorkerClient::WorkerClient(QString session_kind, QObject* parent)
    : QObject(parent), session_kind_(std::move(session_kind))
{
    view_id_ = session_kind_ == QStringLiteral("preview") ? QStringLiteral("scene")
        : session_kind_ == QStringLiteral("game-preview") ? QStringLiteral("game")
        : QStringLiteral("play");
    connect(&frame_timer_, &QTimer::timeout, this, &WorkerClient::poll_frame);
    connect(&connect_timer_, &QTimer::timeout, this, &WorkerClient::connect_control);
    frame_timer_.start(16);
    connect_timer_.setInterval(75);
}

WorkerClient::~WorkerClient()
{
    desired_running_ = false;
    dispose_process(true);
}

void WorkerClient::start_session(const QString& adapter, const QString& snapshot)
{
    ++lifecycle_generation_;
    desired_running_ = true;
    shutting_down_ = false;
    adapter_ = adapter;
    negotiated_adapter_.clear();
    runtime_backend_.clear();
    runtime_device_.clear();
    snapshot_path_ = snapshot;
    snapshot_revision_ = 1;
    camera_revision_ = 0;
    command_revision_ = 0;
    viewport_input_revision_ = 0;
    input_revision_ = 0;
    cached_input_actions_ = {};
    has_cached_input_actions_ = false;
    runtime_ready_ = false;
    desired_paused_ = false;
    awaiting_neutral_input_ack_ = false;
    runtime_input_recovery_requested_ = false;
    last_frame_revision_ = 0;
    last_frame_input_revision_ = 0;
    recovery_count_ = 0;
    minimum_recovery_generation_ = 0;
    if (session_kind_ != QStringLiteral("preview"))
    {
        preview_simulation_enabled_ = false;
    }
    dispose_process(true);
    launch();
}

void WorkerClient::reload_snapshot(const QString& snapshot)
{
    snapshot_path_ = snapshot;
    ++snapshot_revision_;
    ++command_revision_;
    if (socket_ == nullptr || socket_->state() != QLocalSocket::ConnectedState
        || capability_token_.isEmpty())
    {
        if (process_ == nullptr || process_->state() == QProcess::NotRunning)
        {
            start_session(adapter_, snapshot);
        }
        return;
    }
    send_request(QStringLiteral("reloadSnapshot"), {
        {QStringLiteral("snapshotPath"), snapshot_path_},
        {QStringLiteral("snapshotRevision"), static_cast<qint64>(snapshot_revision_)},
    });
}

void WorkerClient::resize_viewport(const QSize& size)
{
    const auto bounded = QSize{
        std::clamp(size.width(), 64, 1920),
        std::clamp(size.height(), 64, 1080),
    };
    if (bounded == viewport_size_)
    {
        return;
    }
    viewport_size_ = bounded;
    send_request(QStringLiteral("resizeViewport"), {
        {QStringLiteral("viewId"), view_id_},
        {QStringLiteral("purpose"), session_kind_},
        {QStringLiteral("width"), viewport_size_.width()},
        {QStringLiteral("height"), viewport_size_.height()},
        {QStringLiteral("cameraRevision"), static_cast<qint64>(camera_revision_)},
        {QStringLiteral("commandRevision"), static_cast<qint64>(command_revision_)},
    });
}

void WorkerClient::update_viewport(
    const QJsonObject& camera,
    const QStringList& selection,
    std::uint64_t camera_revision,
    std::uint64_t command_revision)
{
    camera_revision_ = std::max(camera_revision_, camera_revision);
    command_revision_ = std::max(command_revision_, command_revision);
    cached_camera_ = camera;
    cached_selection_ = selection;
    ++viewport_input_revision_;
    QJsonArray selected;
    for (const auto& id : selection)
    {
        selected.push_back(id);
    }
    send_request(QStringLiteral("viewportInput"), {
        {QStringLiteral("viewId"), view_id_},
        {QStringLiteral("purpose"), session_kind_},
        {QStringLiteral("width"), viewport_size_.width()},
        {QStringLiteral("height"), viewport_size_.height()},
        {QStringLiteral("cameraRevision"), static_cast<qint64>(camera_revision_)},
        {QStringLiteral("commandRevision"), static_cast<qint64>(command_revision_)},
        {QStringLiteral("inputRevision"), static_cast<qint64>(viewport_input_revision_)},
        {QStringLiteral("camera"), camera},
        {QStringLiteral("selection"), selected},
    });
}

void WorkerClient::send_input_actions(const QJsonObject& actions)
{
    send_correlated_input_actions(actions, input_revision_ + 1);
}

void WorkerClient::send_correlated_input_actions(
    const QJsonObject& actions,
    std::uint64_t input_revision)
{
    if (session_kind_ != QStringLiteral("play"))
    {
        return;
    }
    if (input_revision <= input_revision_)
    {
        emit status_message(QStringLiteral("Rejected stale Play input revision %1 (current %2)")
            .arg(input_revision)
            .arg(input_revision_));
        return;
    }
    cached_input_actions_ = actions;
    has_cached_input_actions_ = true;
    input_revision_ = input_revision;
    send_cached_input_actions();
}

void WorkerClient::send_cached_input_actions()
{
    if (!runtime_ready_ || !has_cached_input_actions_ || socket_ == nullptr
        || socket_->state() != QLocalSocket::ConnectedState || capability_token_.isEmpty())
    {
        return;
    }
    auto parameters = cached_input_actions_;
    parameters.insert(QStringLiteral("viewId"), view_id_);
    parameters.insert(QStringLiteral("inputRevision"), static_cast<qint64>(input_revision_));
    send_request(QStringLiteral("runtimeInput"), parameters);
}

void WorkerClient::pick(const QPoint& frame_position)
{
    send_request(QStringLiteral("pick"), {
        {QStringLiteral("viewId"), view_id_},
        {QStringLiteral("x"), frame_position.x()},
        {QStringLiteral("y"), frame_position.y()},
        {QStringLiteral("minimumFrameRevision"), static_cast<qint64>(last_frame_revision_)},
        {QStringLiteral("snapshotRevision"), static_cast<qint64>(snapshot_revision_)},
        {QStringLiteral("cameraRevision"), static_cast<qint64>(camera_revision_)},
        {QStringLiteral("commandRevision"), static_cast<qint64>(command_revision_)},
    });
}

void WorkerClient::pause()
{
    desired_paused_ = true;
    if (runtime_ready_)
    {
        send_request(QStringLiteral("pause"));
    }
}

void WorkerClient::resume()
{
    desired_running_ = true;
    desired_paused_ = false;
    if (runtime_ready_)
    {
        send_request(QStringLiteral("resume"));
    }
}

void WorkerClient::set_preview_simulation(bool enabled)
{
    if (session_kind_ != QStringLiteral("preview"))
    {
        emit status_message(QStringLiteral("Preview simulation is unavailable in a play worker"));
        return;
    }
    preview_simulation_enabled_ = enabled;
    send_request(QStringLiteral("simulatePreview"), {
        {QStringLiteral("enabled"), enabled},
    });
}

void WorkerClient::stop_and_discard()
{
    const auto generation = ++lifecycle_generation_;
    desired_running_ = false;
    desired_paused_ = false;
    shutting_down_ = true;
    if (session_kind_ == QStringLiteral("preview") && preview_simulation_enabled_)
    {
        preview_simulation_enabled_ = false;
        emit preview_simulation_changed(false);
    }
    if (process_ == nullptr)
    {
        emit runtime_stopped();
        return;
    }
    if (socket_ != nullptr && socket_->state() == QLocalSocket::ConnectedState)
    {
        send_request(QStringLiteral("stop"));
        send_request(QStringLiteral("shutdown"));
    }
    QTimer::singleShot(150, this, [this, generation] {
        if (generation != lifecycle_generation_)
        {
            return;
        }
        dispose_process(true);
        emit runtime_stopped();
    });
}

void WorkerClient::force_crash()
{
    last_sequence_ = 0;
    last_frame_revision_ = 0;
    last_frame_input_revision_ = 0;
    negotiated_protocol_version_ = 0;
    runtime_input_recovery_requested_ = false;
    minimum_recovery_generation_ = process_generation_ + 1;
    send_request(QStringLiteral("crash"));
}

void WorkerClient::launch()
{
    ++process_generation_;
    const auto launched_process_generation = process_generation_;
    const auto launched_lifecycle_generation = lifecycle_generation_;
    last_sequence_ = 0;
    last_frame_revision_ = 0;
    last_frame_input_revision_ = 0;
    negotiated_protocol_version_ = 0;
    negotiated_adapter_.clear();
    runtime_backend_.clear();
    runtime_device_.clear();
    runtime_identity_refresh_requested_ = false;
    capability_token_.clear();
    process_id_ = 0;
    incoming_.clear();
    pending_.clear();
    runtime_ready_ = false;
    const auto nonce = QString::number(QRandomGenerator::global()->generate64(), 16);
    const auto endpoint_name = QStringLiteral("dpe-s1-%1-%2-%3")
        .arg(QApplication::applicationPid())
        .arg(session_kind_, nonce);
    control_endpoint_ = dpe::editor::local_socket_endpoint(endpoint_name);
    if (control_endpoint_.isEmpty())
    {
        fail_session(QStringLiteral("Worker control endpoint exceeds the portable path limit"));
        return;
    }
    close_frame_mapping();
    frame_path_ = QDir::temp().filePath(endpoint_name + QStringLiteral(".frame"));
    QFile::remove(frame_path_);
    if (!control_endpoint_.isEmpty() && control_endpoint_.contains(QDir::separator()))
    {
        QFile::remove(control_endpoint_);
    }

    process_ = new QProcess(this);
    process_->setProcessChannelMode(QProcess::SeparateChannels);
    auto process_environment = QProcessEnvironment::systemEnvironment();
    const auto asan_runtime = process_environment.value(QStringLiteral("DPE_ASAN_RUNTIME"));
    if (!asan_runtime.isEmpty())
    {
#if defined(Q_OS_MACOS)
        process_environment.insert(QStringLiteral("DYLD_INSERT_LIBRARIES"), asan_runtime);
#elif defined(Q_OS_UNIX)
        const auto existing_preload = process_environment.value(QStringLiteral("LD_PRELOAD"));
        process_environment.insert(
            QStringLiteral("LD_PRELOAD"),
            existing_preload.isEmpty()
                ? asan_runtime
                : asan_runtime + QLatin1Char(':') + existing_preload);
#endif
        process_environment.insert(QStringLiteral("ASAN_OPTIONS"), QStringLiteral("detect_leaks=0"));
        process_->setProcessEnvironment(process_environment);
    }
    connect(process_, &QProcess::readyReadStandardError, this, [this] {
        const auto message = QString::fromUtf8(process_->readAllStandardError()).trimmed();
        if (!message.isEmpty())
        {
            emit status_message(QStringLiteral("Worker: %1").arg(message));
        }
    });
    connect(process_, &QProcess::finished, this,
        [this, launched_process_generation, launched_lifecycle_generation](
            int exit_code,
            QProcess::ExitStatus status) {
            runtime_ready_ = false;
            emit status_message(QStringLiteral("Worker exited (%1, %2)")
                .arg(exit_code)
                .arg(status == QProcess::CrashExit ? QStringLiteral("crash") : QStringLiteral("normal")));
            if (socket_ != nullptr)
            {
                socket_->abort();
            }
            if (session_kind_ == QStringLiteral("play"))
            {
                neutralize_cached_input_actions();
                awaiting_neutral_input_ack_ = has_cached_input_actions_;
                emit runtime_input_reset();
            }
            if (desired_running_ && !shutting_down_ && recovery_count_ < 3)
            {
                ++recovery_count_;
                emit status_message(QStringLiteral("Restarting isolated %1 worker (%2/3)")
                    .arg(session_kind_)
                    .arg(recovery_count_));
                QTimer::singleShot(200, this,
                    [this, launched_process_generation, launched_lifecycle_generation] {
                        if (!desired_running_ || shutting_down_
                            || process_generation_ != launched_process_generation
                            || lifecycle_generation_ != launched_lifecycle_generation)
                        {
                            return;
                        }
                        dispose_process(false);
                        launch();
                    });
                return;
            }
            if (desired_running_ && !shutting_down_)
            {
                desired_running_ = false;
                emit status_message(QStringLiteral("Worker recovery limit reached; the runtime session stopped"));
                emit runtime_stopped();
            }
        });
    QStringList arguments{
         worker_dll(),
         QStringLiteral("--frame-file"), frame_path_,
         QStringLiteral("--control"), control_endpoint_,
         QStringLiteral("--session"), session_kind_,
         QStringLiteral("--view-id"), view_id_,
         QStringLiteral("--native"), dragonpixel::editor::runtime_paths::file(
             "DPE_EDITOR_NATIVE_LIBRARY",
             QStringLiteral("runtime/native/dragonpixel.dll"),
             QString::fromUtf8(DPE_EDITOR_NATIVE_LIBRARY)),
         QStringLiteral("--width"), QStringLiteral("1920"),
         QStringLiteral("--height"), QStringLiteral("1080"),
         QStringLiteral("--frame-version"), QStringLiteral("2")};
    if (!component_module_manifest_.isEmpty())
    {
        arguments.push_back(QStringLiteral("--component-modules"));
        arguments.push_back(component_module_manifest_);
    }
    process_->start(dragonpixel::editor::runtime_paths::executable(
        "DPE_DOTNET_EXECUTABLE", QString::fromUtf8(DPE_DOTNET_EXECUTABLE)), arguments);
    if (!process_->waitForStarted(5000))
    {
        const auto message = QStringLiteral("Worker failed to start: %1").arg(process_->errorString());
        fail_session(message);
        return;
    }

    socket_ = new QLocalSocket(this);
    connect(socket_, &QLocalSocket::readyRead, this, &WorkerClient::consume_messages);
    connect(socket_, &QLocalSocket::connected, this, [this] {
        connect_timer_.stop();
        emit status_message(QStringLiteral("Versioned local control channel connected"));
        send_request(QStringLiteral("handshake"), {
            {QStringLiteral("protocolVersion"), 2},
            {QStringLiteral("viewId"), view_id_},
            {QStringLiteral("purpose"), session_kind_},
        });
    });
    connect_deadline_.restart();
    connect_timer_.start();
    connect_control();
}

void WorkerClient::connect_control()
{
    if (socket_ == nullptr || socket_->state() == QLocalSocket::ConnectedState
        || socket_->state() == QLocalSocket::ConnectingState)
    {
        return;
    }
    if (connect_deadline_.elapsed() > 5000)
    {
        connect_timer_.stop();
        emit status_message(QStringLiteral("Timed out connecting to worker control endpoint"));
        if (process_ != nullptr && process_->state() != QProcess::NotRunning)
        {
            process_->kill();
        }
        return;
    }
    socket_->abort();
    socket_->connectToServer(control_endpoint_, QIODevice::ReadWrite);
}

void WorkerClient::fail_session(const QString& message)
{
    desired_running_ = false;
    shutting_down_ = true;
    runtime_ready_ = false;
    emit status_message(message);
    if (session_kind_ == QStringLiteral("play"))
    {
        emit runtime_input_reset();
    }
    dispose_process(false);
    emit runtime_stopped();
}

void WorkerClient::recover_from_runtime_input_error(const QString& message)
{
    emit status_message(message);
    if (runtime_input_recovery_requested_ || shutting_down_ || !desired_running_)
    {
        return;
    }

    runtime_input_recovery_requested_ = true;
    runtime_ready_ = false;
    neutralize_cached_input_actions();
    awaiting_neutral_input_ack_ = has_cached_input_actions_;
    minimum_recovery_generation_ = process_generation_ + 1;
    emit runtime_input_reset();
    emit status_message(QStringLiteral(
        "Restarting the isolated Play worker to guarantee neutral input after rejection"));
    if (process_ != nullptr && process_->state() != QProcess::NotRunning)
    {
        process_->kill();
    }
}

void WorkerClient::neutralize_cached_input_actions()
{
    if (session_kind_ != QStringLiteral("play") || !has_cached_input_actions_)
    {
        return;
    }

    QJsonObject neutral_actions;
    const auto cached_actions = cached_input_actions_.value(QStringLiteral("actions")).toObject();
    for (auto iterator = cached_actions.begin(); iterator != cached_actions.end(); ++iterator)
    {
        const auto state = iterator.value().toObject();
        const auto kind = state.value(QStringLiteral("kind")).toString();
        const auto press_count = state.value(QStringLiteral("pressCount")).toInteger(-1);
        const auto release_count = state.value(QStringLiteral("releaseCount")).toInteger(-1);
        if (!is_canonical_action_name(iterator.key())
            || (kind != QStringLiteral("button") && kind != QStringLiteral("axis1d"))
            || press_count < 0 || release_count < 0)
        {
            continue;
        }
        const auto completed_cycles = std::max(press_count, release_count);
        neutral_actions.insert(iterator.key(), QJsonObject{
            {QStringLiteral("kind"), kind},
            {QStringLiteral("value"), 0.0},
            {QStringLiteral("pressCount"), completed_cycles},
            {QStringLiteral("releaseCount"), completed_cycles},
        });
    }
    if (neutral_actions.isEmpty())
    {
        const auto neutral_axis = QJsonObject{
            {QStringLiteral("kind"), QStringLiteral("axis1d")},
            {QStringLiteral("value"), 0.0},
            {QStringLiteral("pressCount"), 0},
            {QStringLiteral("releaseCount"), 0},
        };
        neutral_actions.insert(QStringLiteral("move.x"), neutral_axis);
        neutral_actions.insert(QStringLiteral("move.y"), neutral_axis);
        neutral_actions.insert(QStringLiteral("jump"), QJsonObject{
            {QStringLiteral("kind"), QStringLiteral("button")},
            {QStringLiteral("value"), 0.0},
            {QStringLiteral("pressCount"), 0},
            {QStringLiteral("releaseCount"), 0},
        });
    }
    cached_input_actions_ = QJsonObject{
        {QStringLiteral("focused"), false},
        {QStringLiteral("captured"), false},
        {QStringLiteral("actions"), neutral_actions},
    };
}

void WorkerClient::send_request(const QString& method, const QJsonObject& parameters)
{
    if (socket_ == nullptr || socket_->state() != QLocalSocket::ConnectedState)
    {
        return;
    }
    const auto id = ++next_request_id_;
    QJsonObject request{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id},
        {QStringLiteral("method"), method},
    };
    if (!parameters.isEmpty())
    {
        request[QStringLiteral("params")] = parameters;
    }
    if (method != QStringLiteral("handshake") && !capability_token_.isEmpty())
    {
        request[QStringLiteral("capabilityToken")] = capability_token_;
    }
    const auto payload = QJsonDocument(request).toJson(QJsonDocument::Compact);
    QByteArray framed(static_cast<qsizetype>(sizeof(std::uint32_t)) + payload.size(), Qt::Uninitialized);
    qToLittleEndian<std::uint32_t>(static_cast<std::uint32_t>(payload.size()), framed.data());
    std::memcpy(framed.data() + sizeof(std::uint32_t), payload.constData(), static_cast<std::size_t>(payload.size()));
    pending_[id] = method;
    socket_->write(framed);
    socket_->flush();
}

void WorkerClient::consume_messages()
{
    incoming_.append(socket_->readAll());
    while (incoming_.size() >= static_cast<qsizetype>(sizeof(std::uint32_t)))
    {
        const auto length = qFromLittleEndian<std::uint32_t>(incoming_.constData());
        if (length == 0 || length > 1024U * 1024U)
        {
            emit status_message(QStringLiteral("Worker returned an invalid frame length"));
            socket_->abort();
            return;
        }
        const auto framed_size = static_cast<qsizetype>(sizeof(std::uint32_t) + length);
        if (incoming_.size() < framed_size)
        {
            return;
        }
        const auto payload = incoming_.mid(sizeof(std::uint32_t), length);
        incoming_.remove(0, framed_size);
        const auto document = QJsonDocument::fromJson(payload);
        if (document.isObject())
        {
            handle_response(document.object());
        }
    }
}

void WorkerClient::handle_response(const QJsonObject& response)
{
    const auto id = response.value(QStringLiteral("id")).toInt();
    const auto method = pending_.take(id);
    if (!desired_running_ || shutting_down_)
    {
        return;
    }
    if (response.contains(QStringLiteral("error")))
    {
        if (method == QStringLiteral("simulatePreview"))
        {
            preview_simulation_enabled_ = false;
            emit preview_simulation_changed(false);
        }
        const auto message = QStringLiteral("Worker error for %1: %2")
            .arg(method, QString::fromUtf8(QJsonDocument(
                response.value(QStringLiteral("error")).toObject()).toJson(QJsonDocument::Compact)));
        if (method == QStringLiteral("runtimeInput"))
        {
            recover_from_runtime_input_error(message);
            return;
        }
        if (method == QStringLiteral("handshake") || method == QStringLiteral("initialize")
            || method == QStringLiteral("loadSnapshot") || method == QStringLiteral("play"))
        {
            fail_session(message);
            return;
        }
        emit status_message(message);
        return;
    }
    const auto result = response.value(QStringLiteral("result")).toObject();
    if (method == QStringLiteral("handshake"))
    {
        const auto negotiated_adapter = result.value(QStringLiteral("adapter")).toString();
        if (result.value(QStringLiteral("protocolVersion")).toInt() != 2
            || result.value(QStringLiteral("frameLayoutVersion")).toInt() != 2
            || result.value(QStringLiteral("frameHeaderSize")).toInt() != version_two_header_size
            || result.value(QStringLiteral("pixelFormat")).toString() != QStringLiteral("BGRA8")
            || !result.value(QStringLiteral("revisionCorrelatedFrames")).toBool())
        {
            fail_session(QStringLiteral(
                "Worker did not negotiate the required protocol-v2 revision-correlated BGRA8 frame contract"));
            return;
        }
        if (session_kind_ == QStringLiteral("play")
            && !result.value(QStringLiteral("runtimeInput")).toBool())
        {
            fail_session(QStringLiteral("Play worker did not negotiate the runtimeInput capability"));
            return;
        }
        if (result.value(QStringLiteral("sessionKind")).toString() != session_kind_)
        {
            fail_session(QStringLiteral("Worker session role negotiation failed"));
            return;
        }
        if (result.value(QStringLiteral("viewId")).toString() != view_id_)
        {
            fail_session(QStringLiteral("Worker named-view negotiation failed"));
            return;
        }
        if (negotiated_adapter.isEmpty()
            || negotiated_adapter.compare(adapter_, Qt::CaseInsensitive) != 0)
        {
            fail_session(QStringLiteral("Worker adapter negotiation failed"));
            return;
        }
        capability_token_ = result.value(QStringLiteral("capabilityToken")).toString();
        if (capability_token_.isEmpty())
        {
            fail_session(QStringLiteral("Worker returned an empty capability token"));
            return;
        }
        process_id_ = result.value(QStringLiteral("processId")).toInteger();
        negotiated_protocol_version_ = 2;
        negotiated_adapter_ = negotiated_adapter;
        emit status_message(QStringLiteral("%1 %2 %3 worker connected as PID %4 (%5)")
            .arg(result.value(QStringLiteral("adapter")).toString(),
                 result.value(QStringLiteral("adapterVersion")).toString(),
                 session_kind_)
            .arg(process_id_)
            .arg(result.value(QStringLiteral("frameTransport")).toString()));
        send_request(QStringLiteral("initialize"));
    }
    else if (method == QStringLiteral("initialize"))
    {
        send_request(QStringLiteral("loadSnapshot"), {
            {QStringLiteral("snapshotPath"), snapshot_path_},
            {QStringLiteral("snapshotRevision"), static_cast<qint64>(snapshot_revision_)},
        });
    }
    else if (method == QStringLiteral("loadSnapshot"))
    {
        send_request(QStringLiteral("play"));
    }
    else if (method == QStringLiteral("play"))
    {
        runtime_ready_ = true;
        send_request(QStringLiteral("resizeViewport"), {
            {QStringLiteral("viewId"), view_id_},
            {QStringLiteral("purpose"), session_kind_},
            {QStringLiteral("width"), viewport_size_.width()},
            {QStringLiteral("height"), viewport_size_.height()},
            {QStringLiteral("cameraRevision"), static_cast<qint64>(camera_revision_)},
            {QStringLiteral("commandRevision"), static_cast<qint64>(command_revision_)},
        });
        if (!cached_camera_.isEmpty())
        {
            update_viewport(cached_camera_, cached_selection_, camera_revision_, command_revision_);
        }
        if (session_kind_ == QStringLiteral("play"))
        {
            send_cached_input_actions();
            if (desired_paused_)
            {
                send_request(QStringLiteral("pause"));
            }
            else if (!awaiting_neutral_input_ack_)
            {
                emit runtime_input_ready();
            }
        }
        if (session_kind_ == QStringLiteral("preview") && preview_simulation_enabled_)
        {
            set_preview_simulation(true);
        }
        emit status_message(session_kind_ == QStringLiteral("preview")
            ? QStringLiteral("Preview worker running from editor-owned mirror")
            : QStringLiteral("Play world running from immutable snapshot"));
        send_request(QStringLiteral("diagnostics"));
    }
    else if (method == QStringLiteral("reloadSnapshot"))
    {
        emit status_message(QStringLiteral("Preview snapshot reloaded in place at revision %1")
            .arg(result.value(QStringLiteral("snapshotRevision")).toInteger()));
    }
    else if (method == QStringLiteral("simulatePreview"))
    {
        preview_simulation_enabled_ = result.value(QStringLiteral("enabled")).toBool();
        emit preview_simulation_changed(preview_simulation_enabled_);
        emit status_message(preview_simulation_enabled_
            ? QStringLiteral("Simulate Preview running in an isolated physics world")
            : QStringLiteral("Simulate Preview stopped; authoring transforms restored"));
    }
    else if (method == QStringLiteral("pause"))
    {
        if (desired_paused_)
        {
            emit runtime_pause_changed(true);
        }
    }
    else if (method == QStringLiteral("resume"))
    {
        if (!desired_paused_)
        {
            emit runtime_pause_changed(false);
            if (!awaiting_neutral_input_ack_)
            {
                emit runtime_input_ready();
            }
        }
    }
    else if (method == QStringLiteral("runtimeInput"))
    {
        const auto acknowledged_revision = static_cast<std::uint64_t>(
            result.value(QStringLiteral("inputRevision")).toInteger());
        if (awaiting_neutral_input_ack_ && acknowledged_revision == input_revision_)
        {
            if (!result.value(QStringLiteral("neutral")).toBool())
            {
                recover_from_runtime_input_error(QStringLiteral(
                    "Worker acknowledged a non-neutral input state during recovery"));
                return;
            }
            awaiting_neutral_input_ack_ = false;
            if (!desired_paused_)
            {
                emit runtime_input_ready();
            }
        }
    }
    else if (method == QStringLiteral("pick"))
    {
        const auto frame_revision = static_cast<std::uint64_t>(
            result.value(QStringLiteral("frameRevision")).toInteger());
        const auto snapshot_revision = static_cast<std::uint64_t>(
            result.value(QStringLiteral("snapshotRevision")).toInteger());
        const auto camera_revision = static_cast<std::uint64_t>(
            result.value(QStringLiteral("cameraRevision")).toInteger());
        const auto command_revision = static_cast<std::uint64_t>(
            result.value(QStringLiteral("commandRevision")).toInteger());
        if (frame_revision < last_frame_revision_ || snapshot_revision < snapshot_revision_
            || camera_revision < camera_revision_ || command_revision < command_revision_)
        {
            emit status_message(QStringLiteral("Discarded a stale viewport pick result"));
            return;
        }
        emit pick_ready(
            result.value(QStringLiteral("entityId")).toString(),
            frame_revision);
    }
    else if (method == QStringLiteral("diagnostics"))
    {
        const auto backend = result.value(QStringLiteral("backend")).toString();
        const auto device = result.value(QStringLiteral("device")).toString();
        if (!backend.isEmpty())
        {
            runtime_backend_ = backend;
        }
        if (!device.isEmpty())
        {
            runtime_device_ = device;
        }
        const auto components = result.value(QStringLiteral("projectComponents")).toObject();
        const auto factories = components.value(QStringLiteral("factories")).toInt();
        const auto instances = components.value(QStringLiteral("instances")).toInt();
        if (factories > 0 || instances > 0)
        {
            emit status_message(QStringLiteral("Worker project components: %1 factory/factories, %2 active instance(s)")
                .arg(factories).arg(instances));
        }
        for (const auto& value : components.value(QStringLiteral("diagnostics")).toArray())
        {
            emit status_message(QStringLiteral("Project component: %1").arg(value.toString()));
        }
    }
}

void WorkerClient::poll_frame()
{
    if (frame_path_.isEmpty())
    {
        return;
    }
    if (!frame_file_.isOpen())
    {
        frame_file_.setFileName(frame_path_);
        if (!frame_file_.open(QIODevice::ReadOnly))
        {
            return;
        }
    }
    const auto file_size = frame_file_.size();
    if (file_size < version_one_header_size)
    {
        return;
    }
    if (frame_mapping_ == nullptr || frame_mapping_size_ != file_size)
    {
        close_frame_mapping();
        frame_file_.setFileName(frame_path_);
        if (!frame_file_.open(QIODevice::ReadOnly))
        {
            return;
        }
        frame_mapping_size_ = frame_file_.size();
        if (frame_mapping_size_ < version_one_header_size)
        {
            close_frame_mapping();
            return;
        }
        frame_mapping_ = frame_file_.map(0, frame_mapping_size_);
        if (frame_mapping_ == nullptr)
        {
            close_frame_mapping();
            return;
        }
    }
    const auto* mapped = frame_mapping_;
    const auto width = read_u32(mapped, 8);
    const auto height = read_u32(mapped, 12);
    const auto stride = read_u32(mapped, 16);
    const auto sequence_before = read_u64(mapped, 24);
    const auto version = read_u32(mapped, 4);
    const auto header_size = version == 2 ? version_two_header_size : version_one_header_size;
    const auto required = static_cast<std::uint64_t>(header_size) + (static_cast<std::uint64_t>(stride) * height);
    if (read_u32(mapped, 0) != frame_magic || (version != 1 && version != 2) || read_u32(mapped, 20) != 1
        || width == 0 || height == 0 || (sequence_before & 1U) != 0
        || required > static_cast<std::uint64_t>(frame_mapping_size_) || sequence_before == last_sequence_)
    {
        return;
    }
    const QImage source(
        mapped + header_size,
        static_cast<int>(width),
        static_cast<int>(height),
        static_cast<qsizetype>(stride),
        QImage::Format_ARGB32);
    const auto copy = source.copy();
    const auto input_revision = version == 2 ? read_u64(mapped, 48) : std::uint64_t{};
    const auto frame_revision_v2 = version == 2 ? read_u64(mapped, 56) : std::uint64_t{};
    const auto snapshot_revision = version == 2 ? read_u64(mapped, 64) : std::uint64_t{};
    const auto camera_revision = version == 2 ? read_u64(mapped, 72) : std::uint64_t{};
    const auto command_revision = version == 2 ? read_u64(mapped, 80) : std::uint64_t{};
    const auto sequence_after = read_u64(mapped, 24);
    const auto frame_revision = version == 2 ? frame_revision_v2 : sequence_after / 2;
    const auto current_revision = version != 2
        || (snapshot_revision >= snapshot_revision_
            && camera_revision >= camera_revision_ && command_revision >= command_revision_);
    if (sequence_before == sequence_after && (sequence_after & 1U) == 0
        && !copy.isNull() && current_revision
        && accepts_frame_metadata(version, frame_revision, input_revision))
    {
        const auto first_valid_frame = last_sequence_ == 0;
        last_sequence_ = sequence_after;
        last_frame_revision_ = frame_revision;
        last_frame_input_revision_ = input_revision;
        frame_process_generation_ = process_generation_;
        if (first_valid_frame && !runtime_identity_refresh_requested_
            && (runtime_backend_.isEmpty() || runtime_device_.isEmpty()
                || runtime_device_.compare(
                    QStringLiteral("not initialized"), Qt::CaseInsensitive) == 0))
        {
            runtime_identity_refresh_requested_ = true;
            send_request(QStringLiteral("diagnostics"));
        }
        emit frame_ready(copy);
        emit frame_ready_correlated(copy, input_revision, frame_revision);
    }
}

bool WorkerClient::accepts_frame_metadata(
    std::uint32_t version,
    std::uint64_t frame_revision,
    std::uint64_t input_revision) const noexcept
{
    if (negotiated_protocol_version_ >= 2 && version != 2)
    {
        return false;
    }
    return version != 2
        || (frame_revision > last_frame_revision_
            && input_revision >= last_frame_input_revision_
            && input_revision <= input_revision_);
}

void WorkerClient::close_frame_mapping()
{
    if (frame_mapping_ != nullptr)
    {
        frame_file_.unmap(frame_mapping_);
        frame_mapping_ = nullptr;
    }
    frame_mapping_size_ = 0;
    if (frame_file_.isOpen())
    {
        frame_file_.close();
    }
    frame_file_.setFileName({});
}

void WorkerClient::dispose_process(bool graceful)
{
    connect_timer_.stop();
    runtime_ready_ = false;
    if (socket_ != nullptr)
    {
        socket_->abort();
        socket_->deleteLater();
        socket_ = nullptr;
    }
    if (process_ != nullptr)
    {
        process_->disconnect(this);
        if (process_->state() != QProcess::NotRunning)
        {
            if (graceful)
            {
                process_->terminate();
                if (!process_->waitForFinished(1000))
                {
                    process_->kill();
                    process_->waitForFinished(2000);
                }
            }
            else
            {
                process_->kill();
                process_->waitForFinished(2000);
            }
        }
        process_->deleteLater();
        process_ = nullptr;
    }
    if (!frame_path_.isEmpty())
    {
        close_frame_mapping();
        QFile::remove(frame_path_);
    }
    if (!control_endpoint_.isEmpty() && control_endpoint_.contains(QDir::separator()))
    {
        QFile::remove(control_endpoint_);
    }
}

QString WorkerClient::worker_dll() const
{
    if (adapter_ == QStringLiteral("kni"))
    {
        return dragonpixel::editor::runtime_paths::file(
            "DPE_EDITOR_KNI_DLL",
            QStringLiteral("runtime/workers/kni/DragonPixel.Adapter.Kni.Worker.dll"),
            QString::fromUtf8(DPE_EDITOR_KNI_DLL));
    }
    return dragonpixel::editor::runtime_paths::file(
        "DPE_EDITOR_MONOGAME_DLL",
        QStringLiteral("runtime/workers/monogame/DragonPixel.Adapter.MonoGame.Worker.dll"),
        QString::fromUtf8(DPE_EDITOR_MONOGAME_DLL));
}
