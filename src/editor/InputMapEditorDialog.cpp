#include "InputMapEditorDialog.h"

#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
constexpr int action_id_role = Qt::UserRole;
constexpr int binding_id_role = Qt::UserRole + 1;

QString new_uuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}
}

InputMapEditorDialog::InputMapEditorDialog(InputMapDocument document, QWidget* parent)
    : QDialog(parent), document_(std::move(document))
{
    setObjectName(QStringLiteral("InputMapEditorDialog"));
    setWindowTitle(QStringLiteral("Input Settings"));
    setMinimumSize(760, 520);

    auto* root = new QVBoxLayout(this);
    auto* map_row = new QHBoxLayout;
    map_row->addWidget(new QLabel(QStringLiteral("Control Map:"), this));
    map_selector_ = new QComboBox(this);
    map_selector_->setObjectName(QStringLiteral("InputControlMapSelector"));
    map_selector_->setAccessibleName(QStringLiteral("Active control map"));
    map_row->addWidget(map_selector_, 1);
    map_enabled_ = new QCheckBox(QStringLiteral("Enabled"), this);
    map_enabled_->setObjectName(QStringLiteral("InputControlMapEnabled"));
    map_enabled_->setAccessibleName(QStringLiteral("Enable active control map"));
    map_enabled_->setToolTip(QStringLiteral("Disabled control maps publish neutral action values"));
    map_row->addWidget(map_enabled_);
    auto* add_map_button = new QPushButton(QStringLiteral("Add Map"), this);
    add_map_button->setObjectName(QStringLiteral("AddInputControlMapButton"));
    rename_map_button_ = new QPushButton(QStringLiteral("Rename"), this);
    rename_map_button_->setObjectName(QStringLiteral("RenameInputControlMapButton"));
    remove_map_button_ = new QPushButton(QStringLiteral("Remove"), this);
    remove_map_button_->setObjectName(QStringLiteral("RemoveInputControlMapButton"));
    map_row->addWidget(add_map_button);
    map_row->addWidget(rename_map_button_);
    map_row->addWidget(remove_map_button_);
    root->addLayout(map_row);

    binding_tree_ = new QTreeWidget(this);
    binding_tree_->setObjectName(QStringLiteral("InputBindingTree"));
    binding_tree_->setAccessibleName(QStringLiteral("Input actions and bindings"));
    binding_tree_->setHeaderLabels({
        QStringLiteral("Action / Binding"), QStringLiteral("Kind"),
        QStringLiteral("Scale"), QStringLiteral("Dead Zone")});
    binding_tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    root->addWidget(binding_tree_, 1);

    auto* action_row = new QHBoxLayout;
    auto* add_action_button = new QPushButton(QStringLiteral("Add Action"), this);
    add_action_button->setObjectName(QStringLiteral("AddInputActionButton"));
    rename_action_button_ = new QPushButton(QStringLiteral("Rename Action"), this);
    rename_action_button_->setObjectName(QStringLiteral("RenameInputActionButton"));
    remove_action_button_ = new QPushButton(QStringLiteral("Remove Action"), this);
    remove_action_button_->setObjectName(QStringLiteral("RemoveInputActionButton"));
    add_binding_button_ = new QPushButton(QStringLiteral("Add Binding"), this);
    add_binding_button_->setObjectName(QStringLiteral("AddInputBindingButton"));
    rebind_button_ = new QPushButton(QStringLiteral("Edit Binding"), this);
    rebind_button_->setObjectName(QStringLiteral("RebindInputBindingButton"));
    remove_binding_button_ = new QPushButton(QStringLiteral("Remove Binding"), this);
    remove_binding_button_->setObjectName(QStringLiteral("RemoveInputBindingButton"));
    action_row->addWidget(add_action_button);
    action_row->addWidget(rename_action_button_);
    action_row->addWidget(remove_action_button_);
    action_row->addSpacing(12);
    action_row->addWidget(add_binding_button_);
    action_row->addWidget(rebind_button_);
    action_row->addWidget(remove_binding_button_);
    action_row->addStretch();
    root->addLayout(action_row);

    status_label_ = new QLabel(
        QStringLiteral("Configure keyboard, mouse, and standard-gamepad bindings here. Save activates the selected control map in Play."), this);
    status_label_->setObjectName(QStringLiteral("InputMapStatusLabel"));
    status_label_->setWordWrap(true);
    root->addWidget(status_label_);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("SaveInputMapButton"));
    root->addWidget(buttons);

    connect(map_selector_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0) return;
        document_.active_control_map_id = map_selector_->currentData().toString();
        map_enabled_->blockSignals(true);
        map_enabled_->setChecked(current_map() != nullptr && current_map()->enabled);
        map_enabled_->blockSignals(false);
        rebuild_tree();
    });
    connect(map_enabled_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (auto* map = current_map()) map->enabled = enabled;
    });
    connect(binding_tree_, &QTreeWidget::itemSelectionChanged,
        this, &InputMapEditorDialog::update_buttons);
    connect(add_map_button, &QPushButton::clicked, this, &InputMapEditorDialog::add_map);
    connect(rename_map_button_, &QPushButton::clicked, this, &InputMapEditorDialog::rename_map);
    connect(remove_map_button_, &QPushButton::clicked, this, &InputMapEditorDialog::remove_map);
    connect(add_action_button, &QPushButton::clicked, this, &InputMapEditorDialog::add_action);
    connect(rename_action_button_, &QPushButton::clicked, this, &InputMapEditorDialog::rename_action);
    connect(remove_action_button_, &QPushButton::clicked, this, &InputMapEditorDialog::remove_action);
    connect(add_binding_button_, &QPushButton::clicked, this, &InputMapEditorDialog::add_binding);
    connect(rebind_button_, &QPushButton::clicked, this, &InputMapEditorDialog::rebind_binding);
    connect(remove_binding_button_, &QPushButton::clicked, this, &InputMapEditorDialog::remove_binding);
    connect(buttons, &QDialogButtonBox::accepted, this, &InputMapEditorDialog::accept_validated);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    rebuild_map_selector();
}

