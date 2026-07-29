#include "TileSetWizard.h"

#include <dragonpixel/core/uuid.h>
#include <dragonpixel/tiles/tile_documents.h>

#include <QCheckBox>
#include <QBuffer>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSpinBox>
#include <QVBoxLayout>

#include <array>
#include <cmath>

namespace
{
QString safe_stem(QString value)
{
    value = value.trimmed();
    value.replace(QRegularExpression{QStringLiteral("[^A-Za-z0-9_-]+")}, QStringLiteral("_"));
    while (value.startsWith(QLatin1Char{'_'})) value.remove(0, 1);
    while (value.endsWith(QLatin1Char{'_'})) value.chop(1);
    return value;
}

bool contained(const QString& root, const QString& path)
{
    const auto relative = QDir{root}.relativeFilePath(QFileInfo{path}.absoluteFilePath());
    return !QDir::isAbsolutePath(relative) && relative != QStringLiteral("..")
        && !relative.startsWith(QStringLiteral("../")) && !relative.startsWith(QStringLiteral("..\\"));
}

dragonpixel::core::uuid stable_tile_id(
    const dragonpixel::core::uuid& tile_set_id,
    const QRect& source)
{
    QByteArray seed{reinterpret_cast<const char*>(tile_set_id.bytes().data()), 16};
    seed.append(':').append(QByteArray::number(source.x()))
        .append(':').append(QByteArray::number(source.y()))
        .append(':').append(QByteArray::number(source.width()))
        .append(':').append(QByteArray::number(source.height()));
    const auto digest = QCryptographicHash::hash(seed, QCryptographicHash::Sha256);
    std::array<std::uint8_t, 16> bytes{};
    std::copy_n(reinterpret_cast<const std::uint8_t*>(digest.constData()), bytes.size(), bytes.begin());
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0fU) | 0x50U);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3fU) | 0x80U);
    return dragonpixel::core::uuid{bytes};
}

