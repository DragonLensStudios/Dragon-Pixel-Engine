#include "WorkerClient.h"

#include "LocalEndpoint.h"

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
}

WorkerClient::WorkerClient(QString session_kind, QObject* parent)
    : QObject(parent), session_kind_(std::move(session_kind))
{
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
    snapshot_path_ = snapshot;
    snapshot_revision_ = 1;
    camera_revision_ = 0;
    command_revision_ = 0;
    input_revision_ = 0;
    last_frame_revision_ = 0;
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
    ++input_revision_;
    QJsonArray selected;
    for (const auto& id : selection)
    {
        selected.push_back(id);
    }
    send_request(QStringLiteral("viewportInput"), {
        {QStringLiteral("width"), viewport_size_.width()},
        {QStringLiteral("height"), viewport_size_.height()},
        {QStringLiteral("cameraRevision"), static_cast<qint64>(camera_revision_)},
        {QStringLiteral("commandRevision"), static_cast<qint64>(command_revision_)},
        {QStringLiteral("inputRevision"), static_cast<qint64>(input_revision_)},
        {QStringLiteral("camera"), camera},
        {QStringLiteral("selection"), selected},
    });
}

void WorkerClient::pick(const QPoint& frame_position)
{
    send_request(QStringLiteral("pick"), {
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
    send_request(QStringLiteral("pause"));
}

void WorkerClient::resume()
{
    desired_running_ = true;
    send_request(QStringLiteral("resume"));
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
    minimum_recovery_generation_ = process_generation_ + 1;
    send_request(QStringLiteral("crash"));
}

void WorkerClient::launch()
{
    ++process_generation_;
    const auto launched_process_generation = process_generation_;
    const auto launched_lifecycle_generation = lifecycle_generation_;
    last_sequence_ = 0;
    capability_token_.clear();
    process_id_ = 0;
    incoming_.clear();
    pending_.clear();
    const auto nonce = QString::number(QRandomGenerator::global()->generate64(), 16);
    const auto endpoint_name = QStringLiteral("dpe-s1-%1-%2-%3")
        .arg(QApplication::applicationPid())
        .arg(session_kind_, nonce);
    control_endpoint_ = dpe::editor::local_socket_endpoint(endpoint_name);
    if (control_endpoint_.isEmpty())
    {
        emit status_message(QStringLiteral("Worker control endpoint exceeds the portable path limit"));
        emit runtime_stopped();
        return;
    }
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
            emit status_message(QStringLiteral("Worker exited (%1, %2)")
                .arg(exit_code)
                .arg(status == QProcess::CrashExit ? QStringLiteral("crash") : QStringLiteral("normal")));
            if (socket_ != nullptr)
            {
                socket_->abort();
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
    process_->start(
        QString::fromUtf8(DPE_DOTNET_EXECUTABLE),
        {worker_dll(),
         QStringLiteral("--frame-file"), frame_path_,
         QStringLiteral("--control"), control_endpoint_,
         QStringLiteral("--session"), session_kind_,
         QStringLiteral("--native"), QString::fromUtf8(DPE_EDITOR_NATIVE_LIBRARY),
         QStringLiteral("--width"), QStringLiteral("1920"),
         QStringLiteral("--height"), QStringLiteral("1080"),
         QStringLiteral("--frame-version"), QStringLiteral("2")});
    if (!process_->waitForStarted(5000))
    {
        emit status_message(QStringLiteral("Worker failed to start: %1").arg(process_->errorString()));
        desired_running_ = false;
        dispose_process(false);
        emit runtime_stopped();
        return;
    }

    socket_ = new QLocalSocket(this);
    connect(socket_, &QLocalSocket::readyRead, this, &WorkerClient::consume_messages);
    connect(socket_, &QLocalSocket::connected, this, [this] {
        connect_timer_.stop();
        emit status_message(QStringLiteral("Versioned local control channel connected"));
        send_request(QStringLiteral("handshake"), {{QStringLiteral("protocolVersion"), 2}});
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
    if (response.contains(QStringLiteral("error")))
    {
        if (method == QStringLiteral("simulatePreview"))
        {
            preview_simulation_enabled_ = false;
            emit preview_simulation_changed(false);
        }
        emit status_message(QStringLiteral("Worker error for %1: %2")
            .arg(method, QString::fromUtf8(QJsonDocument(response.value(QStringLiteral("error")).toObject()).toJson(QJsonDocument::Compact))));
        return;
    }
    const auto result = response.value(QStringLiteral("result")).toObject();
    if (method == QStringLiteral("handshake"))
    {
        if (result.value(QStringLiteral("sessionKind")).toString() != session_kind_)
        {
            desired_running_ = false;
            emit status_message(QStringLiteral("Worker session role negotiation failed"));
            dispose_process(false);
            return;
        }
        capability_token_ = result.value(QStringLiteral("capabilityToken")).toString();
        process_id_ = result.value(QStringLiteral("processId")).toInteger();
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
        send_request(QStringLiteral("resizeViewport"), {
            {QStringLiteral("width"), viewport_size_.width()},
            {QStringLiteral("height"), viewport_size_.height()},
            {QStringLiteral("cameraRevision"), static_cast<qint64>(camera_revision_)},
            {QStringLiteral("commandRevision"), static_cast<qint64>(command_revision_)},
        });
        if (!cached_camera_.isEmpty())
        {
            update_viewport(cached_camera_, cached_selection_, camera_revision_, command_revision_);
        }
        if (session_kind_ == QStringLiteral("preview") && preview_simulation_enabled_)
        {
            set_preview_simulation(true);
        }
        emit status_message(session_kind_ == QStringLiteral("preview")
            ? QStringLiteral("Preview worker running from editor-owned mirror")
            : QStringLiteral("Play world running from immutable snapshot"));
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
}

void WorkerClient::poll_frame()
{
    if (frame_path_.isEmpty())
    {
        return;
    }
    QFile file(frame_path_);
    if (!file.open(QIODevice::ReadOnly) || file.size() < version_one_header_size)
    {
        return;
    }
    auto* mapped = file.map(0, file.size());
    if (mapped == nullptr)
    {
        return;
    }
    const auto width = read_u32(mapped, 8);
    const auto height = read_u32(mapped, 12);
    const auto stride = read_u32(mapped, 16);
    const auto sequence_before = read_u64(mapped, 24);
    const auto version = read_u32(mapped, 4);
    const auto header_size = version == 2 ? version_two_header_size : version_one_header_size;
    const auto required = static_cast<std::uint64_t>(header_size) + (static_cast<std::uint64_t>(stride) * height);
    if (read_u32(mapped, 0) != frame_magic || (version != 1 && version != 2) || read_u32(mapped, 20) != 1
        || width == 0 || height == 0 || (sequence_before & 1U) != 0
        || required > static_cast<std::uint64_t>(file.size()) || sequence_before == last_sequence_)
    {
        file.unmap(mapped);
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
    file.unmap(mapped);
    const auto current_revision = version != 2
        || (input_revision >= input_revision_ && snapshot_revision >= snapshot_revision_
            && camera_revision >= camera_revision_ && command_revision >= command_revision_);
    if (sequence_before == sequence_after && (sequence_after & 1U) == 0
        && !copy.isNull() && current_revision)
    {
        last_sequence_ = sequence_after;
        last_frame_revision_ = frame_revision;
        frame_process_generation_ = process_generation_;
        emit frame_ready(copy);
    }
}

void WorkerClient::dispose_process(bool graceful)
{
    connect_timer_.stop();
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
        QFile::remove(frame_path_);
    }
    if (!control_endpoint_.isEmpty() && control_endpoint_.contains(QDir::separator()))
    {
        QFile::remove(control_endpoint_);
    }
}

QString WorkerClient::worker_dll() const
{
    return adapter_ == QStringLiteral("kni")
        ? QString::fromUtf8(DPE_EDITOR_KNI_DLL)
        : QString::fromUtf8(DPE_EDITOR_MONOGAME_DLL);
}
