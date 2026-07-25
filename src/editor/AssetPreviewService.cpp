#include "AssetPreviewService.h"

#include "ProjectIndexService.h"

#include <QByteArrayView>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaObject>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QRunnable>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace
{
struct PreviewWorkResult final
{
    std::optional<AssetPreviewResult> preview;
    std::optional<AssetPreviewDiagnostic> diagnostic;
};

QSize normalized_size(const QSize& requested)
{
    return {
        std::clamp(requested.width(), 16, 512),
        std::clamp(requested.height(), 16, 512),
    };
}

QString canonical_or_absolute(const QString& path)
{
    const QFileInfo info{path};
    const auto canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

void add_hash_field(QCryptographicHash& hash, const QByteArray& value)
{
    hash.addData(QByteArrayView{value});
    hash.addData(QByteArrayView{"\0", 1});
}

std::optional<QByteArray> read_file(
    const QString& path,
    AssetPreviewDiagnosticCode missing_code,
    AssetPreviewDiagnosticCode unreadable_code,
    AssetPreviewDiagnostic& diagnostic)
{
    const QFileInfo info{path};
    if (!info.exists() || !info.isFile())
    {
        diagnostic.code = missing_code;
        diagnostic.message = QStringLiteral("Preview input does not exist: '%1'.").arg(path);
        return std::nullopt;
    }
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly))
    {
        diagnostic.code = unreadable_code;
        diagnostic.message = QStringLiteral("Cannot read preview input '%1': %2")
                                 .arg(path, file.errorString());
        return std::nullopt;
    }
    return file.readAll();
}

std::optional<AssetPreviewKind> preview_kind(const QString& asset_type)
{
    const auto type = asset_type.trimmed().toLower();
    if (type.contains(QStringLiteral("sprite")) || type.contains(QStringLiteral("texture"))
        || type.contains(QStringLiteral("image")))
    {
        return AssetPreviewKind::sprite;
    }
    if (type.contains(QStringLiteral("mesh")) || type == QStringLiteral("model"))
    {
        return AssetPreviewKind::mesh;
    }
    if (type.contains(QStringLiteral("material")))
    {
        return AssetPreviewKind::material;
    }
    return std::nullopt;
}

bool virtual_source(const QString& source)
{
    const QUrl url{source};
    if (!url.isValid() || url.scheme().isEmpty())
    {
        return false;
    }
    const auto scheme = url.scheme().toLower();
    return scheme == QStringLiteral("builtin") || scheme == QStringLiteral("generated")
        || scheme == QStringLiteral("package");
}

QColor seeded_color(const QByteArray& seed, int offset, int saturation = 190, int value = 225)
{
    const auto byte = static_cast<unsigned char>(seed.at(offset % seed.size()));
    return QColor::fromHsv(static_cast<int>(byte) * 359 / 255, saturation, value);
}

void paint_checkerboard(QPainter& painter, const QSize& size)
{
    constexpr int cell = 8;
    for (int y = 0; y < size.height(); y += cell)
    {
        for (int x = 0; x < size.width(); x += cell)
        {
            const auto color = ((x / cell) + (y / cell)) % 2 == 0
                ? QColor{50, 54, 62}
                : QColor{66, 71, 81};
            painter.fillRect(QRect{x, y, cell, cell}, color);
        }
    }
}

