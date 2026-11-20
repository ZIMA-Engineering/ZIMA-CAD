#include "assembly_workspace_window.hpp"
#include "fem_body_dialog.hpp"
#include "workspace/workspace_internal.hpp"
#include <zima/workspace/fem_operations.hpp>
#include <zima/viewer/mesh_view.hpp>
#include <QTreeWidget>

namespace zima::app {
bool AssemblyWorkspaceWindow::active_fem_body()const {
    const auto* part=workspace_.open_part(workspace_.active_document_id());
    const auto* body=part?part->session.document().body_history.find(part->session.document().body_history.active_body_id()):nullptr;
    return body&&body->fem.has_value();
}

void AssemblyWorkspaceWindow::show_fem_body_properties(const std::string& id) {
    if(properties_dialog_){properties_dialog_->raise();return;}
    const auto document_id=workspace_.active_document_id();const auto* part=workspace_.open_part(document_id);
    if(!part)return;
    const auto& graph=part->session.document().body_history;
    if(id.empty()&&!graph.active_body_id().empty())return;
    std::shared_ptr<workspace::BodyGraphEdit> edit;
    std::vector<std::string> candidates;
    try {
        edit=std::make_shared<workspace::BodyGraphEdit>(workspace::prepare_fem_edit(part->session.document(),id));
        candidates=workspace::fem_source_candidates(part->session,*edit);
    }catch(const std::exception& error){state_->setText(tr(error.what()));return;}
    if(id.empty()&&candidates.empty()){state_->setText(tr("FEM requires a valid calculated solid. Regenerate the source first."));return;}
    struct Source {QString name;kernel::BodySnapshot geometry;};std::map<std::string,Source> sources;
    for(const auto& source:candidates) {
        const auto* body=graph.find(source);const auto* operation=graph.find_boolean(source);
        sources.emplace(source,Source{QString::fromStdString(body?body->name:operation->name),part->session.calculated_boundaries().back().body_outputs.at(source)});
    }
    document::BodyHistory initial;
    if(!id.empty())initial=*graph.find(id);
    else {
        initial.scope.id=edit->object_id;initial.name=tr("FEM Body").toStdString();initial.fem=document::FemDefinition{};
        if(tree_->currentItem()) {
            const auto selected=tree_->currentItem()->data(0,Qt::UserRole).toString().toStdString();
            if(sources.contains(selected))initial.fem->source_id=selected;
        }
        if(initial.fem->source_id.empty()&&candidates.size()==1)initial.fem->source_id=candidates.front();
    }
    const auto boundary=id.empty()?graph.insertion_cursor():static_cast<std::size_t>(std::ranges::find(graph.order(),id)-graph.order().begin());
    std::vector<std::string> context;
    for(const auto& source:graph.available_before(boundary))if(sources.contains(source)) {
        const auto* body=graph.find(source);const auto* operation=graph.find_boolean(source);
        if(body?body->visible:operation->visible)context.push_back(source);
    }
    const auto prefix=workspace_.active_occurrence_path();
    auto* dialog=new FemBodyDialog(initial,[this,edit](auto value){static_cast<void>(workspace::commit_fem(workspace_,*edit,std::move(value)));},this);
    const auto* source_body=graph.find(initial.fem->source_id);const auto* source_boolean=graph.find_boolean(initial.fem->source_id);
    if(!initial.fem->source_id.empty())dialog->set_source(initial.fem->source_id,
        QString::fromStdString(source_body?source_body->name:source_boolean?source_boolean->name:initial.fem->source_id));
    properties_dialog_=dialog;properties_dialog_instance_path_=prefix;body_dialog_context_=context;body_dialog_step_id_=initial.scope.id;
    // Publish the command's input once. Entry and the independent eye only
    // consume this packet and the immutable source snapshots below.
    preserve_view_on_refresh_=true;
    if(prefix.empty())refresh_scene();
    else refresh_scene([&] {
        kernel::BodyResult input;
        for(const auto& source:context)workspace_detail::append_mesh(input.mesh,sources.at(source).geometry->mesh);
        return workspace_.build_scene_with_part_override(workspace_.displayed_document_id(),
            assembly::InstancePath::decode(prefix),std::move(input));
    });
    auto base=std::make_shared<kernel::ViewerMesh>(viewer_->mesh());
    auto offered=std::make_shared<kernel::ViewerMesh>(*base);
    const auto transform=[this,prefix](auto p){return prefix.empty()?p:workspace_.occurrence_point_to_scene(workspace_.displayed_document_id(),assembly::InstancePath::decode(prefix),p);};
    for(const auto& [source_id,source]:sources) {
        const auto& mesh=source.geometry->mesh;const auto offset=static_cast<std::uint32_t>(offered->original_references.vertices.size());
        for(auto p:mesh.vertices)offered->original_references.vertices.push_back(transform(p));
        for(auto index:mesh.triangles)offered->original_references.triangles.push_back(offset+index);
        offered->original_references.triangle_references.insert(offered->original_references.triangle_references.end(),mesh.triangles.size()/3,kernel::FaceReference{source_id,"container:display",prefix});
        for(auto edge:mesh.edges) {
            edge.reference={source_id,"container:display",prefix};edge.display_owner_id=source_id;
            for(auto& p:edge.points)p=transform(p);
            if(edge.exact_spline)for(auto& p:edge.exact_spline->poles)p=transform(p);
            if(edge.annotation)kernel::transform_annotation(*edge.annotation,transform);
            offered->original_references.edges.push_back(std::move(edge));
        }
    }
    auto offering=std::make_shared<std::optional<bool>>();
    dialog->changed=[this,dialog,sources,prefix,base,offered,offering,transform] {
        const auto accepts=[sources,prefix](const viewer::ViewerCandidate& c){return c.kind==viewer::CandidateKind::Container&&c.instance_path==prefix&&sources.contains(c.owner_id);};
        const bool active=dialog->active_input();
        if(!*offering||**offering!=active){viewer_->set_mesh(active?*offered:*base);*offering=active;}
        viewer_->set_original_container_selection(active);viewer_->set_selection_contract(active?std::vector{viewer::CandidateKind::Container}:std::vector<viewer::CandidateKind>{});
        viewer_->set_candidate_filter(active?std::function<bool(const viewer::ViewerCandidate&)>(accepts):std::function<bool(const viewer::ViewerCandidate&)>([](const auto&){return false;}));
        tree_->setProperty("commandSelectionActive",active);
        feature_reference_pick_={};
        if(active)feature_reference_pick_=[this,dialog,sources,accepts](const auto& candidate) {
            if(!accepts(candidate))return;
            const auto source=candidate.owner_id;const auto label=sources.at(source).name;auto* target=dialog;
            viewer_->clear_selection();tree_->clearSelection();feature_reference_pick_={};target->set_source(source,label);
        };
        const auto source=sources.find(dialog->pending.fem->source_id);
        dialog->set_source_available(source!=sources.end());
        dialog->set_status(source!=sources.end()||dialog->pending.fem->source_id.empty()?QString{}:tr("The FEM source is missing or suppressed."));
        std::vector<kernel::ViewerEdge> inspection;
        if(dialog->inspected()&&source!=sources.end())for(auto edge:source->second.geometry->mesh.edges) {
            edge.overlay=true;edge.color="#00D1FF";
            for(auto& p:edge.points)p=transform(p);
            if(edge.exact_spline)for(auto& p:edge.exact_spline->poles)p=transform(p);
            if(edge.annotation)kernel::transform_annotation(*edge.annotation,transform);
            inspection.push_back(std::move(edge));
        }
        viewer_->set_transient_edges(std::move(inspection));
        feature_reference_end_=[this,dialog]{auto* target=dialog;feature_reference_pick_={};feature_reference_end_={};target->end_input();};
    };
    connect(dialog,&QDialog::finished,this,[this] {
        properties_dialog_=nullptr;properties_dialog_instance_path_.clear();body_dialog_context_.reset();body_dialog_step_id_.clear();
        feature_reference_pick_={};feature_reference_end_={};viewer_->set_transient_edges({});
        viewer_->set_original_container_selection(false);viewer_->set_candidate_filter({});viewer_->set_selection_contract({});viewer_->clear_selection();
        tree_->setProperty("commandSelectionActive",false);preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
    });
    dialog->show();dialog->changed();if(initial.fem->source_id.empty())dialog->arm();
}

bool AssemblyWorkspaceWindow::accept_fem_tree_reference(QTreeWidgetItem* item) {
    auto* dialog=dynamic_cast<FemBodyDialog*>(properties_dialog_);
    if(!item||!dialog||!dialog->active_input()||!feature_reference_pick_)return false;
    const auto kind=item->data(0,Qt::UserRole+3).toString();
    if(kind!="part-body"&&kind!="part-body-boolean")return false;
    viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Container;
    candidate.owner_id=item->data(0,Qt::UserRole).toString().toStdString();candidate.instance_path=workspace_.active_occurrence_path();
    const auto pick=feature_reference_pick_;pick(candidate);return true;
}
} // namespace zima::app
