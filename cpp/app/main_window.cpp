#include "main_window.hpp"
#include "box_properties_dialog.hpp"

#include <zima/viewer/mesh_view.hpp>

#include <QFileDialog>
#include <QAction>
#include <QCloseEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <chrono>
#include <vector>

namespace zima::app {

MainWindow::MainWindow()
    : session_(zima::document::PartDocument::create_default()) {
    setWindowTitle(tr("ZIMA-CAD C++ – první řez"));
    resize(1100, 720);
    create_actions();
    create_layout();
    rebuild();
}

void MainWindow::create_actions() {
    auto* file = menuBar()->addMenu(tr("Soubor"));
    file->addAction(tr("Nový"), this, [this] { new_document(); });
    file->addAction(tr("Otevřít…"), this, [this] { open_document(); });
    file->addAction(tr("Uložit…"), this, [this] { save_document(); });
    auto* edit = menuBar()->addMenu(tr("Úpravy"));
    undo_action_ = edit->addAction(tr("Zpět"), this, [this] { undo(); });
    undo_action_->setShortcut(QKeySequence::Undo);
    redo_action_ = edit->addAction(tr("Znovu"), this, [this] { redo(); });
    redo_action_->setShortcut(QKeySequence::Redo);
    auto* modeling = menuBar()->addMenu(tr("Modelování"));
    modeling->addAction(tr("Kvádr…"), this, [this] { show_box_properties(); });
}

void MainWindow::create_layout() {
    auto* splitter = new QSplitter(this);
    auto* left = new QWidget(splitter);
    auto* left_layout = new QVBoxLayout(left);
    tree_ = new QTreeWidget(left);
    tree_->setHeaderHidden(true);
    left_layout->addWidget(tree_, 1);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
                if (item != nullptr && item->parent() != nullptr) {
                    show_box_properties(item->data(0, Qt::UserRole).toString().toStdString());
                }
            });

    metrics_ = new QLabel(left);
    metrics_->setWordWrap(true);
    left_layout->addWidget(metrics_);

    viewer_ = new zima::viewer::MeshView(splitter);
    splitter->addWidget(left);
    splitter->addWidget(viewer_);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);
}

void MainWindow::rebuild() {
    const auto& document = session_.document();
    tree_->clear();
    auto* root = new QTreeWidgetItem(tree_, {QString::fromStdString(document.name)});
    for (const auto& container : document.history) {
        auto* item = new QTreeWidgetItem({QString::fromStdString(container.name)});
        item->setData(0, Qt::UserRole, QString::fromStdString(container.id));
        root->addChild(item);
    }
    root->setExpanded(true);

    update_document_actions();
    if (document.history.empty()) {
        metrics_->setText(tr("Prázdný díl"));
        viewer_->set_mesh({});
        return;
    }

    const auto started = std::chrono::steady_clock::now();
    try {
        std::vector<zima::kernel::BoxOperation> operations;
        operations.reserve(document.history.size());
        for (const auto& container : document.history) {
            zima::kernel::BoxRequest box{
                container.box.length, container.box.width, container.box.height};
            box.translation = {
                container.placement.x, container.placement.y, container.placement.z};
            box.rotation_degrees = {
                container.placement.rotation_x,
                container.placement.rotation_y,
                container.placement.rotation_z,
            };
            operations.push_back({
                container.id,
                box,
                container.combine_mode == zima::document::CombineMode::Subtract
                    ? zima::kernel::BooleanOperation::Subtract
                    : zima::kernel::BooleanOperation::Add,
            });
        }
        auto result = kernel_.evaluate_boxes(operations);
        const auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        metrics_->setText(tr("Jádro: %1\nObjem: %2 mm³\nPlocha: %3 mm²\nVýpočet: %4 ms")
            .arg(QString::fromStdString(kernel_.name()))
            .arg(result.volume, 0, 'f', 3)
            .arg(result.surface_area, 0, 'f', 3)
            .arg(elapsed, 0, 'f', 3));
        viewer_->set_mesh(std::move(result.mesh));
        statusBar()->showMessage(tr("Model přepočítán"), 1500);
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Výpočet selhal"), error.what());
    }
}

