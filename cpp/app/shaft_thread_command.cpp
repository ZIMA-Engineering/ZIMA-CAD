#include "assembly_workspace_window.hpp"
#include "shaft_thread_dialog.hpp"
#include "shaft_thread_preview.hpp"
#include <zima/viewer/mesh_view.hpp>
#include <QAction>
#include <QLabel>
#include <QTreeWidget>
namespace zima::app {
void AssemblyWorkspaceWindow::show_shaft_thread_properties(const std::string& id) {
    if (properties_dialog_) return;
    auto* part=workspace_.open_part(workspace_.active_document_id());
    if (!part || part->session.calculated_boundaries().empty()) return;
    const auto occurrence=resolve_active_occurrence(part->session.document().document_id);
    if (!occurrence) return;
    auto initial=document::PartDocument::create_shaft_thread_container();
    if (!id.empty()) {
        const auto* stored=part->session.document().find_container(id);
        if (!stored || stored->feature_kind!=document::FeatureKind::ShaftThread) return;
        initial=*stored;
        const auto boundary=part->session.rollback_boundary(id);
        if (!boundary) {
            state_->setText(tr("Chybí vypočtený vstup závitu. Regenerujte Part."));return;
        }
        part_rollback_=PartRollbackContext{part->session.document().document_id,*occurrence,boundary->history_index,boundary->input_body};
        primitive_reference_geometry_=part->session.calculated_boundaries().back().mesh.original_references;
    } else {
        const auto count=part->session.document().body_operation_count_at_history_cursor();
        if (count==0 || count>part->session.calculated_boundaries().size()) return;
        primitive_reference_geometry_=part->session.calculated_boundaries().back().mesh.original_references;
    }
    // Reference packets are compacted into the last boundary. Offer only
    // owners preceding this edit/insertion while displaying the rollback body.
    const auto operations=part->session.document().kernel_operations();
    const auto limit=id.empty() ? part->session.document().body_operation_count_at_history_cursor()
        : static_cast<std::size_t>(std::distance(operations.begin(),
            std::ranges::find_if(operations,[&](const auto& op) { return op.owner_id==id; })));
    std::set<std::string> owners;
    for (std::size_t i=0;i<std::min(limit,operations.size());++i) owners.insert(operations[i].owner_id);
    for (auto& reference : primitive_reference_geometry_.triangle_references)
        if (!owners.contains(reference.owner_id)) reference={};
    if (id.empty() && std::ranges::none_of(primitive_reference_geometry_.triangle_references,
            [](const auto& reference) { return static_cast<bool>(reference.surface); })) {
        primitive_reference_geometry_={};part_rollback_.reset();
        state_->setText(tr("Pro výběr ploch závitu chybí uložená geometrie. Nejprve regenerujte Part."));
        return;
    }
    if (!id.empty()) {
        // Fallback is for regeneration and display, never for accepting a
        // missing reference while explicitly repairing feature properties.
        const auto present=[&](const kernel::FaceReference& ref) {
            return std::ranges::any_of(primitive_reference_geometry_.triangle_references,
                [&](const auto& candidate) { return candidate==ref && candidate.surface; });
        };
        auto& p=initial.shaft_thread;
        if (!present(p.cylinder)) p.cylinder={};
        if (!present(p.start)) p.start={};
        if (p.chamfer && !present(*p.chamfer)) p.chamfer.reset();
        if (p.end && !present(*p.end)) p.end.reset();
    }
    const auto document_id=part->session.document().document_id;
    auto* dialog=new ShaftThreadDialog(initial,[this,document_id,editing=!id.empty()](auto feature) {
        auto* target=workspace_.open_part(document_id);
        if (!target) throw std::runtime_error("Part již není otevřen.");
        // The same analytic validation drives the wire preview and acceptance.
        static_cast<void>(shaft_thread_preview(feature,primitive_reference_geometry_));
        auto next=target->session.document();
        if (editing) {
            auto* stored=next.find_container(feature.id);
            if (!stored) throw std::runtime_error("Závit již neexistuje.");
            *stored=std::move(feature);
        } else {
            next.insert_history_entry(document::PartHistoryKind::Feature,feature.id);
            next.history.push_back(std::move(feature));
        }
        auto boundaries=calculate_part(next,&target->session.calculated_boundaries());
        target->session.commit(std::move(next),std::move(boundaries));
    },this);
    shaft_thread_dialog_=dialog;properties_dialog_=dialog;
    properties_dialog_instance_path_=*occurrence;
    primitive_parameter_owner_id_=initial.id;
    construction_dimension_object_id_=initial.id;
    tree_reference_state_.watch(dialog,this,document_id,initial.id);
    tree_->setProperty("commandSelectionActive",true);
    dialog->changed=[this] { refresh_shaft_thread_preview(); };
    connect(dialog,&QDialog::finished,this,[this] {
        shaft_thread_dialog_=nullptr;properties_dialog_=nullptr;
        properties_dialog_instance_path_.clear();
        parameter_dimension_preview_.reset();primitive_parameter_owner_id_.clear();
        construction_dimension_object_id_.clear();primitive_reference_geometry_={};
        part_rollback_.reset();
        tree_->setProperty("commandSelectionActive",false);
        viewer_->set_transient_edges({});viewer_->set_transient_dimensions({});
        viewer_->set_constraint_reference_highlights({},{});viewer_->set_feature_preview_owners({});
        viewer_->set_candidate_filter({});viewer_->set_selection_contract({});viewer_->clear_selection();
        preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
    });
    dialog->show();refresh_shaft_thread_preview();
}

void AssemblyWorkspaceWindow::refresh_shaft_thread_preview() {
    if (!shaft_thread_dialog_) return;
    const auto pending=shaft_thread_dialog_->pending();
    parameter_dimension_preview_=pending;
    preserve_view_on_refresh_=true;refresh_scene();
    try {
        auto mesh=shaft_thread_preview(pending,primitive_reference_geometry_);
        if (!properties_dialog_instance_path_.empty()) {
            const auto path=assembly::InstancePath::decode(properties_dialog_instance_path_);
            for (auto& edge : mesh.edges) for (auto& point : edge.points)
                point=workspace_.occurrence_point_to_scene(workspace_.displayed_document_id(),path,point);
        }
        viewer_->set_transient_edges(std::move(mesh.edges));
        state_->setText(tr("Závit: zkontrolujte náhled. OK vytvoří plochy závitu."));
    } catch (const std::exception& error) {
        viewer_->set_transient_edges({});state_->setText(QString::fromUtf8(error.what()));
    }
    const int row=shaft_thread_dialog_->active_reference();
    const auto path=properties_dialog_instance_path_;
    std::set<viewer::EdgeKey> highlights;
    for (int i=0;i<4;++i) if (shaft_thread_dialog_->inspected(i)) {
        const auto ref=shaft_thread_dialog_->reference(i);
        if (ref) highlights.insert({ref->owner_id,ref->semantic_key,path});
    }
    viewer_->set_constraint_reference_highlights({},std::move(highlights));
    viewer_->clear_selection();
    viewer_->set_selection_contract(row>=0 ? std::vector{viewer::CandidateKind::Face}
        : std::vector{viewer::CandidateKind::Dimension});
    viewer_->set_candidate_filter([this,row,path,owner=pending.id](const auto& candidate) {
        if (candidate.instance_path!=path) return false;
        if (row<0) return candidate.kind==viewer::CandidateKind::Dimension &&
            candidate.owner_id==owner && candidate.semantic_key.starts_with("parameter:");
        if (candidate.kind!=viewer::CandidateKind::Face) return false;
        const auto found=std::ranges::find_if(primitive_reference_geometry_.triangle_references,[&](const auto& ref) {
            return ref.owner_id==candidate.owner_id && ref.semantic_key==candidate.semantic_key && ref.surface;
        });
        if (found==primitive_reference_geometry_.triangle_references.end()) return false;
        if (row==3) return found->surface->kind==kernel::SurfaceGeometry::Kind::Plane ||
            found->surface->kind==kernel::SurfaceGeometry::Kind::Cylinder ||
            found->surface->kind==kernel::SurfaceGeometry::Kind::Cone;
        const auto wanted=row==0 ? kernel::SurfaceGeometry::Kind::Cylinder
            : row==2 ? kernel::SurfaceGeometry::Kind::Cone : kernel::SurfaceGeometry::Kind::Plane;
        return found->surface->kind==wanted && (row!=0 || !found->surface->reversed);
    });
}

void AssemblyWorkspaceWindow::accept_shaft_thread_reference(const viewer::ViewerCandidate& candidate) {
    if (!shaft_thread_dialog_ || shaft_thread_dialog_->active_reference()<0 ||
        candidate.instance_path!=properties_dialog_instance_path_) return;
    const auto found=std::ranges::find_if(primitive_reference_geometry_.triangle_references,[&](const auto& ref) {
        return ref.owner_id==candidate.owner_id && ref.semantic_key==candidate.semantic_key && ref.surface;
    });
    if (found==primitive_reference_geometry_.triangle_references.end()) return;
    auto reference=*found;reference.instance_path.clear();
    QString label=QString::fromStdString(reference.semantic_key);
    if (const auto* part=workspace_.open_part(workspace_.active_document_id()))
        if (const auto* owner=part->session.document().find_container(reference.owner_id))
            label=QString::fromStdString(owner->name)+" / "+label;
    shaft_thread_dialog_->set_reference(std::move(reference),label);
}
}
