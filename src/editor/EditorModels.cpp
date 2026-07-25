#include "EditorModels.h"

#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QMimeData>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QUrl>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace
{
constexpr auto hierarchy_mime_type = "application/x-dragonpixel-entity";

QString normalized_path(const QString& path)
{
    if (path.trimmed().isEmpty())
    {
        return {};
    }
    return QDir::cleanPath(QFileInfo{path}.absoluteFilePath());
}

class JsonTupleEditor final : public QWidget
{
public:
    JsonTupleEditor(dragonpixel::metadata::value_type type, QWidget* parent)
        : QWidget(parent), type_(type)
    {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(3);
        QStringList fields;
        if (type == dragonpixel::metadata::value_type::vector2)
        {
            fields = {QStringLiteral("x"), QStringLiteral("y")};
        }
        else if (type == dragonpixel::metadata::value_type::color)
        {
            fields = {QStringLiteral("r"), QStringLiteral("g"), QStringLiteral("b"), QStringLiteral("a")};
        }
        else
        {
            fields = {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")};
        }
        for (const auto& field : fields)
        {
            auto* label = new QLabel{field.toUpper(), this};
            auto* value = new QDoubleSpinBox{this};
            value->setObjectName(QStringLiteral("InspectorTuple.%1").arg(field));
            value->setAccessibleName(QStringLiteral("%1 value").arg(field));
            value->setDecimals(4);
            value->setSingleStep(type == dragonpixel::metadata::value_type::quaternion ? 1.0 : 0.1);
            value->setRange(type == dragonpixel::metadata::value_type::color ? 0.0 : -1000000.0,
                type == dragonpixel::metadata::value_type::color ? 1.0 : 1000000.0);
            fields_.push_back(value);
            layout->addWidget(label);
            layout->addWidget(value, 1);
        }
        setAccessibleName(type == dragonpixel::metadata::value_type::quaternion
            ? QStringLiteral("Quaternion rotation as Euler degrees")
            : type == dragonpixel::metadata::value_type::color
                ? QStringLiteral("RGBA color")
                : QStringLiteral("Vector components"));
    }

    [[nodiscard]] dragonpixel::metadata::value_type type() const noexcept { return type_; }
    [[nodiscard]] const QList<QDoubleSpinBox*>& fields() const noexcept { return fields_; }

private:
    dragonpixel::metadata::value_type type_;
    QList<QDoubleSpinBox*> fields_;
};

QJsonValue parse_json_value(const QString& text)
{
    const auto document = QJsonDocument::fromJson(
        QStringLiteral("[%1]").arg(text).toUtf8());
    return document.isArray() && !document.array().isEmpty() ? document.array().at(0) : QJsonValue{};
}

QString serialize_json_string(const QString& value)
{
    const auto encoded = QJsonDocument{QJsonArray{value}}.toJson(QJsonDocument::Compact);
    return QString::fromUtf8(encoded.mid(1, encoded.size() - 2));
}
}

RecursiveFilterProxyModel::RecursiveFilterProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    setRecursiveFilteringEnabled(true);
    setAutoAcceptChildRows(true);
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setFilterKeyColumn(0);
}

ProjectFilterProxyModel::ProjectFilterProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setRecursiveFilteringEnabled(false);
    setAutoAcceptChildRows(false);
}

