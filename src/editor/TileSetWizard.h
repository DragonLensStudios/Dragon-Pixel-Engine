#pragma once

#include <QString>

class QCheckBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QWidget;

#include <QDialog>

enum class TileSetSlicingMode
{
    automatic,
    cell_size,
    cell_count,
};

struct TileSetCreationRequest final
{
    QString project_root;
    QString source_image;
    QString name;
    TileSetSlicingMode slicing_mode{TileSetSlicingMode::cell_size};
    int cell_width{32};
    int cell_height{32};
    int column_count{1};
    int row_count{1};
    int margin_x{};
    int margin_y{};
    int spacing_x{};
    int spacing_y{};
    double pixels_per_unit{32.0};
    double pivot_x{0.5};
    double pivot_y{0.5};
    bool keep_empty_cells{};
    QString grid_layout{QStringLiteral("rectangular")};
    bool rectangular_collision{true};
};

struct TileSetCreationResult final
{
    bool succeeded{};
    QString error;
    QString tile_set_path;
    QString tile_set_metadata_path;
    QString texture_path;
    QString texture_metadata_path;
    QString tile_set_asset_id;
    QString texture_asset_id;
    int tile_count{};
};

class TileSetCreationService final
{
public:
    [[nodiscard]] static TileSetCreationResult create(const TileSetCreationRequest& request);
};

class TileSetWizard final : public QDialog
{
    Q_OBJECT

public:
    explicit TileSetWizard(QString project_root, QWidget* parent = nullptr);

    [[nodiscard]] const TileSetCreationResult& result() const noexcept { return result_; }
    [[nodiscard]] bool complete_tilemap_workflow() const noexcept;
    [[nodiscard]] QString tile_set_name() const;
    [[nodiscard]] QString grid_layout() const;

private:
    void browse_source();
    void refresh_preview();
    void create_assets();

    QString project_root_;
    QLineEdit* source_{};
    QLineEdit* name_{};
    QComboBox* slicing_mode_{};
    QSpinBox* cell_width_{};
    QSpinBox* cell_height_{};
    QSpinBox* column_count_{};
    QSpinBox* row_count_{};
    QSpinBox* margin_x_{};
    QSpinBox* margin_y_{};
    QSpinBox* spacing_x_{};
    QSpinBox* spacing_y_{};
    QDoubleSpinBox* pixels_per_unit_{};
    QDoubleSpinBox* pivot_x_{};
    QDoubleSpinBox* pivot_y_{};
    QCheckBox* keep_empty_{};
    QComboBox* grid_layout_{};
    QCheckBox* collision_{};
    QCheckBox* create_tilemap_{};
    QLabel* preview_{};
    QLabel* validation_{};
    TileSetCreationResult result_;
};
