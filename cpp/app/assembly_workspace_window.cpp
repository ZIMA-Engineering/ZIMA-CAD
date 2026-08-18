#include "assembly_workspace_window.hpp"
#include "component_properties_dialog.hpp"
#include "mate_properties_dialog.hpp"
#include "primitive_properties_dialog.hpp"

#include <zima/viewer/mesh_view.hpp>

#include <QAction>
#include <QBrush>
#include <QColor>
#include <QFileDialog>
#include <QFont>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMenu>
#include <QSplitter>
#include <QTabBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <set>
#include <type_traits>

namespace zima::app {

AssemblyWorkspaceWindow::AssemblyWorkspaceWindow() {
    setWindowTitle(tr("ZIMA-CAD C++ – Workspace sestavy"));
    resize(1180, 760);
    create_actions();
    create_layout();
    new_assembly();
}

void AssemblyWorkspaceWindow::create_actions() {
    auto* file = menuBar()->addMenu(tr("Soubor"));
    file->addAction(tr("Nový Part"), this, [this] { new_part(); });
    file->addAction(tr("Nová sestava"), this, [this] { new_assembly(); });
    file->addAction(tr("Otevřít Part…"), this, [this] { open_part(); });
    file->addAction(tr("Otevřít sestavu…"), this, [this] { open_assembly(); });
    save_action_ = file->addAction(tr("Uložit…"), this,
        [this] { save_active_document(); });
    auto* edit = menuBar()->addMenu(tr("Úpravy"));
    undo_action_ = edit->addAction(tr("Zpět"), this, [this] { undo(); });
    redo_action_ = edit->addAction(tr("Znovu"), this, [this] { redo(); });
    auto* modeling = menuBar()->addMenu(tr("Modelování"));
    box_action_ = modeling->addAction(tr("Kvádr…"), this,
        [this] { show_primitive_properties(zima::document::FeatureKind::Box); });
    cylinder_action_ = modeling->addAction(tr("Válec…"), this,
        [this] { show_primitive_properties(zima::document::FeatureKind::Cylinder); });
    regenerate_part_action_ = modeling->addAction(tr("Regenerovat Part"), this,
        [this] { regenerate_active_part(); });
    auto* assembly = menuBar()->addMenu(tr("Sestava"));
    insert_action_ = assembly->addAction(tr("Vložit aktivní dokument"), this,
        [this] { insert_active_component(); });
    regenerate_action_ = assembly->addAction(tr("Regenerovat"), this,
        [this] { regenerate_assembly(); });
    plane_mate_action_ = assembly->addAction(tr("Vazba plocha–plocha…"), this,
        [this] { start_plane_mate(); });
}

void AssemblyWorkspaceWindow::create_layout() {
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    tabs_ = new QTabBar(central);
    tabs_->setExpanding(false);
    layout->addWidget(tabs_);
    auto* splitter = new QSplitter(central);
    auto* left = new QWidget(splitter);
    auto* left_layout = new QVBoxLayout(left);
    tree_ = new QTreeWidget(left);
    tree_->setHeaderHidden(true);
    left_layout->addWidget(tree_, 1);
    state_ = new QLabel(left);
    state_->setWordWrap(true);
    left_layout->addWidget(state_);
    viewer_ = new zima::viewer::MeshView(splitter);
    viewer_->set_selection_contract({zima::viewer::CandidateKind::Occurrence});
    viewer_->set_confirmation_callback([this](const auto& candidate) {
        if (mate_selection_active_) {
            accept_mate_face(candidate);
            return;
        }
        if (candidate.kind == zima::viewer::CandidateKind::Occurrence) {
            select_occurrence(candidate.instance_path);
        } else if (candidate.kind == zima::viewer::CandidateKind::Container) {
            select_container(candidate.owner_id);
        }
    });
    viewer_->set_context_menu_callback(
        [this](const auto& candidate, const QPoint& global_position) {
            if (candidate.kind != zima::viewer::CandidateKind::Occurrence) return;
            show_component_context_menu(candidate.instance_path, global_position);
        });
    splitter->addWidget(left);
    splitter->addWidget(viewer_);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);
    setCentralWidget(central);
    connect(tabs_, &QTabBar::currentChanged, this, [this](int index) {
        if (index < 0) return;
        const std::string id = tabs_->tabData(index).toString().toStdString();
        workspace_.activate(id);
        workspace_.display_top_level(id);
        active_occurrence_path_.clear();
        refresh_scene();
    });
    connect(tree_, &QTreeWidget::itemClicked, this,
        [this](QTreeWidgetItem* item) {
            if (item == nullptr || item->parent() == nullptr) return;
            if (item->data(0, Qt::UserRole + 3).toString() == "assembly-mate") return;
            if (item->data(0, Qt::UserRole + 3).toString() == "part-container") {
                viewer_->confirm_container(
                    item->data(0, Qt::UserRole).toString().toStdString());
            } else {
                viewer_->confirm_occurrence(
                    item->data(0, Qt::UserRole + 1).toString().toStdString());
            }
        });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this,
        [this](QTreeWidgetItem* item) {
            if (item == nullptr || item->parent() == nullptr) return;
            if (item->data(0, Qt::UserRole + 3).toString() == "assembly-mate") return;
            if (item->data(0, Qt::UserRole + 3).toString() == "part-container") {
                show_primitive_properties(
                    workspace_.open_part(workspace_.active_document_id())
                        ->session.document().find_container(
                            item->data(0, Qt::UserRole).toString().toStdString())
                        ->feature_kind,
                    item->data(0, Qt::UserRole).toString().toStdString());
                return;
            }
            const std::string source_id =
                item->data(0, Qt::UserRole + 2).toString().toStdString();
            if (workspace_.find(source_id) != nullptr) {
                active_occurrence_path_ =
                    item->data(0, Qt::UserRole + 1).toString().toStdString();
                workspace_.activate(source_id);
                refresh_tabs();
                refresh_scene();
            }
        });
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_, &QTreeWidget::customContextMenuRequested, this,
        [this](const QPoint& position) {
            auto* item = tree_->itemAt(position);
            if (item == nullptr || item->parent() == nullptr) return;
            if (item->data(0, Qt::UserRole + 3).toString() == "assembly-mate") return;
            if (item->data(0, Qt::UserRole + 3).toString() == "part-container") {
                const std::string id = item->data(0, Qt::UserRole).toString().toStdString();
                const auto* part = workspace_.open_part(workspace_.active_document_id());
                const auto* container = part == nullptr
                    ? nullptr : part->session.document().find_container(id);
                if (container != nullptr) show_primitive_properties(container->feature_kind, id);
            } else {
                show_component_context_menu(
                    item->data(0, Qt::UserRole + 1).toString().toStdString(),
                    tree_->viewport()->mapToGlobal(position));
            }
        });
}

