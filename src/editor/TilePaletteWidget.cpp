#include "TilePaletteWidget.h"

#include <QActionGroup>
#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QInputDialog>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QToolBar>
#include <QToolButton>
#include <QTransform>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{
QColor tile_color(const dragonpixel::core::uuid& id)
{
    const auto hash = qHash(QString::fromStdString(id.to_string()));
    return QColor::fromHsv(static_cast<int>(hash % 360U), 150 + static_cast<int>((hash >> 8U) % 80U), 220);
}

QImage tile_image(
    const QImage& atlas,
    const dragonpixel::tiles::tile_definition& tile,
    bool flip_x,
    bool flip_y,
    unsigned rotation_quarter_turns)
{
    const auto source = QRect{tile.source.x, tile.source.y,
        tile.source.width, tile.source.height};
    if (atlas.isNull() || source.x() < 0 || source.y() < 0
        || source.width() <= 0 || source.height() <= 0
        || !atlas.rect().contains(source))
    {
        return {};
    }
    auto result = atlas.copy(source);
    if (flip_x || flip_y)
    {
        Qt::Orientations orientations;
        if (flip_x) orientations |= Qt::Horizontal;
        if (flip_y) orientations |= Qt::Vertical;
        result = result.flipped(orientations);
    }
    if ((rotation_quarter_turns % 4U) != 0U)
    {
        QTransform transform;
        transform.rotate(static_cast<qreal>(rotation_quarter_turns % 4U) * 90.0);
        result = result.transformed(transform, Qt::FastTransformation);
    }
    return result;
}
}

TileCanvas::TileCanvas(TileDocumentService* service, QWidget* parent)
    : QWidget(parent), service_(service)
{
    setObjectName(QStringLiteral("TileCanvas"));
    setAccessibleName(QStringLiteral("Orthogonal tilemap painting canvas"));
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(420, 300);
    connect(service_, &TileDocumentService::documentChanged, this, qOverload<>(&QWidget::update));
}

void TileCanvas::set_zoom(double zoom) noexcept
{
    zoom_ = std::clamp(zoom, 0.5, 4.0);
    update();
}

QPoint TileCanvas::cell_at(const QPoint& position) const
{
    const auto size = 32.0 * zoom_;
    const auto origin = QPointF{width() / 2.0, height() / 2.0};
    return {
        static_cast<int>(std::floor((position.x() - origin.x()) / size)),
        static_cast<int>(std::floor((origin.y() - position.y()) / size)),
    };
}

QRect TileCanvas::cell_rect(int x, int y) const
{
    const auto size = 32.0 * zoom_;
    const auto origin = QPointF{width() / 2.0, height() / 2.0};
    return QRectF{origin.x() + (x * size), origin.y() - ((y + 1) * size), size, size}.toAlignedRect();
}

void TileCanvas::paintEvent(QPaintEvent*)
{
    QPainter painter{this};
    painter.fillRect(rect(), palette().color(QPalette::Dark));
    const auto size = 32.0 * zoom_;
    const auto min_x = static_cast<int>(std::floor(-(width() / 2.0) / size)) - 1;
    const auto max_x = static_cast<int>(std::ceil((width() / 2.0) / size)) + 1;
    const auto min_y = static_cast<int>(std::floor(-(height() / 2.0) / size)) - 1;
    const auto max_y = static_cast<int>(std::ceil((height() / 2.0) / size)) + 1;
    if (const auto* map = service_->tilemap(); map != nullptr && layer_ >= 0
        && layer_ < static_cast<int>(map->layers.size()))
    {
        for (const auto& chunk : map->layers.at(static_cast<std::size_t>(layer_)).chunks)
        {
            for (const auto& cell : chunk.cells)
            {
                const auto x = (chunk.x * 32) + static_cast<int>(cell.index % 32U);
                const auto y = (chunk.y * 32) + static_cast<int>(cell.index / 32U);
                const auto tile = std::find_if(service_->tileset()->tiles.begin(), service_->tileset()->tiles.end(),
                    [&](const auto& value) { return value.tile_id == cell.tile_id; });
                const auto rectangle = cell_rect(x, y).adjusted(1, 1, -1, -1);
                const auto image = tile == service_->tileset()->tiles.end()
                    ? QImage{}
                    : tile_image(atlas_, *tile, cell.flip_x, cell.flip_y,
                          cell.rotation_quarter_turns);
                if (!image.isNull())
                {
                    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
                    painter.drawImage(rectangle, image);
                }
                else
                {
                    painter.fillRect(rectangle, tile_color(cell.tile_id));
                    painter.setPen(QColor{30, 30, 30});
                    painter.drawText(rectangle, Qt::AlignCenter,
                        tile == service_->tileset()->tiles.end()
                            ? QStringLiteral("?")
                            : QString::fromStdString(tile->name).left(3));
                }
            }
        }
    }
    painter.setPen(QPen{palette().color(QPalette::Mid), 1.0});
    for (int x = min_x; x <= max_x; ++x)
    {
        painter.drawLine(cell_rect(x, 0).topLeft().x(), 0, cell_rect(x, 0).topLeft().x(), height());
    }
    for (int y = min_y; y <= max_y; ++y)
    {
        painter.drawLine(0, cell_rect(0, y).bottom(), width(), cell_rect(0, y).bottom());
    }
    painter.setPen(QPen{QColor{80, 180, 255}, 2.0});
    if (selected_cell_) painter.drawRect(cell_rect(selected_cell_->x(), selected_cell_->y()).adjusted(1, 1, -1, -1));
}

