#include "workspace_internal.hpp"

namespace zima::app {
using namespace workspace_detail;


bool AssemblyWorkspaceWindow::has_insertable_component() const {
    const std::string owner_id = workspace_.displayed_document_id();
    if (workspace_.open_assembly(owner_id) == nullptr) return false;
    for (const auto& state : workspace_.documents()) {
        const bool available = std::visit([&](const auto& item) {
            using State = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<State, zima::workspace::PartState>) {
                return item.session.document().document_id != owner_id &&
                    !item.session.document().history.empty() &&
                    !item.session.calculated_boundaries().empty();
            } else if constexpr (
                std::is_same_v<State, zima::workspace::AssemblyState>) {
                return item.session.document().document_id != owner_id;
            }
            return false;
        }, state);
        if (available) return true;
    }
    return false;
}

void AssemblyWorkspaceWindow::rebuild_insert_menu() {
    insert_menu_->clear();
    const std::string owner_id = workspace_.displayed_document_id();
    if (workspace_.open_assembly(owner_id) == nullptr) return;
    auto* choose_file = insert_menu_->addAction(tr("Vybrat soubor…"));
    choose_file->setObjectName("insertComponentFromFileAction");
    connect(choose_file, &QAction::triggered, this,
        [this] { insert_component_from_file(); });
    insert_menu_->addSeparator();
    for (const auto& state : workspace_.documents()) {
        std::visit([&](const auto& item) {
            using State = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<State, zima::workspace::PartState>) {
                const auto& document = item.session.document();
                if (document.document_id == owner_id) return;
                const bool calculated = !document.history.empty() &&
                    !item.session.calculated_boundaries().empty();
                auto* action = insert_menu_->addAction(
                    QString::fromStdString(document.name) +
                    (calculated ? tr(" — Part") : tr(" — Part (není vypočtený)")));
                action->setObjectName("insertSourceAction");
                action->setEnabled(calculated);
                if (!calculated) {
                    action->setToolTip(tr(
                        "Part musí obsahovat alespoň jeden potvrzený a vypočtený prvek."));
                }
                connect(action, &QAction::triggered, this,
                    [this, id = document.document_id] { insert_component(id); });
            } else if constexpr (
                std::is_same_v<State, zima::workspace::AssemblyState>) {
                const auto& document = item.session.document();
                if (document.document_id == owner_id) return;
                auto* action = insert_menu_->addAction(
                    QString::fromStdString(document.name) + tr(" — sestava"));
                action->setObjectName("insertSourceAction");
                connect(action, &QAction::triggered, this,
                    [this, id = document.document_id] { insert_component(id); });
            }
        }, state);
    }
}

void AssemblyWorkspaceWindow::insert_component_from_file() {
    const QString path = open_file(this, tr("Vložit komponentu"),
        QString::fromStdString(working_directory_.string()),
        tr("Komponenty ZIMA-CAD (*.prtz *.asmz)"),
        application_settings_.translations);
    if (path.isEmpty()) return;
    const std::filesystem::path source_path = path.toStdString();
    try {
        std::string source_id;
        if (const auto open_id = workspace_.document_id_for_path(source_path)) {
            source_id = *open_id;
        } else if (path.endsWith(".prtz", Qt::CaseInsensitive)) {
            std::vector<zima::kernel::BodyResult> calculated;
            auto source = zima::document::PartDocument::load(
                source_path, &calculated);
            source_id = source.document_id;
            workspace_.add_part(std::move(source), std::move(calculated), source_path);
        } else if (path.endsWith(".asmz", Qt::CaseInsensitive)) {
            auto source = zima::assembly::AssemblyDocument::load(source_path);
            source_id = source.document_id;
            workspace_.add_assembly(std::move(source), source_path);
        } else {
            throw std::runtime_error("Komponenta musí být Part nebo sestava.");
        }
        if (!source_path.parent_path().empty()) {
            working_directory_ = source_path.parent_path();
        }
        insert_component(source_id);
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Vložení selhalo"), error.what());
    }
}