void AssemblyWorkspaceWindow::new_part() {
    auto document = zima::document::PartDocument::create_default();
    const std::string id = document.document_id;
    workspace_.add_part(std::move(document));
    workspace_.activate(id);
    workspace_.display_top_level(id);
    active_occurrence_path_.clear();
    refresh_tabs();
    refresh_scene();
}

void AssemblyWorkspaceWindow::new_assembly() {
    auto document = zima::assembly::AssemblyDocument::create_default();
    const std::string id = document.document_id;
    workspace_.add_assembly(std::move(document));
    workspace_.activate(id);
    workspace_.display_top_level(id);
    active_occurrence_path_.clear();
    refresh_tabs();
    refresh_scene();
}

void AssemblyWorkspaceWindow::open_part() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Otevřít vypočtený Part"), {}, tr("ZIMA-CAD C++ Part (*.zcp.json)"));
    if (path.isEmpty()) return;
    try {
        std::vector<zima::kernel::BodyResult> calculated;
        auto document = zima::document::PartDocument::load(path.toStdString(), &calculated);
        const std::string id = document.document_id;
        workspace_.add_part(std::move(document), std::move(calculated), path.toStdString());
        workspace_.activate(id);
        active_occurrence_path_.clear();
        refresh_tabs();
        refresh_scene();
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Otevření Partu selhalo"), error.what());
    }
}

void AssemblyWorkspaceWindow::open_assembly() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Otevřít sestavu"), {}, tr("ZIMA-CAD C++ Assembly (*.zca.json)"));
    if (path.isEmpty()) return;
    try {
        auto document = zima::assembly::AssemblyDocument::load(path.toStdString());
        const std::string id = document.document_id;
        workspace_.add_assembly(std::move(document), path.toStdString());
        workspace_.activate(id);
        workspace_.display_top_level(id);
        active_occurrence_path_.clear();
        refresh_tabs();
        refresh_scene();
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Otevření sestavy selhalo"), error.what());
    }
}