void TileCanvas::apply_at(const QPoint& cell, bool preview_rectangle)
{
    if (!service_->is_loaded()) return;
    switch (tool_)
    {
        case Tool::paint:
            if (selected_brush_) static_cast<void>(service_->paint_cell(
                layer_, cell.x(), cell.y(), *selected_brush_));
            break;
        case Tool::erase:
            static_cast<void>(service_->erase_cell(layer_, cell.x(), cell.y()));
            break;
        case Tool::rectangle:
            if (preview_rectangle && stroke_start_ && selected_brush_)
            {
                static_cast<void>(service_->preview_rectangle(layer_, stroke_start_->x(), stroke_start_->y(),
                    cell.x(), cell.y(), selected_brush_->tile_id, false,
                    selected_brush_->flip_x, selected_brush_->flip_y,
                    selected_brush_->rotation_quarter_turns));
            }
            break;
        case Tool::fill:
            if (selected_brush_) static_cast<void>(service_->flood_fill(
                layer_, cell.x(), cell.y(), *selected_brush_));
            break;
        case Tool::eyedropper:
            if (const auto brush = service_->brush_at(layer_, cell.x(), cell.y()))
            {
                emit brushPicked(QString::fromStdString(brush->tile_id.to_string()),
                    brush->flip_x, brush->flip_y,
                    static_cast<int>(brush->rotation_quarter_turns));
            }
            break;
        case Tool::select:
            selected_cell_ = cell;
            emit selectionChanged(cell.x(), cell.y());
            update();
            break;
    }
}

void TileCanvas::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    setFocus(Qt::MouseFocusReason);
    const auto cell = cell_at(event->position().toPoint());
    stroke_start_ = cell;
    last_cell_ = cell;
    if (tool_ == Tool::paint || tool_ == Tool::erase || tool_ == Tool::rectangle || tool_ == Tool::fill)
    {
        service_->begin_stroke();
    }
    apply_at(cell, true);
}

void TileCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & Qt::LeftButton) || !stroke_start_) return;
    const auto cell = cell_at(event->position().toPoint());
    if (tool_ == Tool::rectangle)
    {
        apply_at(cell, true);
    }
    else if (cell != last_cell_ && (tool_ == Tool::paint || tool_ == Tool::erase))
    {
        apply_at(cell, false);
    }
    last_cell_ = cell;
}

void TileCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !stroke_start_) return;
    if (tool_ == Tool::paint || tool_ == Tool::erase || tool_ == Tool::rectangle || tool_ == Tool::fill)
    {
        service_->commit_stroke();
    }
    stroke_start_.reset();
    last_cell_.reset();
}