void AssemblyWorkspaceWindow::insert_component(
    const std::string& source_document_id) {
    const std::string assembly_id = workspace_.displayed_document_id();
    if (workspace_.open_assembly(assembly_id) == nullptr) return;
    try {
        std::string occurrence_id;
        if (const auto* part = workspace_.open_part(source_document_id)) {
            occurrence_id = workspace_.insert_open_part(
                assembly_id, source_document_id, part->session.document().name);
        } else if (const auto* source = workspace_.open_assembly(source_document_id)) {
            occurrence_id = workspace_.insert_open_assembly(
                assembly_id, source_document_id, source->session.document().name);
        } else {
            return;
        }
        workspace_.activate(assembly_id);
        refresh_tabs();
        refresh_scene();
        viewer_->confirm_occurrence(
            zima::assembly::InstancePath{}.child(occurrence_id).encoded());
        show_component_properties(
            zima::assembly::InstancePath{}.child(occurrence_id).encoded(), true);
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Vložení selhalo"), error.what());
    }
}

void AssemblyWorkspaceWindow::regenerate_assembly() {
    const std::string id = workspace_.displayed_document_id();
    try {
        std::set<std::string> regenerated_parts;
        std::set<std::string> visiting_parts;
        const std::function<void(const std::string&)> regenerate_part_dependencies =
            [&](const std::string& part_id) {
                if (regenerated_parts.contains(part_id)) return;
                if (!visiting_parts.insert(part_id).second) {
                    throw std::runtime_error(
                        "External Sketch document dependency cycle detected");
                }
                auto* part = workspace_.open_part(part_id);
                if (part == nullptr) {
                    visiting_parts.erase(part_id);
                    return;
                }
                for (const auto& sketch : part->session.document().sketches) {
                    for (const auto& reference : sketch.external_references) {
                        if (reference.context_assembly_document_id == id &&
                            reference.source_document_id != part_id) {
                            regenerate_part_dependencies(
                                reference.source_document_id);
                        }
                    }
                }
                auto next = part->session.document();
                const bool has_context = std::any_of(
                    next.sketches.begin(), next.sketches.end(), [&](const auto& sketch) {
                        return std::any_of(sketch.external_references.begin(),
                            sketch.external_references.end(), [&](const auto& reference) {
                                return reference.context_assembly_document_id == id;
                            });
                    });
                if (has_context) {
                    const auto& previous = part->session.calculated_boundaries();
                    auto calculated = calculate_part(next, &previous);
                    const auto previous_constructions = next.constructions;
                    next.resolve_constructions(calculated.empty()
                        ? zima::kernel::ViewerReferenceGeometry{}
                        : calculated.back().mesh.original_references);
                    const bool references_changed =
                        refresh_sketch_external_references(next, calculated) |
                        workspace_.refresh_context_external_references(next);
                    if (references_changed) {
                        calculated = calculate_part(next, &calculated);
                    }
                    if (references_changed ||
                        next.constructions != previous_constructions) {
                        part->session.commit(std::move(next), std::move(calculated));
                    } else {
                        part->session.update_calculated_boundaries(
                            std::move(calculated));
                    }
                }
                visiting_parts.erase(part_id);
                regenerated_parts.insert(part_id);
            };
        for (const auto& state : workspace_.documents()) {
            const auto* part = std::get_if<zima::workspace::PartState>(&state);
            if (part != nullptr) {
                regenerate_part_dependencies(
                    part->session.document().document_id);
            }
        }
        workspace_.regenerate_assembly_from_open_dependencies(id);
        if (auto* regenerated = workspace_.open_assembly(id)) {
            auto next = regenerated->session.document();
            const bool references_changed =
                refresh_assembly_sketch_external_references(next);
            calculate_assembly_cuts(next);
            if (references_changed || !next.cuts.empty()) {
                regenerated->session.commit(std::move(next));
            }
        }
        refresh_tabs();
        preserve_view_on_refresh_ = true;
        refresh_scene();
    } catch (const std::exception& error) {
        report_operation_error(tr("Regenerace selhala"), error.what());
    }
}

} // namespace zima::app
