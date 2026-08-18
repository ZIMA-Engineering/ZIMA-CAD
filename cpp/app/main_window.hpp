#pragma once

#include <zima/document/document_session.hpp>
#include <zima/kernel/occt_kernel.hpp>

#include <QMainWindow>

#include <filesystem>

class QLabel;
class QTreeWidget;
class QDialog;
class QAction;
class QCloseEvent;

namespace zima::viewer { class MeshView; }

namespace zima::app {

class MainWindow final : public QMainWindow {
public:
    MainWindow();

private:
    void create_actions();
    void create_layout();
    void rebuild();
    void new_document();
    void open_document();
    bool save_document();
    void show_box_properties(const std::string& container_id = {});
    void undo();
    void redo();
    void update_document_actions();
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
};

}  // namespace zima::app