void ProjectFilterProxyModel::set_search_text(QString text)
{
    text = text.trimmed();
    if (search_text_ == text)
    {
        return;
    }
    beginFilterChange();
    search_text_ = std::move(text);
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

void ProjectFilterProxyModel::set_type_filter(QString type)
{
    type = type.trimmed().toLower();
    if (type_filter_ == type)
    {
        return;
    }
    beginFilterChange();
    type_filter_ = std::move(type);
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

void ProjectFilterProxyModel::set_status_filter(QString status)
{
    status = status.trimmed().toLower();
    if (status_filter_ == status)
    {
        return;
    }
    beginFilterChange();
    status_filter_ = std::move(status);
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

void ProjectFilterProxyModel::clear_search_text()
{
    set_search_text({});
}

void ProjectFilterProxyModel::clear_type_filter()
{
    set_type_filter({});
}

void ProjectFilterProxyModel::clear_status_filter()
{
    set_status_filter({});
}

void ProjectFilterProxyModel::clear_filters()
{
    if (search_text_.isEmpty() && type_filter_.isEmpty() && status_filter_.isEmpty())
    {
        return;
    }
    beginFilterChange();
    search_text_.clear();
    type_filter_.clear();
    status_filter_.clear();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

const QString& ProjectFilterProxyModel::search_text() const noexcept
{
    return search_text_;
}

const QString& ProjectFilterProxyModel::type_filter() const noexcept
{
    return type_filter_;
}

const QString& ProjectFilterProxyModel::status_filter() const noexcept
{
    return status_filter_;
}

bool ProjectFilterProxyModel::filterAcceptsRow(
    int source_row,
    const QModelIndex& source_parent) const
{
    return accepts_source_row(source_row, source_parent);
}

bool ProjectFilterProxyModel::accepts_source_row(
    int source_row,
    const QModelIndex& source_parent) const
{
    const auto* model = sourceModel();
    if (model == nullptr || source_row < 0 || source_row >= model->rowCount(source_parent))
    {
        return false;
    }
    const auto index = model->index(source_row, 0, source_parent);
    if (!index.isValid())
    {
        return false;
    }

    const auto kind = static_cast<ProjectItemKind>(
        index.data(EditorRoles::project_kind).toInt());
    const auto is_entry = kind == ProjectItemKind::scene
        || kind == ProjectItemKind::prefab
        || kind == ProjectItemKind::asset;
    const auto has_structured_filter = !type_filter_.isEmpty() || !status_filter_.isEmpty();
    if (is_entry)
    {
        if (!type_filter_.isEmpty())
        {
            const auto type_tokens = index.data(EditorRoles::project_type_filter)
                                         .toString()
                                         .split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (!type_tokens.contains(type_filter_, Qt::CaseInsensitive))
            {
                return false;
            }
        }
        if (!status_filter_.isEmpty()
            && index.data(EditorRoles::project_status_filter).toString().compare(
                   status_filter_,
                   Qt::CaseInsensitive) != 0)
        {
            return false;
        }
        if (search_text_.isEmpty() || row_matches_text(index))
        {
            return true;
        }
        return !has_structured_filter && ancestor_matches_text(source_parent);
    }

    if (!has_structured_filter)
    {
        if (search_text_.isEmpty() || row_matches_text(index)
            || ancestor_matches_text(source_parent))
        {
            return true;
        }
    }
    return has_matching_descendant(index);
}

bool ProjectFilterProxyModel::row_matches_text(const QModelIndex& source_index) const
{
    if (search_text_.isEmpty())
    {
        return true;
    }
    const auto* model = sourceModel();
    if (model == nullptr)
    {
        return false;
    }
    for (auto column = 0; column < model->columnCount(source_index.parent()); ++column)
    {
        if (source_index.siblingAtColumn(column).data(Qt::DisplayRole).toString().contains(
                search_text_,
                Qt::CaseInsensitive))
        {
            return true;
        }
    }
    const auto diagnostics = source_index.data(EditorRoles::project_diagnostics).toStringList();
    return std::any_of(diagnostics.cbegin(), diagnostics.cend(), [&](const auto& diagnostic) {
        return diagnostic.contains(search_text_, Qt::CaseInsensitive);
    });
}

bool ProjectFilterProxyModel::ancestor_matches_text(const QModelIndex& source_parent) const
{
    auto ancestor = source_parent;
    while (ancestor.isValid())
    {
        if (row_matches_text(ancestor.siblingAtColumn(0)))
        {
            return true;
        }
        ancestor = ancestor.parent();
    }
    return false;
}

bool ProjectFilterProxyModel::has_matching_descendant(const QModelIndex& source_index) const
{
    const auto* model = sourceModel();
    if (model == nullptr)
    {
        return false;
    }
    for (auto row = 0; row < model->rowCount(source_index); ++row)
    {
        if (accepts_source_row(row, source_index))
        {
            return true;
        }
    }
    return false;
}

HierarchyModel::HierarchyModel(QObject* parent) : QStandardItemModel(parent)
{
    setHorizontalHeaderLabels({QStringLiteral("GameObject"), QStringLiteral("Persistent UUID")});
    connect(this, &QStandardItemModel::itemChanged, this, [this](QStandardItem* item) {
        if (rebuilding_ || item == nullptr || item->column() != 0 || !edit_handler_)
        {
            return;
        }
        const auto id = dragonpixel::core::uuid::parse(
            item->data(EditorRoles::entity_id).toString().toStdString());
        if (!id)
        {
            return;
        }
        rebuilding_ = true;
        const auto accepted = edit_handler_(
            *id,
            item->text().trimmed(),
            item->checkState() == Qt::Checked);
        rebuilding_ = false;
        if (!accepted)
        {
            // setData()/the item delegate still owns the current QModelIndex
            // while itemChanged is emitted. Resetting the model reentrantly
            // invalidates that index and can make the delegate commit through
            // freed QStandardItems, so restore rejected values next turn.
            QMetaObject::invokeMethod(this, [this] {
                rebuild(scene_);
            }, Qt::QueuedConnection);
        }
    });
}

void HierarchyModel::set_handlers(EditHandler edit, ReparentHandler reparent)
{
    edit_handler_ = std::move(edit);
    reparent_handler_ = std::move(reparent);
}

void HierarchyModel::rebuild(const dragonpixel::scene::scene* scene)
{
    rebuilding_ = true;
    scene_ = scene;
    clear();
    setHorizontalHeaderLabels({QStringLiteral("GameObject"), QStringLiteral("Persistent UUID")});
    if (scene == nullptr)
    {
        rebuilding_ = false;
        return;
    }

    QHash<QString, QStandardItem*> names;
    QHash<QString, QStandardItem*> ids;
    for (const auto& entity : scene->entities())
    {
        const auto id = QString::fromStdString(entity.id.to_string());
        auto* name_item = new QStandardItem{QString::fromStdString(entity.name)};
        name_item->setData(id, EditorRoles::entity_id);
        name_item->setEditable(true);
        name_item->setCheckable(true);
        name_item->setCheckState(entity.enabled ? Qt::Checked : Qt::Unchecked);
        name_item->setDragEnabled(true);
        name_item->setDropEnabled(true);
        name_item->setAccessibleText(QStringLiteral("%1 GameObject").arg(name_item->text()));
        auto* id_item = new QStandardItem{id};
        id_item->setData(id, EditorRoles::entity_id);
        id_item->setEditable(false);
        names.insert(id, name_item);
        ids.insert(id, id_item);
    }

    auto entities = std::vector<std::reference_wrapper<const dragonpixel::scene::entity>>{};
    entities.reserve(scene->entities().size());
    for (const auto& entity : scene->entities())
    {
        entities.emplace_back(std::cref(entity));
    }
    std::stable_sort(entities.begin(), entities.end(), [](const auto& left, const auto& right) {
        return left.get().sibling_order < right.get().sibling_order;
    });
    for (const auto& reference : entities)
    {
        const auto& entity = reference.get();
        const auto id = QString::fromStdString(entity.id.to_string());
        QList<QStandardItem*> row{names.value(id), ids.value(id)};
        if (entity.parent_id)
        {
            const auto parent_id = QString::fromStdString(entity.parent_id->to_string());
            if (auto* parent = names.value(parent_id, nullptr))
            {
                parent->appendRow(row);
                continue;
            }
        }
        appendRow(row);
    }
    rebuilding_ = false;
}

QModelIndex HierarchyModel::index_for_entity(const dragonpixel::core::uuid& id) const
{
    return find_recursive({}, QString::fromStdString(id.to_string()));
}

QModelIndex HierarchyModel::find_recursive(const QModelIndex& parent, const QString& id) const
{
    for (int row = 0; row < rowCount(parent); ++row)
    {
        const auto current = index(row, 0, parent);
        if (current.data(EditorRoles::entity_id).toString() == id)
        {
            return current;
        }
        const auto nested = find_recursive(current, id);
        if (nested.isValid())
        {
            return nested;
        }
    }
    return {};
}

Qt::ItemFlags HierarchyModel::flags(const QModelIndex& index) const
{
    auto result = QStandardItemModel::flags(index);
    if (index.isValid())
    {
        result |= Qt::ItemIsDragEnabled;
        if (index.column() == 0)
        {
            result |= Qt::ItemIsDropEnabled;
        }
    }
    else
    {
        result |= Qt::ItemIsDropEnabled;
    }
    return result;
}

QStringList HierarchyModel::mimeTypes() const
{
    return {QString::fromLatin1(hierarchy_mime_type)};
}

QMimeData* HierarchyModel::mimeData(const QModelIndexList& indexes) const
{
    auto* mime = new QMimeData;
    for (const auto& index : indexes)
    {
        if (index.column() == 0)
        {
            mime->setData(
                QString::fromLatin1(hierarchy_mime_type),
                index.data(EditorRoles::entity_id).toString().toUtf8());
            break;
        }
    }
    return mime;
}

bool HierarchyModel::dropMimeData(
    const QMimeData* data,
    Qt::DropAction action,
    int row,
    int column,
    const QModelIndex& parent)
{
    Q_UNUSED(column);
    if (action == Qt::IgnoreAction)
    {
        return true;
    }
    if (action != Qt::MoveAction || data == nullptr || !reparent_handler_
        || !data->hasFormat(QString::fromLatin1(hierarchy_mime_type)))
    {
        return false;
    }
    const auto entity_id = dragonpixel::core::uuid::parse(
        QString::fromUtf8(data->data(QString::fromLatin1(hierarchy_mime_type))).toStdString());
    if (!entity_id)
    {
        return false;
    }
    std::optional<dragonpixel::core::uuid> parent_id;
    if (parent.isValid())
    {
        parent_id = dragonpixel::core::uuid::parse(
            parent.siblingAtColumn(0).data(EditorRoles::entity_id).toString().toStdString());
    }
    const auto sibling = row < 0 ? std::optional<std::size_t>{}
                                 : std::optional<std::size_t>{static_cast<std::size_t>(row)};
    return reparent_handler_(*entity_id, parent_id, sibling);
}

Qt::DropActions HierarchyModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

namespace
{
constexpr int project_sort_role = Qt::UserRole + 1000;

[[nodiscard]] int project_column(ProjectColumn column)
{
    return static_cast<int>(column);
}

[[nodiscard]] QString project_path_key(const QString& path)
{
    auto key = QDir::fromNativeSeparators(normalized_path(path));
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}

[[nodiscard]] QString normalize_logical_path(const QString& path)
{
    auto normalized = QDir::fromNativeSeparators(QDir::cleanPath(path.trimmed()));
    if (normalized == QStringLiteral("."))
    {
        return {};
    }
    while (normalized.startsWith(QStringLiteral("./")))
    {
        normalized.remove(0, 2);
    }
    return normalized;
}

[[nodiscard]] QString logical_path_key(const QString& path)
{
    auto key = normalize_logical_path(path);
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}

[[nodiscard]] ProjectItemStatus maximum_status(
    ProjectItemStatus left,
    ProjectItemStatus right)
{
    return static_cast<ProjectItemStatus>(
        std::max(static_cast<int>(left), static_cast<int>(right)));
}

[[nodiscard]] ProjectItemStatus diagnostic_status(ProjectIndexDiagnosticSeverity severity)
{
    switch (severity)
    {
    case ProjectIndexDiagnosticSeverity::information:
        return ProjectItemStatus::information;
    case ProjectIndexDiagnosticSeverity::warning:
        return ProjectItemStatus::warning;
    case ProjectIndexDiagnosticSeverity::error:
        return ProjectItemStatus::error;
    }
    return ProjectItemStatus::error;
}

[[nodiscard]] QString status_display_name(ProjectItemStatus status)
{
    switch (status)
    {
    case ProjectItemStatus::ready:
        return QStringLiteral("Ready");
    case ProjectItemStatus::information:
        return QStringLiteral("Information");
    case ProjectItemStatus::warning:
        return QStringLiteral("Warning");
    case ProjectItemStatus::error:
        return QStringLiteral("Error");
    }
    return QStringLiteral("Error");
}

[[nodiscard]] QString severity_display_name(ProjectIndexDiagnosticSeverity severity)
{
    switch (severity)
    {
    case ProjectIndexDiagnosticSeverity::information:
        return QStringLiteral("information");
    case ProjectIndexDiagnosticSeverity::warning:
        return QStringLiteral("warning");
    case ProjectIndexDiagnosticSeverity::error:
        return QStringLiteral("error");
    }
    return QStringLiteral("error");
}

[[nodiscard]] bool same_project_path(const QString& left, const QString& right)
{
    return !left.isEmpty() && !right.isEmpty()
        && project_path_key(left) == project_path_key(right);
}

[[nodiscard]] QList<ProjectIndexDiagnostic> sorted_diagnostics(
    QList<ProjectIndexDiagnostic> diagnostics)
{
    std::sort(diagnostics.begin(), diagnostics.end(), [](const auto& left, const auto& right) {
        const auto left_key = QStringLiteral("%1|%2|%3|%4|%5")
                                  .arg(static_cast<int>(left.severity))
                                  .arg(static_cast<int>(left.code))
                                  .arg(QDir::fromNativeSeparators(left.document_path))
                                  .arg(left.json_pointer, left.message);
        const auto right_key = QStringLiteral("%1|%2|%3|%4|%5")
                                   .arg(static_cast<int>(right.severity))
                                   .arg(static_cast<int>(right.code))
                                   .arg(QDir::fromNativeSeparators(right.document_path))
                                   .arg(right.json_pointer, right.message);
        return left_key < right_key;
    });
    return diagnostics;
}

[[nodiscard]] QList<ProjectIndexDiagnostic> diagnostics_for_path(
    const QList<ProjectIndexDiagnostic>& diagnostics,
    const QString& path)
{
    QList<ProjectIndexDiagnostic> matches;
    for (const auto& diagnostic : diagnostics)
    {
        if (same_project_path(diagnostic.document_path, path))
        {
            matches.push_back(diagnostic);
        }
    }
    return sorted_diagnostics(std::move(matches));
}

[[nodiscard]] QList<ProjectIndexDiagnostic> diagnostics_for_entry(
    const QList<ProjectIndexDiagnostic>& diagnostics,
    const ProjectIndexEntry& entry)
{
    QList<ProjectIndexDiagnostic> matches;
    for (const auto& diagnostic : diagnostics)
    {
        const auto related_ids = diagnostic.related_id.split(
            QLatin1Char(','),
            Qt::SkipEmptyParts);
        const auto id_matches = !entry.id.isEmpty()
            && std::any_of(related_ids.cbegin(), related_ids.cend(), [&](const auto& related) {
                   return related.trimmed().compare(entry.id, Qt::CaseInsensitive) == 0;
               });
        if (same_project_path(diagnostic.document_path, entry.absolute_path) || id_matches)
        {
            matches.push_back(diagnostic);
        }
    }
    return sorted_diagnostics(std::move(matches));
}

[[nodiscard]] bool has_diagnostic(
    const QList<ProjectIndexDiagnostic>& diagnostics,
    ProjectIndexDiagnosticCode code)
{
    return std::any_of(diagnostics.cbegin(), diagnostics.cend(), [code](const auto& diagnostic) {
        return diagnostic.code == code;
    });
}

[[nodiscard]] ProjectItemStatus status_for(
    const QList<ProjectIndexDiagnostic>& diagnostics,
    bool structurally_valid)
{
    auto status = structurally_valid ? ProjectItemStatus::ready : ProjectItemStatus::error;
    for (const auto& diagnostic : diagnostics)
    {
        status = maximum_status(status, diagnostic_status(diagnostic.severity));
    }
    return status;
}

[[nodiscard]] QStringList diagnostic_lines(
    const QList<ProjectIndexDiagnostic>& diagnostics)
{
    QStringList lines;
    lines.reserve(diagnostics.size());
    for (const auto& diagnostic : diagnostics)
    {
        auto location = diagnostic.json_pointer;
        if (!location.isEmpty())
        {
            location.prepend(QStringLiteral(" at "));
        }
        lines.push_back(QStringLiteral("[%1/%2]%3 %4")
                            .arg(
                                severity_display_name(diagnostic.severity),
                                project_index_diagnostic_code_name(diagnostic.code),
                                location,
                                diagnostic.message));
    }
    return lines;
}

[[nodiscard]] QString entry_import_status(
    const ProjectIndexEntry& entry,
    const QList<ProjectIndexDiagnostic>& diagnostics)
{
    if (entry.kind != ProjectIndexEntryKind::asset)
    {
        return QStringLiteral("N/A");
    }
    if (entry.source.trimmed().isEmpty()
        || has_diagnostic(diagnostics, ProjectIndexDiagnosticCode::missing_asset_source))
    {
        return QStringLiteral("Missing");
    }
    if (has_diagnostic(diagnostics, ProjectIndexDiagnosticCode::unsupported_asset_source))
    {
        return QStringLiteral("Unsupported");
    }
    const auto scheme = QUrl{entry.source}.scheme().toLower();
    if (scheme == QStringLiteral("builtin"))
    {
        return QStringLiteral("Built-in");
    }
    if (scheme == QStringLiteral("generated"))
    {
        return QStringLiteral("Generated");
    }
    if (scheme == QStringLiteral("package"))
    {
        return QStringLiteral("Package");
    }
    return QStringLiteral("Ready");
}

[[nodiscard]] QString entry_dependency_status(
    const ProjectIndexEntry& entry,
    const QList<ProjectIndexDiagnostic>& diagnostics)
{
    const auto missing = has_diagnostic(
        diagnostics,
        ProjectIndexDiagnosticCode::missing_dependency);
    const auto cycle = has_diagnostic(
        diagnostics,
        ProjectIndexDiagnosticCode::dependency_cycle);
    if (missing && cycle)
    {
        return QStringLiteral("Missing / Cycle");
    }
    if (missing)
    {
        return QStringLiteral("Missing");
    }
    if (cycle)
    {
        return QStringLiteral("Cycle");
    }
    if (entry.dependencies.isEmpty())
    {
        return QStringLiteral("None");
    }
    return QStringLiteral("Resolved (%1)").arg(entry.dependencies.size());
}

[[nodiscard]] QString project_sort_key(
    ProjectItemKind kind,
    const QString& name,
    const QString& path,
    const QString& identifier)
{
    int group = 2;
    if (kind == ProjectItemKind::manifest)
    {
        group = 0;
    }
    else if (kind == ProjectItemKind::folder)
    {
        group = 1;
    }
    return QStringLiteral("%1|%2|%3|%4|%5")
        .arg(group)
        .arg(name.toCaseFolded(), name, path, identifier);
}

struct ProjectRowPresentation final
{
    QString name;
    QString kind_type;
    QString identifier;
    QString absolute_path;
    QString logical_path;
    QString import_status{QStringLiteral("N/A")};
    QString dependency_status{QStringLiteral("N/A")};
    QString type_filter;
    QString asset_type;
    QStringList dependencies;
    QStringList diagnostics;
    int format_version{};
    ProjectItemKind kind{ProjectItemKind::project};
    ProjectItemStatus status{ProjectItemStatus::ready};
    bool structurally_valid{true};
};

[[nodiscard]] QString project_row_tooltip(const ProjectRowPresentation& row)
{
    QStringList lines{
        row.name,
        QStringLiteral("Kind / type: %1").arg(row.kind_type),
    };
    if (!row.identifier.isEmpty())
    {
        lines.push_back(QStringLiteral("ID: %1").arg(row.identifier));
    }
    if (!row.logical_path.isEmpty())
    {
        lines.push_back(QStringLiteral("Project path: %1").arg(row.logical_path));
    }
    if (!row.absolute_path.isEmpty())
    {
        lines.push_back(QStringLiteral("Metadata path: %1").arg(row.absolute_path));
    }
    if (row.format_version > 0)
    {
        lines.push_back(QStringLiteral("Format version: %1").arg(row.format_version));
    }
    lines.push_back(QStringLiteral("Import: %1").arg(row.import_status));
    lines.push_back(QStringLiteral("Dependencies: %1").arg(row.dependency_status));
    if (!row.dependencies.isEmpty())
    {
        lines.push_back(QStringLiteral("Dependency IDs: %1").arg(
            row.dependencies.join(QStringLiteral(", "))));
    }
    if (!row.diagnostics.isEmpty())
    {
        lines.push_back(QStringLiteral("Diagnostics:"));
        for (const auto& diagnostic : row.diagnostics)
        {
            lines.push_back(QStringLiteral("- %1").arg(diagnostic));
        }
    }
    lines.push_back(QStringLiteral("Structure: %1")
                        .arg(row.structurally_valid ? QStringLiteral("Valid")
                                                   : QStringLiteral("Invalid")));
    lines.push_back(QStringLiteral("Status: %1").arg(status_display_name(row.status)));
    return lines.join(QLatin1Char('\n'));
}

[[nodiscard]] QStandardItem* append_project_row(
    QStandardItemModel& model,
    QStandardItem* parent,
    const ProjectRowPresentation& row)
{
    const QStringList displays{
        row.name,
        row.kind_type,
        row.identifier,
        row.logical_path.isEmpty() ? row.absolute_path : row.logical_path,
        row.import_status,
        row.dependency_status,
        row.structurally_valid ? QStringLiteral("Valid") : QStringLiteral("Invalid"),
        status_display_name(row.status),
    };
    const auto tooltip = project_row_tooltip(row);
    QList<QStandardItem*> items;
    items.reserve(project_column(ProjectColumn::count));
    for (const auto& display : displays)
    {
        auto* item = new QStandardItem{display};
        item->setEditable(false);
        item->setDragEnabled(false);
        item->setDropEnabled(false);
        item->setToolTip(tooltip);
        item->setData(static_cast<int>(row.kind), EditorRoles::project_kind);
        item->setData(row.absolute_path, EditorRoles::project_path);
        item->setData(row.logical_path, EditorRoles::project_logical_path);
        item->setData(row.asset_type, EditorRoles::asset_type);
        item->setData(
            row.kind == ProjectItemKind::asset ? row.identifier : QString{},
            EditorRoles::asset_id);
        item->setData(row.identifier, EditorRoles::project_entry_id);
        item->setData(row.type_filter, EditorRoles::project_type_filter);
        item->setData(
            status_display_name(row.status).toLower(),
            EditorRoles::project_status_filter);
        item->setData(static_cast<int>(row.status), EditorRoles::project_status);
        item->setData(row.import_status, EditorRoles::project_import_status);
        item->setData(row.dependency_status, EditorRoles::project_dependency_status);
        item->setData(row.structurally_valid, EditorRoles::project_structurally_valid);
        item->setData(row.dependencies, EditorRoles::project_dependencies);
        item->setData(row.diagnostics, EditorRoles::project_diagnostics);
        item->setData(row.format_version, EditorRoles::project_format_version);
        item->setData(
            project_sort_key(row.kind, row.name, row.logical_path, row.identifier),
            project_sort_role);
        items.push_back(item);
    }
    auto* name_item = items.constFirst();
    name_item->setAccessibleText(
        QStringLiteral("%1, %2, %3")
            .arg(row.name, row.kind_type, status_display_name(row.status)));
    if (row.kind == ProjectItemKind::scene || row.kind == ProjectItemKind::prefab
        || row.kind == ProjectItemKind::asset)
    {
        name_item->setDragEnabled(true);
    }
    if (parent == nullptr)
    {
        model.appendRow(items);
    }
    else
    {
        parent->appendRow(items);
    }
    return name_item;
}

[[nodiscard]] QStandardItem* sibling_item(
    QStandardItemModel& model,
    QStandardItem* name_item,
    ProjectColumn column)
{
    if (name_item == nullptr)
    {
        return nullptr;
    }
    const auto column_index = project_column(column);
    return name_item->parent() == nullptr
        ? model.item(name_item->row(), column_index)
        : name_item->parent()->child(name_item->row(), column_index);
}

void update_aggregate_row(
    QStandardItemModel& model,
    QStandardItem* name_item,
    ProjectItemStatus status,
    bool structurally_valid)
{
    const auto status_text = status_display_name(status);
    for (auto column = 0; column < project_column(ProjectColumn::count); ++column)
    {
        auto* item = name_item->parent() == nullptr
            ? model.item(name_item->row(), column)
            : name_item->parent()->child(name_item->row(), column);
        if (item == nullptr)
        {
            continue;
        }
        item->setData(status_text.toLower(), EditorRoles::project_status_filter);
        item->setData(static_cast<int>(status), EditorRoles::project_status);
        item->setData(structurally_valid, EditorRoles::project_structurally_valid);
        auto tooltip = item->toolTip();
        const auto structure_position = tooltip.lastIndexOf(QStringLiteral("\nStructure:"));
        if (structure_position >= 0)
        {
            tooltip.truncate(structure_position);
        }
        tooltip.append(QStringLiteral("\nStructure: %1\nStatus: %2")
                           .arg(structurally_valid ? QStringLiteral("Valid")
                                                   : QStringLiteral("Invalid"),
                               status_text));
        item->setToolTip(tooltip);
    }
    if (auto* structural = sibling_item(model, name_item, ProjectColumn::structural_status))
    {
        structural->setText(
            structurally_valid ? QStringLiteral("Valid") : QStringLiteral("Invalid"));
    }
    if (auto* overall = sibling_item(model, name_item, ProjectColumn::overall_status))
    {
        overall->setText(status_text);
    }
    name_item->setAccessibleText(
        QStringLiteral("%1, %2, %3")
            .arg(name_item->text(),
                 sibling_item(model, name_item, ProjectColumn::kind_type)->text(),
                 status_text));
}

[[nodiscard]] ProjectItemStatus aggregate_container_status(
    QStandardItemModel& model,
    QStandardItem* container)
{
    auto status = static_cast<ProjectItemStatus>(
        container->data(EditorRoles::project_status).toInt());
    auto structurally_valid = container->data(
        EditorRoles::project_structurally_valid).toBool();
    for (auto row = 0; row < container->rowCount(); ++row)
    {
        auto* child = container->child(row, 0);
        if (child == nullptr)
        {
            continue;
        }
        if (static_cast<ProjectItemKind>(child->data(EditorRoles::project_kind).toInt())
            == ProjectItemKind::folder)
        {
            static_cast<void>(aggregate_container_status(model, child));
        }
        status = maximum_status(
            status,
            static_cast<ProjectItemStatus>(child->data(EditorRoles::project_status).toInt()));
        structurally_valid = structurally_valid
            && child->data(EditorRoles::project_structurally_valid).toBool();
    }
    update_aggregate_row(model, container, status, structurally_valid);
    return status;
}

void sort_project_children(QStandardItem* parent)
{
    parent->sortChildren(0, Qt::AscendingOrder);
    for (auto row = 0; row < parent->rowCount(); ++row)
    {
        if (auto* child = parent->child(row, 0); child != nullptr)
        {
            sort_project_children(child);
        }
    }
}

[[nodiscard]] ProjectItemKind model_kind(ProjectIndexEntryKind kind)
{
    switch (kind)
    {
    case ProjectIndexEntryKind::scene:
        return ProjectItemKind::scene;
    case ProjectIndexEntryKind::prefab:
        return ProjectItemKind::prefab;
    case ProjectIndexEntryKind::asset:
        return ProjectItemKind::asset;
    }
    return ProjectItemKind::asset;
}

[[nodiscard]] QString entry_kind_type(const ProjectIndexEntry& entry)
{
    switch (entry.kind)
    {
    case ProjectIndexEntryKind::scene:
        return QStringLiteral("Scene");
    case ProjectIndexEntryKind::prefab:
        return QStringLiteral("Prefab");
    case ProjectIndexEntryKind::asset:
        return entry.asset_type.trimmed().isEmpty()
            ? QStringLiteral("Asset")
            : QStringLiteral("Asset / %1").arg(entry.asset_type);
    }
    return QStringLiteral("Asset");
}

[[nodiscard]] QString entry_type_filter(const ProjectIndexEntry& entry)
{
    switch (entry.kind)
    {
    case ProjectIndexEntryKind::scene:
        return QStringLiteral("scene");
    case ProjectIndexEntryKind::prefab:
        return QStringLiteral("prefab");
    case ProjectIndexEntryKind::asset:
        return entry.asset_type.trimmed().isEmpty()
            ? QStringLiteral("asset")
            : QStringLiteral("asset %1").arg(entry.asset_type.toCaseFolded());
    }
    return QStringLiteral("asset");
}
}

ProjectModel::ProjectModel(QObject* parent) : QStandardItemModel(parent)
{
    setSortRole(project_sort_role);
    setHorizontalHeaderLabels({
        QStringLiteral("Name"),
        QStringLiteral("Kind / Type"),
        QStringLiteral("Identifier"),
        QStringLiteral("Path"),
        QStringLiteral("Import"),
        QStringLiteral("Dependencies"),
        QStringLiteral("Structure"),
        QStringLiteral("Status"),
    });
}

void ProjectModel::rebuild(const ProjectIndexCandidate& candidate)
{
    rebuild_candidate(candidate, {});
}

void ProjectModel::rebuild(const ProjectIndexBuildResult& result)
{
    if (result.candidate)
    {
        rebuild_candidate(*result.candidate, result.diagnostics);
        return;
    }
    ProjectIndexCandidate unavailable;
    if (!result.diagnostics.isEmpty())
    {
        unavailable.manifest_path = result.diagnostics.constFirst().document_path;
        unavailable.project_root = QFileInfo{unavailable.manifest_path}.absolutePath();
        unavailable.name = QStringLiteral("Project unavailable");
    }
    rebuild_candidate(unavailable, result.diagnostics);
}

void ProjectModel::rebuild(const QString& project_root, const QString& manifest_path)
{
    if (project_root.trimmed().isEmpty() && manifest_path.trimmed().isEmpty())
    {
        asset_rows_by_id_.clear();
        asset_rows_by_path_.clear();
        clear();
        setSortRole(project_sort_role);
        setHorizontalHeaderLabels({
            QStringLiteral("Name"),
            QStringLiteral("Kind / Type"),
            QStringLiteral("Identifier"),
            QStringLiteral("Path"),
            QStringLiteral("Import"),
            QStringLiteral("Dependencies"),
            QStringLiteral("Structure"),
            QStringLiteral("Status"),
        });
        return;
    }
    if (manifest_path.trimmed().isEmpty())
    {
        ProjectIndexCandidate candidate;
        candidate.project_root = normalized_path(project_root);
        candidate.name = QFileInfo{candidate.project_root}.fileName();
        rebuild(candidate);
        return;
    }
    rebuild(ProjectIndexService{}.build_candidate(manifest_path));
}

void ProjectModel::rebuild_candidate(
    const ProjectIndexCandidate& candidate,
    const QList<ProjectIndexDiagnostic>& diagnostics)
{
    asset_rows_by_id_.clear();
    asset_rows_by_path_.clear();
    clear();
    setSortRole(project_sort_role);
    setHorizontalHeaderLabels({
        QStringLiteral("Name"),
        QStringLiteral("Kind / Type"),
        QStringLiteral("Identifier"),
        QStringLiteral("Path"),
        QStringLiteral("Import"),
        QStringLiteral("Dependencies"),
        QStringLiteral("Structure"),
        QStringLiteral("Status"),
    });

    if (candidate.project_root.isEmpty() && candidate.manifest_path.isEmpty()
        && diagnostics.isEmpty())
    {
        return;
    }

    const auto all_diagnostics = sorted_diagnostics(diagnostics);
    const auto project_structurally_valid = std::none_of(
        all_diagnostics.cbegin(),
        all_diagnostics.cend(),
        [](const auto& diagnostic) {
            return diagnostic.severity == ProjectIndexDiagnosticSeverity::error;
        });
    auto project_name = candidate.name.trimmed();
    if (project_name.isEmpty())
    {
        project_name = QFileInfo{candidate.project_root}.fileName();
    }
    if (project_name.isEmpty())
    {
        project_name = QStringLiteral("Project unavailable");
    }

    ProjectRowPresentation project_row;
    project_row.name = project_name;
    project_row.kind_type = QStringLiteral("Project");
    project_row.identifier = candidate.project_id;
    project_row.absolute_path = normalized_path(candidate.project_root);
    project_row.logical_path = {};
    project_row.type_filter = QStringLiteral("project");
    project_row.diagnostics = diagnostic_lines(all_diagnostics);
    project_row.format_version = candidate.format_version;
    project_row.kind = ProjectItemKind::project;
    project_row.status = status_for(all_diagnostics, project_structurally_valid);
    project_row.structurally_valid = project_structurally_valid;
    auto* project_item = append_project_row(*this, nullptr, project_row);

    if (!candidate.manifest_path.isEmpty())
    {
        const auto manifest_diagnostics = diagnostics_for_path(
            all_diagnostics,
            candidate.manifest_path);
        const auto manifest_valid = std::none_of(
            manifest_diagnostics.cbegin(),
            manifest_diagnostics.cend(),
            [](const auto& diagnostic) {
                return diagnostic.severity == ProjectIndexDiagnosticSeverity::error;
            });
        ProjectRowPresentation manifest_row;
        manifest_row.name = QFileInfo{candidate.manifest_path}.fileName();
        manifest_row.kind_type = QStringLiteral("Manifest");
        manifest_row.identifier = candidate.project_id;
        manifest_row.absolute_path = normalized_path(candidate.manifest_path);
        manifest_row.logical_path = QFileInfo{candidate.manifest_path}.fileName();
        manifest_row.type_filter = QStringLiteral("manifest");
        manifest_row.diagnostics = diagnostic_lines(manifest_diagnostics);
        manifest_row.format_version = candidate.format_version;
        manifest_row.kind = ProjectItemKind::manifest;
        manifest_row.status = status_for(manifest_diagnostics, manifest_valid);
        manifest_row.structurally_valid = manifest_valid;
        static_cast<void>(append_project_row(*this, project_item, manifest_row));
    }

    QHash<QString, QStandardItem*> folders;
    const auto ensure_folder = [&](const QString& requested_path) -> QStandardItem* {
        auto* parent = project_item;
        auto accumulated = QString{};
        const auto segments = normalize_logical_path(requested_path).split(
            QLatin1Char('/'),
            Qt::SkipEmptyParts);
        for (const auto& segment : segments)
        {
            if (segment == QStringLiteral("."))
            {
                continue;
            }
            accumulated = accumulated.isEmpty()
                ? segment
                : QStringLiteral("%1/%2").arg(accumulated, segment);
            const auto key = logical_path_key(accumulated);
            if (auto* existing = folders.value(key, nullptr); existing != nullptr)
            {
                parent = existing;
                continue;
            }
            ProjectRowPresentation folder_row;
            folder_row.name = segment;
            folder_row.kind_type = QStringLiteral("Folder");
            folder_row.absolute_path = normalized_path(
                QDir{candidate.project_root}.filePath(accumulated));
            folder_row.logical_path = accumulated;
            folder_row.type_filter = QStringLiteral("folder");
            folder_row.kind = ProjectItemKind::folder;
            parent = append_project_row(*this, parent, folder_row);
            folders.insert(key, parent);
        }
        return parent;
    };

    auto roots = candidate.roots;
    std::sort(roots.begin(), roots.end(), [](const auto& left, const auto& right) {
        const auto left_path = normalize_logical_path(left.declared_path);
        const auto right_path = normalize_logical_path(right.declared_path);
        const auto folded_comparison = QString::compare(
            left_path.toCaseFolded(),
            right_path.toCaseFolded(),
            Qt::CaseSensitive);
        if (folded_comparison != 0)
        {
            return folded_comparison < 0;
        }
        if (left_path != right_path)
        {
            return left_path < right_path;
        }
        return static_cast<int>(left.kind) < static_cast<int>(right.kind);
    });
    for (const auto& root : roots)
    {
        static_cast<void>(ensure_folder(root.declared_path));
    }

    auto entries = candidate.entries;
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        const auto left_path = normalize_logical_path(left.logical_path);
        const auto right_path = normalize_logical_path(right.logical_path);
        const auto folded_comparison = QString::compare(
            left_path.toCaseFolded(),
            right_path.toCaseFolded(),
            Qt::CaseSensitive);
        if (folded_comparison != 0)
        {
            return folded_comparison < 0;
        }
        if (left_path != right_path)
        {
            return left_path < right_path;
        }
        if (left.kind != right.kind)
        {
            return static_cast<int>(left.kind) < static_cast<int>(right.kind);
        }
        return left.id < right.id;
    });

    for (const auto& entry : entries)
    {
        const auto logical_path = normalize_logical_path(entry.logical_path);
        auto parent_path = QFileInfo{logical_path}.path();
        if (parent_path == QStringLiteral("."))
        {
            parent_path.clear();
        }
        auto* parent = ensure_folder(parent_path);
        const auto entry_diagnostics = diagnostics_for_entry(all_diagnostics, entry);
        auto display_name = entry.display_name.trimmed();
        if (display_name.isEmpty())
        {
            display_name = entry.document.value(QStringLiteral("name")).toString().trimmed();
        }
        if (display_name.isEmpty())
        {
            display_name = QFileInfo{logical_path}.completeBaseName();
        }

        ProjectRowPresentation row;
        row.name = display_name;
        row.kind_type = entry_kind_type(entry);
        row.identifier = entry.id;
        row.absolute_path = entry.absolute_path.isEmpty()
            ? normalized_path(QDir{candidate.project_root}.filePath(logical_path))
            : normalized_path(entry.absolute_path);
        row.logical_path = logical_path;
        row.import_status = entry_import_status(entry, entry_diagnostics);
        row.dependency_status = entry_dependency_status(entry, entry_diagnostics);
        row.type_filter = entry_type_filter(entry);
        row.asset_type = entry.asset_type;
        row.dependencies = entry.dependencies;
        row.dependencies.removeDuplicates();
        row.dependencies.sort();
        row.diagnostics = diagnostic_lines(entry_diagnostics);
        row.format_version = entry.format_version;
        row.kind = model_kind(entry.kind);
        row.status = status_for(entry_diagnostics, entry.structurally_valid);
        row.structurally_valid = entry.structurally_valid;
        auto* item = append_project_row(*this, parent, row);
        if (row.kind == ProjectItemKind::asset)
        {
            const auto persistent = QPersistentModelIndex{indexFromItem(item)};
            if (!entry.id.isEmpty())
            {
                asset_rows_by_id_[entry.id.toLower()].push_back(persistent);
            }
            asset_rows_by_path_.insert(project_path_key(row.absolute_path), persistent);
        }
    }

    sort_project_children(project_item);
    static_cast<void>(aggregate_container_status(*this, project_item));
}

bool ProjectModel::set_asset_preview(
    const QString& asset_id_or_metadata_path,
    const QIcon& icon,
    const QString& content_identity)
{
    if (asset_id_or_metadata_path.trimmed().isEmpty() || icon.isNull()
        || content_identity.trimmed().isEmpty())
    {
        return false;
    }

    QPersistentModelIndex target;
    const auto id_rows = asset_rows_by_id_.constFind(
        asset_id_or_metadata_path.trimmed().toLower());
    if (id_rows != asset_rows_by_id_.cend())
    {
        if (id_rows->size() != 1)
        {
            return false;
        }
        target = id_rows->constFirst();
    }
    else
    {
        target = asset_rows_by_path_.value(
            project_path_key(asset_id_or_metadata_path));
    }

    if (!target.isValid() || item_kind(target) != ProjectItemKind::asset)
    {
        return false;
    }
    auto* item = itemFromIndex(target);
    if (item == nullptr)
    {
        return false;
    }
    item->setIcon(icon);
    item->setData(content_identity, EditorRoles::preview_content_identity);
    return true;
}

ProjectItemKind ProjectModel::item_kind(const QModelIndex& index)
{
    return static_cast<ProjectItemKind>(
        index.siblingAtColumn(0).data(EditorRoles::project_kind).toInt());
}

QString ProjectModel::item_path(const QModelIndex& index)
{
    return index.siblingAtColumn(0).data(EditorRoles::project_path).toString();
}

QString ProjectModel::asset_type(const QModelIndex& index)
{
    return index.siblingAtColumn(0).data(EditorRoles::asset_type).toString();
}

QString ProjectModel::asset_id(const QModelIndex& index)
{
    return index.siblingAtColumn(0).data(EditorRoles::asset_id).toString();
}

QStringList ProjectModel::mimeTypes() const
{
    return {QStringLiteral("application/x-dragonpixel-project-item")};
}

QMimeData* ProjectModel::mimeData(const QModelIndexList& indexes) const
{
    auto* mime = new QMimeData;
    const auto found = std::find_if(indexes.begin(), indexes.end(), [](const QModelIndex& index) {
        return index.isValid() && index.column() == 0;
    });
    if (found == indexes.end())
    {
        return mime;
    }
    const auto kind = item_kind(*found);
    auto kind_text = QStringLiteral("other");
    if (kind == ProjectItemKind::asset)
    {
        kind_text = QStringLiteral("asset");
    }
    else if (kind == ProjectItemKind::prefab)
    {
        kind_text = QStringLiteral("prefab");
    }
    else if (kind == ProjectItemKind::scene)
    {
        kind_text = QStringLiteral("scene");
    }
    const QJsonObject payload{
        {QStringLiteral("path"), item_path(*found)},
        {QStringLiteral("kind"), kind_text},
        {QStringLiteral("assetType"), asset_type(*found)},
        {QStringLiteral("assetId"), asset_id(*found)},
    };
    mime->setData(
        QStringLiteral("application/x-dragonpixel-project-item"),
        QJsonDocument(payload).toJson(QJsonDocument::Compact));
    return mime;
}

Qt::DropActions ProjectModel::supportedDragActions() const
{
    return Qt::CopyAction;
}

namespace
{
[[nodiscard]] int console_column(ConsoleColumn column)
{
    return static_cast<int>(column);
}

[[nodiscard]] const QStringList& console_headers()
{
    static const QStringList headers{
        QStringLiteral("Time"),
        QStringLiteral("Severity"),
        QStringLiteral("Subsystem"),
        QStringLiteral("Worker"),
        QStringLiteral("Session"),
        QStringLiteral("Correlation ID"),
        QStringLiteral("Context"),
        QStringLiteral("Message"),
    };
    return headers;
}

[[nodiscard]] QString console_field(const ConsoleEntry& entry, int column)
{
    switch (static_cast<ConsoleColumn>(column))
    {
    case ConsoleColumn::timestamp:
        return entry.timestamp;
    case ConsoleColumn::severity:
        return entry.severity;
    case ConsoleColumn::subsystem:
        return entry.subsystem;
    case ConsoleColumn::worker:
        return entry.worker;
    case ConsoleColumn::session:
        return entry.session;
    case ConsoleColumn::correlation_id:
        return entry.correlation_id;
    case ConsoleColumn::context:
        return entry.context;
    case ConsoleColumn::message:
        return entry.message;
    case ConsoleColumn::count:
        break;
    }
    return {};
}

[[nodiscard]] QStringList console_fields(const ConsoleEntry& entry)
{
    QStringList fields;
    fields.reserve(console_column(ConsoleColumn::count));
    for (auto column = 0; column < console_column(ConsoleColumn::count); ++column)
    {
        fields.push_back(console_field(entry, column));
    }
    return fields;
}

[[nodiscard]] QString console_tooltip(const ConsoleEntry& entry)
{
    QStringList lines{
        QStringLiteral("Time: %1").arg(entry.timestamp),
        QStringLiteral("Severity: %1").arg(entry.severity),
        QStringLiteral("Subsystem: %1").arg(entry.subsystem),
    };
    if (!entry.worker.isEmpty())
    {
        lines.push_back(QStringLiteral("Worker: %1").arg(entry.worker));
    }
    if (!entry.session.isEmpty())
    {
        lines.push_back(QStringLiteral("Session: %1").arg(entry.session));
    }
    if (!entry.correlation_id.isEmpty())
    {
        lines.push_back(QStringLiteral("Correlation ID: %1").arg(entry.correlation_id));
    }
    if (!entry.context.isEmpty())
    {
        lines.push_back(QStringLiteral("Context: %1").arg(entry.context));
    }
    if (!entry.entity_id.isEmpty())
    {
        lines.push_back(QStringLiteral("Entity: %1").arg(entry.entity_id));
    }
    if (!entry.asset_id.isEmpty())
    {
        lines.push_back(QStringLiteral("Asset: %1").arg(entry.asset_id));
    }
    if (!entry.navigation_path.isEmpty())
    {
        lines.push_back(QStringLiteral("Path: %1").arg(entry.navigation_path));
    }
    lines.push_back(QStringLiteral("Message: %1").arg(entry.message));
    return lines.join(QLatin1Char('\n'));
}

[[nodiscard]] QString console_filter_text(const ConsoleEntry& entry)
{
    auto fields = console_fields(entry);
    fields.push_back(entry.entity_id);
    fields.push_back(entry.asset_id);
    fields.push_back(entry.navigation_path);
    return fields.join(QLatin1Char(' '));
}

[[nodiscard]] QList<int> normalized_console_rows(
    const QList<int>& requested,
    int entry_count)
{
    QList<int> rows = requested;
    if (rows.isEmpty())
    {
        rows.reserve(entry_count);
        for (auto row = 0; row < entry_count; ++row)
        {
            rows.push_back(row);
        }
        return rows;
    }
    rows.erase(
        std::remove_if(rows.begin(), rows.end(), [entry_count](int row) {
            return row < 0 || row >= entry_count;
        }),
        rows.end());
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    return rows;
}

[[nodiscard]] QString quote_csv_field(QString value)
{
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(value);
}

[[nodiscard]] QString quote_plain_field(QString value)
{
    value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    value.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    value.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    value.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
    return QStringLiteral("\"%1\"").arg(value);
}
}

ConsoleModel::ConsoleModel(QObject* parent) : QAbstractTableModel(parent) {}

int ConsoleModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : entries_.size();
}

int ConsoleModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : console_column(ConsoleColumn::count);
}