QImage render_sprite(
    const QSize& size,
    const QByteArray& seed,
    const std::optional<QByteArray>& source_bytes,
    AssetPreviewDiagnostic& diagnostic)
{
    QImage canvas{size, QImage::Format_ARGB32_Premultiplied};
    canvas.fill(Qt::transparent);
    QPainter painter{&canvas};
    painter.setRenderHint(QPainter::Antialiasing, true);
    paint_checkerboard(painter, size);

    if (source_bytes)
    {
        const auto source = QImage::fromData(*source_bytes);
        if (source.isNull())
        {
            diagnostic.code = AssetPreviewDiagnosticCode::decode_failed;
            diagnostic.message = QStringLiteral("Sprite source is not a supported image.");
            return {};
        }
        const auto available = size - QSize{12, 12};
        const auto scaled = source.scaled(
            available,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
        const QPoint position{
            (size.width() - scaled.width()) / 2,
            (size.height() - scaled.height()) / 2,
        };
        painter.drawImage(position, scaled);
        return canvas;
    }

    const auto primary = seeded_color(seed, 0);
    const auto secondary = seeded_color(seed, 1, 150, 245);
    const QRectF body{
        size.width() * 0.19,
        size.height() * 0.24,
        size.width() * 0.62,
        size.height() * 0.52,
    };
    painter.setPen(QPen{secondary.lighter(150), 2.0});
    painter.setBrush(primary);
    painter.drawRoundedRect(body, 7.0, 7.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(secondary);
    painter.drawEllipse(QPointF{size.width() * 0.39, size.height() * 0.46}, 4.0, 4.0);
    painter.drawEllipse(QPointF{size.width() * 0.61, size.height() * 0.46}, 4.0, 4.0);
    painter.setPen(QPen{secondary, 3.0});
    painter.drawLine(
        QPointF{size.width() * 0.39, size.height() * 0.62},
        QPointF{size.width() * 0.61, size.height() * 0.62});
    return canvas;
}

QImage render_mesh(const QSize& size, const QByteArray& seed)
{
    QImage canvas{size, QImage::Format_ARGB32_Premultiplied};
    canvas.fill(QColor{24, 29, 38});
    QPainter painter{&canvas};
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.setPen(QPen{QColor{43, 51, 64}, 1.0});
    for (int line = 1; line < 8; ++line)
    {
        const auto y = size.height() * line / 8;
        painter.drawLine(0, y, size.width(), y);
    }

    const auto front = seeded_color(seed, 2, 135, 180);
    const auto side = front.darker(145);
    const auto top = front.lighter(130);
    const QPointF center{size.width() * 0.5, size.height() * 0.52};
    const auto width = size.width() * 0.28;
    const auto height = size.height() * 0.28;
    const auto depth = size.width() * 0.16;
    const QPointF a{center.x() - width, center.y() - height};
    const QPointF b{center.x() + width, center.y() - height};
    const QPointF c{center.x() + width, center.y() + height};
    const QPointF d{center.x() - width, center.y() + height};
    const QPointF offset{depth, -depth * 0.65};

    painter.setPen(QPen{QColor{220, 230, 244}, 1.5});
    painter.setBrush(top);
    painter.drawPolygon(QPolygonF{a, b, b + offset, a + offset});
    painter.setBrush(side);
    painter.drawPolygon(QPolygonF{b, c, c + offset, b + offset});
    painter.setBrush(front);
    painter.drawPolygon(QPolygonF{a, b, c, d});
    painter.setPen(QPen{QColor{240, 245, 255}, 1.0, Qt::DashLine});
    painter.drawLine(a + offset, d + offset);
    painter.drawLine(d + offset, c + offset);
    painter.drawLine(d, d + offset);
    return canvas;
}

QImage render_material(const QSize& size, const QByteArray& seed)
{
    QImage canvas{size, QImage::Format_ARGB32_Premultiplied};
    canvas.fill(QColor{31, 34, 40});
    const auto base = seeded_color(seed, 3, 175, 230);
    const auto center_x = static_cast<double>(size.width()) * 0.5;
    const auto center_y = static_cast<double>(size.height()) * 0.5;
    const auto radius = static_cast<double>(std::min(size.width(), size.height())) * 0.36;
    for (int y = 0; y < size.height(); ++y)
    {
        auto* scanline = reinterpret_cast<QRgb*>(canvas.scanLine(y));
        for (int x = 0; x < size.width(); ++x)
        {
            const auto nx = (static_cast<double>(x) - center_x) / radius;
            const auto ny = (static_cast<double>(y) - center_y) / radius;
            const auto distance = (nx * nx) + (ny * ny);
            if (distance > 1.0)
            {
                continue;
            }
            const auto nz = std::sqrt(1.0 - distance);
            const auto light = std::clamp(((-nx * 0.35) + (-ny * 0.45) + (nz * 0.82)), 0.08, 1.0);
            scanline[x] = qRgba(
                static_cast<int>(static_cast<double>(base.red()) * light),
                static_cast<int>(static_cast<double>(base.green()) * light),
                static_cast<int>(static_cast<double>(base.blue()) * light),
                255);
        }
    }
    QPainter painter{&canvas};
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen{QColor{225, 230, 238, 150}, 1.0});
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(
        QPointF{center_x, center_y},
        radius,
        radius);
    return canvas;
}

PreviewWorkResult build_preview(
    AssetPreviewRequest request,
    quint64 generation,
    quint64 request_id)
{
    PreviewWorkResult work;
    AssetPreviewDiagnostic diagnostic{
        generation,
        request_id,
        request.asset_id,
        request.metadata_path,
        AssetPreviewDiagnosticCode::invalid_asset,
        {},
    };
    if (!request.structurally_valid)
    {
        diagnostic.message = QStringLiteral("Asset metadata is structurally invalid.");
        work.diagnostic = std::move(diagnostic);
        return work;
    }

    const auto kind = preview_kind(request.asset_type);
    if (!kind)
    {
        diagnostic.code = AssetPreviewDiagnosticCode::unsupported_type;
        diagnostic.message = QStringLiteral("No preview renderer is registered for asset type '%1'.")
                                 .arg(request.asset_type);
        work.diagnostic = std::move(diagnostic);
        return work;
    }

    request.metadata_path = canonical_or_absolute(request.metadata_path);
    diagnostic.metadata_path = request.metadata_path;
    const auto metadata_bytes = read_file(
        request.metadata_path,
        AssetPreviewDiagnosticCode::missing_metadata,
        AssetPreviewDiagnosticCode::unreadable_metadata,
        diagnostic);
    if (!metadata_bytes)
    {
        work.diagnostic = std::move(diagnostic);
        return work;
    }

    QString source_path = request.resolved_source_path;
    const auto source_is_virtual = virtual_source(request.source);
    if (source_path.isEmpty() && !source_is_virtual && !request.source.isEmpty())
    {
        source_path = QFileInfo{request.metadata_path}.dir().absoluteFilePath(request.source);
    }

    std::optional<QByteArray> source_bytes;
    if (!source_path.isEmpty())
    {
        source_path = canonical_or_absolute(source_path);
        source_bytes = read_file(
            source_path,
            AssetPreviewDiagnosticCode::missing_source,
            AssetPreviewDiagnosticCode::unreadable_source,
            diagnostic);
        if (!source_bytes)
        {
            work.diagnostic = std::move(diagnostic);
            return work;
        }
    }
    else if (!source_is_virtual && request.source.isEmpty())
    {
        diagnostic.code = AssetPreviewDiagnosticCode::missing_source;
        diagnostic.message = QStringLiteral("Asset metadata does not identify a preview source.");
        work.diagnostic = std::move(diagnostic);
        return work;
    }

    const auto size = normalized_size(request.thumbnail_size);
    QCryptographicHash identity_hash{QCryptographicHash::Sha256};
    add_hash_field(identity_hash, QByteArrayLiteral("dpe-asset-preview-v1"));
    add_hash_field(identity_hash, request.metadata_path.toUtf8());
    const QFileInfo metadata_info{request.metadata_path};
    add_hash_field(identity_hash, QByteArray::number(metadata_info.lastModified().toMSecsSinceEpoch()));
    add_hash_field(identity_hash, QByteArray::number(metadata_info.size()));
    add_hash_field(identity_hash, *metadata_bytes);
    add_hash_field(identity_hash, request.asset_type.toUtf8());
    add_hash_field(identity_hash, request.source.toUtf8());
    add_hash_field(identity_hash, source_path.toUtf8());
    if (source_bytes)
    {
        const QFileInfo source_info{source_path};
        add_hash_field(identity_hash, QByteArray::number(source_info.lastModified().toMSecsSinceEpoch()));
        add_hash_field(identity_hash, QByteArray::number(source_info.size()));
        add_hash_field(identity_hash, *source_bytes);
    }
    add_hash_field(identity_hash, QByteArray::number(size.width()));
    add_hash_field(identity_hash, QByteArray::number(size.height()));
    const auto identity = identity_hash.result().toHex();

    AssetPreviewResult result;
    result.project_generation = generation;
    result.request_id = request_id;
    result.asset_id = request.asset_id;
    result.metadata_path = request.metadata_path;
    result.kind = *kind;
    result.content_identity = QString::fromLatin1(identity);
    switch (*kind)
    {
    case AssetPreviewKind::sprite:
        result.image = render_sprite(size, identity, source_bytes, diagnostic);
        if (result.image.isNull())
        {
            work.diagnostic = std::move(diagnostic);
            return work;
        }
        break;
    case AssetPreviewKind::mesh:
        result.image = render_mesh(size, identity);
        break;
    case AssetPreviewKind::material:
        result.image = render_material(size, identity);
        break;
    }
    work.preview = std::move(result);
    return work;
}
}