void AssemblyWorkspaceWindow::insert_active_component() {
    const std::string source_id = workspace_.active_document_id();
    const std::string assembly_id = workspace_.displayed_document_id();
    if (workspace_.open_assembly(assembly_id) == nullptr) return;
    try {
        std::string occurrence_id;
        if (const auto* part = workspace_.open_part(source_id)) {
            occurrence_id = workspace_.insert_open_part(
                assembly_id, source_id, part->session.document().name);
        } else if (const auto* source = workspace_.open_assembly(source_id)) {
            occurrence_id = workspace_.insert_open_assembly(
                assembly_id, source_id, source->session.document().name);
        } else {
            return;
        }
        refresh_scene();
        viewer_->confirm_occurrence(
            zima::assembly::InstancePath{}.child(occurrence_id).encoded());
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Vložení selhalo"), error.what());
    }
}

void AssemblyWorkspaceWindow::regenerate_assembly() {
    const std::string id = workspace_.displayed_document_id();
    try {
        workspace_.regenerate_assembly_from_open_dependencies(id);
        refresh_scene();
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Regenerace selhala"), error.what());
    }
}

void AssemblyWorkspaceWindow::start_plane_mate() {
    if (properties_dialog_ != nullptr ||
        workspace_.open_assembly(workspace_.active_document_id()) == nullptr ||
        workspace_.open_assembly(workspace_.displayed_document_id()) == nullptr) return;
    pending_mate_reference_.reset();
    mate_selection_active_ = true;
    viewer_->set_selection_contract({zima::viewer::CandidateKind::Face});
    state_->setText(tr("Vyberte pohyblivou rovinnou plochu."));
}

std::optional<zima::assembly::MateReference>
AssemblyWorkspaceWindow::local_mate_reference(
    const zima::viewer::ViewerCandidate& candidate) const {
    if (candidate.kind != zima::viewer::CandidateKind::Face ||
        candidate.owner_id.empty() || candidate.semantic_key.empty()) return std::nullopt;
    auto path = zima::assembly::InstancePath::decode(candidate.instance_path);
    if (!active_occurrence_path_.empty()) {
        const auto prefix = zima::assembly::InstancePath::decode(active_occurrence_path_);
        if (path.occurrence_ids.size() <= prefix.occurrence_ids.size() ||
            !std::equal(prefix.occurrence_ids.begin(), prefix.occurrence_ids.end(),
                        path.occurrence_ids.begin())) return std::nullopt;
        path.occurrence_ids.erase(
            path.occurrence_ids.begin(),
            path.occurrence_ids.begin() +
                static_cast<std::ptrdiff_t>(prefix.occurrence_ids.size()));
    }
    return zima::assembly::MateReference{
        zima::assembly::MateReferenceKind::Face, std::move(path),
        candidate.owner_id, candidate.semantic_key};
}

void AssemblyWorkspaceWindow::accept_mate_face(
    const zima::viewer::ViewerCandidate& candidate) {
    auto reference = local_mate_reference(candidate);
    if (!reference) {
        state_->setText(tr("Plocha nepatří do aktivní sestavy."));
        return;
    }
    if (!pending_mate_reference_) {
        pending_mate_reference_ = std::move(reference);
        state_->setText(tr("Vyberte pevnou referenční rovinnou plochu."));
        return;
    }
    try {
        auto mate = zima::assembly::AssemblyDocument::create_mate(
            "Plocha na plochu", zima::assembly::MateKind::PlaneCoincident,
            std::move(*pending_mate_reference_), std::move(*reference));
        pending_mate_reference_.reset();
        mate_selection_active_ = false;
        auto* dialog = new MatePropertiesDialog(
            std::move(mate),
            [this, assembly_id = workspace_.active_document_id()]
            (zima::assembly::AssemblyMate committed) {
                auto* assembly = workspace_.open_assembly(assembly_id);
                if (assembly == nullptr) throw std::runtime_error("Assembly is no longer open");
                auto next = assembly->session.document();
                next.add_mate(std::move(committed));
                next.calculate_mates();
                if (next.mates.back().status != zima::assembly::MateStatus::Valid) {
                    throw std::runtime_error(
                        "Vybrané plochy nejsou dvě dostupné rovnoběžné roviny");
                }
                assembly->session.commit(std::move(next));
            }, this);
        properties_dialog_ = dialog;
        connect(dialog, &QObject::destroyed, this, [this] {
            properties_dialog_ = nullptr;
            pending_mate_reference_.reset();
            mate_selection_active_ = false;
            refresh_tabs();
            refresh_scene();
        });
        dialog->show();
    } catch (const std::exception& error) {
        pending_mate_reference_.reset();
        mate_selection_active_ = false;
        viewer_->set_selection_contract({zima::viewer::CandidateKind::Occurrence});
        QMessageBox::warning(this, tr("Vazbu nelze vytvořit"), error.what());
        refresh_scene();
    }
}