QVariant ConsoleModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= entries_.size())
    {
        return {};
    }
    const auto& entry = entries_.at(index.row());
    if (role == Qt::DisplayRole)
    {
        return console_field(entry, index.column());
    }
    if (role == Qt::ToolTipRole)
    {
        return console_tooltip(entry);
    }
    if (role == Qt::AccessibleTextRole)
    {
        return QStringLiteral("%1 %2: %3")
            .arg(entry.severity, entry.subsystem, entry.message);
    }
    if (role == EditorRoles::console_timestamp)
    {
        return entry.timestamp;
    }
    if (role == EditorRoles::console_severity)
    {
        return entry.severity;
    }
    if (role == EditorRoles::console_subsystem)
    {
        return entry.subsystem;
    }
    if (role == EditorRoles::console_worker)
    {
        return entry.worker;
    }
    if (role == EditorRoles::console_session)
    {
        return entry.session;
    }
    if (role == EditorRoles::console_correlation_id)
    {
        return entry.correlation_id;
    }
    if (role == EditorRoles::console_context)
    {
        return entry.context;
    }
    if (role == EditorRoles::console_message)
    {
        return entry.message;
    }
    if (role == EditorRoles::console_entity_id)
    {
        return entry.entity_id;
    }
    if (role == EditorRoles::console_asset_id)
    {
        return entry.asset_id;
    }
    if (role == EditorRoles::console_navigation_path)
    {
        return entry.navigation_path;
    }
    if (role == EditorRoles::console_filter_text)
    {
        return console_filter_text(entry);
    }
    if (role == Qt::ForegroundRole)
    {
        if (entry.severity.compare(QStringLiteral("Error"), Qt::CaseInsensitive) == 0)
        {
            return QColor{220, 70, 70};
        }
        if (entry.severity.compare(QStringLiteral("Warning"), Qt::CaseInsensitive) == 0)
        {
            return QColor{220, 160, 40};
        }
    }
    return {};
}