InputControlMap* InputMapEditorDialog::current_map()
{
    const auto id = map_selector_->currentData().toString();
    const auto iterator = std::find_if(document_.control_maps.begin(), document_.control_maps.end(),
        [&id](const InputControlMap& map) { return map.id == id; });
    return iterator == document_.control_maps.end() ? nullptr : &*iterator;
}

InputAction* InputMapEditorDialog::selected_action()
{
    const auto items = binding_tree_->selectedItems();
    if (items.isEmpty()) return nullptr;
    const auto id = items.front()->data(0, action_id_role).toString();
    auto* map = current_map();
    if (map == nullptr) return nullptr;
    const auto iterator = std::find_if(map->actions.begin(), map->actions.end(),
        [&id](const InputAction& action) { return action.id == id; });
    return iterator == map->actions.end() ? nullptr : &*iterator;
}

InputBinding* InputMapEditorDialog::selected_binding()
{
    const auto items = binding_tree_->selectedItems();
    if (items.isEmpty()) return nullptr;
    const auto id = items.front()->data(0, binding_id_role).toString();
    auto* action = selected_action();
    if (action == nullptr || id.isEmpty()) return nullptr;
    const auto iterator = std::find_if(action->bindings.begin(), action->bindings.end(),
        [&id](const InputBinding& binding) { return binding.id == id; });
    return iterator == action->bindings.end() ? nullptr : &*iterator;
}

void InputMapEditorDialog::rebuild_map_selector()
{
    map_selector_->blockSignals(true);
    map_selector_->clear();
    int active_index = 0;
    for (qsizetype index = 0; index < document_.control_maps.size(); ++index)
    {
        const auto& map = document_.control_maps.at(index);
        map_selector_->addItem(map.name, map.id);
        if (map.id == document_.active_control_map_id) active_index = static_cast<int>(index);
    }
    map_selector_->setCurrentIndex(active_index);
    map_selector_->blockSignals(false);
    if (!document_.control_maps.isEmpty())
    {
        document_.active_control_map_id = map_selector_->currentData().toString();
    }
    map_enabled_->blockSignals(true);
    map_enabled_->setChecked(current_map() != nullptr && current_map()->enabled);
    map_enabled_->blockSignals(false);
    rebuild_tree();
}

