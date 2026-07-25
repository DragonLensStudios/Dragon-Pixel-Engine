#pragma once

#include <QHash>
#include <QIcon>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QThreadPool>

#include <atomic>
#include <memory>

struct ProjectIndexEntry;

enum class AssetPreviewKind
{
    sprite,
    mesh,
    material,
};

enum class AssetPreviewDiagnosticCode
{
    invalid_asset,
    missing_metadata,
    unreadable_metadata,
    missing_source,
    unreadable_source,
    decode_failed,
    unsupported_type,
};

[[nodiscard]] QString asset_preview_diagnostic_code_name(AssetPreviewDiagnosticCode code);

struct AssetPreviewRequest final
{
    QString asset_id;
    QString metadata_path;
    QString asset_type;
    QString source;
    QString resolved_source_path;
    QSize thumbnail_size{128, 128};
    bool structurally_valid{true};
};

struct AssetPreviewResult final
{
    quint64 project_generation{};
    quint64 request_id{};
    QString asset_id;
    QString metadata_path;
    AssetPreviewKind kind{AssetPreviewKind::sprite};
    QString content_identity;
    QImage image;
    QIcon icon;
    bool cache_hit{};
};

struct AssetPreviewDiagnostic final
{
    quint64 project_generation{};
    quint64 request_id{};
    QString asset_id;
    QString metadata_path;
    AssetPreviewDiagnosticCode code{AssetPreviewDiagnosticCode::invalid_asset};
    QString message;
};

Q_DECLARE_METATYPE(AssetPreviewResult)
Q_DECLARE_METATYPE(AssetPreviewDiagnostic)

// AssetPreviewService performs all file reads and thumbnail rasterization on a
// private thread pool. Signals are emitted on the service's owning Qt thread.
class AssetPreviewService final : public QObject
{
    Q_OBJECT

public:
    explicit AssetPreviewService(QObject* parent = nullptr, int maximum_threads = 2);
    ~AssetPreviewService() override;

    AssetPreviewService(const AssetPreviewService&) = delete;
    AssetPreviewService& operator=(const AssetPreviewService&) = delete;

    void set_project_generation(quint64 generation);
    [[nodiscard]] quint64 project_generation() const noexcept;

    [[nodiscard]] quint64 request_preview(
        const ProjectIndexEntry& entry,
        const QSize& thumbnail_size = QSize{128, 128});
    [[nodiscard]] quint64 request_preview(AssetPreviewRequest request);

    void clear_cache();
    [[nodiscard]] qsizetype cache_entry_count() const noexcept;

signals:
    void previewReady(const AssetPreviewResult& result);
    void previewDiagnostic(const AssetPreviewDiagnostic& diagnostic);

private:
    struct GenerationState final
    {
        std::atomic<quint64> generation{};
        std::atomic_bool alive{true};
    };

    void complete_preview(AssetPreviewResult result);
    void complete_diagnostic(AssetPreviewDiagnostic diagnostic);

    QThreadPool pool_;
    std::shared_ptr<GenerationState> generation_state_;
    QHash<QString, AssetPreviewResult> cache_;
    quint64 next_request_id_{1};
};
