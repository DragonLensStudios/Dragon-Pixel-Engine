#include "TilePaletteWidget.h"

#include <dragonpixel/tiles/tile_grid.h>
#include <dragonpixel/tiles/tile_evaluator.h>

#include <nlohmann/json.hpp>

#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QMimeData>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QGroupBox>
#include <QPainter>
#include <QPushButton>
#include <QPolygonF>
#include <QInputDialog>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSettings>
#include <QSplitter>
#include <QToolBar>
#include <QToolButton>
#include <QTransform>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>

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
    const auto grid = service_->tilemap()
        ? service_->tilemap()->grid : dragonpixel::tiles::tile_grid_settings{};
    const auto cell = dragonpixel::tiles::unproject_cell(grid,
        {(position.x() - origin.x()) / size, (origin.y() - position.y()) / size});
    return {cell.x, cell.y};
}

std::optional<QRect> TileCanvas::selection() const
{
    if (!selection_start_ || !selection_end_) return std::nullopt;
    return QRect{*selection_start_, *selection_end_}.normalized();
}

QRect TileCanvas::cell_rect(int x, int y) const
{
    return cell_polygon(x, y).boundingRect().toAlignedRect();
}

QPolygonF TileCanvas::cell_polygon(int x, int y) const
{
    const auto size = 32.0 * zoom_;
    const auto origin = QPointF{width() / 2.0, height() / 2.0};
    const auto grid = service_->tilemap()
        ? service_->tilemap()->grid : dragonpixel::tiles::tile_grid_settings{};
    QPolygonF polygon;
    for (const auto& point : dragonpixel::tiles::grid_collision_polygon(grid, {x, y}))
        polygon.push_back({origin.x() + point.x * size, origin.y() - point.y * size});
    return polygon;
}

void TileCanvas::paintEvent(QPaintEvent*)
{
    QPainter painter{this};
    painter.fillRect(rect(), palette().color(QPalette::Dark));
    const auto size = 32.0 * zoom_;
    const auto extent = static_cast<int>(std::ceil(std::max(width(), height()) / size)) + 4;
    const auto min_x = -extent;
    const auto max_x = extent;
    const auto min_y = -extent;
    const auto max_y = extent;
    if (const auto* map = service_->tilemap(); map != nullptr && layer_ >= 0
        && layer_ < static_cast<int>(map->layers.size()))
    {
        for (const auto& chunk : map->layers.at(static_cast<std::size_t>(layer_)).chunks)
        {
            for (const auto& cell : chunk.cells)
            {
                const auto x = (chunk.x * 32) + static_cast<int>(cell.index % 32U);
                const auto y = (chunk.y * 32) + static_cast<int>(cell.index / 32U);
                const dragonpixel::tiles::tile_definition* tile = nullptr;
                const dragonpixel::tiles::tile_set_document* owner = nullptr;
                for (const auto& set : service_->tilesets())
                {
                    if (!cell.tile_set_id.is_nil() && set.asset_id != cell.tile_set_id) continue;
                    const auto found = std::find_if(set.tiles.begin(), set.tiles.end(),
                        [&](const auto& value) { return value.tile_id == cell.tile_id; });
                    if (found != set.tiles.end())
                    {
                        tile = &*found;
                        owner = &set;
                        break;
                    }
                }
                const auto rectangle = cell_rect(x, y).adjusted(1, 1, -1, -1);
                const auto owner_key = owner
                    ? QString::fromStdString(owner->asset_id.to_string()) : QString{};
                const auto owner_atlas = atlases_.value(owner_key, atlas_);
                const auto image = tile == nullptr
                    ? QImage{}
                    : tile_image(owner_atlas, *tile, cell.flip_x, cell.flip_y,
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
                        tile == nullptr
                            ? QStringLiteral("?")
                            : QString::fromStdString(tile->name).left(3));
                }
            }
        }
    }
    painter.setPen(QPen{palette().color(QPalette::Mid), 1.0});
    for (int x = min_x; x <= max_x; ++x)
        for (int y = min_y; y <= max_y; ++y) painter.drawPolygon(cell_polygon(x, y));
    painter.setPen(QPen{QColor{80, 180, 255}, 2.0});
    if (selected_cell_) painter.drawRect(cell_rect(selected_cell_->x(), selected_cell_->y()).adjusted(1, 1, -1, -1));
    if (selection_start_ && selection_end_)
    {
        const auto top_left = cell_rect(std::min(selection_start_->x(), selection_end_->x()),
            std::max(selection_start_->y(), selection_end_->y())).topLeft();
        const auto bottom_right = cell_rect(std::max(selection_start_->x(), selection_end_->x()),
            std::min(selection_start_->y(), selection_end_->y())).bottomRight();
        painter.setBrush(QColor{80, 180, 255, 35});
        painter.drawRect(QRect{top_left, bottom_right}.normalized());
    }
}

