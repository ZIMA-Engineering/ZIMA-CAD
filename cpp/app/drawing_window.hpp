#pragma once

#include <zima/drawing/drawing_document.hpp>

#include <QMainWindow>
#include <QPointer>
#include <QPointF>

#include <filesystem>
#include <functional>

class QAction;
class QDialog;
class QComboBox;
class QLabel;
class QPushButton;
class QDoubleSpinBox;
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
    void save_pdf();
    void export_pdf(const std::filesystem::path& path);
    void edit_workspace_document(const std::string& document_id);
    void set_formats_directory(const QString& directory);
    void set_status_handler(std::function<void(const QString&)> handler);
    [[nodiscard]] const zima::drawing::DrawingDocument& document_for_test() const {
        return document_;
    }
    void set_document_changed_handler(std::function<void()> handler) { changed_handler_=std::move(handler); }
    void set_selection_handler(std::function<void(const std::string&)> handler) { selection_handler_=std::move(handler); }
    void set_properties_handler(std::function<void(QDialog*)> handler) { properties_handler_=std::move(handler); }
    void select_view(const std::string& view_id);
    void select_view_for_test(const std::string& view_id);
    void load_frame_for_test(const std::filesystem::path& path);
    void load_title_block_for_test(const std::filesystem::path& path);
    std::optional<QPointF> title_field_center_for_test(const std::string& id) const;

private:
    zima::drawing::DrawingDocument document_;
    std::filesystem::path path_;
    zima::workspace::Workspace* workspace_{};
    std::string workspace_document_id_;
    QTabBar* sheets_{};
    QString formats_directory_;
    QComboBox* source_variant_{};
    QWidget* sheet_controls_{};
    QPointer<QDialog> view_dialog_;
    DrawingCanvas* canvas_{};
    QLabel* state_{};
    std::function<void(const QString&)> status_handler_;
    std::function<void()> changed_handler_;
    std::function<void(const std::string&)> selection_handler_;
    std::function<void(QDialog*)> properties_handler_;
    void set_status_message(const QString& message);
    QComboBox* sheet_format_{};
    QComboBox* projection_method_{};
    QComboBox* lineweight_mode_{};
    QDoubleSpinBox* scale_numerator_{};
    QDoubleSpinBox* scale_denominator_{};
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
    QAction* selection_action_{};
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
    void remove_frame();
    void load_title_block();
    void remove_title_block();
    void edit_title_block();
    void insert_view();
    void show_view_properties(zima::drawing::DrawingView view, bool creating);
    void update_source_variant();
    void create_projected_view();
    void edit_selected_view();
    void regenerate_selected_view();
    void delete_selected_view();
    void start_selection();
    void start_linear_dimension();
    void update_action_states();
    void refresh();
    void sync_workspace_document();
    void refresh_title_block_context();
    [[nodiscard]] zima::drawing::DrawingSheet* active_sheet();
};

}  // namespace zima::app