QVariant ConsoleModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    {
        return {};
    }
    const auto& headers = console_headers();
    return section >= 0 && section < headers.size() ? headers.at(section) : QVariant{};
}

void ConsoleModel::append(ConsoleEntry entry)
{
    constexpr auto maximum_entries = 2000;
    if (entries_.size() >= maximum_entries)
    {
        beginRemoveRows({}, 0, 0);
        entries_.removeFirst();
        endRemoveRows();
    }
    const auto row = entries_.size();
    beginInsertRows({}, row, row);
    entries_.push_back(std::move(entry));
    endInsertRows();
}

void ConsoleModel::clear()
{
    if (entries_.isEmpty())
    {
        return;
    }
    beginResetModel();
    entries_.clear();
    endResetModel();
}

const ConsoleEntry* ConsoleModel::entry_at(int row) const
{
    return row >= 0 && row < entries_.size() ? &entries_.at(row) : nullptr;
}

QString ConsoleModel::copy_plain_text(const QList<int>& rows) const
{
    QStringList lines;
    for (const auto row : normalized_console_rows(rows, entries_.size()))
    {
        auto fields = console_fields(entries_.at(row));
        std::transform(fields.begin(), fields.end(), fields.begin(), [](QString value) {
            return quote_plain_field(std::move(value));
        });
        lines.push_back(fields.join(QLatin1Char('\t')));
    }
    return lines.join(QLatin1Char('\n'));
}