QString asset_preview_diagnostic_code_name(AssetPreviewDiagnosticCode code)
{
    switch (code)
    {
    case AssetPreviewDiagnosticCode::invalid_asset: return QStringLiteral("invalid-asset");
    case AssetPreviewDiagnosticCode::missing_metadata: return QStringLiteral("missing-metadata");
    case AssetPreviewDiagnosticCode::unreadable_metadata: return QStringLiteral("unreadable-metadata");
    case AssetPreviewDiagnosticCode::missing_source: return QStringLiteral("missing-source");
    case AssetPreviewDiagnosticCode::unreadable_source: return QStringLiteral("unreadable-source");
    case AssetPreviewDiagnosticCode::decode_failed: return QStringLiteral("decode-failed");
    case AssetPreviewDiagnosticCode::unsupported_type: return QStringLiteral("unsupported-type");
    }
    return QStringLiteral("unknown");
}

AssetPreviewService::AssetPreviewService(QObject* parent, int maximum_threads)
    : QObject(parent), generation_state_(std::make_shared<GenerationState>())
{
    qRegisterMetaType<AssetPreviewResult>();
    qRegisterMetaType<AssetPreviewDiagnostic>();
    pool_.setMaxThreadCount(std::max(1, maximum_threads));
    pool_.setExpiryTimeout(5000);
}

