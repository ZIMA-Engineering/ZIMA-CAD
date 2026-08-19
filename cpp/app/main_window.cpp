#include "main_window.hpp"
#include "primitive_properties_dialog.hpp"

#include <zima/viewer/mesh_view.hpp>

#include <QFileDialog>
#include <QAction>
#include <QFont>
#include <QCloseEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMenu>
#include <QSplitter>
#include <QStatusBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QBrush>

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
    modeling->addAction(tr("Válec…"), this, [this] { show_cylinder_properties(); });
    modeling->addAction(tr("Koule…"), this, [this] {
        show_primitive_properties(zima::document::FeatureKind::Sphere); });
    modeling->addAction(tr("Regenerovat"), this, [this] { regenerate(); });
}

void MainWindow::create_layout() {
    auto* splitter = new QSplitter(this);
    auto* left = new QWidget(splitter);
    auto* left_layout = new QVBoxLayout(left);
    tree_ = new QTreeWidget(left);
    tree_->setHeaderHidden(true);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    left_layout->addWidget(tree_, 1);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
                if (item != nullptr && item->parent() != nullptr) {
                    show_container_properties(
                        item->data(0, Qt::UserRole).toString().toStdString());
                }
            });

    metrics_ = new QLabel(left);
    metrics_->setWordWrap(true);
    left_layout->addWidget(metrics_);

    viewer_ = new zima::viewer::MeshView(splitter);
    viewer_->set_confirmation_callback([this](const auto& candidate) {
        if (candidate.kind == zima::viewer::CandidateKind::Container) {
            selected_container_id_ = candidate.owner_id;
            select_tree_container(candidate.owner_id);
        }
    });
    viewer_->set_context_menu_callback(
        [this](const auto& candidate, const QPoint& global_position) {
            if (candidate.kind == zima::viewer::CandidateKind::Container) {
                show_container_context_menu(candidate.owner_id, global_position);
            }
        });
    connect(tree_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current) {
                if (rebuilding_tree_) return;
                if (current == nullptr || current->parent() == nullptr) {
                    selected_container_id_.clear();
                    viewer_->clear_selection();
                    return;
                }
                selected_container_id_ =
                    current->data(0, Qt::UserRole).toString().toStdString();
                viewer_->confirm_container(selected_container_id_);
            });
    connect(tree_, &QTreeWidget::customContextMenuRequested, this,
            [this](const QPoint& position) {
                auto* item = tree_->itemAt(position);
                if (item == nullptr || item->parent() == nullptr) return;
                tree_->setCurrentItem(item);
                show_container_context_menu(
                    item->data(0, Qt::UserRole).toString().toStdString(),
                    tree_->viewport()->mapToGlobal(position));
            });
    splitter->addWidget(left);
    splitter->addWidget(viewer_);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);
}

void MainWindow::show_container_context_menu(
    const std::string& container_id, const QPoint& global_position) {
    if (session_.document().find_container(container_id) == nullptr) {
        return;
    }
    QMenu menu(this);
    auto* properties = menu.addAction(tr("Vlastnosti"));
    auto* select_parent = menu.addAction(tr("Vybrat nadřazený"));
    const QAction* selected = menu.exec(global_position);
    if (selected == properties) {
        show_container_properties(container_id);
    } else if (selected == select_parent) {
        tree_->setCurrentItem(tree_->topLevelItem(0));
    }
}

void MainWindow::show_container_properties(const std::string& container_id) {
    const auto* container = session_.document().find_container(container_id);
    if (container == nullptr) return;
    if (container->feature_kind == zima::document::FeatureKind::Box) {
        show_box_properties(container_id);
    } else if (container->feature_kind == zima::document::FeatureKind::Cylinder) {
        show_cylinder_properties(container_id);
    } else if (container->feature_kind == zima::document::FeatureKind::Extrusion) {
        show_primitive_properties(zima::document::FeatureKind::Extrusion, container_id);
    } else if (container->feature_kind == zima::document::FeatureKind::Revolution) {
        show_primitive_properties(zima::document::FeatureKind::Revolution, container_id);
    } else {
        show_primitive_properties(container->feature_kind, container_id);
    }
}

