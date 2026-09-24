#include "assembly_workspace_window.hpp"
#include "cylinder_axis_dialog.hpp"
#include <zima/workspace/placement_edit.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/viewer/mesh_view.hpp>
#include <QLabel>
#include <QTreeWidget>

namespace zima::app {
void AssemblyWorkspaceWindow::show_cylinder_axis_properties(const std::string& id) {
    if (properties_dialog_) return;
    const auto document_id = workspace_.active_document_id();
    const auto* part = workspace_.open_part(document_id);
    const auto* assembly = workspace_.open_assembly(document_id);
    if (!part && !assembly) return;
    auto value = document::PartDocument::create_construction(document::ConstructionKind::Axis);
    value.definition = document::ConstructionDefinition::CylinderAxis;
    value.name = tr("Osa válcové plochy").toStdString();
    if (!id.empty()) {
        const auto* stored = part ? part->session.document().find_construction(id)
                                 : assembly->session.document().find_construction(id);
        if (!stored || stored->definition != document::ConstructionDefinition::CylinderAxis) return;
        value = *stored;
    }
    kernel::ViewerReferenceGeometry geometry;
    if (part) {
        auto next = part->session.document();
        if (id.empty()) { next.insert_history_entry(document::PartHistoryKind::Construction, value.id); next.constructions.push_back(value); }
        geometry = workspace::construction_reference_source_geometry(part->session.calculated_boundaries());
        std::set<std::string> preceding;
        for (const auto& entry : next.history_order) {
            if (entry.id == value.id) break;
            preceding.insert(entry.id);
        }
        for (auto& ref : geometry.triangle_references)
            if (!preceding.contains(ref.owner_id)) ref = {};
        geometry = next.construction_reference_geometry_for(value.id, std::move(geometry));
    } else geometry = assembly->session.document().build_drawing_scene().original_references;
    // Selection and preview need one analytical record per face, not a copy
    // of every triangle in a potentially large STEP model.
    auto references = std::make_shared<kernel::ViewerReferenceGeometry>();
    std::set<std::tuple<std::string, std::string, std::string>> faces;
    for (auto& ref : geometry.triangle_references)
        if (ref.surface && ref.surface->kind == kernel::SurfaceGeometry::Kind::Cylinder &&
            faces.emplace(ref.instance_path, ref.owner_id, ref.semantic_key).second)
            references->triangle_references.push_back(std::move(ref));
    const auto path = workspace_.active_occurrence_path();
    auto* dialog = new CylinderAxisDialog(value, [this, document_id, editing=!id.empty(), references](auto committed) {
        if (!document::resolve_construction(committed, *references))
            throw std::runtime_error(tr("Vyberte válcovou plochu.").toStdString());
        if (editing) {
            const auto* part = workspace_.open_part(document_id);
            const auto* group = workspace_.open_assembly(document_id);
            const auto* stored = part ? part->session.document().find_construction(committed.id)
                : group ? group->session.document().find_construction(committed.id) : nullptr;
            if (stored && *stored == committed) return;
        }
        static_cast<void>(workspace::commit_construction(workspace_, document_id, std::move(committed),
            editing ? workspace::ConstructionEditMode::Replace : workspace::ConstructionEditMode::Create));
    }, this);
    properties_dialog_ = dialog; track_tree_edit(dialog);
    viewer_->set_original_face_selection(true);
    properties_dialog_instance_path_ = path;
    const auto source = [references, path](const viewer::ViewerCandidate& candidate) -> std::optional<document::ConstructionReference> {
        if (candidate.kind != viewer::CandidateKind::Face) return {};
        auto occurrence = assembly::InstancePath::decode(candidate.instance_path);
        const auto prefix = assembly::InstancePath::decode(path);
        if (occurrence.occurrence_ids.size() < prefix.occurrence_ids.size() ||
            !std::equal(prefix.occurrence_ids.begin(), prefix.occurrence_ids.end(), occurrence.occurrence_ids.begin())) return {};
        occurrence.occurrence_ids.erase(occurrence.occurrence_ids.begin(), occurrence.occurrence_ids.begin() + prefix.occurrence_ids.size());
        const auto local_path = occurrence.encoded();
        const auto face = std::ranges::find_if(references->triangle_references, [&](const auto& ref) {
            return ref.owner_id == candidate.owner_id && ref.semantic_key == candidate.semantic_key &&
                ref.instance_path == local_path && ref.surface && ref.surface->kind == kernel::SurfaceGeometry::Kind::Cylinder;
        });
        if (face == references->triangle_references.end()) return {};
        return document::ConstructionReference{local_path, face->owner_id, face->semantic_key};
    };
    dialog->changed = [this, dialog, references, source, path, document_id] {
        auto preview = dialog->pending();
        document::PartDocument packet;
        const bool valid = document::resolve_construction(preview, *references);
        if (const auto* part = workspace_.open_part(document_id)) {
            packet = part->session.document();
            if (auto* stored = packet.find_construction(preview.id)) *stored = preview;
            else { packet.insert_history_entry(document::PartHistoryKind::Construction, preview.id); packet.constructions.push_back(preview); }
        } else if (valid) packet.constructions.push_back(preview);
        construction_preview_mesh_ = valid ? packet.construction_viewer_mesh(preview.id) : kernel::ViewerMesh{};
        viewer_->set_feature_preview_owners({preview.entity_id, preview.container_origin.id});
        preserve_view_on_refresh_ = true; refresh_scene();
        viewer_->set_original_face_selection(true);
        viewer_->clear_selection();
        tree_->setProperty("commandSelectionActive", dialog->active());
        viewer_->set_selection_contract({viewer::CandidateKind::Face});
        viewer_->set_candidate_filter([dialog, source](const auto& candidate) { return dialog->active() && source(candidate).has_value(); });
        std::set<viewer::EdgeKey> highlights;
        if (dialog->inspected() && !preview.references.empty()) {
            const auto& ref = preview.references.front();
            auto full = assembly::InstancePath::decode(path);
            const auto local = assembly::InstancePath::decode(ref.instance_path);
            full.occurrence_ids.insert(full.occurrence_ids.end(), local.occurrence_ids.begin(), local.occurrence_ids.end());
            highlights.insert({ref.owner_id, ref.semantic_key, full.encoded()});
        }
        viewer_->set_constraint_reference_highlights({}, std::move(highlights));
        state_->setText(tr("Vyberte válcovou plochu."));
    };
    feature_reference_pick_ = [dialog, source](const auto& candidate) {
        if (dialog->active()) if (auto ref = source(candidate)) dialog->set_reference(std::move(*ref));
    };
    feature_reference_end_ = [dialog] { dialog->end_entry(); };
    connect(dialog, &QDialog::finished, this, [this, dialog] {
        dialog->changed = {}; feature_reference_pick_ = {}; feature_reference_end_ = {};
        properties_dialog_ = nullptr; properties_dialog_instance_path_.clear(); construction_preview_mesh_.reset();
        tree_->setProperty("commandSelectionActive", false);
        viewer_->set_candidate_filter({}); viewer_->set_selection_contract({}); viewer_->clear_selection();
        viewer_->set_original_face_selection(false);
        viewer_->set_constraint_reference_highlights({}, {}); viewer_->set_feature_preview_owners({});
        preserve_view_on_refresh_ = true; refresh_tabs(); refresh_scene();
    });
    dialog->show(); dialog->changed();
}
}