void AssemblyWorkspaceWindow::save_active_assembly() {
    auto* assembly = workspace_.open_assembly(workspace_.displayed_document_id());
    if (assembly == nullptr) return;
    QString path = QString::fromStdString(assembly->path.string());
    if (path.isEmpty()) path = QFileDialog::getSaveFileName(
        this, tr("Uložit sestavu"), "assembly.zca.json",
        tr("ZIMA-CAD C++ Assembly (*.zca.json)"));
    if (path.isEmpty()) return;
    try {
        assembly->session.document().save(path.toStdString());
        assembly->path = path.toStdString();
        assembly->session.mark_saved();
        refresh_tabs();
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Uložení sestavy selhalo"), error.what());
    }
}

void AssemblyWorkspaceWindow::save_active_document() {
    if (workspace_.open_assembly(workspace_.active_document_id()) != nullptr) {
        workspace_.display_top_level(workspace_.active_document_id());
        save_active_assembly();
        return;
    }
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    QString path = QString::fromStdString(part->path.string());
    if (path.isEmpty()) path = QFileDialog::getSaveFileName(
        this, tr("Uložit Part"), "part.zcp.json",
        tr("ZIMA-CAD C++ Part (*.zcp.json)"));
    if (path.isEmpty()) return;
    try {
        part->session.document().save(path.toStdString(),
                                      part->session.calculated_boundaries());
        part->path = path.toStdString();
        part->session.mark_saved();
        refresh_tabs();
        refresh_scene();
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Uložení Partu selhalo"), error.what());
    }
}

std::vector<zima::kernel::BodyResult> AssemblyWorkspaceWindow::calculate_part(
    const zima::document::PartDocument& document) const {
    return kernel_.evaluate_history(document.kernel_operations());
}

void AssemblyWorkspaceWindow::show_primitive_properties(
    zima::document::FeatureKind feature_kind,
    const std::string& container_id) {
    if (properties_dialog_ != nullptr) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const auto& document = part->session.document();
    const auto* edited = container_id.empty()
        ? nullptr : document.find_container(container_id);
    if (!container_id.empty() &&
        (edited == nullptr || edited->feature_kind != feature_kind)) return;
    const bool edit_mode = edited != nullptr;
    std::optional<std::string> rollback_occurrence;
    const auto edit_index = edit_mode
        ? document.history_index(container_id) : std::optional<std::size_t>{};
    if (edit_mode) {
        rollback_occurrence = resolve_active_occurrence(document.document_id);
        if (!rollback_occurrence) {
            QMessageBox::warning(
                this, tr("Nejednoznačný výskyt"),
                tr("Nejprve aktivujte přesný výskyt Partu ve stromu sestavy."));
            return;
        }
    }
    const auto initial = edit_mode ? *edited
        : feature_kind == zima::document::FeatureKind::Cylinder
            ? zima::document::PartDocument::create_cylinder_container()
            : zima::document::PartDocument::create_box_container();
    const bool allow_subtract = !document.history.empty() &&
        !(edit_mode && document.history.front().id == initial.id);
    const std::string part_id = document.document_id;
    auto* dialog = new PrimitivePropertiesDialog(
        initial, edit_mode, allow_subtract,
        [this, part_id, edit_mode](zima::document::HistoryContainer committed) {
            auto* target_part = workspace_.open_part(part_id);
            if (target_part == nullptr) throw std::runtime_error("Part is no longer open");
            auto next = target_part->session.document();
            if (edit_mode) {
                auto* target = next.find_container(committed.id);
                if (target == nullptr) throw std::runtime_error("Container no longer exists");
                *target = std::move(committed);
            } else {
                next.history.push_back(std::move(committed));
            }
            auto calculated = calculate_part(next);
            target_part->session.commit(std::move(next), std::move(calculated));
        }, this);
    properties_dialog_ = dialog;
    if (edit_index) {
        part_rollback_ = PartRollbackContext{
            document.document_id, *rollback_occurrence, *edit_index};
        refresh_scene();
    }
    connect(dialog, &QObject::destroyed, this, [this] {
        properties_dialog_ = nullptr;
        part_rollback_.reset();
        refresh_tabs();
        refresh_scene();
    });
    dialog->show();
}

