#pragma once

#include "ProjectIndexService.h"

#include <dragonpixel/core/uuid.h>
#include <dragonpixel/metadata/descriptor.h>
#include <dragonpixel/scene/scene.h>

#include <QAbstractTableModel>
#include <QHash>
#include <QPersistentModelIndex>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QStyledItemDelegate>

#include <functional>
#include <optional>

class QMimeData;
class QIcon;

namespace EditorRoles
{
inline constexpr int entity_id = Qt::UserRole + 1;
inline constexpr int component_type = Qt::UserRole + 2;
inline constexpr int property_id = Qt::UserRole + 3;
inline constexpr int value_type = Qt::UserRole + 4;
inline constexpr int project_path = Qt::UserRole + 5;
inline constexpr int project_kind = Qt::UserRole + 6;
inline constexpr int asset_type = Qt::UserRole + 7;
inline constexpr int entity_ids = Qt::UserRole + 8;
inline constexpr int asset_id = Qt::UserRole + 9;
inline constexpr int enum_choices = Qt::UserRole + 10;
inline constexpr int minimum = Qt::UserRole + 11;
inline constexpr int maximum = Qt::UserRole + 12;
inline constexpr int step = Qt::UserRole + 13;
inline constexpr int drawer_key = Qt::UserRole + 14;
inline constexpr int project_logical_path = Qt::UserRole + 15;
inline constexpr int project_entry_id = Qt::UserRole + 16;
inline constexpr int project_type_filter = Qt::UserRole + 17;
inline constexpr int project_status_filter = Qt::UserRole + 18;
inline constexpr int project_status = Qt::UserRole + 19;
inline constexpr int project_import_status = Qt::UserRole + 20;
inline constexpr int project_dependency_status = Qt::UserRole + 21;
inline constexpr int project_structurally_valid = Qt::UserRole + 22;
inline constexpr int project_dependencies = Qt::UserRole + 23;
inline constexpr int project_diagnostics = Qt::UserRole + 24;
inline constexpr int project_format_version = Qt::UserRole + 25;
inline constexpr int preview_content_identity = Qt::UserRole + 26;
inline constexpr int console_timestamp = Qt::UserRole + 27;
inline constexpr int console_severity = Qt::UserRole + 28;
inline constexpr int console_subsystem = Qt::UserRole + 29;
inline constexpr int console_worker = Qt::UserRole + 30;
inline constexpr int console_session = Qt::UserRole + 31;
inline constexpr int console_correlation_id = Qt::UserRole + 32;
inline constexpr int console_context = Qt::UserRole + 33;
inline constexpr int console_message = Qt::UserRole + 34;
inline constexpr int console_entity_id = Qt::UserRole + 35;
inline constexpr int console_asset_id = Qt::UserRole + 36;
inline constexpr int console_navigation_path = Qt::UserRole + 37;
inline constexpr int console_filter_text = Qt::UserRole + 38;
}

class RecursiveFilterProxyModel final : public QSortFilterProxyModel
{
public:
    explicit RecursiveFilterProxyModel(QObject* parent = nullptr);
};

class ProjectFilterProxyModel final : public QSortFilterProxyModel
{
public:
    explicit ProjectFilterProxyModel(QObject* parent = nullptr);

    void set_search_text(QString text);
    void set_type_filter(QString type);
    void set_status_filter(QString status);
    void clear_search_text();
    void clear_type_filter();
    void clear_status_filter();
    void clear_filters();

    [[nodiscard]] const QString& search_text() const noexcept;
    [[nodiscard]] const QString& type_filter() const noexcept;
    [[nodiscard]] const QString& status_filter() const noexcept;

protected:
    [[nodiscard]] bool filterAcceptsRow(
        int source_row,
        const QModelIndex& source_parent) const override;

private:
    [[nodiscard]] bool accepts_source_row(
        int source_row,
        const QModelIndex& source_parent) const;
    [[nodiscard]] bool row_matches_text(const QModelIndex& source_index) const;
    [[nodiscard]] bool ancestor_matches_text(const QModelIndex& source_parent) const;
    [[nodiscard]] bool has_matching_descendant(const QModelIndex& source_index) const;

    QString search_text_;
    QString type_filter_;
    QString status_filter_;
};

class HierarchyModel final : public QStandardItemModel
{
public:
    using EditHandler = std::function<bool(
        const dragonpixel::core::uuid&,
        const QString&,
        bool)>;
    using ReparentHandler = std::function<bool(
        const dragonpixel::core::uuid&,
        const std::optional<dragonpixel::core::uuid>&,
        std::optional<std::size_t>)>;

