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
class QTreeWidgetItem;

namespace zima::viewer { class MeshView; struct ViewerCandidate; }

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
    QAction* plane_mate_action_{};
    QAction* axis_mate_action_{};
    QDialog* properties_dialog_{};
    struct PartRollbackContext {
        std::string part_document_id;
        std::string instance_path;
        std::size_t history_limit{};
    };
    std::optional<PartRollbackContext> part_rollback_;
    std::string active_occurrence_path_;
    std::optional<zima::assembly::MateReference> pending_mate_reference_;
    bool mate_selection_active_{};
    zima::assembly::MateKind pending_mate_kind_{
        zima::assembly::MateKind::PlaneCoincident};

    void create_layout();
    void create_actions();
    void new_assembly();
    void new_part();
    void open_part();
    void open_assembly();
    void insert_active_component();
    void regenerate_assembly();
    void start_plane_mate();
    void start_axis_mate();
    void accept_mate_reference(const zima::viewer::ViewerCandidate& candidate);
    [[nodiscard]] std::optional<zima::assembly::MateReference>
        local_mate_reference(const zima::viewer::ViewerCandidate& candidate) const;
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
    void add_assembly_tree_children(
        QTreeWidgetItem* parent,
        const std::string& assembly_document_id,
        const zima::assembly::InstancePath& parent_path,
        bool ancestor_suppressed = false);
    void add_snapshot_tree_children(
        QTreeWidgetItem* parent,
        const std::vector<zima::assembly::OccurrenceSnapshot>& snapshots,
        const std::string& owner_assembly_document_id,
        const zima::assembly::InstancePath& parent_path,
        bool ancestor_suppressed = false);
    void select_occurrence(const std::string& instance_path);
    void select_container(const std::string& container_id);
    void show_component_properties(const std::string& instance_path);
    void show_component_context_menu(
        const std::string& instance_path, const QPoint& global_position);
    [[nodiscard]] std::optional<std::string> resolve_active_occurrence(
        const std::string& part_document_id) const;
};

}  // namespace zima::app