std::optional<std::string> AssemblyWorkspaceWindow::resolve_active_occurrence(
    const std::string& part_document_id) const {
    const auto* assembly = workspace_.open_assembly(workspace_.displayed_document_id());
    if (assembly == nullptr) return std::string{};
    if (!active_occurrence_path_.empty()) {
        try {
            const auto address = workspace_.resolve_occurrence(
                workspace_.displayed_document_id(),
                zima::assembly::InstancePath::decode(active_occurrence_path_));
            if (address && address->source_document_id == part_document_id &&
                address->source_kind == zima::assembly::ComponentSourceKind::Part) {
                return active_occurrence_path_;
            }
        } catch (const std::invalid_argument&) {
            return std::nullopt;
        }
    }
    std::optional<std::string> only;
    std::set<std::string> paths;
    for (const auto& reference : assembly->session.document()
             .build_scene().triangle_references) {
        paths.insert(reference.instance_path);
    }
    for (const auto& path : paths) {
        const auto address = workspace_.resolve_occurrence(
            workspace_.displayed_document_id(),
            zima::assembly::InstancePath::decode(path));
        if (!address || address->source_document_id != part_document_id) continue;
        if (only) return std::nullopt;
        only = path;
    }
    return only;
}

void AssemblyWorkspaceWindow::regenerate_active_part() {
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || properties_dialog_ != nullptr) return;
    try {
        part->session.update_calculated_boundaries(
            calculate_part(part->session.document()));
        refresh_tabs();
        refresh_scene();
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Regenerace Partu selhala"), error.what());
    }
}

void AssemblyWorkspaceWindow::undo() {
    if (properties_dialog_ != nullptr) return;
    if (auto* part = workspace_.open_part(workspace_.active_document_id())) {
        if (part->session.undo()) refresh_scene();
    } else if (auto* assembly = workspace_.open_assembly(workspace_.active_document_id())) {
        if (assembly->session.undo()) refresh_scene();
    }
    refresh_tabs();
}

void AssemblyWorkspaceWindow::redo() {
    if (properties_dialog_ != nullptr) return;
    if (auto* part = workspace_.open_part(workspace_.active_document_id())) {
        if (part->session.redo()) refresh_scene();
    } else if (auto* assembly = workspace_.open_assembly(workspace_.active_document_id())) {
        if (assembly->session.redo()) refresh_scene();
    }
    refresh_tabs();
}

void AssemblyWorkspaceWindow::refresh_tabs() {
    tabs_->blockSignals(true);
    while (tabs_->count() > 0) tabs_->removeTab(0);
    int active_index = 0;
    for (const auto& state : workspace_.documents()) {
        std::visit([&](const auto& document) {
            using State = std::decay_t<decltype(document)>;
            const auto& model = document.session.document();
            const int index = tabs_->addTab(QString::fromStdString(
                model.name + (document.session.is_dirty() ? " *" : "")));
            tabs_->setTabData(index, QString::fromStdString(model.document_id));
            if (model.document_id == workspace_.active_document_id()) active_index = index;
        }, state);
    }
    tabs_->setCurrentIndex(active_index);
    tabs_->blockSignals(false);
}

