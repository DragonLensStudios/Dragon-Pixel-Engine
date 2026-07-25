#include <QApplication>
#include <QComboBox>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QtEndian>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{
constexpr auto frame_header_size = 64;
constexpr std::uint32_t frame_magic = 0x46504544U;
constexpr std::uint32_t expected_content = 3U;

std::uint32_t read_u32(const unsigned char* bytes, std::size_t offset)
{
    return qFromLittleEndian<std::uint32_t>(bytes + offset);
}

std::uint64_t read_u64(const unsigned char* bytes, std::size_t offset)
{
    return qFromLittleEndian<std::uint64_t>(bytes + offset);
}

class ViewerWindow final : public QMainWindow
{
public:
    ViewerWindow()
    {
        setWindowTitle(QStringLiteral("Dragon Pixel Engine - POC B Viewer"));
        resize(900, 570);

        viewport_ = new QLabel(QStringLiteral("Start a framework worker to receive shared frames."), this);
        viewport_->setAlignment(Qt::AlignCenter);
        viewport_->setMinimumSize(640, 360);
        viewport_->setStyleSheet(QStringLiteral("background: #18121e; color: #d8d0e0;"));
        setCentralWidget(viewport_);

        auto* toolbar = addToolBar(QStringLiteral("Runtime"));
        adapter_ = new QComboBox(toolbar);
        adapter_->addItem(QStringLiteral("MonoGame"), QStringLiteral("monogame"));
        adapter_->addItem(QStringLiteral("KNI (experimental)"), QStringLiteral("kni"));
        toolbar->addWidget(adapter_);

        auto add_button = [toolbar](const QString& text) {
            auto* button = new QPushButton(text, toolbar);
            toolbar->addWidget(button);
            return button;
        };
        auto* start = add_button(QStringLiteral("Start / Restart"));
        auto* pause = add_button(QStringLiteral("Pause"));
        auto* resume = add_button(QStringLiteral("Resume"));
        auto* stop = add_button(QStringLiteral("Stop"));
        auto* crash = add_button(QStringLiteral("Force Crash"));

        connect(start, &QPushButton::clicked, this, [this] { start_adapter(adapter_->currentData().toString()); });
        connect(pause, &QPushButton::clicked, this, [this] { send_command(QStringLiteral("pause")); });
        connect(resume, &QPushButton::clicked, this, [this] { send_command(QStringLiteral("resume")); });
        connect(stop, &QPushButton::clicked, this, [this] { send_command(QStringLiteral("stop")); });
        connect(crash, &QPushButton::clicked, this, [this] { send_command(QStringLiteral("crash")); });

        connect(&frame_timer_, &QTimer::timeout, this, [this] { poll_frame(); });
        frame_timer_.start(16);
        statusBar()->showMessage(QStringLiteral("Worker stopped"));
    }

    ~ViewerWindow() override
    {
        shutdown_worker();
    }

    void start_adapter(const QString& adapter)
    {
        shutdown_worker();
        last_sequence_ = 0;
        last_content_ = 0;
        frame_path_ = QDir::temp().filePath(
            QStringLiteral("dpe-poc-b-%1-%2.frame")
                .arg(QApplication::applicationPid())
                .arg(adapter));
        QFile::remove(frame_path_);

        const auto worker = adapter == QStringLiteral("kni")
            ? QString::fromUtf8(DPE_POC_B_KNI_DLL)
            : QString::fromUtf8(DPE_POC_B_MONOGAME_DLL);
        worker_ = new QProcess(this);
        worker_->setProcessChannelMode(QProcess::SeparateChannels);
        connect(worker_, &QProcess::finished, this, [this](int exit_code, QProcess::ExitStatus status) {
            statusBar()->showMessage(
                QStringLiteral("Worker exited (%1, %2)")
                    .arg(exit_code)
                    .arg(status == QProcess::CrashExit ? QStringLiteral("crash") : QStringLiteral("normal")));
        });
        worker_->start(
            QString::fromUtf8(DPE_DOTNET_EXECUTABLE),
            {worker,
             QStringLiteral("--frame-file"), frame_path_,
             QStringLiteral("--width"), QStringLiteral("1280"),
             QStringLiteral("--height"), QStringLiteral("720")});
        if (!worker_->waitForStarted(5000))
        {
            statusBar()->showMessage(QStringLiteral("Worker failed to start: %1").arg(worker_->errorString()));
            return;
        }

        send_command(QStringLiteral("handshake"));
        send_command(QStringLiteral("initialize"));
        send_command(QStringLiteral("play"));
        statusBar()->showMessage(QStringLiteral("%1 worker running").arg(adapter));
    }

