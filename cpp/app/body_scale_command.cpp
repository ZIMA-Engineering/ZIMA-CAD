#include "assembly_workspace_window.hpp"
#include "body_scale_dialog.hpp"
#include "workspace/workspace_internal.hpp"
#include <zima/workspace/body_scale_operations.hpp>
#include <zima/viewer/mesh_view.hpp>
#include <QTreeWidget>

namespace zima::app {
void AssemblyWorkspaceWindow::show_body_scale_properties(const std::string& id) {
    if(properties_dialog_){properties_dialog_->raise();return;}
    const auto document_id=workspace_.active_document_id();const auto* part=workspace_.open_part(document_id);
    if(!part)return;
    const auto& graph=part->session.document().body_history;
    std::shared_ptr<workspace::BodyGraphEdit> edit;
    try{edit=std::make_shared<workspace::BodyGraphEdit>(workspace::prepare_body_scale_edit(part->session.document(),id));}
    catch(const std::exception& error){state_->setText(tr(error.what()));return;}
    const auto boundary=id.empty()?graph.insertion_cursor():static_cast<std::size_t>(std::ranges::find(graph.order(),id)-graph.order().begin());
    const auto available=graph.available_before(boundary);
    std::vector<std::string> context;
    struct Source {QString name;kernel::BodySnapshot geometry;};std::map<std::string,Source> sources;
    if(!part->session.calculated_boundaries().empty()) {
        const auto& outputs=part->session.calculated_boundaries().back().body_outputs;
        for(const auto& source:available)if(const auto found=outputs.find(source);found!=outputs.end()) {
            const auto* body=graph.find(source);const auto* operation=graph.find_boolean(source);
            sources.emplace(source,Source{QString::fromStdString(body?body->name:operation->name),found->second});
            if(body?(body->visible&&!body->suppressed):(operation->visible&&!operation->suppressed))context.push_back(source);
        }
    }
    if(sources.empty()){state_->setText(tr("Select a source Body."));return;}
    document::BodyHistory initial;
    if(!id.empty())initial=*graph.find(id);
    else {initial.scope.id=edit->object_id;initial.name=tr("Body scale").toStdString();initial.scale=document::BodyScale{};}
    if(id.empty()&&tree_->currentItem()) {
        const auto selected=tree_->currentItem()->data(0,Qt::UserRole).toString().toStdString();
        if(sources.contains(selected))initial.scale->source_id=selected;
    }
    const auto prefix=workspace_.active_occurrence_path();
    auto* dialog=new BodyScaleDialog(initial,[this,edit](auto value){
        static_cast<void>(workspace::commit_body_scale(workspace_,kernel_,*edit,std::move(value)));
    },this);
    if(sources.contains(initial.scale->source_id))dialog->set_source(initial.scale->source_id,sources.at(initial.scale->source_id).name);
    properties_dialog_=dialog;properties_dialog_instance_path_=prefix;body_dialog_context_=context;body_dialog_step_id_=initial.scope.id;
    const auto offer=[this,sources,prefix,dialog] {
        auto mesh=viewer_->mesh();
        for(const auto& [id,source]:sources) {
            const auto& geometry=source.geometry->mesh;
            const auto offset=static_cast<std::uint32_t>(mesh.original_references.vertices.size());
            for(auto p:geometry.vertices) {
                if(!prefix.empty())p=workspace_.occurrence_point_to_scene(workspace_.displayed_document_id(),assembly::InstancePath::decode(prefix),p);
                mesh.original_references.vertices.push_back(p);
            }
            for(auto index:geometry.triangles)mesh.original_references.triangles.push_back(offset+index);
            mesh.original_references.triangle_references.insert(mesh.original_references.triangle_references.end(),
                geometry.triangles.size()/3,kernel::FaceReference{id,"container:display",prefix});
            for(auto edge:geometry.edges) {
                edge.reference={id,"container:display",prefix};edge.display_owner_id=id;
                if(!prefix.empty()) {
                    const auto transform=[&](auto p){return workspace_.occurrence_point_to_scene(workspace_.displayed_document_id(),assembly::InstancePath::decode(prefix),p);};
                    for(auto& p:edge.points)p=transform(p);
                    if(edge.exact_spline)for(auto& p:edge.exact_spline->poles)p=transform(p);
                    if(edge.annotation)kernel::transform_annotation(*edge.annotation,transform);
                }
                mesh.original_references.edges.push_back(std::move(edge));
            }
        }
        viewer_->set_mesh(std::move(mesh));viewer_->set_selection_contract({viewer::CandidateKind::Container});
        viewer_->set_original_container_selection(true);
        const auto accepts=[sources,prefix](const viewer::ViewerCandidate& c){return c.kind==viewer::CandidateKind::Container&&c.instance_path==prefix&&sources.contains(c.owner_id);};
        viewer_->set_candidate_filter(accepts);tree_->setProperty("commandSelectionActive",true);
        feature_reference_pick_=[this,dialog,sources,accepts](const auto& c){if(!accepts(c))return;
            const auto source=c.owner_id;const auto label=sources.at(source).name;auto* target=dialog;
            viewer_->clear_selection();tree_->clearSelection();feature_reference_pick_={};target->set_source(source,label);};
    };
    dialog->request_source=[dialog]{dialog->arm();dialog->changed();};
    dialog->changed=[this,dialog,sources,prefix,offer,context]{
        if(!dialog->isVisible())return;
        preserve_view_on_refresh_=true;refresh_scene();
        if(!prefix.empty()) {
            kernel::BodyResult input;
            for(const auto& id:context)workspace_detail::append_mesh(input.mesh,sources.at(id).geometry->mesh);
            viewer_->set_mesh(workspace_.build_scene_with_part_override(workspace_.displayed_document_id(),
                assembly::InstancePath::decode(prefix),std::move(input)));
        }
        viewer_->set_original_container_selection(false);viewer_->set_candidate_filter({});viewer_->set_selection_contract({});
        std::vector<kernel::ViewerEdge> preview;std::set<viewer::EdgeKey> highlights;
        const auto& scale=*dialog->pending.scale;
        try {
            if(sources.contains(scale.source_id)) {
                kernel::validate_body_scale(scale.factor,scale.center);
                const auto& source=sources.at(scale.source_id).geometry->mesh;
                preview=source.edges;
                for(auto& edge:preview) {
                    for(auto& point:edge.points)point=kernel::scaled_body_point(point,scale.factor,scale.center);
                    if(edge.exact_spline)for(auto& point:edge.exact_spline->poles)point=kernel::scaled_body_point(point,scale.factor,scale.center);
                    if(edge.annotation)kernel::transform_annotation(*edge.annotation,[&](auto p){return kernel::scaled_body_point(p,scale.factor,scale.center);});
                }
                if(dialog->inspected()) {
                    auto mesh=viewer_->mesh();
                    for(auto edge:source.edges) {
                        highlights.insert({edge.reference.owner_id,edge.reference.semantic_key,prefix});
                        edge.reference.instance_path=prefix;
                        if(!prefix.empty()) {
                            const auto transform=[&](auto p){return workspace_.occurrence_point_to_scene(workspace_.displayed_document_id(),assembly::InstancePath::decode(prefix),p);};
                            for(auto& p:edge.points)p=transform(p);
                            if(edge.exact_spline)for(auto& p:edge.exact_spline->poles)p=transform(p);
                            if(edge.annotation)kernel::transform_annotation(*edge.annotation,transform);
                        }
                        mesh.original_references.edges.push_back(std::move(edge));
                    }
                    viewer_->set_mesh(std::move(mesh));
                }
            }
            dialog->set_status({});
        }catch(const std::exception& error){dialog->set_status(tr(error.what()));}
        for(auto& edge:preview){edge.overlay=true;if(!prefix.empty()) {
            const auto transform=[&](auto p){return workspace_.occurrence_point_to_scene(workspace_.displayed_document_id(),assembly::InstancePath::decode(prefix),p);};
            for(auto& p:edge.points)p=transform(p);
            if(edge.exact_spline)for(auto& p:edge.exact_spline->poles)p=transform(p);
            if(edge.annotation)kernel::transform_annotation(*edge.annotation,transform);
        }}
        viewer_->set_transient_edges(std::move(preview));viewer_->set_constraint_reference_highlights({},std::move(highlights));
        if(dialog->active_input())offer();else feature_reference_pick_={};
        feature_reference_end_=[this,dialog]{auto* target=dialog;feature_reference_pick_={};feature_reference_end_={};target->end_input();target->changed();};
    };
    connect(dialog,&QDialog::finished,this,[this]{
        properties_dialog_=nullptr;properties_dialog_instance_path_.clear();body_dialog_context_.reset();body_dialog_step_id_.clear();
        feature_reference_pick_={};feature_reference_end_={};viewer_->set_transient_edges({});viewer_->set_constraint_reference_highlights({},{});
        viewer_->set_original_container_selection(false);viewer_->set_candidate_filter({});viewer_->set_selection_contract({});viewer_->clear_selection();
        tree_->setProperty("commandSelectionActive",false);preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
    });
    dialog->show();dialog->changed();if(initial.scale->source_id.empty())dialog->request_source();
}
bool AssemblyWorkspaceWindow::accept_body_scale_tree_reference(QTreeWidgetItem* item) {
    auto* dialog=dynamic_cast<BodyScaleDialog*>(properties_dialog_);
    if(!dialog||!dialog->active_input()||!feature_reference_pick_)return false;
    const auto kind=item->data(0,Qt::UserRole+3).toString();
    if(kind!="part-body"&&kind!="part-body-boolean")return false;
    viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Container;
    candidate.owner_id=item->data(0,Qt::UserRole).toString().toStdString();candidate.instance_path=workspace_.active_occurrence_path();
    const auto pick=feature_reference_pick_;pick(candidate);return true;
}
} // namespace zima::app