void InputMapEditorDialog::rebuild_tree()
{
    binding_tree_->clear();
    const auto* map = current_map();
    if (map != nullptr)
    {
        for (const auto& action : map->actions)
        {
            auto* action_item = new QTreeWidgetItem(binding_tree_, {
                action.name, InputMapService::action_kind_name(action.kind), {}, {}});
            action_item->setData(0, action_id_role, action.id);
            action_item->setExpanded(true);
            for (const auto& binding : action.bindings)
            {
                auto* binding_item = new QTreeWidgetItem(action_item, {
                    binding.path, QStringLiteral("binding"),
                    QString::number(binding.scale, 'g', 5),
                    QString::number(binding.dead_zone, 'g', 5)});
                binding_item->setData(0, action_id_role, action.id);
                binding_item->setData(0, binding_id_role, binding.id);
            }
        }
    }
    binding_tree_->resizeColumnToContents(0);
    update_buttons();
}

void InputMapEditorDialog::add_map()
{
    bool accepted = false;
    const auto name = QInputDialog::getText(
        this, QStringLiteral("Add Control Map"), QStringLiteral("Name:"),
        QLineEdit::Normal, QStringLiteral("Gameplay"), &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    InputControlMap map;
    map.id = new_uuid();
    map.name = name;
    document_.active_control_map_id = map.id;
    document_.control_maps.push_back(std::move(map));
    rebuild_map_selector();
    map_selector_->setCurrentIndex(map_selector_->count() - 1);
}

void InputMapEditorDialog::rename_map()
{
    auto* map = current_map();
    if (map == nullptr) return;
    bool accepted = false;
    const auto name = QInputDialog::getText(
        this, QStringLiteral("Rename Control Map"), QStringLiteral("Name:"),
        QLineEdit::Normal, map->name, &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    map->name = name;
    rebuild_map_selector();
}

void InputMapEditorDialog::remove_map()
{
    if (document_.control_maps.size() <= 1) return;
    const auto id = map_selector_->currentData().toString();
    const auto iterator = std::find_if(
        document_.control_maps.begin(), document_.control_maps.end(),
        [&id](const InputControlMap& map) { return map.id == id; });
    if (iterator != document_.control_maps.end()) document_.control_maps.erase(iterator);
    document_.active_control_map_id = document_.control_maps.front().id;
    rebuild_map_selector();
}

void InputMapEditorDialog::add_action()
{
    auto* map = current_map();
    if (map == nullptr) return;
    bool accepted = false;
    auto name = QInputDialog::getText(
        this, QStringLiteral("Add Input Action"), QStringLiteral("Canonical name:"),
        QLineEdit::Normal, QStringLiteral("action"), &accepted).trimmed().toLower();
    if (!accepted || name.isEmpty()) return;
    const auto kind_name = QInputDialog::getItem(
        this, QStringLiteral("Action Kind"), QStringLiteral("Kind:"),
        {QStringLiteral("button"), QStringLiteral("axis1d")}, 0, false, &accepted);
    if (!accepted) return;
    InputAction action;
    action.id = new_uuid();
    action.name = std::move(name);
    action.kind = kind_name == QStringLiteral("axis1d")
        ? InputActionKind::axis1d : InputActionKind::button;
    map->actions.push_back(std::move(action));
    rebuild_tree();
}

void InputMapEditorDialog::remove_action()
{
    auto* map = current_map();
    auto* action = selected_action();
    if (map == nullptr || action == nullptr) return;
    const auto id = action->id;
    const auto iterator = std::find_if(map->actions.begin(), map->actions.end(),
        [&id](const InputAction& item) { return item.id == id; });
    if (iterator != map->actions.end()) map->actions.erase(iterator);
    rebuild_tree();
}

void InputMapEditorDialog::rename_action()
{
    auto* action = selected_action();
    if (action == nullptr) return;
    bool accepted = false;
    const auto name = QInputDialog::getText(
        this, QStringLiteral("Rename Input Action"), QStringLiteral("Canonical name:"),
        QLineEdit::Normal, action->name, &accepted).trimmed().toLower();
    if (!accepted || name.isEmpty()) return;
    action->name = name;
    rebuild_tree();
}

void InputMapEditorDialog::add_binding()
{
    auto* action = selected_action();
    if (action == nullptr) return;
    bool accepted = false;
    const auto path = QInputDialog::getItem(
        this, QStringLiteral("Add Input Binding"), QStringLiteral("Control:"),
        InputMapService::supported_control_paths(), 0, false, &accepted);
    if (!accepted || path.isEmpty()) return;
    const auto scale = QInputDialog::getDouble(
        this, QStringLiteral("Binding Scale"), QStringLiteral("Scale:"),
        1.0, -16.0, 16.0, 3, &accepted);
    if (!accepted) return;
    const auto dead_zone = path.startsWith(QStringLiteral("gamepad/"))
        ? QInputDialog::getDouble(this, QStringLiteral("Binding Dead Zone"),
              QStringLiteral("Dead zone:"), 0.18, 0.0, 0.99, 3, &accepted)
        : 0.0;
    if (!accepted) return;
    action->bindings.push_back(InputBinding{new_uuid(), path, scale, dead_zone, {}});
    rebuild_tree();
}

void InputMapEditorDialog::rebind_binding()
{
    auto* binding = selected_binding();
    if (binding == nullptr) return;
    const auto paths = InputMapService::supported_control_paths();
    bool accepted = false;
    const auto path = QInputDialog::getItem(
        this, QStringLiteral("Rebind Input"), QStringLiteral("Control:"), paths,
        static_cast<int>(std::max<qsizetype>(0, paths.indexOf(binding->path))),
        false, &accepted);
    if (!accepted || path.isEmpty()) return;
    const auto scale = QInputDialog::getDouble(
        this, QStringLiteral("Binding Scale"), QStringLiteral("Scale:"),
        binding->scale, -16.0, 16.0, 3, &accepted);
    if (!accepted) return;
    const auto dead_zone = path.startsWith(QStringLiteral("gamepad/"))
        ? QInputDialog::getDouble(this, QStringLiteral("Binding Dead Zone"),
              QStringLiteral("Dead zone:"), binding->dead_zone, 0.0, 0.99, 3, &accepted)
        : 0.0;
    if (!accepted) return;
    binding->path = path;
    binding->scale = scale;
    binding->dead_zone = dead_zone;
    rebuild_tree();
}

void InputMapEditorDialog::remove_binding()
{
    auto* action = selected_action();
    auto* binding = selected_binding();
    if (action == nullptr || binding == nullptr) return;
    const auto id = binding->id;
    const auto iterator = std::find_if(action->bindings.begin(), action->bindings.end(),
        [&id](const InputBinding& item) { return item.id == id; });
    if (iterator != action->bindings.end()) action->bindings.erase(iterator);
    rebuild_tree();
}

void InputMapEditorDialog::accept_validated()
{
    const auto validation = service_.parse(service_.serialize(document_));
    if (!validation.succeeded())
    {
        const auto& diagnostic = validation.diagnostics.front();
        status_label_->setText(QStringLiteral("%1: %2")
            .arg(diagnostic.json_pointer, diagnostic.message));
        QMessageBox::warning(this, QStringLiteral("Input Map Invalid"),
            QStringLiteral("%1\n%2").arg(diagnostic.json_pointer, diagnostic.message));
        return;
    }
    document_ = *validation.document;
    QDialog::accept();
}

void InputMapEditorDialog::update_buttons()
{
    const auto* map = current_map();
    const auto* action = selected_action();
    const auto* binding = selected_binding();
    map_enabled_->setEnabled(map != nullptr);
    rename_map_button_->setEnabled(map != nullptr);
    remove_map_button_->setEnabled(document_.control_maps.size() > 1);
    rename_action_button_->setEnabled(action != nullptr);
    remove_action_button_->setEnabled(action != nullptr);
    add_binding_button_->setEnabled(action != nullptr);
    rebind_button_->setEnabled(binding != nullptr);
    remove_binding_button_->setEnabled(binding != nullptr);
}
