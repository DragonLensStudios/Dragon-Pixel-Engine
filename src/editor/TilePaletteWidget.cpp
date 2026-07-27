#include "TilePaletteWidget.h"

#include <QActionGroup>
#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QToolBar>
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
                auto rectangle = cell_rect(x, y).adjusted(2, 2, -2, -2);
                painter.fillRect(rectangle, tile_color(cell.tile_id));
                painter.setPen(QColor{30, 30, 30});
                painter.drawText(rectangle, Qt::AlignCenter,
                    tile == service_->tileset()->tiles.end() ? QStringLiteral("?") : QString::fromStdString(tile->name).left(3));
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
            if (selected_tile_) static_cast<void>(service_->paint_cell(layer_, cell.x(), cell.y(), *selected_tile_));
            break;
        case Tool::erase:
            static_cast<void>(service_->erase_cell(layer_, cell.x(), cell.y()));
            break;
        case Tool::rectangle:
            if (preview_rectangle && stroke_start_ && selected_tile_)
            {
                static_cast<void>(service_->preview_rectangle(layer_, stroke_start_->x(), stroke_start_->y(),
                    cell.x(), cell.y(), *selected_tile_, false));
            }
            break;
        case Tool::fill:
            if (selected_tile_) static_cast<void>(service_->flood_fill(layer_, cell.x(), cell.y(), *selected_tile_));
            break;
        case Tool::eyedropper:
            if (const auto tile = service_->tile_at(layer_, cell.x(), cell.y())) emit tilePicked(QString::fromStdString(tile->to_string()));
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
        connect(action, &QAction::triggered, this, [this, tool] { canvas_->set_tool(tool); });
        return action;
    };
    add_tool(QStringLiteral("Paint"), QStringLiteral("TilePaintTool"), TileCanvas::Tool::paint)->setChecked(true);
    add_tool(QStringLiteral("Erase"), QStringLiteral("TileEraseTool"), TileCanvas::Tool::erase);
    add_tool(QStringLiteral("Rect"), QStringLiteral("TileRectangleTool"), TileCanvas::Tool::rectangle);
    add_tool(QStringLiteral("Fill"), QStringLiteral("TileFillTool"), TileCanvas::Tool::fill);
    add_tool(QStringLiteral("Pick"), QStringLiteral("TileEyedropperTool"), TileCanvas::Tool::eyedropper);
    add_tool(QStringLiteral("Select"), QStringLiteral("TileSelectionTool"), TileCanvas::Tool::select);
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

    connect(layers_, &QComboBox::currentIndexChanged, canvas_, &TileCanvas::set_layer);
    connect(zoom_, &QSlider::valueChanged, this, [this](int value) { canvas_->set_zoom(value / 100.0); });
    connect(tiles_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current) {
        const auto id = current ? dragonpixel::core::uuid::parse(current->data(Qt::UserRole).toString().toStdString()) : std::nullopt;
        canvas_->set_selected_tile(id);
    });
    connect(canvas_, &TileCanvas::tilePicked, this, [this](const QString& id) {
        for (int row = 0; row < tiles_->count(); ++row)
        {
            if (tiles_->item(row)->data(Qt::UserRole).toString() == id)
            {
                tiles_->setCurrentRow(row);
                break;
            }
        }
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
}

bool TilePaletteWidget::load_documents(const QString& tilemap_path, const QString& tileset_path)
{
    const auto loaded = service_->load(tilemap_path, tileset_path);
    if (loaded) rebuild();
    return loaded;
}

void TilePaletteWidget::rebuild()
{
    const auto* map = service_->tilemap();
    const auto* set = service_->tileset();
    if (map == nullptr || set == nullptr) return;
    const auto selected_layer = layers_->currentIndex();
    const auto selected_tile = tiles_->currentItem() ? tiles_->currentItem()->data(Qt::UserRole).toString() : QString{};
    const QSignalBlocker layer_blocker{layers_};
    const QSignalBlocker tile_blocker{tiles_};
    layers_->clear();
    for (const auto& layer : map->layers) layers_->addItem(QString::fromStdString(layer.name));
    layers_->setCurrentIndex(std::clamp(selected_layer, 0, std::max(0, layers_->count() - 1)));
    canvas_->set_layer(layers_->currentIndex());
    tiles_->clear();
    for (const auto& tile : set->tiles)
    {
        QPixmap preview{40, 40};
        preview.fill(tile_color(tile.tile_id));
        auto* item = new QListWidgetItem{QIcon{preview}, QString::fromStdString(tile.name), tiles_};
        item->setData(Qt::UserRole, QString::fromStdString(tile.tile_id.to_string()));
        item->setToolTip(QStringLiteral("%1\n%2")
            .arg(QString::fromStdString(tile.name), QString::fromStdString(tile.tile_id.to_string())));
        if (item->data(Qt::UserRole).toString() == selected_tile) tiles_->setCurrentItem(item);
    }
    if (tiles_->currentItem() == nullptr && tiles_->count() > 0) tiles_->setCurrentRow(0);
    if (tiles_->currentItem())
    {
        canvas_->set_selected_tile(dragonpixel::core::uuid::parse(
            tiles_->currentItem()->data(Qt::UserRole).toString().toStdString()));
    }
    status_->setText(QStringLiteral("%1  ·  %2 layers  ·  %3 tiles%4")
        .arg(QString::fromStdString(map->name)).arg(map->layers.size()).arg(set->tiles.size())
        .arg(service_->is_dirty() ? QStringLiteral("  ·  Unsaved") : QString{}));
    canvas_->update();
}