QString ConsoleModel::export_csv(const QList<int>& rows, bool include_header) const
{
    QStringList lines;
    if (include_header)
    {
        auto headers = console_headers();
        std::transform(headers.begin(), headers.end(), headers.begin(), [](QString value) {
            return quote_csv_field(std::move(value));
        });
        lines.push_back(headers.join(QLatin1Char(',')));
    }
    for (const auto row : normalized_console_rows(rows, entries_.size()))
    {
        auto fields = console_fields(entries_.at(row));
        std::transform(fields.begin(), fields.end(), fields.begin(), [](QString value) {
            return quote_csv_field(std::move(value));
        });
        lines.push_back(fields.join(QLatin1Char(',')));
    }
    return lines.join(QStringLiteral("\r\n"));
}

InspectorDelegate::InspectorDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

QWidget* InspectorDelegate::createEditor(
    QWidget* parent,
    const QStyleOptionViewItem& option,
    const QModelIndex& index) const
{
    Q_UNUSED(option);
    const auto type = static_cast<dragonpixel::metadata::value_type>(
        index.data(EditorRoles::value_type).toInt());
    if (type == dragonpixel::metadata::value_type::boolean)
    {
        auto* editor = new QComboBox{parent};
        editor->setObjectName(QStringLiteral("InspectorBooleanEditor"));
        editor->setProperty("booleanEditor", true);
        editor->addItems({QStringLiteral("false"), QStringLiteral("true")});
        return editor;
    }
    if (type == dragonpixel::metadata::value_type::integer)
    {
        auto* editor = new QSpinBox{parent};
        editor->setObjectName(QStringLiteral("InspectorIntegerEditor"));
        editor->setRange(
            index.data(EditorRoles::minimum).isValid() ? index.data(EditorRoles::minimum).toInt() : -1000000000,
            index.data(EditorRoles::maximum).isValid() ? index.data(EditorRoles::maximum).toInt() : 1000000000);
        if (index.data(EditorRoles::step).isValid())
        {
            editor->setSingleStep(std::max(1, index.data(EditorRoles::step).toInt()));
        }
        return editor;
    }
    if (type == dragonpixel::metadata::value_type::number)
    {
        auto* editor = new QDoubleSpinBox{parent};
        editor->setObjectName(QStringLiteral("InspectorNumberEditor"));
        editor->setDecimals(5);
        editor->setRange(
            index.data(EditorRoles::minimum).isValid() ? index.data(EditorRoles::minimum).toDouble() : -1000000000.0,
            index.data(EditorRoles::maximum).isValid() ? index.data(EditorRoles::maximum).toDouble() : 1000000000.0);
        if (index.data(EditorRoles::step).isValid())
        {
            editor->setSingleStep(index.data(EditorRoles::step).toDouble());
        }
        return editor;
    }
    if (type == dragonpixel::metadata::value_type::vector2
        || type == dragonpixel::metadata::value_type::vector3
        || type == dragonpixel::metadata::value_type::quaternion
        || type == dragonpixel::metadata::value_type::color)
    {
        return new JsonTupleEditor{type, parent};
    }
    const auto choices = index.data(EditorRoles::enum_choices).toStringList();
    if (!choices.isEmpty())
    {
        auto* editor = new QComboBox{parent};
        editor->setObjectName(QStringLiteral("InspectorEnumEditor"));
        editor->setProperty("enumEditor", true);
        editor->addItems(choices);
        editor->setAccessibleName(QStringLiteral("Inspector enum choice"));
        return editor;
    }
    auto* editor = new QLineEdit{parent};
    editor->setObjectName(QStringLiteral("InspectorValueEditor"));
    editor->setAccessibleName(QStringLiteral("Inspector property value"));
    return editor;
}

void InspectorDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
    const auto text = index.data(Qt::EditRole).toString();
    if (auto* combo = qobject_cast<QComboBox*>(editor))
    {
        if (combo->property("booleanEditor").toBool())
        {
            combo->setCurrentIndex(text == QStringLiteral("true") ? 1 : 0);
        }
        else
        {
            const auto value = parse_json_value(text).toString(text);
            combo->setCurrentIndex(std::max(0, combo->findText(value)));
        }
    }
    else if (auto* integer = qobject_cast<QSpinBox*>(editor))
    {
        integer->setValue(text.toInt());
    }
    else if (auto* number = qobject_cast<QDoubleSpinBox*>(editor))
    {
        number->setValue(text.toDouble());
    }
    else if (auto* line = qobject_cast<QLineEdit*>(editor))
    {
        const auto value = parse_json_value(text);
        line->setText(value.isString() ? value.toString() : text);
        line->selectAll();
    }
    else if (auto* tuple = dynamic_cast<JsonTupleEditor*>(editor))
    {
        const auto object = parse_json_value(text).toObject();
        auto& fields = tuple->fields();
        if (tuple->type() == dragonpixel::metadata::value_type::quaternion)
        {
            const auto x = object.value(QStringLiteral("x")).toDouble();
            const auto y = object.value(QStringLiteral("y")).toDouble();
            const auto z = object.value(QStringLiteral("z")).toDouble();
            const auto w = object.value(QStringLiteral("w")).toDouble(1.0);
            const auto roll = std::atan2(2.0 * ((w * x) + (y * z)), 1.0 - (2.0 * ((x * x) + (y * y))));
            const auto pitch = std::asin(std::clamp(2.0 * ((w * y) - (z * x)), -1.0, 1.0));
            const auto yaw = std::atan2(2.0 * ((w * z) + (x * y)), 1.0 - (2.0 * ((y * y) + (z * z))));
            fields.at(0)->setValue(qRadiansToDegrees(roll));
            fields.at(1)->setValue(qRadiansToDegrees(pitch));
            fields.at(2)->setValue(qRadiansToDegrees(yaw));
        }
        else
        {
            const auto names = tuple->type() == dragonpixel::metadata::value_type::color
                ? QStringList{QStringLiteral("r"), QStringLiteral("g"), QStringLiteral("b"), QStringLiteral("a")}
                : tuple->type() == dragonpixel::metadata::value_type::vector2
                    ? QStringList{QStringLiteral("x"), QStringLiteral("y")}
                    : QStringList{QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")};
            for (int field_index = 0; field_index < names.size(); ++field_index)
            {
                fields.at(field_index)->setValue(object.value(names.at(field_index)).toDouble(
                    tuple->type() == dragonpixel::metadata::value_type::color && names.at(field_index) == QStringLiteral("a") ? 1.0 : 0.0));
            }
        }
    }
}