void TileCanvas::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && stroke_start_)
    {
        service_->cancel_stroke();
        stroke_start_.reset();
        last_cell_.reset();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

TilePaletteWidget::TilePaletteWidget(TileDocumentService* service, QWidget* parent)
    : QWidget(parent), service_(service)
{
    setObjectName(QStringLiteral("TilePalettePanel"));
    setAccessibleName(QStringLiteral("Tile Palette"));
    auto* layout = new QVBoxLayout{this};
    layout->setContentsMargins(4, 4, 4, 4);
    auto* toolbar = new QToolBar{this};
    toolbar->setObjectName(QStringLiteral("TileTools"));
    auto* tools = new QActionGroup{toolbar};
    tools->setExclusive(true);
    const auto add_tool = [toolbar, tools, this](const QString& text, const QString& name, TileCanvas::Tool tool) {
        auto* action = toolbar->addAction(text);
        action->setObjectName(name);
        action->setCheckable(true);
        tools->addAction(action);
        connect(action, &QAction::triggered, this, [this, tool] {
            canvas_->set_tool(tool);
            emit authoringStateChanged();
        });
        return action;
    };
    const auto configure_tool = [](QAction* action, const QString& shortcut) {
        action->setShortcut(QKeySequence{shortcut});
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        return action;
    };
    configure_tool(add_tool(QStringLiteral("Paint"), QStringLiteral("TilePaintTool"), TileCanvas::Tool::paint),
        QStringLiteral("1"))->setChecked(true);
    configure_tool(add_tool(QStringLiteral("Erase"), QStringLiteral("TileEraseTool"), TileCanvas::Tool::erase),
        QStringLiteral("2"));
    configure_tool(add_tool(QStringLiteral("Rect"), QStringLiteral("TileRectangleTool"), TileCanvas::Tool::rectangle),
        QStringLiteral("3"));
    configure_tool(add_tool(QStringLiteral("Fill"), QStringLiteral("TileFillTool"), TileCanvas::Tool::fill),
        QStringLiteral("4"));
    configure_tool(add_tool(QStringLiteral("Pick"), QStringLiteral("TileEyedropperTool"), TileCanvas::Tool::eyedropper),
        QStringLiteral("5"));
    configure_tool(add_tool(QStringLiteral("Select"), QStringLiteral("TileSelectionTool"), TileCanvas::Tool::select),
        QStringLiteral("6"));
    toolbar->addSeparator();
    flip_x_ = new QToolButton{toolbar};
    flip_x_->setObjectName(QStringLiteral("TileBrushFlipX"));
    flip_x_->setText(QStringLiteral("Flip X"));
    flip_x_->setCheckable(true);
    flip_x_->setAccessibleName(QStringLiteral("Flip tile brush horizontally"));
    toolbar->addWidget(flip_x_);
    flip_y_ = new QToolButton{toolbar};
    flip_y_->setObjectName(QStringLiteral("TileBrushFlipY"));
    flip_y_->setText(QStringLiteral("Flip Y"));
    flip_y_->setCheckable(true);
    flip_y_->setAccessibleName(QStringLiteral("Flip tile brush vertically"));
    toolbar->addWidget(flip_y_);
    rotate_ = new QToolButton{toolbar};
    rotate_->setObjectName(QStringLiteral("TileBrushRotate"));
    rotate_->setText(QStringLiteral("Rotate 0 deg"));
    rotate_->setAccessibleName(QStringLiteral("Rotate tile brush clockwise by 90 degrees"));
    toolbar->addWidget(rotate_);
    toolbar->addSeparator();
    auto* undo = toolbar->addAction(QStringLiteral("Undo"));
    undo->setObjectName(QStringLiteral("TileUndo"));
    connect(undo, &QAction::triggered, service_, [service] { static_cast<void>(service->undo()); });
    auto* redo = toolbar->addAction(QStringLiteral("Redo"));
    redo->setObjectName(QStringLiteral("TileRedo"));
    connect(redo, &QAction::triggered, service_, [service] { static_cast<void>(service->redo()); });
    auto* save = toolbar->addAction(QStringLiteral("Save"));
    save->setObjectName(QStringLiteral("TileSave"));
    connect(save, &QAction::triggered, service_, [service] { static_cast<void>(service->save()); });
    layout->addWidget(toolbar);

    auto* controls = new QHBoxLayout;
    controls->addWidget(new QLabel{QStringLiteral("Layer"), this});
    layers_ = new QComboBox{this};
    layers_->setObjectName(QStringLiteral("TileLayer"));
    layers_->setAccessibleName(QStringLiteral("Tilemap layer"));
    controls->addWidget(layers_, 1);
    layer_visible_ = new QToolButton{this};
    layer_visible_->setObjectName(QStringLiteral("TileLayerVisible"));
    layer_visible_->setText(QStringLiteral("Visible"));
    layer_visible_->setCheckable(true);
    layer_visible_->setAccessibleName(QStringLiteral("Show selected Tilemap layer"));
    controls->addWidget(layer_visible_);
    auto* layer_add = new QToolButton{this};
    layer_add->setObjectName(QStringLiteral("TileLayerAdd"));
    layer_add->setText(QStringLiteral("Add"));
    layer_add->setAccessibleName(QStringLiteral("Add Tilemap layer"));
    controls->addWidget(layer_add);
    auto* layer_rename = new QToolButton{this};
    layer_rename->setObjectName(QStringLiteral("TileLayerRename"));
    layer_rename->setText(QStringLiteral("Rename"));
    layer_rename->setAccessibleName(QStringLiteral("Rename selected Tilemap layer"));
    controls->addWidget(layer_rename);
    layer_up_ = new QToolButton{this};
    layer_up_->setObjectName(QStringLiteral("TileLayerUp"));
    layer_up_->setText(QStringLiteral("Up"));
    layer_up_->setAccessibleName(QStringLiteral("Move selected Tilemap layer up"));
    controls->addWidget(layer_up_);
    layer_down_ = new QToolButton{this};
    layer_down_->setObjectName(QStringLiteral("TileLayerDown"));
    layer_down_->setText(QStringLiteral("Down"));
    layer_down_->setAccessibleName(QStringLiteral("Move selected Tilemap layer down"));
    controls->addWidget(layer_down_);
    layer_remove_ = new QToolButton{this};
    layer_remove_->setObjectName(QStringLiteral("TileLayerRemove"));
    layer_remove_->setText(QStringLiteral("Remove"));
    layer_remove_->setAccessibleName(QStringLiteral("Remove selected Tilemap layer"));
    controls->addWidget(layer_remove_);
    controls->addWidget(new QLabel{QStringLiteral("Zoom"), this});
    zoom_ = new QSlider{Qt::Horizontal, this};
    zoom_->setObjectName(QStringLiteral("TileZoom"));
    zoom_->setRange(50, 400);
    zoom_->setValue(100);
    zoom_->setAccessibleName(QStringLiteral("Tile palette zoom"));
    controls->addWidget(zoom_);
    layout->addLayout(controls);

    auto* splitter = new QSplitter{Qt::Horizontal, this};
    tiles_ = new QListWidget{splitter};
    tiles_->setObjectName(QStringLiteral("TileList"));
    tiles_->setAccessibleName(QStringLiteral("Tiles in selected TileSet"));
    tiles_->setViewMode(QListView::IconMode);
    tiles_->setIconSize(QSize{40, 40});
    tiles_->setResizeMode(QListView::Adjust);
    tiles_->setMinimumWidth(150);
    canvas_ = new TileCanvas{service_, splitter};
    splitter->addWidget(tiles_);
    splitter->addWidget(canvas_);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);
    status_ = new QLabel{QStringLiteral("Open a tilemap asset from Project Explorer."), this};
    status_->setObjectName(QStringLiteral("TileStatus"));
    status_->setAccessibleName(QStringLiteral("Tile authoring status"));
    layout->addWidget(status_);

    connect(layers_, &QComboBox::currentIndexChanged, this, [this](int index) {
        canvas_->set_layer(index);
        update_layer_controls();
        emit authoringStateChanged();
    });
    connect(zoom_, &QSlider::valueChanged, this, [this](int value) { canvas_->set_zoom(value / 100.0); });
    connect(tiles_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current) {
        const auto id = current ? dragonpixel::core::uuid::parse(current->data(Qt::UserRole).toString().toStdString()) : std::nullopt;
        if (id)
        {
            canvas_->set_selected_brush(TileDocumentService::Brush{
                *id, flip_x_->isChecked(), flip_y_->isChecked(), brush_rotation_});
        }
        else
        {
            canvas_->set_selected_brush(std::nullopt);
        }
        emit authoringStateChanged();
    });
    connect(canvas_, &TileCanvas::brushPicked, this,
        [this](const QString& id, bool flip_x, bool flip_y, int rotation) {
        const auto tile_id = dragonpixel::core::uuid::parse(id.toStdString());
        if (tile_id) select_brush(TileDocumentService::Brush{
            *tile_id, flip_x, flip_y,
            static_cast<unsigned>(std::clamp(rotation, 0, 3))});
    });
    connect(canvas_, &TileCanvas::selectionChanged, this, [this](int x, int y) {
        status_->setText(QStringLiteral("Selected cell (%1, %2)%3")
            .arg(x).arg(y).arg(service_->is_dirty() ? QStringLiteral("  ·  Unsaved") : QString{}));
    });
    connect(service_, &TileDocumentService::documentChanged, this, &TilePaletteWidget::rebuild);
    connect(service_, &TileDocumentService::dirtyChanged, this, [this](bool dirty) {
        if (service_->is_loaded())
        {
            status_->setText(QStringLiteral("%1%2")
                .arg(service_->tilemap() ? QString::fromStdString(service_->tilemap()->name) : QStringLiteral("Tilemap"),
                     dirty ? QStringLiteral("  ·  Unsaved") : QStringLiteral("  ·  Saved")));
        }
    });
    connect(service_, &TileDocumentService::diagnostic, status_, &QLabel::setText);

    connect(flip_x_, &QToolButton::toggled, this, [this] { update_brush(); });
    connect(flip_y_, &QToolButton::toggled, this, [this] { update_brush(); });
    connect(rotate_, &QToolButton::clicked, this, [this] {
        brush_rotation_ = (brush_rotation_ + 1U) % 4U;
        rotate_->setText(QStringLiteral("Rotate %1 deg").arg(brush_rotation_ * 90U));
        update_brush();
    });
    connect(layer_visible_, &QToolButton::toggled, this, [this](bool visible) {
        if (!rebuilding_)
        {
            static_cast<void>(service_->set_layer_visible(layers_->currentIndex(), visible));
        }
    });
    connect(layer_add, &QToolButton::clicked, this, [this] {
        const auto suggested = QStringLiteral("Layer %1").arg(layers_->count() + 1);
        bool accepted = false;
        const auto name = QInputDialog::getText(this, QStringLiteral("Add Tilemap Layer"),
            QStringLiteral("Layer name"), QLineEdit::Normal, suggested, &accepted);
        if (accepted && service_->add_layer(name))
        {
            layers_->setCurrentIndex(layers_->count() - 1);
        }
    });
    connect(layer_rename, &QToolButton::clicked, this, [this] {
        if (layers_->currentIndex() < 0) return;
        bool accepted = false;
        const auto name = QInputDialog::getText(this, QStringLiteral("Rename Tilemap Layer"),
            QStringLiteral("Layer name"), QLineEdit::Normal,
            layers_->currentText(), &accepted);
        if (accepted)
        {
            static_cast<void>(service_->rename_layer(layers_->currentIndex(), name));
        }
    });
    connect(layer_up_, &QToolButton::clicked, this, [this] {
        const auto current = layers_->currentIndex();
        if (service_->move_layer(current, current - 1))
        {
            layers_->setCurrentIndex(current - 1);
        }
    });
    connect(layer_down_, &QToolButton::clicked, this, [this] {
        const auto current = layers_->currentIndex();
        if (service_->move_layer(current, current + 1))
        {
            layers_->setCurrentIndex(current + 1);
        }
    });
    connect(layer_remove_, &QToolButton::clicked, this, [this] {
        const auto current = layers_->currentIndex();
        const auto* map = service_->tilemap();
        if (map == nullptr || current < 0
            || current >= static_cast<int>(map->layers.size())) return;
        const auto& layer = map->layers.at(static_cast<std::size_t>(current));
        if (!layer.chunks.empty()
            && QMessageBox::question(this, QStringLiteral("Remove Tilemap Layer"),
                QStringLiteral("Remove '%1' and its painted cells? Undo can restore it.")
                    .arg(QString::fromStdString(layer.name)),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
                != QMessageBox::Yes)
        {
            return;
        }
        static_cast<void>(service_->remove_layer(current));
    });
}

