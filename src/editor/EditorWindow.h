#pragma once

#include "AutomationBroker.h"
#include "AuthoringViewport.h"
#include "WorkerClient.h"

#include <dragonpixel/metadata/registry.h>
#include <dragonpixel/scene/scene.h>

#include <QComboBox>
#include <QDockWidget>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QTreeWidget>

#include <optional>

class QProcess;

class EditorWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit EditorWindow(QString initial_document, QWidget* parent = nullptr);

    void start_self_test(const QString& adapter);
    void crash_self_test_worker();
    [[nodiscard]] bool self_test_ready() const;
    [[nodiscard]] bool self_test_recovered() const;
    [[nodiscard]] QString self_test_diagnostics() const;
    [[nodiscard]] const QString& authoring_scene_path() const noexcept { return scene_path_; }

private:
    void build_interface();
    void load_project(const QString& path);
    void close_project();
    void load_scene(const QString& path);
    void save_scene();
    void rebuild_hierarchy();
    void rebuild_scene_summary();
    void rebuild_assets();
    void inspect_selected_entity();
    void rename_entity(QTreeWidgetItem* item, int column);
    void edit_property(QTreeWidgetItem* item, int column);
    void add_entity();
    void reparent_entity();
    void add_component();
    void remove_component();
    void refresh_preview();
    void start_automation_self_test();
    [[nodiscard]] AutomationResponse handle_automation_request(const QString& method, const QJsonObject& parameters);
    void start_play();
    void stop_play();
    void append_console(const QString& message);
    [[nodiscard]] bool run_authoring_self_test();
    [[nodiscard]] std::optional<dragonpixel::core::uuid> selected_entity_id() const;
    [[nodiscard]] dragonpixel::scene::entity const* selected_entity() const;

    dragonpixel::metadata::registry metadata_;
    std::optional<dragonpixel::scene::scene> scene_;
    QString scene_path_;
    QString project_manifest_path_;
    QString project_root_;
    QTemporaryDir runtime_directory_;
    AuthoringViewport* viewport_{};
    WorkerClient* preview_worker_{};
    WorkerClient* play_worker_{};
    AutomationBroker* automation_broker_{};
    QProcess* automation_test_process_{};
    QTreeWidget* hierarchy_{};
    QTreeWidget* inspector_{};
    QListWidget* assets_{};
    QListWidget* scene_summary_{};
    QPlainTextEdit* console_{};
    QComboBox* adapter_{};
    QDockWidget* scene_dock_{};
    QDockWidget* hierarchy_dock_{};
    QDockWidget* assets_dock_{};
    QDockWidget* inspector_dock_{};
    QDockWidget* console_dock_{};
    bool rebuilding_hierarchy_{};
    bool rebuilding_inspector_{};
    bool authoring_self_test_passed_{};
    bool automation_self_test_passed_{};
    std::optional<dragonpixel::core::uuid> self_test_entity_id_;
};