void InspectorDelegate::setModelData(
    QWidget* editor,
    QAbstractItemModel* model,
    const QModelIndex& index) const
{
    if (auto* combo = qobject_cast<QComboBox*>(editor))
    {
        model->setData(index, combo->property("booleanEditor").toBool()
            ? (combo->currentIndex() == 1 ? QStringLiteral("true") : QStringLiteral("false"))
            : serialize_json_string(combo->currentText()));
    }
    else if (auto* integer = qobject_cast<QSpinBox*>(editor))
    {
        model->setData(index, QString::number(integer->value()));
    }
    else if (auto* number = qobject_cast<QDoubleSpinBox*>(editor))
    {
        model->setData(index, QString::number(number->value(), 'g', 12));
    }
    else if (auto* line = qobject_cast<QLineEdit*>(editor))
    {
        model->setData(index, serialize_json_string(line->text()));
    }
    else if (auto* tuple = dynamic_cast<JsonTupleEditor*>(editor))
    {
        const auto& fields = tuple->fields();
        QJsonObject value;
        if (tuple->type() == dragonpixel::metadata::value_type::quaternion)
        {
            const auto roll = qDegreesToRadians(fields.at(0)->value()) * 0.5;
            const auto pitch = qDegreesToRadians(fields.at(1)->value()) * 0.5;
            const auto yaw = qDegreesToRadians(fields.at(2)->value()) * 0.5;
            const auto cr = std::cos(roll);
            const auto sr = std::sin(roll);
            const auto cp = std::cos(pitch);
            const auto sp = std::sin(pitch);
            const auto cy = std::cos(yaw);
            const auto sy = std::sin(yaw);
            value = {
                {QStringLiteral("w"), (cr * cp * cy) + (sr * sp * sy)},
                {QStringLiteral("x"), (sr * cp * cy) - (cr * sp * sy)},
                {QStringLiteral("y"), (cr * sp * cy) + (sr * cp * sy)},
                {QStringLiteral("z"), (cr * cp * sy) - (sr * sp * cy)},
            };
        }
        else
        {
            const auto names = tuple->type() == dragonpixel::metadata::value_type::color
                ? QStringList{QStringLiteral("r"), QStringLiteral("g"), QStringLiteral("b"), QStringLiteral("a")}
                : tuple->type() == dragonpixel::metadata::value_type::vector2
                    ? QStringList{QStringLiteral("x"), QStringLiteral("y")}
                    : QStringList{QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")};
            for (int field = 0; field < names.size(); ++field)
            {
                value.insert(names.at(field), fields.at(field)->value());
            }
        }
        model->setData(index, QString::fromUtf8(QJsonDocument{value}.toJson(QJsonDocument::Compact)));
    }
}
