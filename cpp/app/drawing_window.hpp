#pragma once

#include <zima/drawing/drawing_document.hpp>

#include <QMainWindow>

#include <filesystem>

class QComboBox;
class QLabel;
class QTabBar;

namespace zima::app {

class DrawingCanvas;

class DrawingWindow final : public QMainWindow {
public:
    DrawingWindow();

private:
    zima::drawing::DrawingDocument document_;
    std::filesystem::path path_;
    QTabBar* sheets_{};
    DrawingCanvas* canvas_{};
    QLabel* state_{};

    void create_actions();
    void create_layout();
    void new_document();
    void open_document();
    void save_document();
    void add_sheet();
    void remove_sheet();
    void edit_sheet();
    void insert_view();
    void create_projected_view();
    void edit_selected_view();
    void regenerate_selected_view();
    void delete_selected_view();
    void start_linear_dimension();
    void refresh();
    [[nodiscard]] zima::drawing::DrawingSheet* active_sheet();
};

}  // namespace zima::app