void AssemblyWorkspaceWindow::refresh_scene() {
    tree_->clear();
    const auto* assembly = workspace_.open_assembly(workspace_.displayed_document_id());
    if (assembly == nullptr) {
        const auto* part = workspace_.open_part(workspace_.displayed_document_id());
        if (part == nullptr) {
            viewer_->set_mesh({});
            state_->setText(tr("Není vybrán zobrazovaný dokument."));
            return;
        }
        const auto& document = part->session.document();
        auto* root = new QTreeWidgetItem(tree_, {QString::fromStdString(document.name)});
        for (std::size_t index = 0; index < document.history.size(); ++index) {
            const auto& container = document.history[index];
            auto* item = new QTreeWidgetItem(root, {QString::fromStdString(container.name)});
            item->setData(0, Qt::UserRole, QString::fromStdString(container.id));
            item->setData(0, Qt::UserRole + 3, "part-container");
            if (part_rollback_ &&
                part_rollback_->part_document_id == document.document_id) {
                if (index == part_rollback_->history_limit) {
                    item->setForeground(0, QBrush(QColor(70, 190, 95)));
                    QFont font = item->font(0);
                    font.setBold(true);
                    item->setFont(0, font);
                } else if (index > part_rollback_->history_limit) {
                    item->setForeground(0, QBrush(QColor(125, 125, 125)));
                }
            }
        }
        root->setExpanded(true);
        viewer_->set_selection_contract({zima::viewer::CandidateKind::Container});
        const auto& calculated = part->session.calculated_boundaries();
        if (part_rollback_ &&
            part_rollback_->part_document_id == document.document_id) {
            viewer_->set_mesh(part_rollback_->history_limit == 0 ||
                    calculated.size() < part_rollback_->history_limit
                ? zima::kernel::ViewerMesh{}
                : calculated[part_rollback_->history_limit - 1].mesh);
        } else {
            viewer_->set_mesh(calculated.empty()
                ? zima::kernel::ViewerMesh{} : calculated.back().mesh);
        }
        state_->setText(tr("Zobrazený Part: %1").arg(
            QString::fromStdString(document.name)));
        insert_action_->setEnabled(false);
        regenerate_action_->setEnabled(false);
        plane_mate_action_->setEnabled(false);
        save_action_->setEnabled(true);
        box_action_->setEnabled(true);
        cylinder_action_->setEnabled(true);
        regenerate_part_action_->setEnabled(true);
        undo_action_->setEnabled(part->session.can_undo());
        redo_action_->setEnabled(part->session.can_redo());
        return;
    }
    const auto& document = assembly->session.document();
    auto* root = new QTreeWidgetItem(tree_, {QString::fromStdString(document.name)});
    add_assembly_tree_children(root, document.document_id, {});
    if (!document.mates.empty()) {
        auto* mates_root = new QTreeWidgetItem(root, {tr("Vazby")});
        for (const auto& mate : document.mates) {
            QString label = QString::fromStdString(mate.name);
            if (mate.status == zima::assembly::MateStatus::Uncalculated) {
                label += tr(" [nevypočtená]");
            } else if (mate.status == zima::assembly::MateStatus::MissingReference) {
                label += tr(" [chybí reference]");
            } else if (mate.status ==
                       zima::assembly::MateStatus::UnsupportedGeometry) {
                label += tr(" [nepodporovaná geometrie]");
            }
            auto* item = new QTreeWidgetItem(mates_root, {label});
            item->setData(0, Qt::UserRole, QString::fromStdString(mate.mate_id));
            item->setData(0, Qt::UserRole + 3, "assembly-mate");
            if (mate.status == zima::assembly::MateStatus::MissingReference ||
                mate.status == zima::assembly::MateStatus::UnsupportedGeometry) {
                item->setForeground(0, QBrush(QColor(205, 65, 65)));
            } else if (mate.status == zima::assembly::MateStatus::Uncalculated) {
                item->setForeground(0, QBrush(QColor(155, 105, 55)));
            }
        }
        mates_root->setExpanded(true);
    }
    root->setExpanded(true);
    viewer_->set_selection_contract({zima::viewer::CandidateKind::Occurrence});
    if (part_rollback_ && !part_rollback_->instance_path.empty()) {
        const auto* active_part =
            workspace_.open_part(part_rollback_->part_document_id);
        if (active_part != nullptr) {
            const auto& boundaries = active_part->session.calculated_boundaries();
            zima::kernel::BodyResult boundary = part_rollback_->history_limit == 0 ||
                    boundaries.size() < part_rollback_->history_limit
                ? zima::kernel::BodyResult{}
                : boundaries[part_rollback_->history_limit - 1];
            viewer_->set_mesh(workspace_.build_scene_with_part_override(
                document.document_id,
                zima::assembly::InstancePath::decode(part_rollback_->instance_path),
                std::move(boundary)));
        } else {
            viewer_->set_mesh(document.build_scene());
        }
    } else {
        viewer_->set_mesh(document.build_scene());
    }
    state_->setText(tr("Zobrazená sestava: %1\nAktivní dokument: %2")
        .arg(QString::fromStdString(document.name),
             QString::fromStdString(workspace_.active_document_id())));
    insert_action_->setEnabled(
        workspace_.active_document_id() != workspace_.displayed_document_id() &&
        workspace_.find(workspace_.active_document_id()) != nullptr);
    regenerate_action_->setEnabled(true);
    plane_mate_action_->setEnabled(
        workspace_.open_assembly(workspace_.active_document_id()) != nullptr);
    save_action_->setEnabled(true);
    const auto* active_part = workspace_.open_part(workspace_.active_document_id());
    box_action_->setEnabled(active_part != nullptr);
    cylinder_action_->setEnabled(active_part != nullptr);
    regenerate_part_action_->setEnabled(active_part != nullptr);
    if (active_part != nullptr) {
        undo_action_->setEnabled(active_part->session.can_undo());
        redo_action_->setEnabled(active_part->session.can_redo());
    } else {
        const auto* active_assembly =
            workspace_.open_assembly(workspace_.active_document_id());
        undo_action_->setEnabled(active_assembly != nullptr &&
                                 active_assembly->session.can_undo());
        redo_action_->setEnabled(active_assembly != nullptr &&
                                 active_assembly->session.can_redo());
    }
}