void MainWindow::select_tree_container(const std::string& container_id) {
    const QString expected = QString::fromStdString(container_id);
    auto* root = tree_->topLevelItem(0);
    if (root == nullptr) return;
    for (int index = 0; index < root->childCount(); ++index) {
        auto* item = root->child(index);
        if (item->data(0, Qt::UserRole).toString() == expected) {
            tree_->setCurrentItem(item);
            tree_->scrollToItem(item);
            return;
        }
    }
}

void MainWindow::restore_container_selection() {
    if (selected_container_id_.empty()) return;
    if (session_.document().find_container(selected_container_id_) == nullptr) {
        selected_container_id_.clear();
        viewer_->clear_selection();
        return;
    }
    select_tree_container(selected_container_id_);
    viewer_->confirm_container(selected_container_id_);
}

void MainWindow::rebuild(std::optional<std::size_t> history_limit,
                         const std::string& active_container_id) {
    const auto& document = session_.document();
    rebuilding_tree_ = true;
    tree_->clear();
    auto* root = new QTreeWidgetItem(tree_, {QString::fromStdString(document.name)});
    for (std::size_t index = 0; index < document.history.size(); ++index) {
        const auto& container = document.history[index];
        auto* item = new QTreeWidgetItem({QString::fromStdString(container.name)});
        item->setData(0, Qt::UserRole, QString::fromStdString(container.id));
        if (container.id == active_container_id) {
            item->setForeground(0, QBrush(QColor(70, 190, 95)));
            QFont font = item->font(0);
            font.setBold(true);
            item->setFont(0, font);
        } else if (history_limit && index >= *history_limit) {
            item->setForeground(0, QBrush(QColor(125, 125, 125)));
        }
        root->addChild(item);
    }
    root->setExpanded(true);
    rebuilding_tree_ = false;

    update_document_actions();
    const std::size_t evaluated_count = history_limit
        ? std::min(*history_limit, document.history.size())
        : document.history.size();
    if (evaluated_count == 0) {
        metrics_->setText(active_container_id.empty()
            ? tr("Prázdný díl")
            : tr("Vstup před aktivním kontejnerem je prázdný"));
        viewer_->set_mesh({});
        restore_container_selection();
        return;
    }

    const auto& calculated = session_.calculated_boundaries();
    if (calculated.size() < evaluated_count) {
        metrics_->setText(tr("Model není vypočítán. Použijte Regenerovat."));
        viewer_->set_mesh({});
        restore_container_selection();
        return;
    }
    try {
        const auto& result = calculated[evaluated_count - 1];
        metrics_->setText(tr("Jádro: %1\nObjem: %2 mm³\nPlocha: %3 mm²\nZdroj: vypočtená cache")
            .arg(QString::fromStdString(kernel_.name()))
            .arg(result.volume, 0, 'f', 3)
            .arg(result.surface_area, 0, 'f', 3));
        viewer_->set_mesh(result.mesh);
        restore_container_selection();
    } catch (const std::exception& error) {
        viewer_->set_mesh({});
        restore_container_selection();
        QMessageBox::critical(this, tr("Výpočet selhal"), error.what());
    }
}

void MainWindow::show_box_properties(const std::string& container_id) {
    show_primitive_properties(zima::document::FeatureKind::Box, container_id);
}

void MainWindow::show_cylinder_properties(const std::string& container_id) {
    show_primitive_properties(zima::document::FeatureKind::Cylinder, container_id);
}

