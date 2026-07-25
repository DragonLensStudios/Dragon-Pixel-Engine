#include "WorkerClient.h"

#include "LocalEndpoint.h"

#include <QApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QtEndian>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <utility>

namespace
{
constexpr auto header_size = 64;
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
    recovery_count_ = 0;
    minimum_recovery_generation_ = 0;
    dispose_process(true);
    launch();
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

void WorkerClient::stop_and_discard()
{
    const auto generation = ++lifecycle_generation_;
    desired_running_ = false;
    shutting_down_ = true;
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
    connect(process_, &QProcess::readyReadStandardError, this, [this] {
        const auto message = QString::fromUtf8(process_->readAllStandardError()).trimmed();
        if (!message.isEmpty())
        {
            emit status_message(QStringLiteral("Worker: %1").arg(message));
        }
    });
    connect(process_, &QProcess::finished, this, [this](int exit_code, QProcess::ExitStatus status) {
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
            QTimer::singleShot(200, this, [this] {
                dispose_process(false);
                launch();
            });
        }
    });
    process_->start(
        QString::fromUtf8(DPE_DOTNET_EXECUTABLE),
        {worker_dll(),
         QStringLiteral("--frame-file"), frame_path_,
         QStringLiteral("--control"), control_endpoint_,
         QStringLiteral("--session"), session_kind_,
         QStringLiteral("--width"), QStringLiteral("960"),
         QStringLiteral("--height"), QStringLiteral("540")});
    if (!process_->waitForStarted(5000))
    {
        emit status_message(QStringLiteral("Worker failed to start: %1").arg(process_->errorString()));
        return;
    }

    socket_ = new QLocalSocket(this);
    connect(socket_, &QLocalSocket::readyRead, this, &WorkerClient::consume_messages);
    connect(socket_, &QLocalSocket::connected, this, [this] {
        connect_timer_.stop();
        emit status_message(QStringLiteral("Versioned local control channel connected"));
        send_request(QStringLiteral("handshake"));
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
        send_request(QStringLiteral("loadSnapshot"), {{QStringLiteral("snapshotPath"), snapshot_path_}});
    }
    else if (method == QStringLiteral("loadSnapshot"))
    {
        send_request(QStringLiteral("play"));
    }
    else if (method == QStringLiteral("play"))
    {
        emit status_message(session_kind_ == QStringLiteral("preview")
            ? QStringLiteral("Preview worker running from editor-owned mirror")
            : QStringLiteral("Play world running from immutable snapshot"));
    }
}

void WorkerClient::poll_frame()
{
    if (frame_path_.isEmpty())
    {
        return;
    }
    QFile file(frame_path_);
    if (!file.open(QIODevice::ReadOnly) || file.size() < header_size)
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
    const auto required = header_size + (static_cast<std::uint64_t>(stride) * height);
    if (read_u32(mapped, 0) != frame_magic || read_u32(mapped, 4) != 1 || read_u32(mapped, 20) != 1
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
    const auto sequence_after = read_u64(mapped, 24);
    file.unmap(mapped);
    if (sequence_before == sequence_after && (sequence_after & 1U) == 0 && !copy.isNull())
    {
        last_sequence_ = sequence_after;
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
}

QString WorkerClient::worker_dll() const
{
    return adapter_ == QStringLiteral("kni")
        ? QString::fromUtf8(DPE_EDITOR_KNI_DLL)
        : QString::fromUtf8(DPE_EDITOR_MONOGAME_DLL);
}