void AssemblyWorkspaceWindow::add_assembly_tree_children(
    QTreeWidgetItem* parent,
    const std::string& assembly_document_id,
    const zima::assembly::InstancePath& parent_path,
    bool ancestor_suppressed) {
    const auto* assembly = workspace_.open_assembly(assembly_document_id);
    if (assembly == nullptr) return;
    add_snapshot_tree_children(
        parent, assembly->session.document().occurrence_snapshot(),
        assembly_document_id, parent_path, ancestor_suppressed);
}

void AssemblyWorkspaceWindow::add_snapshot_tree_children(
    QTreeWidgetItem* parent,
    const std::vector<zima::assembly::OccurrenceSnapshot>& snapshots,
    const std::string& owner_assembly_document_id,
    const zima::assembly::InstancePath& parent_path,
    bool ancestor_suppressed) {
    for (const auto& component : snapshots) {
        const bool suppressed = ancestor_suppressed ||
            component.manually_suppressed || component.dependency_suppressed;
        QString label = QString::fromStdString(component.name);
        if (component.manually_suppressed) label += tr(" [potlačeno]");
        else if (suppressed) {
            label += tr(" [potlačeno závislostí]");
        }
        else if (!component.visible) label += tr(" [skryto]");
        auto* item = new QTreeWidgetItem(parent, {label});
        const auto path = parent_path.child(component.occurrence_id);
        item->setData(0, Qt::UserRole, QString::fromStdString(component.occurrence_id));
        item->setData(0, Qt::UserRole + 1, QString::fromStdString(path.encoded()));
        item->setData(0, Qt::UserRole + 2,
                      QString::fromStdString(component.source_document_id));
        item->setData(0, Qt::UserRole + 3,
            component.source_kind == zima::assembly::ComponentSourceKind::Assembly
                ? "assembly-occurrence" : "part-occurrence");
        item->setData(0, Qt::UserRole + 4,
                      QString::fromStdString(owner_assembly_document_id));
        const bool active_assembly_occurrence =
            component.source_kind == zima::assembly::ComponentSourceKind::Assembly &&
            path.encoded() == active_occurrence_path_ &&
            component.source_document_id == workspace_.active_document_id();
        if ((part_rollback_ && path.encoded() == part_rollback_->instance_path) ||
            active_assembly_occurrence) {
            item->setForeground(0, QBrush(QColor(70, 190, 95)));
            QFont font = item->font(0);
            font.setBold(true);
            item->setFont(0, font);
        } else if (suppressed || !component.visible) {
            item->setForeground(0, QBrush(QColor(125, 125, 125)));
        }
        if (component.source_kind == zima::assembly::ComponentSourceKind::Assembly) {
            const auto* active_source = active_assembly_occurrence
                ? workspace_.open_assembly(component.source_document_id) : nullptr;
            if (active_source != nullptr) {
                add_snapshot_tree_children(
                    item, active_source->session.document().occurrence_snapshot(),
                    component.source_document_id, path,
                    suppressed || !component.visible);
            } else {
                add_snapshot_tree_children(
                    item, component.children, component.source_document_id, path,
                    suppressed || !component.visible);
            }
            item->setExpanded(true);
        }
    }
}

