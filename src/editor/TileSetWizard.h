#pragma once

#include <QString>

class QCheckBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QDoubleSpinBox;
class QWidget;

#include <QDialog>

struct TileSetCreationRequest final
{
    QString project_root;
    QString source_png;
    QString name;
    int cell_width{32};
    int cell_height{32};
    int margin_x{};
    int margin_y{};
    int spacing_x{};
    int spacing_y{};
    double pixels_per_unit{32.0};
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

private:
    void browse_source();
    void refresh_preview();
    void create_assets();

    QString project_root_;
    QLineEdit* source_{};
    QLineEdit* name_{};
    QSpinBox* cell_width_{};
    QSpinBox* cell_height_{};
    QSpinBox* margin_x_{};
    QSpinBox* margin_y_{};
    QSpinBox* spacing_x_{};
    QSpinBox* spacing_y_{};
    QDoubleSpinBox* pixels_per_unit_{};
    QCheckBox* collision_{};
    QLabel* preview_{};
    QLabel* validation_{};
    TileSetCreationResult result_;
};
