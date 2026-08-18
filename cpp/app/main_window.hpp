#pragma once

#include <zima/document/document_session.hpp>
#include <zima/kernel/occt_kernel.hpp>

#include <QMainWindow>

#include <filesystem>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

class QLabel;
class QTreeWidget;
class QDialog;
class QAction;
class QCloseEvent;
class QPoint;

namespace zima::viewer {
class MeshView;
}

namespace zima::app {

class MainWindow final : public QMainWindow {
public:
    MainWindow();

private:
    void create_actions();
    void create_layout();
    void rebuild(std::optional<std::size_t> history_limit = std::nullopt,
                 const std::string& active_container_id = {});
    void new_document();
    void open_document();
    bool save_document();
    void show_box_properties(const std::string& container_id = {});
    void undo();
    void redo();
    void regenerate();
    [[nodiscard]] std::vector<zima::kernel::BodyResult> calculate_document(
        const zima::document::PartDocument& document) const;
    void update_document_actions();
    void select_tree_container(const std::string& container_id);
    void show_container_context_menu(
        const std::string& container_id, const QPoint& global_position);
    void restore_container_selection();
    bool confirm_document_replacement();

protected:
    void closeEvent(QCloseEvent* event) override;

    zima::document::DocumentSession session_;
    zima::kernel::OcctKernel kernel_;
    std::filesystem::path file_path_;
    QTreeWidget* tree_{};
    zima::viewer::MeshView* viewer_{};
    QLabel* metrics_{};
    QDialog* box_properties_{};
    QAction* undo_action_{};
    QAction* redo_action_{};
    std::string selected_container_id_;
    bool rebuilding_tree_{};
    std::unordered_map<std::uint64_t, std::vector<zima::kernel::BodyResult>>
        calculated_boundaries_;
};

}  // namespace zima::app