void MainWindow::show_box_properties(const std::string& container_id) {
    if (box_properties_ != nullptr) {
        box_properties_->raise();
        box_properties_->activateWindow();
        return;
    }
    const auto& document = session_.document();
    const auto* edited = container_id.empty()
        ? nullptr : document.find_container(container_id);
    if (!container_id.empty() && edited == nullptr) return;
    const bool edit_mode = edited != nullptr;
    const zima::document::HistoryContainer initial = edit_mode
        ? *edited : zima::document::PartDocument::create_box_container();
    const bool allow_subtract = !document.history.empty() &&
        !(edit_mode && document.history.front().id == initial.id);
    auto* dialog = new BoxPropertiesDialog(
        initial, edit_mode, allow_subtract,
        [this, edit_mode](zima::document::HistoryContainer committed) {
            auto next = session_.document();
            if (edit_mode) {
                if (auto* target = next.find_container(committed.id)) {
                    *target = std::move(committed);
                }
            } else {
                next.history.push_back(std::move(committed));
            }
            session_.commit(std::move(next));
            rebuild();
        }, this);
    box_properties_ = dialog;
    update_document_actions();
    connect(dialog, &QObject::destroyed, this, [this] {
        box_properties_ = nullptr;
        update_document_actions();
    });
    dialog->show();
}

void MainWindow::new_document() {
    if (box_properties_ != nullptr || !confirm_document_replacement()) return;
    session_.replace(zima::document::PartDocument::create_default());
    file_path_.clear();
    rebuild();
}

void MainWindow::open_document() {
    if (box_properties_ != nullptr || !confirm_document_replacement()) return;
    const QString selected = QFileDialog::getOpenFileName(
        this, tr("Otevřít C++ prototyp"), {}, tr("ZIMA-CAD C++ Part (*.zcp.json)"));
    if (selected.isEmpty()) return;
    try {
        session_.replace(zima::document::PartDocument::load(selected.toStdString()));
        file_path_ = selected.toStdString();
        rebuild();
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Otevření selhalo"), error.what());
    }
}

bool MainWindow::save_document() {
    QString selected = file_path_.empty() ? QString{} : QString::fromStdString(file_path_.string());
    if (selected.isEmpty()) {
        selected = QFileDialog::getSaveFileName(
            this, tr("Uložit C++ prototyp"), "part.zcp.json",
            tr("ZIMA-CAD C++ Part (*.zcp.json)"));
    }
    if (selected.isEmpty()) return false;
    try {
        session_.document().save(selected.toStdString());
        file_path_ = selected.toStdString();
        session_.mark_saved();
        update_document_actions();
        statusBar()->showMessage(tr("Uloženo"), 2000);
        return true;
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Uložení selhalo"), error.what());
        return false;
    }
}

bool MainWindow::confirm_document_replacement() {
    if (!session_.is_dirty()) return true;
    const auto choice = QMessageBox::warning(
        this,
        tr("Neuložené změny"),
        tr("Dokument obsahuje neuložené změny."),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (choice == QMessageBox::Cancel) return false;
    if (choice == QMessageBox::Save) return save_document();
    return choice == QMessageBox::Discard;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (box_properties_ != nullptr) {
        box_properties_->raise();
        box_properties_->activateWindow();
        event->ignore();
        return;
    }
    if (confirm_document_replacement()) {
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::undo() {
    if (box_properties_ == nullptr && session_.undo()) rebuild();
}

void MainWindow::redo() {
    if (box_properties_ == nullptr && session_.redo()) rebuild();
}

void MainWindow::update_document_actions() {
    undo_action_->setEnabled(box_properties_ == nullptr && session_.can_undo());
    redo_action_->setEnabled(box_properties_ == nullptr && session_.can_redo());
    const QString file_name = file_path_.empty()
        ? tr("Nový díl") : QString::fromStdString(file_path_.filename().string());
    setWindowTitle(tr("ZIMA-CAD C++ – %1%2")
        .arg(file_name, session_.is_dirty() ? " *" : ""));
}

}  // namespace zima::app
