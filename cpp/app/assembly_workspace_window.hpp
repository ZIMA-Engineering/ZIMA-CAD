#pragma once

#include <zima/workspace/workspace.hpp>
#include <zima/kernel/occt_kernel.hpp>

#include <QMainWindow>

#include <optional>

class QAction;
class QDialog;
class QLabel;
class QTabBar;
class QTreeWidget;

namespace zima::viewer { class MeshView; }

namespace zima::app {

class AssemblyWorkspaceWindow final : public QMainWindow {
public:
    AssemblyWorkspaceWindow();

private:
    zima::workspace::Workspace workspace_;
    zima::kernel::OcctKernel kernel_;
    QTabBar* tabs_{};
    QTreeWidget* tree_{};
    zima::viewer::MeshView* viewer_{};
    QLabel* state_{};
    QAction* insert_action_{};
    QAction* regenerate_action_{};
    QAction* save_action_{};
    QAction* undo_action_{};
    QAction* redo_action_{};
    QAction* box_action_{};
    QAction* cylinder_action_{};
    QAction* regenerate_part_action_{};
    QDialog* properties_dialog_{};
    struct PartRollbackContext {
        std::string part_document_id;
        std::string occurrence_id;
        std::size_t history_limit{};
    };
    std::optional<PartRollbackContext> part_rollback_;
    std::string active_occurrence_id_;

    void create_layout();
    void create_actions();
    void new_assembly();
    void new_part();
    void open_part();
    void open_assembly();
    void insert_active_component();
    void regenerate_assembly();
    void save_active_assembly();
    void save_active_document();
    void show_primitive_properties(
        zima::document::FeatureKind feature_kind,
        const std::string& container_id = {});
    void regenerate_active_part();
    void undo();
    void redo();
    [[nodiscard]] std::vector<zima::kernel::BodyResult> calculate_part(
        const zima::document::PartDocument& document) const;
    void refresh_tabs();
    void refresh_scene();
    void select_occurrence(const std::string& instance_path);
    void select_container(const std::string& container_id);
    void show_component_properties(const std::string& occurrence_id);
    void show_component_context_menu(
        const std::string& occurrence_id, const QPoint& global_position);
    [[nodiscard]] std::string occurrence_id_for_path(
        const std::string& instance_path) const;
    [[nodiscard]] std::optional<std::string> resolve_active_occurrence(
        const std::string& part_document_id) const;
};

}  // namespace zima::app