AssetPreviewService::~AssetPreviewService()
{
    generation_state_->alive.store(false, std::memory_order_release);
    pool_.clear();
    pool_.waitForDone();
}

void AssetPreviewService::set_project_generation(quint64 generation)
{
    generation_state_->generation.store(generation, std::memory_order_release);
    pool_.clear();
}

quint64 AssetPreviewService::project_generation() const noexcept
{
    return generation_state_->generation.load(std::memory_order_acquire);
}

quint64 AssetPreviewService::request_preview(
    const ProjectIndexEntry& entry,
    const QSize& thumbnail_size)
{
    return request_preview(AssetPreviewRequest{
        entry.id,
        entry.absolute_path,
        entry.asset_type,
        entry.source,
        entry.resolved_source_path,
        thumbnail_size,
        entry.structurally_valid && entry.kind == ProjectIndexEntryKind::asset,
    });
}

quint64 AssetPreviewService::request_preview(AssetPreviewRequest request)
{
    const auto request_id = next_request_id_++;
    const auto generation = project_generation();
    const auto state = generation_state_;
    auto* task = QRunnable::create([
        this,
        state,
        generation,
        request_id,
        request = std::move(request)]() mutable {
        if (!state->alive.load(std::memory_order_acquire)
            || state->generation.load(std::memory_order_acquire) != generation)
        {
            return;
        }
        auto work = build_preview(std::move(request), generation, request_id);
        if (!state->alive.load(std::memory_order_acquire)
            || state->generation.load(std::memory_order_acquire) != generation)
        {
            return;
        }
        QMetaObject::invokeMethod(
            this,
            [this, state, generation, work = std::move(work)]() mutable {
                if (!state->alive.load(std::memory_order_acquire)
                    || project_generation() != generation)
                {
                    return;
                }
                if (work.preview)
                {
                    complete_preview(std::move(*work.preview));
                }
                else if (work.diagnostic)
                {
                    complete_diagnostic(std::move(*work.diagnostic));
                }
            },
            Qt::QueuedConnection);
    });
    pool_.start(task);
    return request_id;
}

void AssetPreviewService::clear_cache()
{
    cache_.clear();
}

qsizetype AssetPreviewService::cache_entry_count() const noexcept
{
    return cache_.size();
}

void AssetPreviewService::complete_preview(AssetPreviewResult result)
{
    const auto existing = cache_.constFind(result.content_identity);
    if (existing != cache_.cend())
    {
        const auto generation = result.project_generation;
        const auto request_id = result.request_id;
        const auto asset_id = result.asset_id;
        const auto metadata_path = result.metadata_path;
        result = existing.value();
        result.project_generation = generation;
        result.request_id = request_id;
        result.asset_id = asset_id;
        result.metadata_path = metadata_path;
        result.cache_hit = true;
    }
    else
    {
        if (QGuiApplication::instance() != nullptr)
        {
            result.icon = QIcon{QPixmap::fromImage(result.image)};
        }
        result.cache_hit = false;
        cache_.insert(result.content_identity, result);
    }
    emit previewReady(result);
}

void AssetPreviewService::complete_diagnostic(AssetPreviewDiagnostic diagnostic)
{
    emit previewDiagnostic(diagnostic);
}
