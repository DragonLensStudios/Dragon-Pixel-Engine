#include "TileSetWizard.h"

#include <dragonpixel/core/uuid.h>
#include <dragonpixel/tiles/tile_documents.h>

#include <QCheckBox>
#include <QBuffer>
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
    int column,
    int row)
{
    QByteArray seed{reinterpret_cast<const char*>(tile_set_id.bytes().data()), 16};
    seed.append(':').append(QByteArray::number(column)).append(':').append(QByteArray::number(row));
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

struct png_source
{
    QImage image;
    QByteArray bytes;
    QString error;
};

png_source read_png_source(const QString& requested_path)
{
    png_source result;
    const QFileInfo source_info{requested_path.trimmed()};
    if (!source_info.isFile())
    {
        result.error = QStringLiteral("Select an existing PNG sprite sheet.");
        return result;
    }

    QFile source{source_info.absoluteFilePath()};
    if (!source.open(QIODevice::ReadOnly))
    {
        result.error = QStringLiteral("Could not read the selected PNG sprite sheet: %1")
                           .arg(source.errorString());
        return result;
    }
    result.bytes = source.readAll();
    if (source.error() != QFileDevice::NoError)
    {
        result.error = QStringLiteral("Could not read the selected PNG sprite sheet: %1")
                           .arg(source.errorString());
        result.bytes.clear();
        return result;
    }

    QBuffer buffer{&result.bytes};
    if (!buffer.open(QIODevice::ReadOnly))
    {
        result.error = QStringLiteral("Could not inspect the selected PNG sprite sheet.");
        result.bytes.clear();
        return result;
    }
    QImageReader reader{&buffer, QByteArray{"png"}};
    if (!reader.canRead())
    {
        result.error = QStringLiteral("The selected file is not a readable PNG image.");
        result.bytes.clear();
        return result;
    }
    result.image = reader.read();
    if (result.image.isNull())
    {
        result.error = QStringLiteral("The selected file is not a readable PNG image: %1")
                           .arg(reader.errorString());
        result.bytes.clear();
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

QString slicing_error(const QImage& image, const TileSetCreationRequest& request, int& columns, int& rows)
{
    if (image.isNull()) return QStringLiteral("The selected file is not a readable PNG image.");
    if (request.cell_width <= 0 || request.cell_height <= 0) return QStringLiteral("Cell dimensions must be positive.");
    const auto usable_width = image.width() - request.margin_x * 2;
    const auto usable_height = image.height() - request.margin_y * 2;
    const auto stride_x = request.cell_width + request.spacing_x;
    const auto stride_y = request.cell_height + request.spacing_y;
    if (usable_width <= 0 || usable_height <= 0 || stride_x <= 0 || stride_y <= 0
        || (usable_width + request.spacing_x) % stride_x != 0
        || (usable_height + request.spacing_y) % stride_y != 0)
    {
        return QStringLiteral("The PNG dimensions do not divide evenly using the requested margins, spacing, and cell size.");
    }
    columns = (usable_width + request.spacing_x) / stride_x;
    rows = (usable_height + request.spacing_y) / stride_y;
    if (columns <= 0 || rows <= 0 || static_cast<qint64>(columns) * rows > 65536)
        return QStringLiteral("The slice must contain between 1 and 65,536 tiles.");
    return {};
}
}

TileSetCreationResult TileSetCreationService::create(const TileSetCreationRequest& request)
{
    TileSetCreationResult result;
    const auto project_root = QFileInfo{request.project_root}.absoluteFilePath();
    const auto stem = safe_stem(request.name);
    const auto source = read_png_source(request.source_png);
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
    int columns{};
    int rows{};
    result.error = slicing_error(source.image, request, columns, rows);
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
    document.cell_size = {request.cell_width, request.cell_height};
    document.margin = {request.margin_x, request.margin_y};
    document.spacing = {request.spacing_x, request.spacing_y};
    document.pixels_per_unit = request.pixels_per_unit;
    for (int row = 0; row < rows; ++row)
    {
        for (int column = 0; column < columns; ++column)
        {
            dragonpixel::tiles::tile_definition tile;
            tile.tile_id = stable_tile_id(tile_set_id, column, row);
            tile.name = QStringLiteral("%1 %2,%3").arg(request.name.trimmed()).arg(column).arg(row).toStdString();
            tile.source = {
                request.margin_x + column * (request.cell_width + request.spacing_x),
                request.margin_y + row * (request.cell_height + request.spacing_y),
                request.cell_width,
                request.cell_height,
            };
            if (request.rectangular_collision)
            {
                tile.collision = dragonpixel::tiles::collision_rectangle{0.0, 0.0, 1.0, 1.0};
            }
            document.tiles.push_back(std::move(tile));
        }
    }

    const auto tile_bytes = QByteArray::fromStdString(dragonpixel::tiles::write_tile_set(document));
    const auto texture_metadata = asset_metadata(
        result.texture_asset_id, QStringLiteral("sprite"), QStringLiteral("Textures/%1.png").arg(stem), {},
        {{QStringLiteral("filter"), QStringLiteral("nearest")},
         {QStringLiteral("pixelsPerUnit"), request.pixels_per_unit}});
    const auto tile_metadata = asset_metadata(
        result.tile_set_asset_id, QStringLiteral("tileset"), QStringLiteral("Tiles/%1.dpetileset").arg(stem),
        {result.texture_asset_id}, {{QStringLiteral("grid"), QStringLiteral("orthogonal")}});
    const std::array<std::pair<QString, QByteArray>, 4> writes{{
        {result.texture_path, source.bytes},
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
    result.tile_count = columns * rows;
    result.succeeded = true;
    return result;
}

TileSetWizard::TileSetWizard(QString project_root, QWidget* parent)
    : QDialog(parent), project_root_(std::move(project_root))
{
    setWindowTitle(QStringLiteral("Create TileSet from PNG"));
    setObjectName(QStringLiteral("TileSetWizard"));
    setAccessibleName(QStringLiteral("Create TileSet from PNG sprite sheet"));
    resize(620, 600);
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    auto* source_row = new QWidget(this);
    auto* source_layout = new QHBoxLayout(source_row);
    source_layout->setContentsMargins(0, 0, 0, 0);
    source_ = new QLineEdit(source_row);
    source_->setObjectName(QStringLiteral("TileSetSourcePng"));
    source_->setAccessibleName(QStringLiteral("PNG sprite sheet path"));
    auto* browse = new QPushButton(QStringLiteral("Browse..."), source_row);
    browse->setObjectName(QStringLiteral("BrowseTileSetSource"));
    connect(browse, &QPushButton::clicked, this, &TileSetWizard::browse_source);
    source_layout->addWidget(source_);
    source_layout->addWidget(browse);
    form->addRow(QStringLiteral("PNG sprite sheet"), source_row);
    name_ = new QLineEdit(this);
    name_->setObjectName(QStringLiteral("TileSetName"));
    form->addRow(QStringLiteral("Name"), name_);
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
    margin_x_ = add_spin(QStringLiteral("Horizontal margin"), QStringLiteral("TileMarginX"), 0, 8192, 0);
    margin_y_ = add_spin(QStringLiteral("Vertical margin"), QStringLiteral("TileMarginY"), 0, 8192, 0);
    spacing_x_ = add_spin(QStringLiteral("Horizontal spacing"), QStringLiteral("TileSpacingX"), 0, 8192, 0);
    spacing_y_ = add_spin(QStringLiteral("Vertical spacing"), QStringLiteral("TileSpacingY"), 0, 8192, 0);
    pixels_per_unit_ = new QDoubleSpinBox(this);
    pixels_per_unit_->setObjectName(QStringLiteral("TilePixelsPerUnit"));
    pixels_per_unit_->setRange(0.001, 100000.0);
    pixels_per_unit_->setValue(32.0);
    form->addRow(QStringLiteral("Pixels per unit"), pixels_per_unit_);
    collision_ = new QCheckBox(QStringLiteral("Generate rectangular 2D collision per tile"), this);
    collision_->setObjectName(QStringLiteral("TileCollisionDefault"));
    collision_->setChecked(true);
    form->addRow(QString{}, collision_);
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
    for (auto* spin : {cell_width_, cell_height_, margin_x_, margin_y_, spacing_x_, spacing_y_})
        connect(spin, &QSpinBox::valueChanged, this, &TileSetWizard::refresh_preview);
}

void TileSetWizard::browse_source()
{
    const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Select PNG sprite sheet"),
        project_root_, QStringLiteral("PNG images (*.png);;All files (*)"));
    if (path.isEmpty()) return;
    source_->setText(path);
    if (name_->text().trimmed().isEmpty()) name_->setText(QFileInfo{path}.completeBaseName());
}

void TileSetWizard::refresh_preview()
{
    const auto source = read_png_source(source_->text());
    const auto& image = source.image;
    TileSetCreationRequest request{project_root_, source_->text(), name_->text(), cell_width_->value(),
        cell_height_->value(), margin_x_->value(), margin_y_->value(), spacing_x_->value(), spacing_y_->value()};
    int columns{};
    int rows{};
    const auto error = source.error.isEmpty() ? slicing_error(image, request, columns, rows) : source.error;
    validation_->setText(error.isEmpty()
        ? QStringLiteral("%1 × %2 grid · %3 tiles · source %4 × %5 px")
            .arg(columns).arg(rows).arg(columns * rows).arg(image.width()).arg(image.height())
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
    for (int x = 0; x <= columns; ++x)
    {
        const auto px = margin_x_->value() + x * (cell_width_->value() + spacing_x_->value());
        painter.drawLine(px, margin_y_->value(), px, image.height() - margin_y_->value());
    }
    for (int y = 0; y <= rows; ++y)
    {
        const auto py = margin_y_->value() + y * (cell_height_->value() + spacing_y_->value());
        painter.drawLine(margin_x_->value(), py, image.width() - margin_x_->value(), py);
    }
    painter.end();
    preview_->setPixmap(QPixmap::fromImage(picture).scaled(
        preview_->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
}

void TileSetWizard::create_assets()
{
    TileSetCreationRequest request{project_root_, source_->text(), name_->text(), cell_width_->value(),
        cell_height_->value(), margin_x_->value(), margin_y_->value(), spacing_x_->value(), spacing_y_->value(),
        pixels_per_unit_->value(), collision_->isChecked()};
    result_ = TileSetCreationService::create(request);
    if (!result_.succeeded)
    {
        validation_->setText(result_.error);
        QMessageBox::warning(this, QStringLiteral("TileSet creation failed"), result_.error);
        return;
    }
    accept();
}