bool TilePaletteWidget::load_documents(
    const QString& tilemap_path,
    const QString& tileset_path,
    const QString& texture_path)
{
    texture_path_ = texture_path;
    atlas_ = texture_path_.isEmpty() ? QImage{} : QImage{texture_path_};
    canvas_->set_atlas(atlas_);
    const auto loaded = service_->load(tilemap_path, tileset_path);
    if (loaded) rebuild();
    return loaded;
}

TileCanvas::Tool TilePaletteWidget::active_tool() const noexcept
{
    return canvas_->tool();
}

int TilePaletteWidget::active_layer() const noexcept
{
    return layers_->currentIndex();
}

std::optional<TileDocumentService::Brush> TilePaletteWidget::active_brush() const
{
    const auto* item = tiles_->currentItem();
    if (item == nullptr) return std::nullopt;
    const auto tile_id = dragonpixel::core::uuid::parse(
        item->data(Qt::UserRole).toString().toStdString());
    if (!tile_id) return std::nullopt;
    return TileDocumentService::Brush{
        *tile_id, flip_x_->isChecked(), flip_y_->isChecked(), brush_rotation_ % 4U};
}

void TilePaletteWidget::select_brush(const TileDocumentService::Brush& brush)
{
    const auto id = QString::fromStdString(brush.tile_id.to_string());
    const QSignalBlocker tile_blocker{tiles_};
    for (int row = 0; row < tiles_->count(); ++row)
    {
        if (tiles_->item(row)->data(Qt::UserRole).toString() == id)
        {
            tiles_->setCurrentRow(row);
            break;
        }
    }
    const QSignalBlocker flip_x_blocker{flip_x_};
    const QSignalBlocker flip_y_blocker{flip_y_};
    flip_x_->setChecked(brush.flip_x);
    flip_y_->setChecked(brush.flip_y);
    brush_rotation_ = brush.rotation_quarter_turns % 4U;
    rotate_->setText(QStringLiteral("Rotate %1 deg").arg(brush_rotation_ * 90U));
    update_brush();
}

