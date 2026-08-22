#pragma once

#include <zima/drawing/drawing_document.hpp>

#include <QMainWindow>

#include <filesystem>

class QAction;
class QComboBox;
class QLabel;
class QTabBar;
class QToolBar;

namespace zima::workspace { class Workspace; }

namespace zima::app {

class DrawingCanvas;

class DrawingWindow final : public QMainWindow {
public:
    explicit DrawingWindow(
        zima::workspace::Workspace* workspace = nullptr,
        bool create_initial_document = true);
    void edit_workspace_document(const std::string& document_id);
    [[nodiscard]] const zima::drawing::DrawingDocument& document_for_test() const {
        return document_;
    }
    void select_view_for_test(const std::string& view_id);

private:
    zima::drawing::DrawingDocument document_;
    std::filesystem::path path_;
    zima::workspace::Workspace* workspace_{};
    std::string workspace_document_id_;
    QTabBar* sheets_{};
    DrawingCanvas* canvas_{};
    QLabel* state_{};
    QToolBar* drawing_toolbar_{};
    QAction* save_action_{};
    QAction* add_sheet_action_{};
    QAction* remove_sheet_action_{};
    QAction* edit_sheet_action_{};
    QAction* edit_title_block_action_{};
    QAction* insert_view_action_{};
    QAction* projected_view_action_{};
    QAction* edit_view_action_{};
    QAction* regenerate_view_action_{};
    QAction* delete_view_action_{};
    QAction* linear_dimension_action_{};

    void create_actions();
    void create_layout();
    void new_document();
    void open_document();
    void save_document();
    void add_sheet();
    void remove_sheet();
    void edit_sheet();
    void load_frame();
    void load_title_block();
    void edit_title_block();
    void insert_view();
    void insert_view_from_file();
    void begin_view_insertion(
        std::string source_id, std::filesystem::path source_path,
        zima::kernel::ViewerMesh mesh);
    void create_projected_view();
    void edit_selected_view();
    void regenerate_selected_view();
    void delete_selected_view();
    void start_linear_dimension();
    void update_action_states();
    void refresh();
    void sync_workspace_document();
    [[nodiscard]] zima::drawing::DrawingSheet* active_sheet();
};

}  // namespace zima::app