bool write_atomic(const QString& path, const QByteArray& bytes, QString& error)
{
    QSaveFile file{path};
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    {
        error = QStringLiteral("Could not atomically write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

struct image_source
{
    QImage image;
    QByteArray png_bytes;
    QString error;
};

image_source read_image_source(const QString& requested_path)
{
    image_source result;
    const QFileInfo source_info{requested_path.trimmed()};
    if (!source_info.isFile())
    {
        result.error = QStringLiteral("Select an existing image sprite sheet.");
        return result;
    }

    QFile source{source_info.absoluteFilePath()};
    if (!source.open(QIODevice::ReadOnly))
    {
        result.error = QStringLiteral("Could not read the selected image sprite sheet: %1")
                           .arg(source.errorString());
        return result;
    }
    auto source_bytes = source.readAll();
    if (source.error() != QFileDevice::NoError)
    {
        result.error = QStringLiteral("Could not read the selected image sprite sheet: %1")
                           .arg(source.errorString());
        return result;
    }

    QBuffer buffer{&source_bytes};
    if (!buffer.open(QIODevice::ReadOnly))
    {
        result.error = QStringLiteral("Could not inspect the selected image sprite sheet.");
        return result;
    }
    QImageReader reader{&buffer};
    reader.setDecideFormatFromContent(true);
    if (!reader.canRead())
    {
        result.error = QStringLiteral("The selected file is not a supported readable image (PNG, JPEG, BMP, or GIF).");
        return result;
    }
    auto format = reader.format().toLower();
    if (format == QByteArray{"jpg"}) format = QByteArray{"jpeg"};
    if (format != QByteArray{"png"} && format != QByteArray{"jpeg"}
        && format != QByteArray{"bmp"} && format != QByteArray{"gif"})
    {
        result.error = QStringLiteral("The selected file is not a supported image format (PNG, JPEG, BMP, or GIF).");
        return result;
    }
    result.image = reader.read();
    if (result.image.isNull())
    {
        result.error = QStringLiteral("The selected file is not a readable image: %1")
                           .arg(reader.errorString());
        return result;
    }
    if (format == QByteArray{"png"})
    {
        result.png_bytes = std::move(source_bytes);
        return result;
    }

    QBuffer png_buffer{&result.png_bytes};
    if (!png_buffer.open(QIODevice::WriteOnly) || !result.image.save(&png_buffer, "PNG"))
    {
        result.error = QStringLiteral("Could not normalize the selected image to PNG.");
        result.image = {};
        result.png_bytes.clear();
    }
    return result;
}

QByteArray asset_metadata(
    const QString& asset_id,
    const QString& type,
    const QString& source,
    const QStringList& dependencies,
    const QJsonObject& import_settings)
{
    QJsonArray dependency_values;
    for (const auto& dependency : dependencies) dependency_values.push_back(dependency);
    const QJsonObject root{
        {QStringLiteral("$schema"), QStringLiteral("https://dragonpixel.dev/schemas/v2/asset-metadata.schema.json")},
        {QStringLiteral("format"), QStringLiteral("dpe.asset")},
        {QStringLiteral("formatVersion"), 2},
        {QStringLiteral("engineVersion"), QStringLiteral("0.3.0-slice2")},
        {QStringLiteral("assetId"), asset_id},
        {QStringLiteral("assetType"), type},
        {QStringLiteral("source"), source},
        {QStringLiteral("dependencies"), dependency_values},
        {QStringLiteral("importSettings"), import_settings},
        {QStringLiteral("importerDiagnostics"), QJsonArray{}},
        {QStringLiteral("previewDiagnostics"), QJsonArray{}},
    };
    return QJsonDocument{root}.toJson(QJsonDocument::Indented);
}

struct slice_region final
{
    QRect source;
    int column{};
    int row{};
};

struct slice_plan final
{
    QVector<slice_region> regions;
    int columns{};
    int rows{};
    int cell_width{};
    int cell_height{};
    QString error;
};

bool region_is_empty(const QImage& image, const QRect& region)
{
    for (int y = region.top(); y <= region.bottom(); ++y)
        for (int x = region.left(); x <= region.right(); ++x)
            if (qAlpha(image.pixel(x, y)) != 0) return false;
    return true;
}

QVector<QPair<int, int>> occupied_runs(const QImage& image, bool horizontal)
{
    QVector<QPair<int, int>> runs;
    const auto outer = horizontal ? image.width() : image.height();
    const auto inner = horizontal ? image.height() : image.width();
    int start = -1;
    for (int position = 0; position < outer; ++position)
    {
        bool occupied = false;
        for (int cross = 0; cross < inner && !occupied; ++cross)
        {
            const auto x = horizontal ? position : cross;
            const auto y = horizontal ? cross : position;
            occupied = qAlpha(image.pixel(x, y)) != 0;
        }
        if (occupied && start < 0) start = position;
        if (!occupied && start >= 0)
        {
            runs.push_back({start, position - start});
            start = -1;
        }
    }
    if (start >= 0) runs.push_back({start, outer - start});
    return runs;
}

slice_plan build_slice_plan(const QImage& image, const TileSetCreationRequest& request)
{
    slice_plan result;
    if (image.isNull())
    {
        result.error = QStringLiteral("The selected file is not a readable supported image.");
        return result;
    }
    if (request.slicing_mode == TileSetSlicingMode::automatic)
    {
        const auto columns = occupied_runs(image, true);
        const auto rows = occupied_runs(image, false);
        result.columns = columns.size();
        result.rows = rows.size();
        for (int row = 0; row < rows.size(); ++row)
            for (int column = 0; column < columns.size(); ++column)
            {
                const QRect region{columns[column].first, rows[row].first,
                    columns[column].second, rows[row].second};
                if (request.keep_empty_cells || !region_is_empty(image, region))
                    result.regions.push_back({region, column, row});
            }
        if (result.regions.isEmpty())
            result.error = QStringLiteral("Automatic slicing found no non-transparent sprite regions.");
        if (result.regions.size() > 65536)
            result.error = QStringLiteral("The slice must contain between 1 and 65,536 tiles.");
        if (!result.regions.isEmpty())
        {
            result.cell_width = result.regions.front().source.width();
            result.cell_height = result.regions.front().source.height();
        }
        return result;
    }
    const auto usable_width = image.width() - request.margin_x * 2;
    const auto usable_height = image.height() - request.margin_y * 2;
    if (usable_width <= 0 || usable_height <= 0)
    {
        result.error = QStringLiteral("The slicing offset leaves no usable image area.");
        return result;
    }
    if (request.slicing_mode == TileSetSlicingMode::cell_count)
    {
        if (request.column_count <= 0 || request.row_count <= 0
            || (usable_width + request.spacing_x) % request.column_count != 0
            || (usable_height + request.spacing_y) % request.row_count != 0)
        {
            result.error = QStringLiteral("The image dimensions do not divide evenly using the requested cell count, offset, and padding.");
            return result;
        }
        result.columns = request.column_count;
        result.rows = request.row_count;
        result.cell_width = (usable_width + request.spacing_x) / request.column_count - request.spacing_x;
        result.cell_height = (usable_height + request.spacing_y) / request.row_count - request.spacing_y;
    }
    else
    {
        if (request.cell_width <= 0 || request.cell_height <= 0)
        {
            result.error = QStringLiteral("Cell dimensions must be positive.");
            return result;
        }
        const auto stride_x = request.cell_width + request.spacing_x;
        const auto stride_y = request.cell_height + request.spacing_y;
        if (stride_x <= 0 || stride_y <= 0
            || (usable_width + request.spacing_x) % stride_x != 0
            || (usable_height + request.spacing_y) % stride_y != 0)
        {
            result.error = QStringLiteral("The image dimensions do not divide evenly using the requested offset, padding, and cell size.");
            return result;
        }
        result.columns = (usable_width + request.spacing_x) / stride_x;
        result.rows = (usable_height + request.spacing_y) / stride_y;
        result.cell_width = request.cell_width;
        result.cell_height = request.cell_height;
    }
    if (result.cell_width <= 0 || result.cell_height <= 0 || result.columns <= 0 || result.rows <= 0
        || static_cast<qint64>(result.columns) * result.rows > 65536)
    {
        result.error = QStringLiteral("The slice must contain between 1 and 65,536 positive-size tiles.");
        return result;
    }
    for (int row = 0; row < result.rows; ++row)
        for (int column = 0; column < result.columns; ++column)
        {
            const QRect region{
                request.margin_x + column * (result.cell_width + request.spacing_x),
                request.margin_y + row * (result.cell_height + request.spacing_y),
                result.cell_width,
                result.cell_height,
            };
            if (request.keep_empty_cells || !region_is_empty(image, region))
                result.regions.push_back({region, column, row});
        }
    if (result.regions.isEmpty())
        result.error = QStringLiteral("Slicing produced no non-transparent tiles; enable Keep Empty Cells to retain them.");
    return result;
}
}

TileSetCreationResult TileSetCreationService::create(const TileSetCreationRequest& request)
{
    TileSetCreationResult result;
    const auto project_root = QFileInfo{request.project_root}.absoluteFilePath();
    const auto stem = safe_stem(request.name);
    const auto source = read_image_source(request.source_image);
    if (!source.error.isEmpty())
    {
        result.error = source.error;
        return result;
    }
    if (stem.isEmpty() || !contained(project_root, QDir{project_root}.filePath(QStringLiteral("Assets"))))
    {
        result.error = QStringLiteral("A valid project root and TileSet name are required.");
        return result;
    }
    const auto slices = build_slice_plan(source.image, request);
    result.error = slices.error;
    if (!result.error.isEmpty()) return result;

    QDir project{project_root};
    if (!project.mkpath(QStringLiteral("Assets/Textures")) || !project.mkpath(QStringLiteral("Assets/Tiles")))
    {
        result.error = QStringLiteral("Could not create the contained texture and TileSet folders.");
        return result;
    }
    result.texture_path = project.filePath(QStringLiteral("Assets/Textures/%1.png").arg(stem));
    result.texture_metadata_path = project.filePath(QStringLiteral("Assets/%1.texture.dpeasset").arg(stem));
    result.tile_set_path = project.filePath(QStringLiteral("Assets/Tiles/%1.dpetileset").arg(stem));
    result.tile_set_metadata_path = project.filePath(QStringLiteral("Assets/%1.tileset.dpeasset").arg(stem));
    const QStringList outputs{result.texture_path, result.texture_metadata_path,
        result.tile_set_path, result.tile_set_metadata_path};
    for (const auto& output : outputs)
    {
        if (QFileInfo::exists(output))
        {
            result.error = QStringLiteral("An asset already exists at %1; no files were changed.").arg(output);
            return result;
        }
    }

    const auto texture_id = dragonpixel::core::uuid::random_v4();
    const auto tile_set_id = dragonpixel::core::uuid::random_v4();
    result.texture_asset_id = QString::fromStdString(texture_id.to_string());
    result.tile_set_asset_id = QString::fromStdString(tile_set_id.to_string());
    dragonpixel::tiles::tile_set_document document;
    document.asset_id = tile_set_id;
    document.name = request.name.trimmed().toStdString();
    document.texture_asset_id = texture_id;
    document.texture_asset_ids = {texture_id};
    document.cell_size = {slices.cell_width, slices.cell_height};
    document.margin = {request.margin_x, request.margin_y};
    document.spacing = {request.spacing_x, request.spacing_y};
    document.pixels_per_unit = request.pixels_per_unit;
    document.slicing.mode = request.slicing_mode == TileSetSlicingMode::automatic
        ? dragonpixel::tiles::slice_mode::automatic
        : request.slicing_mode == TileSetSlicingMode::cell_count
            ? dragonpixel::tiles::slice_mode::cell_count
            : dragonpixel::tiles::slice_mode::cell_size;
    document.slicing.cell_count = {slices.columns, slices.rows};
    document.slicing.keep_empty_rects = request.keep_empty_cells;
    document.slicing.pivot = {request.pivot_x, request.pivot_y};
    for (const auto& slice : slices.regions)
    {
        dragonpixel::tiles::tile_definition tile;
        tile.tile_id = stable_tile_id(tile_set_id, slice.source);
        tile.name = QStringLiteral("%1 %2,%3").arg(request.name.trimmed())
            .arg(slice.column).arg(slice.row).toStdString();
        tile.source = {slice.source.x(), slice.source.y(),
            slice.source.width(), slice.source.height()};
        tile.texture_asset_id = texture_id;
        tile.pivot = {request.pivot_x, request.pivot_y};
        if (request.rectangular_collision)
        {
            tile.collider_mode = dragonpixel::tiles::tile_collider_mode::grid;
            tile.collision = dragonpixel::tiles::collision_rectangle{0.0, 0.0, 1.0, 1.0};
        }
        document.tiles.push_back(std::move(tile));
    }

    const auto tile_bytes = QByteArray::fromStdString(dragonpixel::tiles::write_tile_set(document));
    const auto texture_metadata = asset_metadata(
        result.texture_asset_id, QStringLiteral("sprite"), QStringLiteral("Textures/%1.png").arg(stem), {},
        {{QStringLiteral("filter"), QStringLiteral("nearest")},
         {QStringLiteral("pixelsPerUnit"), request.pixels_per_unit}});
    const auto tile_metadata = asset_metadata(
        result.tile_set_asset_id, QStringLiteral("tileset"), QStringLiteral("Tiles/%1.dpetileset").arg(stem),
        {result.texture_asset_id}, {{QStringLiteral("gridLayout"), request.grid_layout},
            {QStringLiteral("slicingMode"), request.slicing_mode == TileSetSlicingMode::automatic
                ? QStringLiteral("automatic")
                : request.slicing_mode == TileSetSlicingMode::cell_count
                    ? QStringLiteral("cell-count") : QStringLiteral("cell-size")}});
    const std::array<std::pair<QString, QByteArray>, 4> writes{{
        {result.texture_path, source.png_bytes},
        {result.texture_metadata_path, texture_metadata},
        {result.tile_set_path, tile_bytes},
        {result.tile_set_metadata_path, tile_metadata},
    }};
    QStringList created;
    for (const auto& [path, bytes] : writes)
    {
        if (!write_atomic(path, bytes, result.error))
        {
            for (const auto& created_path : created) QFile::remove(created_path);
            return result;
        }
        created.push_back(path);
    }
    result.tile_count = slices.regions.size();
    result.succeeded = true;
    return result;
}

TileSetSlicePlan TileSetCreationService::plan_slices(
    const QImage& image,
    const TileSetCreationRequest& request)
{
    const auto internal = build_slice_plan(image, request);
    TileSetSlicePlan result;
    result.columns = internal.columns;
    result.rows = internal.rows;
    result.cell_width = internal.cell_width;
    result.cell_height = internal.cell_height;
    result.error = internal.error;
    result.regions.reserve(internal.regions.size());
    for (const auto& region : internal.regions)
        result.regions.push_back({region.source, region.column, region.row});
    return result;
}

TileSetWizard::TileSetWizard(QString project_root, QWidget* parent)
    : QDialog(parent), project_root_(std::move(project_root))
{
    setWindowTitle(QStringLiteral("Create TileSet from Image"));
    setObjectName(QStringLiteral("TileSetWizard"));
    setAccessibleName(QStringLiteral("Create TileSet from image sprite sheet"));
    resize(620, 600);
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    auto* source_row = new QWidget(this);
    auto* source_layout = new QHBoxLayout(source_row);
    source_layout->setContentsMargins(0, 0, 0, 0);
    source_ = new QLineEdit(source_row);
    source_->setObjectName(QStringLiteral("TileSetSourceImage"));
    source_->setAccessibleName(QStringLiteral("Image sprite sheet path"));
    auto* browse = new QPushButton(QStringLiteral("Browse..."), source_row);
    browse->setObjectName(QStringLiteral("BrowseTileSetSource"));
    connect(browse, &QPushButton::clicked, this, &TileSetWizard::browse_source);
    source_layout->addWidget(source_);
    source_layout->addWidget(browse);
    form->addRow(QStringLiteral("Image sprite sheet"), source_row);
    name_ = new QLineEdit(this);
    name_->setObjectName(QStringLiteral("TileSetName"));
    form->addRow(QStringLiteral("Name"), name_);
    slicing_mode_ = new QComboBox(this);
    slicing_mode_->setObjectName(QStringLiteral("TileSlicingMode"));
    slicing_mode_->setAccessibleName(QStringLiteral("Sprite slicing mode"));
    slicing_mode_->addItem(QStringLiteral("Automatic"), static_cast<int>(TileSetSlicingMode::automatic));
    slicing_mode_->addItem(QStringLiteral("Cell Size"), static_cast<int>(TileSetSlicingMode::cell_size));
    slicing_mode_->addItem(QStringLiteral("Cell Count"), static_cast<int>(TileSetSlicingMode::cell_count));
    slicing_mode_->setCurrentIndex(1);
    form->addRow(QStringLiteral("Slicing"), slicing_mode_);
    auto add_spin = [this, form](const QString& label, const QString& id, int minimum, int maximum, int value) {
        auto* spin = new QSpinBox(this);
        spin->setObjectName(id);
        spin->setRange(minimum, maximum);
        spin->setValue(value);
        form->addRow(label, spin);
        return spin;
    };
    cell_width_ = add_spin(QStringLiteral("Cell width"), QStringLiteral("TileCellWidth"), 1, 8192, 32);
    cell_height_ = add_spin(QStringLiteral("Cell height"), QStringLiteral("TileCellHeight"), 1, 8192, 32);
    column_count_ = add_spin(QStringLiteral("Column count"), QStringLiteral("TileColumnCount"), 1, 8192, 1);
    row_count_ = add_spin(QStringLiteral("Row count"), QStringLiteral("TileRowCount"), 1, 8192, 1);
    margin_x_ = add_spin(QStringLiteral("Offset X"), QStringLiteral("TileMarginX"), 0, 8192, 0);
    margin_y_ = add_spin(QStringLiteral("Offset Y"), QStringLiteral("TileMarginY"), 0, 8192, 0);
    spacing_x_ = add_spin(QStringLiteral("Padding X"), QStringLiteral("TileSpacingX"), 0, 8192, 0);
    spacing_y_ = add_spin(QStringLiteral("Padding Y"), QStringLiteral("TileSpacingY"), 0, 8192, 0);
    pixels_per_unit_ = new QDoubleSpinBox(this);
    pixels_per_unit_->setObjectName(QStringLiteral("TilePixelsPerUnit"));
    pixels_per_unit_->setRange(0.001, 100000.0);
    pixels_per_unit_->setValue(32.0);
    form->addRow(QStringLiteral("Pixels per unit"), pixels_per_unit_);
    pivot_x_ = new QDoubleSpinBox(this);
    pivot_y_ = new QDoubleSpinBox(this);
    for (auto* pivot : {pivot_x_, pivot_y_})
    {
        pivot->setRange(0.0, 1.0);
        pivot->setSingleStep(0.05);
        pivot->setValue(0.5);
    }
    pivot_x_->setObjectName(QStringLiteral("TilePivotX"));
    pivot_y_->setObjectName(QStringLiteral("TilePivotY"));
    form->addRow(QStringLiteral("Pivot X"), pivot_x_);
    form->addRow(QStringLiteral("Pivot Y"), pivot_y_);
    keep_empty_ = new QCheckBox(QStringLiteral("Keep empty cells"), this);
    keep_empty_->setObjectName(QStringLiteral("TileKeepEmptyCells"));
    keep_empty_->setAccessibleName(QStringLiteral("Keep fully transparent sliced cells"));
    form->addRow(QString{}, keep_empty_);
    grid_layout_ = new QComboBox(this);
    grid_layout_->setObjectName(QStringLiteral("TileGridLayout"));
    grid_layout_->setAccessibleName(QStringLiteral("Tilemap grid layout"));
    grid_layout_->addItem(QStringLiteral("Rectangular"), QStringLiteral("rectangular"));
    grid_layout_->addItem(QStringLiteral("Hex Point Top"), QStringLiteral("hex-point-top"));
    grid_layout_->addItem(QStringLiteral("Hex Flat Top"), QStringLiteral("hex-flat-top"));
    grid_layout_->addItem(QStringLiteral("Isometric"), QStringLiteral("isometric"));
    grid_layout_->addItem(QStringLiteral("Isometric Z as Y"), QStringLiteral("isometric-z-as-y"));
    form->addRow(QStringLiteral("Grid layout"), grid_layout_);
    collision_ = new QCheckBox(QStringLiteral("Generate rectangular 2D collision per tile"), this);
    collision_->setObjectName(QStringLiteral("TileCollisionDefault"));
    collision_->setChecked(true);
    form->addRow(QString{}, collision_);
    create_tilemap_ = new QCheckBox(
        QStringLiteral("Create a Tilemap and GameObject ready for painting"), this);
    create_tilemap_->setObjectName(QStringLiteral("CompleteTilemapWorkflow"));
    create_tilemap_->setAccessibleName(
        QStringLiteral("Create a Tilemap and assigned GameObject after the TileSet"));
    create_tilemap_->setToolTip(QStringLiteral(
        "Creates an empty Tilemap using this TileSet, assigns it to a Tilemap2D GameObject, and opens the 2D painting workspace."));
    create_tilemap_->setChecked(true);
    form->addRow(QString{}, create_tilemap_);
    layout->addLayout(form);
    preview_ = new QLabel(this);
    preview_->setObjectName(QStringLiteral("TileSetSlicePreview"));
    preview_->setAccessibleName(QStringLiteral("TileSet slicing preview"));
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setMinimumHeight(220);
    preview_->setFrameShape(QFrame::StyledPanel);
    layout->addWidget(preview_, 1);
    validation_ = new QLabel(this);
    validation_->setObjectName(QStringLiteral("TileSetValidation"));
    validation_->setWordWrap(true);
    layout->addWidget(validation_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Create"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("CreateTileSetAction"));
    connect(buttons, &QDialogButtonBox::accepted, this, &TileSetWizard::create_assets);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    connect(source_, &QLineEdit::textChanged, this, [this](const QString& path) {
        source_->setToolTip(path);
        refresh_preview();
    });
    connect(name_, &QLineEdit::textChanged, this, &TileSetWizard::refresh_preview);
    const auto update_slicing_controls = [this] {
        const auto mode = static_cast<TileSetSlicingMode>(slicing_mode_->currentData().toInt());
        cell_width_->setEnabled(mode == TileSetSlicingMode::cell_size);
        cell_height_->setEnabled(mode == TileSetSlicingMode::cell_size);
        column_count_->setEnabled(mode == TileSetSlicingMode::cell_count);
        row_count_->setEnabled(mode == TileSetSlicingMode::cell_count);
        const auto grid_mode = mode != TileSetSlicingMode::automatic;
        margin_x_->setEnabled(grid_mode);
        margin_y_->setEnabled(grid_mode);
        spacing_x_->setEnabled(grid_mode);
        spacing_y_->setEnabled(grid_mode);
        refresh_preview();
    };
    connect(slicing_mode_, &QComboBox::currentIndexChanged, this,
        [update_slicing_controls](int) { update_slicing_controls(); });
    for (auto* spin : {cell_width_, cell_height_, column_count_, row_count_,
             margin_x_, margin_y_, spacing_x_, spacing_y_})
        connect(spin, &QSpinBox::valueChanged, this, &TileSetWizard::refresh_preview);
    for (auto* spin : {pixels_per_unit_, pivot_x_, pivot_y_})
        connect(spin, &QDoubleSpinBox::valueChanged, this, &TileSetWizard::refresh_preview);
    connect(keep_empty_, &QCheckBox::toggled, this, &TileSetWizard::refresh_preview);
    update_slicing_controls();
}

bool TileSetWizard::complete_tilemap_workflow() const noexcept
{
    return create_tilemap_ != nullptr && create_tilemap_->isChecked();
}

QString TileSetWizard::tile_set_name() const
{
    return name_ == nullptr ? QString{} : name_->text().trimmed();
}

QString TileSetWizard::grid_layout() const
{
    return grid_layout_ == nullptr ? QStringLiteral("rectangular")
                                   : grid_layout_->currentData().toString();
}

void TileSetWizard::browse_source()
{
    const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Select image sprite sheet"),
        project_root_, QStringLiteral(
            "Supported images (*.png *.jpg *.jpeg *.bmp *.gif);;PNG images (*.png);;JPEG images (*.jpg *.jpeg);;BMP images (*.bmp);;GIF images (*.gif);;All files (*)"));
    if (path.isEmpty()) return;
    source_->setText(path);
    if (name_->text().trimmed().isEmpty()) name_->setText(QFileInfo{path}.completeBaseName());
}

void TileSetWizard::refresh_preview()
{
    const auto source = read_image_source(source_->text());
    const auto& image = source.image;
    TileSetCreationRequest request;
    request.project_root = project_root_;
    request.source_image = source_->text();
    request.name = name_->text();
    request.slicing_mode = static_cast<TileSetSlicingMode>(slicing_mode_->currentData().toInt());
    request.cell_width = cell_width_->value();
    request.cell_height = cell_height_->value();
    request.column_count = column_count_->value();
    request.row_count = row_count_->value();
    request.margin_x = margin_x_->value();
    request.margin_y = margin_y_->value();
    request.spacing_x = spacing_x_->value();
    request.spacing_y = spacing_y_->value();
    request.pixels_per_unit = pixels_per_unit_->value();
    request.pivot_x = pivot_x_->value();
    request.pivot_y = pivot_y_->value();
    request.keep_empty_cells = keep_empty_->isChecked();
    request.grid_layout = grid_layout();
    const auto slices = build_slice_plan(image, request);
    const auto error = source.error.isEmpty() ? slices.error : source.error;
    validation_->setText(error.isEmpty()
        ? QStringLiteral("%1 × %2 grid · %3 tiles · source %4 × %5 px")
            .arg(slices.columns).arg(slices.rows).arg(slices.regions.size())
            .arg(image.width()).arg(image.height())
        : error);
    if (image.isNull())
    {
        preview_->setPixmap({});
        preview_->setText(QStringLiteral("Choose a PNG to preview its tile grid"));
        return;
    }
    auto picture = image.convertToFormat(QImage::Format_ARGB32);
    QPainter painter{&picture};
    painter.setPen(QPen{QColor{255, 210, 32, 220}, 1});
    for (const auto& slice : slices.regions) painter.drawRect(slice.source);
    painter.end();
    preview_->setPixmap(QPixmap::fromImage(picture).scaled(
        preview_->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
}

void TileSetWizard::create_assets()
{
    TileSetCreationRequest request;
    request.project_root = project_root_;
    request.source_image = source_->text();
    request.name = name_->text();
    request.slicing_mode = static_cast<TileSetSlicingMode>(slicing_mode_->currentData().toInt());
    request.cell_width = cell_width_->value();
    request.cell_height = cell_height_->value();
    request.column_count = column_count_->value();
    request.row_count = row_count_->value();
    request.margin_x = margin_x_->value();
    request.margin_y = margin_y_->value();
    request.spacing_x = spacing_x_->value();
    request.spacing_y = spacing_y_->value();
    request.pixels_per_unit = pixels_per_unit_->value();
    request.pivot_x = pivot_x_->value();
    request.pivot_y = pivot_y_->value();
    request.keep_empty_cells = keep_empty_->isChecked();
    request.grid_layout = grid_layout();
    request.rectangular_collision = collision_->isChecked();
    result_ = TileSetCreationService::create(request);
    if (!result_.succeeded)
    {
        validation_->setText(result_.error);
        QMessageBox::warning(this, QStringLiteral("TileSet creation failed"), result_.error);
        return;
    }
    accept();
}