    explicit HierarchyModel(QObject* parent = nullptr);

    void set_handlers(EditHandler edit, ReparentHandler reparent);
    void rebuild(const dragonpixel::scene::scene* scene);
    [[nodiscard]] QModelIndex index_for_entity(const dragonpixel::core::uuid& id) const;

    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QMimeData* mimeData(const QModelIndexList& indexes) const override;
    [[nodiscard]] bool dropMimeData(
        const QMimeData* data,
        Qt::DropAction action,
        int row,
        int column,
        const QModelIndex& parent) override;
    [[nodiscard]] Qt::DropActions supportedDropActions() const override;

private:
    [[nodiscard]] QModelIndex find_recursive(
        const QModelIndex& parent,
        const QString& id) const;

    const dragonpixel::scene::scene* scene_{};
    EditHandler edit_handler_;
    ReparentHandler reparent_handler_;
    bool rebuilding_{};
};

enum class ProjectItemKind
{
    project,
    folder,
    scene,
    prefab,
    asset,
    manifest,
};

enum class ProjectItemStatus
{
    ready,
    information,
    warning,
    error,
};

enum class ProjectColumn
{
    name,
    kind_type,
    identifier,
    path,
    import_status,
    dependency_status,
    structural_status,
    overall_status,
    count,
};

class ProjectModel final : public QStandardItemModel
{
public:
    explicit ProjectModel(QObject* parent = nullptr);

    // Production callers pass the detached project-index result. These
    // overloads never touch the filesystem or rescan project roots.
    void rebuild(const ProjectIndexCandidate& candidate);
    void rebuild(const ProjectIndexBuildResult& result);

    // Compatibility wrapper for current callers. New code should build a
    // candidate off the UI path and call one of the overloads above.
    void rebuild(const QString& project_root, const QString& manifest_path);
    [[nodiscard]] bool set_asset_preview(
        const QString& asset_id_or_metadata_path,
        const QIcon& icon,
        const QString& content_identity);
    [[nodiscard]] static ProjectItemKind item_kind(const QModelIndex& index);
    [[nodiscard]] static QString item_path(const QModelIndex& index);
    [[nodiscard]] static QString asset_type(const QModelIndex& index);
    [[nodiscard]] static QString asset_id(const QModelIndex& index);
    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QMimeData* mimeData(const QModelIndexList& indexes) const override;
    [[nodiscard]] Qt::DropActions supportedDragActions() const override;

private:
    void rebuild_candidate(
        const ProjectIndexCandidate& candidate,
        const QList<ProjectIndexDiagnostic>& diagnostics);

    QHash<QString, QList<QPersistentModelIndex>> asset_rows_by_id_;
    QHash<QString, QPersistentModelIndex> asset_rows_by_path_;
};

enum class ConsoleColumn
{
    timestamp,
    severity,
    subsystem,
    worker,
    session,
    correlation_id,
    context,
    message,
    count,
};

struct ConsoleEntry final
{
    // Keep the original five fields first so existing aggregate append calls
    // remain source-compatible. New structured fields are optional.
    QString timestamp;
    QString severity;
    QString subsystem;
    QString context;
    QString message;
    QString worker{};
    QString session{};
    QString correlation_id{};
    QString entity_id{};
    QString asset_id{};
    QString navigation_path{};
};

class ConsoleModel final : public QAbstractTableModel
{
public:
    explicit ConsoleModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QVariant headerData(
        int section,
        Qt::Orientation orientation,
        int role) const override;

    void append(ConsoleEntry entry);
    void clear();
    [[nodiscard]] const ConsoleEntry* entry_at(int row) const;
    [[nodiscard]] QString copy_plain_text(const QList<int>& rows = {}) const;
    [[nodiscard]] QString export_csv(
        const QList<int>& rows = {},
        bool include_header = true) const;

private:
    QList<ConsoleEntry> entries_;
};

class InspectorDelegate final : public QStyledItemDelegate
{
public:
    explicit InspectorDelegate(QObject* parent = nullptr);

    [[nodiscard]] QWidget* createEditor(
        QWidget* parent,
        const QStyleOptionViewItem& option,
        const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(
        QWidget* editor,
        QAbstractItemModel* model,
        const QModelIndex& index) const override;
};