void TilePaletteWidget::update_brush()
{
    canvas_->set_selected_brush(active_brush());
    emit authoringStateChanged();
}

void TilePaletteWidget::update_layer_controls()
{
    const auto* map = service_->tilemap();
    const auto current = layers_->currentIndex();
    const auto valid = map != nullptr && current >= 0
        && current < static_cast<int>(map->layers.size());
    const QSignalBlocker visible_blocker{layer_visible_};
    layer_visible_->setEnabled(valid);
    layer_visible_->setChecked(valid
        && map->layers.at(static_cast<std::size_t>(current)).visible);
    layer_up_->setEnabled(valid && current > 0);
    layer_down_->setEnabled(valid && current + 1 < layers_->count());
    layer_remove_->setEnabled(valid && layers_->count() > 1);
}

void TilePaletteWidget::rebuild()
{
    const auto* map = service_->tilemap();
    const auto* set = service_->tileset();
    if (map == nullptr || set == nullptr) return;
    rebuilding_ = true;
    const auto selected_layer_id = layers_->currentData().toString();
    const auto selected_tile = tiles_->currentItem() ? tiles_->currentItem()->data(Qt::UserRole).toString() : QString{};
    const QSignalBlocker layer_blocker{layers_};
    const QSignalBlocker tile_blocker{tiles_};
    layers_->clear();
    for (const auto& layer : map->layers)
    {
        layers_->addItem(
            QString::fromStdString(layer.name),
            QString::fromStdString(layer.layer_id.to_string()));
    }
    const auto restored_layer = layers_->findData(selected_layer_id);
    layers_->setCurrentIndex(restored_layer >= 0 ? restored_layer : 0);
    canvas_->set_layer(layers_->currentIndex());
    tiles_->clear();
    for (const auto& tile : set->tiles)
    {
        QPixmap preview;
        const auto image = tile_image(atlas_, tile, false, false, 0U);
        if (!image.isNull())
        {
            preview = QPixmap::fromImage(image).scaled(
                48, 48, Qt::KeepAspectRatio, Qt::FastTransformation);
        }
        else
        {
            preview = QPixmap{48, 48};
            preview.fill(tile_color(tile.tile_id));
        }
        auto* item = new QListWidgetItem{QIcon{preview}, QString::fromStdString(tile.name), tiles_};
        item->setData(Qt::UserRole, QString::fromStdString(tile.tile_id.to_string()));
        item->setToolTip(QStringLiteral("%1\n%2")
            .arg(QString::fromStdString(tile.name), QString::fromStdString(tile.tile_id.to_string())));
        if (item->data(Qt::UserRole).toString() == selected_tile) tiles_->setCurrentItem(item);
    }
    if (tiles_->currentItem() == nullptr && tiles_->count() > 0) tiles_->setCurrentRow(0);
    rebuilding_ = false;
    update_brush();
    update_layer_controls();
    status_->setText(QStringLiteral("%1 | %2 layers | %3 tiles | Atlas %4%5")
        .arg(QString::fromStdString(map->name)).arg(map->layers.size()).arg(set->tiles.size())
        .arg(atlas_.isNull() ? QStringLiteral("unavailable") : QStringLiteral("ready"))
        .arg(service_->is_dirty() ? QStringLiteral(" | Unsaved") : QString{}));
    canvas_->update();
    emit authoringStateChanged();
}