void AssemblyWorkspaceWindow::select_container(const std::string& container_id) {
    auto* root = tree_->topLevelItem(0);
    if (root == nullptr) return;
    for (int index = 0; index < root->childCount(); ++index) {
        auto* item = root->child(index);
        if (item->data(0, Qt::UserRole).toString().toStdString() == container_id) {
            tree_->setCurrentItem(item);
            return;
        }
    }
}

void AssemblyWorkspaceWindow::select_occurrence(const std::string& instance_path) {
    auto* root = tree_->topLevelItem(0);
    if (root == nullptr) return;
    std::vector<QTreeWidgetItem*> pending{root};
    while (!pending.empty()) {
        auto* item = pending.back();
        pending.pop_back();
        if (item->data(0, Qt::UserRole + 1).toString().toStdString() == instance_path) {
            tree_->setCurrentItem(item);
            return;
        }
        for (int index = 0; index < item->childCount(); ++index) {
            pending.push_back(item->child(index));
        }
    }
}

void AssemblyWorkspaceWindow::show_component_properties(
    const std::string& instance_path) {
    if (properties_dialog_ != nullptr) return;
    std::optional<zima::workspace::OccurrenceAddress> address;
    try {
        address = workspace_.resolve_occurrence(
            workspace_.displayed_document_id(),
            zima::assembly::InstancePath::decode(instance_path));
    } catch (const std::invalid_argument&) {
        return;
    }
    if (!address) return;
    auto* assembly = workspace_.open_assembly(address->owner_assembly_document_id);
    if (assembly == nullptr) return;
    const auto* occurrence = assembly->session.document().find_occurrence(
        address->occurrence_id);
    if (occurrence == nullptr) return;
    auto* dialog = new ComponentPropertiesDialog(
        *occurrence,
        [this, assembly_id = address->owner_assembly_document_id]
        (zima::assembly::PartOccurrence committed) {
            auto* assembly = workspace_.open_assembly(assembly_id);
            if (assembly == nullptr) {
                throw std::runtime_error("Assembly is no longer open");
            }
            auto next = assembly->session.document();
            auto found = std::find_if(next.components.begin(), next.components.end(),
                [&](const auto& item) { return item.occurrence_id == committed.occurrence_id; });
            if (found == next.components.end()) {
                throw std::runtime_error("Assembly occurrence no longer exists");
            }
            *found = std::move(committed);
            assembly->session.commit(std::move(next));
        }, this);
    properties_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this] {
        properties_dialog_ = nullptr;
        refresh_scene();
    });
    dialog->show();
}

void AssemblyWorkspaceWindow::show_component_context_menu(
    const std::string& instance_path, const QPoint& global_position) {
    std::optional<zima::workspace::OccurrenceAddress> address;
    try {
        address = workspace_.resolve_occurrence(
            workspace_.displayed_document_id(),
            zima::assembly::InstancePath::decode(instance_path));
    } catch (const std::invalid_argument&) {
        return;
    }
    if (!address) return;
    auto* assembly = workspace_.open_assembly(address->owner_assembly_document_id);
    if (assembly == nullptr || properties_dialog_ != nullptr) return;
    const auto* occurrence = assembly->session.document().find_occurrence(
        address->occurrence_id);
    if (occurrence == nullptr) return;
    QMenu menu(this);
    auto* properties = menu.addAction(tr("Vlastnosti"));
    auto* visibility = menu.addAction(
        occurrence->visible ? tr("Skrýt") : tr("Zobrazit"));
    auto* suppression = menu.addAction(
        occurrence->suppressed ? tr("Obnovit") : tr("Potlačit"));
    const QAction* selected = menu.exec(global_position);
    if (selected == properties) {
        show_component_properties(instance_path);
        return;
    }
    if (selected != visibility && selected != suppression) return;
    auto next = assembly->session.document();
    auto found = std::find_if(next.components.begin(), next.components.end(),
        [&](const auto& item) { return item.occurrence_id == address->occurrence_id; });
    if (found == next.components.end()) return;
    if (selected == visibility) found->visible = !found->visible;
    if (selected == suppression) found->suppressed = !found->suppressed;
    assembly->session.commit(std::move(next));
    refresh_tabs();
    refresh_scene();
}

}  // namespace zima::app