void TileCanvas::apply_at(const QPoint& cell, bool preview_rectangle)
{
    if (!service_->is_loaded()) return;
    switch (tool_)
    {
        case Tool::paint:
            if (pattern_provider_)
            {
                for (const auto& [target, brush] : pattern_provider_(cell.x(), cell.y()))
                    static_cast<void>(service_->paint_cell(layer_, target.x(), target.y(), brush));
            }
            else if (const auto brush = brush_provider_ ? brush_provider_(cell.x(), cell.y()) : selected_brush_)
            {
                static_cast<void>(service_->paint_cell(layer_, cell.x(), cell.y(), *brush));
            }
            break;
        case Tool::erase:
            static_cast<void>(service_->erase_cell(layer_, cell.x(), cell.y()));
            break;
        case Tool::rectangle:
            if (preview_rectangle && stroke_start_ && selected_brush_)
            {
                static_cast<void>(service_->preview_rectangle(layer_, stroke_start_->x(), stroke_start_->y(),
                    cell.x(), cell.y(), *selected_brush_));
            }
            break;
        case Tool::line:
            if (preview_rectangle && stroke_start_ && selected_brush_)
                static_cast<void>(service_->preview_line(layer_, stroke_start_->x(), stroke_start_->y(),
                    cell.x(), cell.y(), *selected_brush_));
            break;
        case Tool::fill:
            if (selected_brush_) static_cast<void>(service_->flood_fill(
                layer_, cell.x(), cell.y(), *selected_brush_));
            break;
        case Tool::eyedropper:
            if (const auto brush = service_->brush_at(layer_, cell.x(), cell.y()))
            {
                emit brushPicked(QString::fromStdString(brush->tile_set_id.to_string()),
                    QString::fromStdString(brush->tile_id.to_string()),
                    brush->flip_x, brush->flip_y,
                    static_cast<int>(brush->rotation_quarter_turns));
            }
            break;
        case Tool::select:
            selection_start_ = stroke_start_.value_or(cell);
            selection_end_ = cell;
            selected_cell_ = cell;
            emit selectionChanged(cell.x(), cell.y());
            update();
            break;
        case Tool::move:
            selected_cell_ = cell;
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
    if (tool_ == Tool::paint || tool_ == Tool::erase || tool_ == Tool::rectangle
        || tool_ == Tool::line || tool_ == Tool::fill)
    {
        service_->begin_stroke();
    }
    apply_at(cell, true);
}

void TileCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & Qt::LeftButton) || !stroke_start_) return;
    const auto cell = cell_at(event->position().toPoint());
    if (tool_ == Tool::rectangle || tool_ == Tool::line)
    {
        apply_at(cell, true);
    }
    else if (tool_ == Tool::select)
    {
        apply_at(cell, false);
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
    if (tool_ == Tool::paint || tool_ == Tool::erase || tool_ == Tool::rectangle
        || tool_ == Tool::line || tool_ == Tool::fill)
    {
        service_->commit_stroke();
    }
    else if (tool_ == Tool::move && selection_start_ && selection_end_)
    {
        const auto destination = cell_at(event->position().toPoint());
        const auto delta = destination - *stroke_start_;
        if (service_->move_selection(layer_, selection_start_->x(), selection_start_->y(),
                selection_end_->x(), selection_end_->y(), delta.x(), delta.y()))
        {
            *selection_start_ += delta;
            *selection_end_ += delta;
        }
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
    if (selection_start_ && selection_end_ && event->key() == Qt::Key_Delete)
    {
        static_cast<void>(service_->delete_selection(layer_, selection_start_->x(), selection_start_->y(),
            selection_end_->x(), selection_end_->y()));
        event->accept();
        return;
    }
    const auto delta = event->key() == Qt::Key_Left ? QPoint{-1, 0}
        : event->key() == Qt::Key_Right ? QPoint{1, 0}
        : event->key() == Qt::Key_Up ? QPoint{0, 1}
        : event->key() == Qt::Key_Down ? QPoint{0, -1} : QPoint{};
    if (selection_start_ && selection_end_ && !delta.isNull()
        && service_->move_selection(layer_, selection_start_->x(), selection_start_->y(),
            selection_end_->x(), selection_end_->y(), delta.x(), delta.y()))
    {
        *selection_start_ += delta;
        *selection_end_ += delta;
        event->accept();
        update();
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
    auto* paint_action = configure_tool(add_tool(QStringLiteral("Paint"), QStringLiteral("TilePaintTool"), TileCanvas::Tool::paint),
        QStringLiteral("1"));
    paint_action->setChecked(true);
    auto* erase_action = configure_tool(add_tool(QStringLiteral("Erase"), QStringLiteral("TileEraseTool"), TileCanvas::Tool::erase),
        QStringLiteral("2"));
    auto* box_action = configure_tool(add_tool(QStringLiteral("Box"), QStringLiteral("TileRectangleTool"), TileCanvas::Tool::rectangle),
        QStringLiteral("3"));
    auto* line_action = configure_tool(add_tool(QStringLiteral("Line"), QStringLiteral("TileLineTool"), TileCanvas::Tool::line),
        QStringLiteral("4"));
    auto* fill_action = configure_tool(add_tool(QStringLiteral("Flood"), QStringLiteral("TileFillTool"), TileCanvas::Tool::fill),
        QStringLiteral("5"));
    auto* pick_action = configure_tool(add_tool(QStringLiteral("Pick"), QStringLiteral("TileEyedropperTool"), TileCanvas::Tool::eyedropper),
        QStringLiteral("6"));
    auto* select_action = configure_tool(add_tool(QStringLiteral("Select"), QStringLiteral("TileSelectionTool"), TileCanvas::Tool::select),
        QStringLiteral("7"));
    auto* move_action = configure_tool(add_tool(QStringLiteral("Move"), QStringLiteral("TileMoveTool"), TileCanvas::Tool::move),
        QStringLiteral("8"));
    auto* shortcut_profile = new QComboBox{toolbar};
    shortcut_profile->setObjectName(QStringLiteral("TileShortcutProfile"));
    shortcut_profile->setAccessibleName(QStringLiteral("Tile tool shortcut profile"));
    shortcut_profile->addItem(QStringLiteral("Familiar letters"), QStringLiteral("letters"));
    shortcut_profile->addItem(QStringLiteral("Legacy numbers"), QStringLiteral("numbers"));
    toolbar->addWidget(shortcut_profile);
    const auto apply_shortcuts = [paint_action, erase_action, box_action, line_action, fill_action,
                                     pick_action, select_action, move_action](const QString& profile) {
        const QStringList keys = profile == QStringLiteral("numbers")
            ? QStringList{QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"),
                  QStringLiteral("4"), QStringLiteral("5"), QStringLiteral("6"),
                  QStringLiteral("7"), QStringLiteral("8")}
            : QStringList{QStringLiteral("P"), QStringLiteral("E"), QStringLiteral("B"),
                  QStringLiteral("L"), QStringLiteral("F"), QStringLiteral("I"),
                  QStringLiteral("S"), QStringLiteral("M")};
        const std::array actions{paint_action, erase_action, box_action, line_action, fill_action,
            pick_action, select_action, move_action};
        for (std::size_t index = 0; index < actions.size(); ++index)
        {
            actions[index]->setShortcut(QKeySequence{keys.at(static_cast<qsizetype>(index))});
            actions[index]->setToolTip(QStringLiteral("%1 (%2)")
                .arg(actions[index]->text(), keys.at(static_cast<qsizetype>(index))));
        }
    };
    QSettings tile_settings;
    const auto saved_profile = tile_settings.value(
        QStringLiteral("tiles/shortcutProfile"), QStringLiteral("letters")).toString();
    shortcut_profile->setCurrentIndex(saved_profile == QStringLiteral("numbers") ? 1 : 0);
    apply_shortcuts(shortcut_profile->currentData().toString());
    connect(shortcut_profile, &QComboBox::currentIndexChanged, this,
        [shortcut_profile, apply_shortcuts](int) {
            const auto profile = shortcut_profile->currentData().toString();
            apply_shortcuts(profile);
            QSettings{}.setValue(QStringLiteral("tiles/shortcutProfile"), profile);
        });
    auto* reset_shortcuts = new QToolButton{toolbar};
    reset_shortcuts->setObjectName(QStringLiteral("ResetTileShortcuts"));
    reset_shortcuts->setText(QStringLiteral("Reset Keys"));
    reset_shortcuts->setAccessibleName(QStringLiteral("Reset Tile tool shortcuts"));
    toolbar->addWidget(reset_shortcuts);
    connect(reset_shortcuts, &QToolButton::clicked, shortcut_profile,
        [shortcut_profile] { shortcut_profile->setCurrentIndex(0); });
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
    controls->addWidget(new QLabel{QStringLiteral("Active Palette"), this});
    palettes_ = new QComboBox{this};
    palettes_->setObjectName(QStringLiteral("ActiveTilePalette"));
    palettes_->setAccessibleName(QStringLiteral("Active Tile Palette"));
    palettes_->setToolTip(QStringLiteral("Selects the durable logical Tile Palette used by the brush."));
    controls->addWidget(palettes_, 1);
    controls->addWidget(new QLabel{QStringLiteral("Brush"), this});
    brush_behavior_ = new QComboBox{this};
    brush_behavior_->setObjectName(QStringLiteral("TileBrushBehavior"));
    brush_behavior_->setAccessibleName(QStringLiteral("Tile brush behavior"));
    brush_behavior_->addItem(QStringLiteral("Basic"), QStringLiteral("basic"));
    brush_behavior_->addItem(QStringLiteral("Random Selection"), QStringLiteral("random"));
    brush_behavior_->addItem(QStringLiteral("Group Stamp"), QStringLiteral("group"));
    brush_behavior_->addItem(QStringLiteral("GameObject"), QStringLiteral("object"));
    brush_behavior_->setToolTip(QStringLiteral(
        "Random Selection chooses deterministically; Group Stamp preserves selected palette offsets; "
        "GameObject places a dropped prefab or copied scene-object subtree."));
    controls->addWidget(brush_behavior_);
    controls->addWidget(new QLabel{QStringLiteral("Active Target"), this});
    layers_ = new QComboBox{this};
    layers_->setObjectName(QStringLiteral("TileLayer"));
    layers_->setAccessibleName(QStringLiteral("Active Tilemap target layer"));
    controls->addWidget(layers_, 1);
    target_pin_ = new QToolButton{this};
    target_pin_->setObjectName(QStringLiteral("PinActiveTilemapTarget"));
    target_pin_->setText(QStringLiteral("Pin"));
    target_pin_->setCheckable(true);
    target_pin_->setAccessibleName(QStringLiteral("Pin active Tilemap2D GameObject target"));
    target_pin_->setToolTip(QStringLiteral(
        "Keeps painting on the current Tilemap2D GameObject while another Hierarchy object is selected."));
    controls->addWidget(target_pin_);
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

    auto* palette_controls = new QHBoxLayout;
    auto* add_loaded_tiles = new QToolButton{this};
    add_loaded_tiles->setObjectName(QStringLiteral("AddLoadedTilesToPalette"));
    add_loaded_tiles->setText(QStringLiteral("Add Loaded Tiles"));
    add_loaded_tiles->setAccessibleName(QStringLiteral("Add all loaded TileSet tiles to the active palette"));
    palette_controls->addWidget(add_loaded_tiles);
    auto* remove_palette_tile = new QToolButton{this};
    remove_palette_tile->setObjectName(QStringLiteral("RemoveTileFromPalette"));
    remove_palette_tile->setText(QStringLiteral("Remove Palette Tile"));
    remove_palette_tile->setAccessibleName(QStringLiteral("Remove selected tile from the active palette"));
    palette_controls->addWidget(remove_palette_tile);
    palette_controls->addStretch(1);
    layout->addLayout(palette_controls);

    auto* brush_inspector = new QGroupBox{QStringLiteral("Brush Inspector"), this};
    brush_inspector->setObjectName(QStringLiteral("TileBrushInspector"));
    brush_inspector->setAccessibleName(QStringLiteral("Tile Brush Inspector"));
    brush_inspector->setCheckable(true);
    brush_inspector->setChecked(false);
    auto* brush_form = new QFormLayout{brush_inspector};
    brush_tint_ = new QLineEdit{QStringLiteral("#FFFFFFFF"), brush_inspector};
    brush_tint_->setObjectName(QStringLiteral("TileBrushTint"));
    brush_tint_->setAccessibleName(QStringLiteral("Brush tint in hexadecimal RGBA"));
    brush_form->addRow(QStringLiteral("Tint"), brush_tint_);
    const auto add_double = [brush_inspector, brush_form](const QString& label,
                                const QString& name, double minimum, double maximum, double value) {
        auto* field = new QDoubleSpinBox{brush_inspector};
        field->setObjectName(name);
        field->setRange(minimum, maximum);
        field->setDecimals(3);
        field->setValue(value);
        brush_form->addRow(label, field);
        return field;
    };
    brush_offset_x_ = add_double(QStringLiteral("Offset X"), QStringLiteral("TileBrushOffsetX"), -1024.0, 1024.0, 0.0);
    brush_offset_y_ = add_double(QStringLiteral("Offset Y"), QStringLiteral("TileBrushOffsetY"), -1024.0, 1024.0, 0.0);
    brush_rotation_degrees_ = add_double(QStringLiteral("Rotation"), QStringLiteral("TileBrushRotationDegrees"), -36000.0, 36000.0, 0.0);
    brush_scale_x_ = add_double(QStringLiteral("Scale X"), QStringLiteral("TileBrushScaleX"), -1000.0, 1000.0, 1.0);
    brush_scale_y_ = add_double(QStringLiteral("Scale Y"), QStringLiteral("TileBrushScaleY"), -1000.0, 1000.0, 1.0);
    brush_elevation_ = new QSpinBox{brush_inspector};
    brush_elevation_->setObjectName(QStringLiteral("TileBrushElevation"));
    brush_elevation_->setRange(-1'000'000, 1'000'000);
    brush_form->addRow(QStringLiteral("Elevation"), brush_elevation_);
    group_gap_ = new QSpinBox{brush_inspector};
    group_gap_->setObjectName(QStringLiteral("TileGroupBrushGap"));
    group_gap_->setAccessibleName(QStringLiteral("Group Brush cell gap"));
    group_gap_->setRange(0, 64);
    brush_form->addRow(QStringLiteral("Group gap"), group_gap_);
    group_limit_ = new QSpinBox{brush_inspector};
    group_limit_->setObjectName(QStringLiteral("TileGroupBrushLimit"));
    group_limit_->setAccessibleName(QStringLiteral("Group Brush cell limit"));
    group_limit_->setRange(1, 4096);
    group_limit_->setValue(256);
    brush_form->addRow(QStringLiteral("Group limit"), group_limit_);
    brush_lock_color_ = new QCheckBox{QStringLiteral("Lock color"), brush_inspector};
    brush_lock_color_->setObjectName(QStringLiteral("TileBrushLockColor"));
    brush_form->addRow(QString{}, brush_lock_color_);
    brush_lock_transform_ = new QCheckBox{QStringLiteral("Lock transform"), brush_inspector};
    brush_lock_transform_->setObjectName(QStringLiteral("TileBrushLockTransform"));
    brush_form->addRow(QString{}, brush_lock_transform_);
    auto* apply_selection = new QPushButton{QStringLiteral("Apply to Grid Selection"), brush_inspector};
    apply_selection->setObjectName(QStringLiteral("ApplyBrushInspectorToSelection"));
    apply_selection->setAccessibleName(QStringLiteral("Apply Brush Inspector values to selected Tilemap cells"));
    brush_form->addRow(QString{}, apply_selection);
    layout->addWidget(brush_inspector);

    auto* tile_editor = new QGroupBox{QStringLiteral("Tile Definition Editor"), this};
    tile_editor->setObjectName(QStringLiteral("TileDefinitionEditor"));
    tile_editor->setAccessibleName(QStringLiteral("Selected TileSet tile definition editor"));
    tile_editor->setCheckable(true);
    tile_editor->setChecked(false);
    auto* tile_form = new QFormLayout{tile_editor};
    tile_name_ = new QLineEdit{tile_editor};
    tile_name_->setObjectName(QStringLiteral("TileDefinitionName"));
    tile_name_->setMaxLength(256);
    tile_form->addRow(QStringLiteral("Name"), tile_name_);
    tile_kind_ = new QComboBox{tile_editor};
    tile_kind_->setObjectName(QStringLiteral("TileDefinitionKind"));
    tile_kind_->addItem(QStringLiteral("Basic"), static_cast<int>(dragonpixel::tiles::tile_kind::basic));
    tile_kind_->addItem(QStringLiteral("Animated"), static_cast<int>(dragonpixel::tiles::tile_kind::animated));
    tile_kind_->addItem(QStringLiteral("Rule"), static_cast<int>(dragonpixel::tiles::tile_kind::rule));
    tile_kind_->addItem(QStringLiteral("Rule Override"), static_cast<int>(dragonpixel::tiles::tile_kind::rule_override));
    tile_kind_->addItem(QStringLiteral("Custom"), static_cast<int>(dragonpixel::tiles::tile_kind::custom));
    tile_form->addRow(QStringLiteral("Kind"), tile_kind_);
    tile_collider_ = new QComboBox{tile_editor};
    tile_collider_->setObjectName(QStringLiteral("TileDefinitionCollider"));
    tile_collider_->addItem(QStringLiteral("None"), static_cast<int>(dragonpixel::tiles::tile_collider_mode::none));
    tile_collider_->addItem(QStringLiteral("Grid"), static_cast<int>(dragonpixel::tiles::tile_collider_mode::grid));
    tile_collider_->addItem(QStringLiteral("Sprite Outline"), static_cast<int>(dragonpixel::tiles::tile_collider_mode::sprite_outline));
    tile_form->addRow(QStringLiteral("Collider"), tile_collider_);
    const auto tile_speed = [tile_editor, tile_form](const QString& label, const QString& name,
                                double minimum, double maximum, double value) {
        auto* field = new QDoubleSpinBox{tile_editor};
        field->setObjectName(name);
        field->setRange(minimum, maximum);
        field->setDecimals(4);
        field->setValue(value);
        tile_form->addRow(label, field);
        return field;
    };
    tile_minimum_speed_ = tile_speed(QStringLiteral("Minimum speed"),
        QStringLiteral("TileDefinitionMinimumSpeed"), 0.0001, 1000.0, 1.0);
    tile_maximum_speed_ = tile_speed(QStringLiteral("Maximum speed"),
        QStringLiteral("TileDefinitionMaximumSpeed"), 0.0001, 1000.0, 1.0);
    tile_start_time_ = tile_speed(QStringLiteral("Start time"),
        QStringLiteral("TileDefinitionStartTime"), 0.0, 1'000'000.0, 0.0);
    tile_start_frame_ = new QSpinBox{tile_editor};
    tile_start_frame_->setObjectName(QStringLiteral("TileDefinitionStartFrame"));
    tile_start_frame_->setRange(0, 1'000'000);
    tile_form->addRow(QStringLiteral("Start frame"), tile_start_frame_);
    tile_loop_once_ = new QCheckBox{QStringLiteral("Play once"), tile_editor};
    tile_loop_once_->setObjectName(QStringLiteral("TileDefinitionLoopOnce"));
    tile_form->addRow(QString{}, tile_loop_once_);
    tile_paused_ = new QCheckBox{QStringLiteral("Paused"), tile_editor};
    tile_paused_->setObjectName(QStringLiteral("TileDefinitionPaused"));
    tile_form->addRow(QString{}, tile_paused_);
    tile_update_physics_ = new QCheckBox{QStringLiteral("Refresh physics on frame change"), tile_editor};
    tile_update_physics_->setObjectName(QStringLiteral("TileDefinitionUpdatePhysics"));
    tile_form->addRow(QString{}, tile_update_physics_);
    tile_custom_type_ = new QLineEdit{tile_editor};
    tile_custom_type_->setObjectName(QStringLiteral("TileDefinitionCustomType"));
    tile_form->addRow(QStringLiteral("Custom type ID"), tile_custom_type_);
    tile_custom_payload_ = new QLineEdit{QStringLiteral("{}"), tile_editor};
    tile_custom_payload_->setObjectName(QStringLiteral("TileDefinitionCustomPayload"));
    tile_form->addRow(QStringLiteral("Custom JSON"), tile_custom_payload_);
    auto* apply_tile_definition = new QPushButton{
        QStringLiteral("Apply Tile Definition"), tile_editor};
    apply_tile_definition->setObjectName(QStringLiteral("ApplyTileDefinition"));
    apply_tile_definition->setAccessibleName(QStringLiteral(
        "Apply selected tile definition through Tile workspace Undo"));
    tile_form->addRow(QString{}, apply_tile_definition);
    layout->addWidget(tile_editor);

    auto* splitter = new QSplitter{Qt::Horizontal, this};
    tiles_ = new QListWidget{splitter};
    tiles_->setObjectName(QStringLiteral("TileList"));
    tiles_->setAccessibleName(QStringLiteral("Tiles in selected TileSet"));
    tiles_->setViewMode(QListView::IconMode);
    tiles_->setIconSize(QSize{40, 40});
    tiles_->setResizeMode(QListView::Adjust);
    tiles_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tiles_->setMinimumWidth(150);
    tiles_->setAcceptDrops(true);
    tiles_->viewport()->setAcceptDrops(true);
    tiles_->viewport()->installEventFilter(this);
    canvas_ = new TileCanvas{service_, splitter};
    canvas_->set_brush_provider([this](int x, int y) { return active_brush_at(x, y); });
    canvas_->set_pattern_provider([this](int x, int y) { return active_brush_pattern_at(x, y); });
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
    connect(target_pin_, &QToolButton::toggled, this, [this](bool pinned) {
        target_pin_->setText(pinned ? QStringLiteral("Pinned") : QStringLiteral("Pin"));
        emit authoringStateChanged();
    });
    connect(brush_behavior_, &QComboBox::currentIndexChanged, this,
        [this] { update_brush(); });
    connect(tiles_, &QListWidget::itemSelectionChanged, this, [this] {
        if (brush_behavior_->currentData().toString() == QStringLiteral("random"))
            update_brush();
    });
    connect(zoom_, &QSlider::valueChanged, this, [this](int value) { canvas_->set_zoom(value / 100.0); });
    connect(tiles_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current) {
        const auto id = current ? dragonpixel::core::uuid::parse(current->data(Qt::UserRole).toString().toStdString()) : std::nullopt;
        const auto set_id = current ? dragonpixel::core::uuid::parse(
            current->data(Qt::UserRole + 1).toString().toStdString()) : std::nullopt;
        if (id && set_id)
        {
            const QSignalBlocker flip_x_blocker{flip_x_};
            const QSignalBlocker flip_y_blocker{flip_y_};
            flip_x_->setChecked(current->data(Qt::UserRole + 2).toBool());
            flip_y_->setChecked(current->data(Qt::UserRole + 3).toBool());
            brush_rotation_ = current->data(Qt::UserRole + 4).toUInt() % 4U;
            rotate_->setText(QStringLiteral("Rotate %1 deg").arg(brush_rotation_ * 90U));
            canvas_->set_selected_brush(TileDocumentService::Brush{
                *id, flip_x_->isChecked(), flip_y_->isChecked(), brush_rotation_, *set_id});
        }
        else
        {
            canvas_->set_selected_brush(std::nullopt);
        }
        update_tile_editor();
        emit authoringStateChanged();
    });
    connect(canvas_, &TileCanvas::brushPicked, this,
        [this](const QString& set, const QString& id, bool flip_x, bool flip_y, int rotation) {
        const auto tile_id = dragonpixel::core::uuid::parse(id.toStdString());
        const auto tile_set_id = dragonpixel::core::uuid::parse(set.toStdString());
        if (tile_id && tile_set_id) select_brush(TileDocumentService::Brush{
            *tile_id, flip_x, flip_y,
            static_cast<unsigned>(std::clamp(rotation, 0, 3)), *tile_set_id});
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
    connect(apply_selection, &QPushButton::clicked, this, [this] {
        const auto selected = canvas_->selection();
        const auto brush = active_brush();
        if (selected && brush)
            static_cast<void>(service_->edit_selection(active_layer(), selected->left(), selected->top(),
                selected->right(), selected->bottom(), *brush));
    });
    connect(brush_tint_, &QLineEdit::editingFinished, this, &TilePaletteWidget::update_brush);
    for (auto* field : {brush_offset_x_, brush_offset_y_, brush_rotation_degrees_,
             brush_scale_x_, brush_scale_y_})
        connect(field, &QDoubleSpinBox::valueChanged, this, &TilePaletteWidget::update_brush);
    connect(brush_elevation_, &QSpinBox::valueChanged, this, &TilePaletteWidget::update_brush);
    connect(group_gap_, &QSpinBox::valueChanged, this, &TilePaletteWidget::update_brush);
    connect(group_limit_, &QSpinBox::valueChanged, this, &TilePaletteWidget::update_brush);
    connect(brush_lock_color_, &QCheckBox::toggled, this, &TilePaletteWidget::update_brush);
    connect(brush_lock_transform_, &QCheckBox::toggled, this, &TilePaletteWidget::update_brush);
    connect(apply_tile_definition, &QPushButton::clicked, this, [this] {
        const auto* item = tiles_->currentItem();
        const auto tile_id = item ? dragonpixel::core::uuid::parse(
            item->data(Qt::UserRole).toString().toStdString()) : std::nullopt;
        const auto set_id = item ? dragonpixel::core::uuid::parse(
            item->data(Qt::UserRole + 1).toString().toStdString()) : std::nullopt;
        if (!tile_id || !set_id) return;
        const auto owner = std::find_if(service_->tilesets().begin(), service_->tilesets().end(),
            [&](const auto& set) { return set.asset_id == *set_id; });
        if (owner == service_->tilesets().end()) return;
        const auto existing = std::find_if(owner->tiles.begin(), owner->tiles.end(),
            [&](const auto& tile) { return tile.tile_id == *tile_id; });
        if (existing == owner->tiles.end()) return;
        auto edited = *existing;
        const auto name = tile_name_->text().trimmed();
        if (name.isEmpty())
        {
            status_->setText(QStringLiteral("Tile definition names cannot be empty."));
            return;
        }
        const auto payload = nlohmann::ordered_json::parse(
            tile_custom_payload_->text().trimmed().toStdString(), nullptr, false);
        if (payload.is_discarded())
        {
            status_->setText(QStringLiteral("Custom tile payload must be valid JSON."));
            return;
        }
        edited.name = name.toStdString();
        edited.kind = static_cast<dragonpixel::tiles::tile_kind>(tile_kind_->currentData().toInt());
        edited.collider_mode = static_cast<dragonpixel::tiles::tile_collider_mode>(
            tile_collider_->currentData().toInt());
        edited.minimum_speed = tile_minimum_speed_->value();
        edited.maximum_speed = tile_maximum_speed_->value();
        if (edited.minimum_speed > edited.maximum_speed)
        {
            status_->setText(QStringLiteral("Minimum animation speed cannot exceed maximum speed."));
            return;
        }
        edited.animation_start_time = tile_start_time_->value();
        edited.animation_start_frame = static_cast<unsigned>(tile_start_frame_->value());
        edited.loop_once = tile_loop_once_->isChecked();
        edited.pause_animation = tile_paused_->isChecked();
        edited.update_physics = tile_update_physics_->isChecked();
        edited.custom_type_id = tile_custom_type_->text().trimmed().toStdString();
        edited.opaque_payload_json = payload.dump();
        if (edited.kind == dragonpixel::tiles::tile_kind::animated
            && edited.animation_frames.empty())
        {
            edited.animation_frames.push_back({
                {edited.texture_asset_id.is_nil() ? owner->texture_asset_id : edited.texture_asset_id,
                    edited.source, edited.pivot},
                1.0 / 12.0});
        }
        if (edited.collider_mode == dragonpixel::tiles::tile_collider_mode::sprite_outline
            && edited.collision_outline.empty())
            edited.collision_outline = {{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}};
        if (!service_->update_tile_definition(*set_id, edited))
            status_->setText(service_->error().isEmpty()
                ? QStringLiteral("Tile definition did not change.") : service_->error());
    });

    connect(add_loaded_tiles, &QToolButton::clicked, this, [this] {
        if (service_->palette() == nullptr) return;
        int next = static_cast<int>(service_->palette()->cells.size());
        for (const auto& set : service_->tilesets())
            for (const auto& tile : set.tiles)
            {
                const auto exists = std::any_of(service_->palette()->cells.begin(), service_->palette()->cells.end(),
                    [&](const auto& cell) {
                        return cell.tile.tile_set_id == set.asset_id && cell.tile.tile_id == tile.tile_id;
                    });
                if (!exists)
                {
                    static_cast<void>(service_->add_palette_cell(next % 16, next / 16,
                        TileDocumentService::Brush{tile.tile_id, false, false, 0U, set.asset_id}));
                    ++next;
                }
            }
    });
    connect(remove_palette_tile, &QToolButton::clicked, this, [this] {
        const auto brush = active_brush();
        const auto* palette = service_->palette();
        if (!brush || palette == nullptr) return;
        const auto cell = std::find_if(palette->cells.begin(), palette->cells.end(), [&](const auto& candidate) {
            return candidate.tile.tile_set_id == brush->tile_set_id
                && candidate.tile.tile_id == brush->tile_id;
        });
        if (cell != palette->cells.end())
            static_cast<void>(service_->remove_palette_cell(cell->u, cell->v));
    });

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

bool TilePaletteWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != tiles_->viewport()) return QWidget::eventFilter(watched, event);
    if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove)
    {
        auto* drag = static_cast<QDragMoveEvent*>(event);
        if (drag->mimeData()->hasFormat(QStringLiteral("application/x-dragonpixel-project-item"))
            || drag->mimeData()->hasFormat(QStringLiteral("application/x-dragonpixel-entity")))
        {
            drag->acceptProposedAction();
            return true;
        }
    }
    if (event->type() != QEvent::Drop) return QWidget::eventFilter(watched, event);
    auto* drop = static_cast<QDropEvent*>(event);
    if (drop->mimeData()->hasFormat(QStringLiteral("application/x-dragonpixel-entity")))
    {
        const auto payload = QJsonDocument::fromJson(drop->mimeData()->data(
            QStringLiteral("application/x-dragonpixel-entity"))).object();
        ObjectBrushSource source;
        source.kind = ObjectBrushKind::scene_objects;
        source.project_id = payload.value(QStringLiteral("projectId")).toString();
        source.scene_id = payload.value(QStringLiteral("sceneId")).toString();
        source.source_revision = payload.value(QStringLiteral("sourceRevision")).toInteger();
        for (const auto& value : payload.value(QStringLiteral("items")).toArray())
        {
            const auto id = dragonpixel::core::uuid::parse(
                value.toObject().value(QStringLiteral("id")).toString().toStdString());
            if (id) source.entity_ids.push_back(*id);
        }
        if (payload.value(QStringLiteral("format")).toString() == QStringLiteral("dpe.drag")
            && payload.value(QStringLiteral("formatVersion")).toInt() == 1
            && !source.entity_ids.empty())
        {
            object_brush_ = std::move(source);
            brush_behavior_->setCurrentIndex(
                brush_behavior_->findData(QStringLiteral("object")));
            status_->setText(QStringLiteral(
                "GameObject Brush armed with %1 scene selection root(s). Select the Tilemap2D target, then paint.")
                    .arg(object_brush_->entity_ids.size()));
            drop->acceptProposedAction();
        }
        else status_->setText(QStringLiteral("The dropped scene selection is not a valid GameObject Brush source."));
        return true;
    }
    if (!drop->mimeData()->hasFormat(QStringLiteral("application/x-dragonpixel-project-item")))
        return QWidget::eventFilter(watched, event);
    const auto payload = QJsonDocument::fromJson(drop->mimeData()->data(
        QStringLiteral("application/x-dragonpixel-project-item"))).object();
    if (payload.value(QStringLiteral("format")).toString() != QStringLiteral("dpe.drag")
        || payload.value(QStringLiteral("formatVersion")).toInt() != 1)
    {
        status_->setText(QStringLiteral("The dropped Project Explorer payload is not valid."));
        return true;
    }
    for (const auto& item : payload.value(QStringLiteral("items")).toArray())
    {
        const auto object = item.toObject();
        if (object.value(QStringLiteral("kind")).toString() != QStringLiteral("prefab")) continue;
        ObjectBrushSource source;
        source.kind = ObjectBrushKind::prefab;
        source.source_path = object.value(QStringLiteral("path")).toString();
        source.project_id = payload.value(QStringLiteral("projectId")).toString();
        source.source_revision = payload.value(QStringLiteral("sourceRevision")).toInteger();
        if (!source.source_path.isEmpty())
        {
            object_brush_ = std::move(source);
            brush_behavior_->setCurrentIndex(
                brush_behavior_->findData(QStringLiteral("object")));
            status_->setText(QStringLiteral(
                "GameObject Brush armed with prefab '%1'. Paint on the active Tilemap2D target.")
                    .arg(QFileInfo{object_brush_->source_path}.completeBaseName()));
            drop->acceptProposedAction();
        }
        return true;
    }
    if (service_->palette() == nullptr)
    {
        status_->setText(QStringLiteral("Open a durable Tile Palette before adding dropped assets."));
        return true;
    }
    std::vector<const dragonpixel::tiles::tile_set_document*> owners;
    for (const auto& item : payload.value(QStringLiteral("items")).toArray())
    {
        const auto object = item.toObject();
        const auto asset_id = dragonpixel::core::uuid::parse(
            object.value(QStringLiteral("assetId")).toString().toStdString());
        const auto asset_type = object.value(QStringLiteral("assetType")).toString().toLower();
        if (!asset_id) continue;
        for (const auto& set : service_->tilesets())
        {
            const auto matches_set = asset_type.contains(QStringLiteral("tileset"))
                && set.asset_id == *asset_id;
            const auto matches_image = (asset_type.contains(QStringLiteral("sprite"))
                    || asset_type.contains(QStringLiteral("image"))
                    || asset_type.contains(QStringLiteral("texture")))
                && std::find(set.texture_asset_ids.begin(), set.texture_asset_ids.end(), *asset_id)
                    != set.texture_asset_ids.end();
            if ((matches_set || matches_image)
                && std::find(owners.begin(), owners.end(), &set) == owners.end()) owners.push_back(&set);
        }
    }
    if (owners.empty())
    {
        status_->setText(QStringLiteral(
            "Drop a loaded image or TileSet here. Activate a different TileSet or palette in Project Explorer first."));
        return true;
    }
    int next = static_cast<int>(service_->palette()->cells.size());
    for (const auto* owner : owners)
        for (const auto& tile : owner->tiles)
        {
            const auto exists = std::any_of(service_->palette()->cells.begin(), service_->palette()->cells.end(),
                [&](const auto& cell) {
                    return cell.tile.tile_set_id == owner->asset_id && cell.tile.tile_id == tile.tile_id;
                });
            if (!exists && service_->add_palette_cell(next % 16, next / 16,
                    TileDocumentService::Brush{tile.tile_id, false, false, 0U, owner->asset_id})) ++next;
        }
    drop->acceptProposedAction();
    return true;
}

std::optional<TilePaletteWidget::ObjectBrushSource> TilePaletteWidget::active_object_brush() const
{
    if (brush_behavior_ == nullptr
        || brush_behavior_->currentData().toString() != QStringLiteral("object")) return std::nullopt;
    return object_brush_;
}

bool TilePaletteWidget::load_documents(
    const QString& tilemap_path,
    const QString& tileset_path,
    const QString& texture_path,
    const QString& palette_path)
{
    return load_documents(tilemap_path, QStringList{tileset_path},
        QStringList{texture_path}, palette_path);
}

bool TilePaletteWidget::load_documents(
    const QString& tilemap_path,
    const QStringList& tileset_paths,
    const QStringList& texture_paths,
    const QString& palette_path)
{
    texture_path_ = texture_paths.isEmpty() ? QString{} : texture_paths.constFirst();
    atlas_ = texture_path_.isEmpty() ? QImage{} : QImage{texture_path_};
    atlases_.clear();
    for (qsizetype index = 0; index < tileset_paths.size(); ++index)
    {
        QFile set_file{tileset_paths.at(index)};
        if (!set_file.open(QIODevice::ReadOnly)) continue;
        const auto parsed = dragonpixel::tiles::read_tile_set(set_file.readAll().toStdString());
        if (!parsed.succeeded()) continue;
        const auto texture = index < texture_paths.size() && !texture_paths.at(index).isEmpty()
            ? QImage{texture_paths.at(index)} : QImage{};
        atlases_.insert(QString::fromStdString(parsed.document->asset_id.to_string()), texture);
    }
    canvas_->set_atlas(atlas_);
    canvas_->set_atlases(atlases_);
    const auto loaded = service_->load(tilemap_path, tileset_paths, palette_path);
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
    const auto tile_set_id = dragonpixel::core::uuid::parse(
        item->data(Qt::UserRole + 1).toString().toStdString());
    if (!tile_id || !tile_set_id) return std::nullopt;
    auto tint = QColor{brush_tint_->text().trimmed()};
    if (!tint.isValid()) tint = Qt::white;
    return TileDocumentService::Brush{
        *tile_id, flip_x_->isChecked(), flip_y_->isChecked(), brush_rotation_ % 4U,
        *tile_set_id,
        {tint.redF(), tint.greenF(), tint.blueF(), tint.alphaF()},
        {brush_offset_x_->value(), brush_offset_y_->value()},
        brush_rotation_degrees_->value(),
        {brush_scale_x_->value(), brush_scale_y_->value()},
        brush_elevation_->value(), brush_lock_color_->isChecked(),
        brush_lock_transform_->isChecked()};
}

bool TilePaletteWidget::target_pinned() const noexcept
{
    return target_pin_ != nullptr && target_pin_->isChecked();
}

std::optional<TileDocumentService::Brush> TilePaletteWidget::active_brush_at(int x, int y) const
{
    if (brush_behavior_ != nullptr
        && brush_behavior_->currentData().toString() == QStringLiteral("object")) return std::nullopt;
    auto brush = active_brush();
    const auto* map = service_->tilemap();
    const auto layer_index = active_layer();
    if (!brush || map == nullptr || brush_behavior_ == nullptr
        || brush_behavior_->currentData().toString() != QStringLiteral("random")
        || layer_index < 0 || layer_index >= static_cast<int>(map->layers.size()))
    {
        return brush;
    }
    std::vector<dragonpixel::tiles::weighted_tile_reference> candidates;
    for (const auto* item : tiles_->selectedItems())
    {
        const auto tile_id = dragonpixel::core::uuid::parse(
            item->data(Qt::UserRole).toString().toStdString());
        const auto set_id = dragonpixel::core::uuid::parse(
            item->data(Qt::UserRole + 1).toString().toStdString());
        if (tile_id && set_id) candidates.push_back({{*set_id, *tile_id}, 1.0});
    }
    if (candidates.empty()) return brush;
    const auto selected = dragonpixel::tiles::random_brush_tile(candidates,
        map->asset_id, map->layers[static_cast<std::size_t>(layer_index)].layer_id,
        {x, y});
    brush->tile_set_id = selected.tile_set_id;
    brush->tile_id = selected.tile_id;
    return brush;
}

std::vector<std::pair<QPoint, TileDocumentService::Brush>>
TilePaletteWidget::active_brush_pattern_at(int x, int y) const
{
    auto brush = active_brush_at(x, y);
    if (!brush) return {};
    if (brush_behavior_ == nullptr
        || brush_behavior_->currentData().toString() != QStringLiteral("group"))
    {
        return {{{x, y}, *brush}};
    }
    std::vector<dragonpixel::tiles::brush_cell> occupied;
    auto minimum = dragonpixel::tiles::integer_point{};
    auto maximum = dragonpixel::tiles::integer_point{};
    bool first = true;
    for (const auto* item : tiles_->selectedItems())
    {
        const auto tile_id = dragonpixel::core::uuid::parse(
            item->data(Qt::UserRole).toString().toStdString());
        const auto set_id = dragonpixel::core::uuid::parse(
            item->data(Qt::UserRole + 1).toString().toStdString());
        if (!tile_id || !set_id) continue;
        const dragonpixel::tiles::integer_point position{
            item->data(Qt::UserRole + 5).toInt(), item->data(Qt::UserRole + 6).toInt()};
        occupied.push_back({position, {*set_id, *tile_id}});
        if (first)
        {
            minimum = maximum = position;
            first = false;
        }
        else
        {
            minimum.x = std::min(minimum.x, position.x);
            minimum.y = std::min(minimum.y, position.y);
            maximum.x = std::max(maximum.x, position.x);
            maximum.y = std::max(maximum.y, position.y);
        }
    }
    if (occupied.empty()) return {{{x, y}, *brush}};
    const auto picked = dragonpixel::tiles::group_pick(occupied, minimum, maximum,
        group_gap_->value(), static_cast<std::size_t>(group_limit_->value()));
    std::vector<std::pair<QPoint, TileDocumentService::Brush>> result;
    result.reserve(picked.size());
    for (const auto& cell : picked)
    {
        auto stamped = *brush;
        stamped.tile_set_id = cell.tile.tile_set_id;
        stamped.tile_id = cell.tile.tile_id;
        result.push_back({QPoint{x + cell.u, y + cell.v}, stamped});
    }
    return result;
}

void TilePaletteWidget::select_brush(const TileDocumentService::Brush& brush)
{
    const auto id = QString::fromStdString(brush.tile_id.to_string());
    const QSignalBlocker tile_blocker{tiles_};
    for (int row = 0; row < tiles_->count(); ++row)
    {
        if (tiles_->item(row)->data(Qt::UserRole).toString() == id
            && (brush.tile_set_id.is_nil()
                || tiles_->item(row)->data(Qt::UserRole + 1).toString()
                    == QString::fromStdString(brush.tile_set_id.to_string())))
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
    brush_tint_->setText(QColor::fromRgbF(brush.tint.red, brush.tint.green,
        brush.tint.blue, brush.tint.alpha)
        .name(QColor::HexArgb));
    brush_offset_x_->setValue(brush.offset.x);
    brush_offset_y_->setValue(brush.offset.y);
    brush_rotation_degrees_->setValue(brush.rotation_degrees);
    brush_scale_x_->setValue(brush.scale.x);
    brush_scale_y_->setValue(brush.scale.y);
    brush_elevation_->setValue(brush.elevation);
    brush_lock_color_->setChecked(brush.lock_color);
    brush_lock_transform_->setChecked(brush.lock_transform);
    update_brush();
}

void TilePaletteWidget::update_tile_editor()
{
    const auto* item = tiles_ == nullptr ? nullptr : tiles_->currentItem();
    const auto tile_id = item ? dragonpixel::core::uuid::parse(
        item->data(Qt::UserRole).toString().toStdString()) : std::nullopt;
    const auto set_id = item ? dragonpixel::core::uuid::parse(
        item->data(Qt::UserRole + 1).toString().toStdString()) : std::nullopt;
    const dragonpixel::tiles::tile_definition* selected = nullptr;
    if (tile_id && set_id)
    {
        const auto owner = std::find_if(service_->tilesets().begin(), service_->tilesets().end(),
            [&](const auto& set) { return set.asset_id == *set_id; });
        if (owner != service_->tilesets().end())
        {
            const auto tile = std::find_if(owner->tiles.begin(), owner->tiles.end(),
                [&](const auto& candidate) { return candidate.tile_id == *tile_id; });
            if (tile != owner->tiles.end()) selected = &*tile;
        }
    }
    for (auto* widget : std::array<QWidget*, 11>{tile_name_, tile_kind_, tile_collider_,
             tile_minimum_speed_, tile_maximum_speed_, tile_start_time_, tile_start_frame_,
             tile_loop_once_, tile_paused_, tile_update_physics_, tile_custom_type_})
        widget->setEnabled(selected != nullptr);
    tile_custom_payload_->setEnabled(selected != nullptr);
    if (selected == nullptr) return;
    tile_name_->setText(QString::fromStdString(selected->name));
    tile_kind_->setCurrentIndex(tile_kind_->findData(static_cast<int>(selected->kind)));
    tile_collider_->setCurrentIndex(
        tile_collider_->findData(static_cast<int>(selected->collider_mode)));
    tile_minimum_speed_->setValue(selected->minimum_speed);
    tile_maximum_speed_->setValue(selected->maximum_speed);
    tile_start_time_->setValue(selected->animation_start_time);
    tile_start_frame_->setValue(static_cast<int>(std::min<unsigned>(
        selected->animation_start_frame, 1'000'000U)));
    tile_loop_once_->setChecked(selected->loop_once);
    tile_paused_->setChecked(selected->pause_animation);
    tile_update_physics_->setChecked(selected->update_physics);
    tile_custom_type_->setText(QString::fromStdString(selected->custom_type_id));
    tile_custom_payload_->setText(QString::fromStdString(selected->opaque_payload_json));
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
    const auto selected_set = tiles_->currentItem()
        ? tiles_->currentItem()->data(Qt::UserRole + 1).toString() : QString{};
    const QSignalBlocker layer_blocker{layers_};
    const QSignalBlocker tile_blocker{tiles_};
    const QSignalBlocker palette_blocker{palettes_};
    palettes_->clear();
    if (const auto* palette = service_->palette())
    {
        palettes_->addItem(QString::fromStdString(palette->name),
            QString::fromStdString(palette->asset_id.to_string()));
    }
    else
    {
        palettes_->addItem(QStringLiteral("TileSet preview (not a palette)"));
    }
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
    const auto append_tile = [&](const dragonpixel::tiles::tile_set_document& owner,
                                 const dragonpixel::tiles::tile_definition& tile,
                                 bool flip_x, bool flip_y, unsigned rotation, int u, int v) {
        const auto owner_id = QString::fromStdString(owner.asset_id.to_string());
        QPixmap preview;
        const auto image = tile_image(atlases_.value(owner_id, atlas_), tile,
            flip_x, flip_y, rotation);
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
        item->setData(Qt::UserRole + 1, owner_id);
        item->setData(Qt::UserRole + 2, flip_x);
        item->setData(Qt::UserRole + 3, flip_y);
        item->setData(Qt::UserRole + 4, rotation);
        item->setData(Qt::UserRole + 5, u);
        item->setData(Qt::UserRole + 6, v);
        item->setToolTip(QStringLiteral("%1\n%2")
            .arg(QString::fromStdString(tile.name), QString::fromStdString(tile.tile_id.to_string())));
        if (item->data(Qt::UserRole).toString() == selected_tile
            && owner_id == selected_set) tiles_->setCurrentItem(item);
    };
    if (const auto* palette = service_->palette())
    {
        auto cells = palette->cells;
        std::sort(cells.begin(), cells.end(), [](const auto& left, const auto& right) {
            return std::tie(left.v, left.u) < std::tie(right.v, right.u);
        });
        for (const auto& cell : cells)
        {
            const auto owner = std::find_if(service_->tilesets().begin(), service_->tilesets().end(),
                [&](const auto& candidate) { return candidate.asset_id == cell.tile.tile_set_id; });
            if (owner == service_->tilesets().end()) continue;
            const auto tile = std::find_if(owner->tiles.begin(), owner->tiles.end(),
                [&](const auto& candidate) { return candidate.tile_id == cell.tile.tile_id; });
            if (tile != owner->tiles.end())
                append_tile(*owner, *tile, cell.flip_x, cell.flip_y,
                    cell.rotation_quarter_turns, cell.u, cell.v);
        }
    }
    else
    {
        int preview_index{};
        for (const auto& owner : service_->tilesets())
            for (const auto& tile : owner.tiles)
            {
                append_tile(owner, tile, false, false, 0U,
                    preview_index % 16, preview_index / 16);
                ++preview_index;
            }
    }
    if (tiles_->currentItem() == nullptr && tiles_->count() > 0) tiles_->setCurrentRow(0);
    rebuilding_ = false;
    update_tile_editor();
    update_brush();
    update_layer_controls();
    status_->setText(QStringLiteral("%1 | %2 targets | %3 palette cells | %4 TileSet(s) | Atlas %5%6")
        .arg(QString::fromStdString(map->name)).arg(map->layers.size()).arg(tiles_->count())
        .arg(service_->tilesets().size())
        .arg(atlas_.isNull() ? QStringLiteral("unavailable") : QStringLiteral("ready"))
        .arg(service_->is_dirty() ? QStringLiteral(" | Unsaved") : QString{}));
    canvas_->update();
    emit authoringStateChanged();
}