void MainWindow::show_primitive_properties(
    zima::document::FeatureKind feature_kind,
    const std::string& container_id) {
    if (properties_dialog_ != nullptr) {
        properties_dialog_->raise();
        properties_dialog_->activateWindow();
        return;
    }
    const auto& document = session_.document();
    if (container_id.empty() &&
        (feature_kind == zima::document::FeatureKind::Extrusion ||
         feature_kind == zima::document::FeatureKind::Revolution ||
         feature_kind == zima::document::FeatureKind::Sphere ||
         feature_kind == zima::document::FeatureKind::Fillet ||
         feature_kind == zima::document::FeatureKind::Chamfer)) return;
    const auto* edited = container_id.empty()
        ? nullptr : document.find_container(container_id);
    if (!container_id.empty() &&
        (edited == nullptr || edited->feature_kind != feature_kind)) {
        return;
    }
    const bool edit_mode = edited != nullptr;
    const auto edit_index = edit_mode
        ? document.history_index(container_id) : std::optional<std::size_t>{};
    const zima::document::HistoryContainer initial = edit_mode
        ? *edited
        : feature_kind == zima::document::FeatureKind::Cylinder
            ? zima::document::PartDocument::create_cylinder_container()
        : feature_kind == zima::document::FeatureKind::Sphere
            ? zima::document::PartDocument::create_sphere_container()
            : zima::document::PartDocument::create_box_container();
    const bool allow_subtract = !document.history.empty() &&
        !(edit_mode && document.history.front().id == initial.id);
    auto* dialog = new PrimitivePropertiesDialog(
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
            auto boundaries = calculate_document(next);
            session_.commit(std::move(next), std::move(boundaries));
        }, this);
    properties_dialog_ = dialog;
    if (edit_index) rebuild(*edit_index, container_id);
    update_document_actions();
    connect(dialog, &QObject::destroyed, this, [this] {
        properties_dialog_ = nullptr;
        rebuild();
    });
    dialog->show();
}

void MainWindow::new_document() {
    if (properties_dialog_ != nullptr || !confirm_document_replacement()) return;
    session_.replace(zima::document::PartDocument::create_default());
    file_path_.clear();
    rebuild();
}

void MainWindow::open_document() {
    if (properties_dialog_ != nullptr || !confirm_document_replacement()) return;
    const QString selected = QFileDialog::getOpenFileName(
        this, tr("Otevřít C++ prototyp"), {}, tr("ZIMA-CAD C++ Part (*.zcp.json)"));
    if (selected.isEmpty()) return;
    try {
        std::vector<zima::kernel::BodyResult> loaded_boundaries;
        auto loaded_document = zima::document::PartDocument::load(
            selected.toStdString(), &loaded_boundaries);
        session_.replace(
            std::move(loaded_document), std::move(loaded_boundaries));
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
        session_.document().save(
            selected.toStdString(), session_.calculated_boundaries());
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
    if (properties_dialog_ != nullptr) {
        properties_dialog_->raise();
        properties_dialog_->activateWindow();
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
    if (properties_dialog_ == nullptr && session_.undo()) rebuild();
}

void MainWindow::redo() {
    if (properties_dialog_ == nullptr && session_.redo()) rebuild();
}

std::vector<zima::kernel::BodyResult> MainWindow::calculate_document(
    const zima::document::PartDocument& document) const {
    return kernel_.evaluate_history(document.kernel_operations());
}

void MainWindow::regenerate() {
    if (properties_dialog_ != nullptr) return;
    const auto started = std::chrono::steady_clock::now();
    try {
        session_.update_calculated_boundaries(
            calculate_document(session_.document()));
        const auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        rebuild();
        statusBar()->showMessage(
            tr("Model přepočítán za %1 ms").arg(elapsed, 0, 'f', 3), 2500);
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Výpočet selhal"), error.what());
    }
}

void MainWindow::update_document_actions() {
    undo_action_->setEnabled(properties_dialog_ == nullptr && session_.can_undo());
    redo_action_->setEnabled(properties_dialog_ == nullptr && session_.can_redo());
    const QString file_name = file_path_.empty()
        ? tr("Nový díl") : QString::fromStdString(file_path_.filename().string());
    setWindowTitle(tr("ZIMA-CAD C++ – %1%2")
        .arg(file_name, session_.is_dirty() ? " *" : ""));
}

}  // namespace zima::app