    [[nodiscard]] bool has_complete_scene() const
    {
        return last_sequence_ > 0 && last_content_ == expected_content;
    }

private:
    void send_command(const QString& method)
    {
        if (worker_ == nullptr || worker_->state() != QProcess::Running)
        {
            return;
        }

        QJsonObject request{
            {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("id"), ++request_id_},
            {QStringLiteral("method"), method},
        };
        const auto payload = QJsonDocument(request).toJson(QJsonDocument::Compact);
        QByteArray framed(static_cast<qsizetype>(sizeof(std::uint32_t)) + payload.size(), Qt::Uninitialized);
        qToLittleEndian<std::uint32_t>(static_cast<std::uint32_t>(payload.size()), framed.data());
        std::memcpy(framed.data() + sizeof(std::uint32_t), payload.constData(), static_cast<std::size_t>(payload.size()));
        worker_->write(framed);
        worker_->waitForBytesWritten(1000);
    }

    void poll_frame()
    {
        if (frame_path_.isEmpty())
        {
            return;
        }

        QFile file(frame_path_);
        if (!file.open(QIODevice::ReadOnly) || file.size() < frame_header_size)
        {
            return;
        }

        auto* mapped = file.map(0, file.size());
        if (mapped == nullptr)
        {
            return;
        }

        const auto magic = read_u32(mapped, 0);
        const auto version = read_u32(mapped, 4);
        const auto width = read_u32(mapped, 8);
        const auto height = read_u32(mapped, 12);
        const auto stride = read_u32(mapped, 16);
        const auto format = read_u32(mapped, 20);
        const auto sequence_before = read_u64(mapped, 24);
        const auto content = read_u32(mapped, 40);
        const auto required_size = frame_header_size + (static_cast<std::uint64_t>(stride) * height);
        if (magic != frame_magic || version != 1 || format != 1 || (sequence_before & 1U) != 0
            || width == 0 || height == 0 || required_size > static_cast<std::uint64_t>(file.size()))
        {
            file.unmap(mapped);
            return;
        }

        const QImage mapped_image(
            mapped + frame_header_size,
            static_cast<int>(width),
            static_cast<int>(height),
            static_cast<qsizetype>(stride),
            QImage::Format_ARGB32);
        const auto image = mapped_image.copy();
        const auto sequence_after = read_u64(mapped, 24);
        file.unmap(mapped);
        if (sequence_before != sequence_after || (sequence_after & 1U) != 0 || image.isNull())
        {
            return;
        }

        last_sequence_ = sequence_after;
        last_content_ = content;
        viewport_->setPixmap(QPixmap::fromImage(image).scaled(
            viewport_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        statusBar()->showMessage(
            QStringLiteral("Frame %1 | BGRA8 | sprite + static mesh")
                .arg(last_sequence_ / 2));
    }

    void shutdown_worker()
    {
        if (worker_ == nullptr)
        {
            return;
        }

        if (worker_->state() == QProcess::Running)
        {
            send_command(QStringLiteral("shutdown"));
            if (!worker_->waitForFinished(3000))
            {
                worker_->kill();
                worker_->waitForFinished(3000);
            }
        }
        worker_->deleteLater();
        worker_ = nullptr;
    }

    QLabel* viewport_{};
    QComboBox* adapter_{};
    QProcess* worker_{};
    QTimer frame_timer_;
    QString frame_path_;
    std::uint64_t last_sequence_{};
    std::uint32_t last_content_{};
    int request_id_{};
};
}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    ViewerWindow window;

    const auto arguments = application.arguments();
    const auto self_test_index = arguments.indexOf(QStringLiteral("--self-test"));
    if (self_test_index >= 0 && self_test_index + 1 < arguments.size())
    {
        const auto adapter = arguments[self_test_index + 1];
        window.start_adapter(adapter);
        auto* timeout = new QTimer(&application);
        timeout->setSingleShot(true);
        QObject::connect(timeout, &QTimer::timeout, &application, [&application, &window] {
            application.exit(window.has_complete_scene() ? 0 : 1);
        });
        timeout->start(5000);
        return application.exec();
    }

    window.show();
    return application.exec();
}
