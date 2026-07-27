#pragma once

#include "InputMapService.h"

#include <QDialog>

class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class InputMapEditorDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit InputMapEditorDialog(InputMapDocument document, QWidget* parent = nullptr);

    [[nodiscard]] const InputMapDocument& document() const noexcept { return document_; }

private:
    InputControlMap* current_map();
    InputAction* selected_action();
    InputBinding* selected_binding();
    void rebuild_map_selector();
    void rebuild_tree();
    void add_map();
    void rename_map();
    void remove_map();
    void add_action();
    void rename_action();
    void remove_action();
    void add_binding();
    void rebind_binding();
    void remove_binding();
    void accept_validated();
    void update_buttons();

    InputMapService service_;
    InputMapDocument document_;
    QComboBox* map_selector_{};
    QCheckBox* map_enabled_{};
    QTreeWidget* binding_tree_{};
    QLabel* status_label_{};
    QPushButton* rename_map_button_{};
    QPushButton* remove_map_button_{};
    QPushButton* rename_action_button_{};
    QPushButton* remove_action_button_{};
    QPushButton* add_binding_button_{};
    QPushButton* rebind_button_{};
    QPushButton* remove_binding_button_{};
};
