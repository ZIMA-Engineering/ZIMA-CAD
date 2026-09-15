#include "assembly_workspace_window.hpp"
#include "derived_copy_dialog.hpp"
#include <zima/workspace/derived_copy_operations.hpp>
#include <zima/viewer/mesh_view.hpp>
#include <zima/kernel/stable_id.hpp>
#include <QTreeWidget>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include "resource_icon.hpp"

namespace zima::app {
namespace {
template<class Geometry> void append_derived_copy_references(Geometry& target,const Geometry& source) {
    const auto offset=static_cast<std::uint32_t>(target.vertices.size());
    target.vertices.insert(target.vertices.end(),source.vertices.begin(),source.vertices.end());
    for(auto i:source.triangles)target.triangles.push_back(offset+i);
    target.triangle_references.insert(target.triangle_references.end(),source.triangle_references.begin(),source.triangle_references.end());
    target.edges.insert(target.edges.end(),source.edges.begin(),source.edges.end());target.points.insert(target.points.end(),source.points.begin(),source.points.end());
    target.axes.insert(target.axes.end(),source.axes.begin(),source.axes.end());
}
kernel::ViewerMesh mirror_origin(const document::HistoryContainer& c) {
    document::PartDocument carrier;document::ConstructionObject origin;
    origin.id=c.id;origin.entity_id=c.feature_id;origin.container_origin=c.container_origin;origin.kind=document::ConstructionKind::Point;
    origin.origin={c.placement.x,c.placement.y,c.placement.z};origin.rotation={c.placement.rotation_x,c.placement.rotation_y,c.placement.rotation_z};origin.reference_valid=false;
    carrier.constructions.push_back(origin);return carrier.construction_viewer_mesh(c.id);
}
struct MirrorSource {QString name;kernel::ViewerMesh mesh;bool whole_body{};};
bool has_copy_source_offers(const kernel::ViewerMesh& mesh) {
    return std::ranges::any_of(mesh.original_references.triangle_references,[](const auto& ref){return ref.semantic_key=="container:display";});
}
}

void AssemblyWorkspaceWindow::show_derived_copy_properties(const std::string& id,bool pattern) {
    if(properties_dialog_){properties_dialog_->raise();return;}
    const auto document_id=workspace_.active_document_id();auto* part=workspace_.open_part(document_id);auto* assembly=workspace_.open_assembly(document_id);
    if(!part&&!assembly)return;
    const auto selected=viewer_->confirmed_candidate();
    std::shared_ptr<workspace::DerivedCopyEdit> edit;
    try {edit=std::make_shared<workspace::DerivedCopyEdit>(workspace::prepare_derived_copy_edit(workspace_,document_id,id,pattern));}
    catch(const std::exception& error){state_->setText(tr(error.what()));return;}
    document::HistoryContainer initial;initial.id=edit->initial.id;
    initial.feature_id=initial.id+":entity";initial.container_origin=document::create_container_origin(initial.id);
    initial.name=edit->initial.name;initial.placement=edit->initial.placement;
    auto parameters=edit->initial.parameters;
    const auto& choices=edit->sources;
    document::BodyHistoryGraph graph;
    std::map<std::string,MirrorSource> sources;
    const auto& geometry=edit->references;
    std::vector<std::string> available;
    if(part) {
        const auto& document=part->session.document();graph=document.body_history;
        if(id.empty())graph.set_insertion_cursor(choices.boundary);
        available=choices.context_bodies;
        for(const auto& source:choices.items)
            sources.emplace(source.id,MirrorSource{QString::fromStdString(source.name),workspace::derived_copy_source_mesh(part->session,source),source.kind!=workspace::CopySourceKind::Solid});
    } else {
        const auto& document=assembly->session.document();
        auto preview=document;bool downstream=false;
        for(auto& component:preview.components){if(component.occurrence_id==id)downstream=true;if(downstream)component.visible=false;}
        derived_copy_assembly_preview_=std::move(preview);
        for(const auto& choice:choices.items) {
            const auto& source=*document.find_occurrence(choice.id);
            auto isolated=document;isolated.components={source};isolated.dependencies.clear();isolated.constructions.clear();
            sources.emplace(source.occurrence_id,MirrorSource{QString::fromStdString(source.name),isolated.build_scene()});
        }
    }
    if(sources.empty()){derived_copy_assembly_preview_.reset();state_->setText(tr("Nejprve vytvořte zdrojový solid, těleso nebo vložte komponentu."));return;}
    const auto prefix=workspace_.active_occurrence_path();
    const auto source_id=[this,document_id,prefix,sources](const viewer::ViewerCandidate& candidate)->std::string {
        if(workspace_.open_part(document_id)) {
            if(candidate.instance_path!=prefix)return {};
            if(sources.contains(candidate.owner_id))return candidate.owner_id;
            return {};
        }
        const auto path=assembly::InstancePath::decode(candidate.instance_path),parent=assembly::InstancePath::decode(prefix);
        if(path.occurrence_ids.size()!=parent.occurrence_ids.size()+1||!std::equal(parent.occurrence_ids.begin(),parent.occurrence_ids.end(),path.occurrence_ids.begin()))return {};
        const auto& source=path.occurrence_ids.back();return sources.contains(source)?source:std::string{};
    };
    if(id.empty()&&selected)parameters.source_id=source_id(*selected);
    auto* dialog=new DerivedCopyDialog(initial,parameters,[this,edit](auto value,auto parameters) {
        auto copy=edit->initial;copy.id=value.id;copy.name=value.name;
        copy.placement=value.placement;copy.parameters=std::move(parameters);
        try {static_cast<void>(workspace::commit_derived_copy(workspace_,kernel_,*edit,std::move(copy)));}
        catch(const std::exception& error){throw std::runtime_error(tr(error.what()).toStdString());}
    },this);
    if(sources.contains(parameters.source_id))dialog->set_source(parameters.source_id,sources.at(parameters.source_id).name);
    properties_dialog_=dialog;properties_dialog_instance_path_=prefix;primitive_parameter_owner_id_=initial.id;
    primitive_reference_dialog_=dialog;primitive_reference_geometry_=geometry;
    if(part){body_dialog_context_=available;body_dialog_step_id_=initial.id;}

    dialog->request_placement=[this,dialog](std::size_t index){
        viewer_->set_original_container_selection(false);
        dialog->end_input();dialog->changed();feature_reference_pick_={};feature_reference_end_={};start_primitive_reference_selection(index);
    };
    dialog->request_input=[this,dialog,geometry,source_id,prefix,sources](int row) {
        pending_primitive_reference_index_.reset();primitive_reference_auto_advance_=false;set_local_origin_selection_mode(false);dialog->arm(row);
        // Offer whole Bodies alongside leaf solids through the same picker.
        // These temporary container-display markers never become topology
        // references and are discarded when reference input changes/ends.
        if(row!=1&&has_copy_source_offers(viewer_->mesh())){dialog->changed();return;}
        if(row==1&&!has_copy_source_offers(viewer_->mesh())) {
            auto mesh=viewer_->mesh();
            for(const auto& [id,source]:sources)if(source.whole_body) {
                kernel::ViewerMesh offer;offer.vertices=source.mesh.vertices;offer.triangles=source.mesh.triangles;offer.edges=source.mesh.edges;
                offer.triangle_references.resize(offer.triangles.size()/3,kernel::FaceReference{id,"container:display",prefix});
                for(auto& edge:offer.edges){edge.reference={id,"container:display",prefix};edge.display_owner_id=id;}
                if(!prefix.empty()) {
                    const auto path=assembly::InstancePath::decode(prefix);
                    for(auto& p:offer.vertices)p=workspace_.occurrence_point_to_scene(workspace_.displayed_document_id(),path,p);
                    for(auto& edge:offer.edges)for(auto& p:edge.points)p=workspace_.occurrence_point_to_scene(workspace_.displayed_document_id(),path,p);
                }
                kernel::ViewerReferenceGeometry references{offer.vertices,offer.triangles,offer.triangle_references,offer.edges,{},{}};
                append_derived_copy_references(mesh.original_references,references);
            }
            viewer_->set_mesh(std::move(mesh));
        }
        const auto accepts=[dialog,geometry,source_id,prefix,row](const viewer::ViewerCandidate& candidate) {
            if(row==1)return (candidate.kind==viewer::CandidateKind::Occurrence||candidate.kind==viewer::CandidateKind::Container)&&!source_id(candidate).empty();
            if(row>=2) {
                if(candidate.kind!=viewer::CandidateKind::Axis||candidate.instance_path!=prefix||candidate.owner_id!=dialog->pending.container_origin.id)return false;
                const auto key=candidate.semantic_key;
                if(key!="origin:axis:x"&&key!="origin:axis:y"&&key!="origin:axis:z")return false;
                const int axis=static_cast<int>(std::string("xyz").find(key.back()));
                for(int i=0;i<3;++i)if(i!=row-2&&dialog->derived_copy.pattern->linear[i].local_axis==axis)return false;
                return true;
            }
            if(dialog->derived_copy.pattern) {
                if(candidate.kind!=viewer::CandidateKind::Axis&&candidate.kind!=viewer::CandidateKind::Edge)return false;
            } else if(candidate.kind!=viewer::CandidateKind::Plane&&candidate.kind!=viewer::CandidateKind::Face)return false;
            auto mirror=dialog->derived_copy;mirror.reference={candidate.instance_path,candidate.owner_id,candidate.semantic_key};
            if(mirror.reference.instance_path==prefix)mirror.reference.instance_path.clear();
            else if(!prefix.empty()) {
                const auto path=assembly::InstancePath::decode(mirror.reference.instance_path),parent=assembly::InstancePath::decode(prefix);
                if(path.occurrence_ids.size()<=parent.occurrence_ids.size()||!std::equal(parent.occurrence_ids.begin(),parent.occurrence_ids.end(),path.occurrence_ids.begin()))return false;
                auto local=path;local.occurrence_ids.erase(local.occurrence_ids.begin(),local.occurrence_ids.begin()+parent.occurrence_ids.size());mirror.reference.instance_path=local.encoded();
            }
            try{document::PartDocument::resolve_copy_reference(mirror,dialog->pending.id,dialog->pending.placement,geometry);return true;}catch(const std::exception&){return false;}
        };
        tree_->setProperty("commandSelectionActive",true);
        viewer_->set_selection_contract(row==1?std::vector<viewer::CandidateKind>{viewer::CandidateKind::Occurrence,viewer::CandidateKind::Container}:
            dialog->derived_copy.pattern ? std::vector<viewer::CandidateKind>{viewer::CandidateKind::Axis,viewer::CandidateKind::Edge} :
            std::vector<viewer::CandidateKind>{viewer::CandidateKind::Plane,viewer::CandidateKind::Face});
        viewer_->set_original_container_selection(row==1);
        viewer_->set_candidate_priority(row==1?std::function<int(const viewer::ViewerCandidate&)>{[source_id,sources](const auto& candidate) {
            const auto id=source_id(candidate);return sources.contains(id)&&sources.at(id).whole_body?1:0;
        }}:std::function<int(const viewer::ViewerCandidate&)>{});
        // set_selection_contract clears any previous filter.
        viewer_->set_candidate_filter(accepts);
        feature_reference_pick_=[this,dialog,source_id,prefix,row,accepts,sources](const auto& candidate){if(!accepts(candidate))return;
            feature_reference_pick_={};feature_reference_end_={};
            if(row==1){const auto id=source_id(candidate);dialog->set_source(id,sources.at(id).name);}
            else if(row>=2)dialog->set_linear_axis(row-2,static_cast<int>(std::string("xyz").find(candidate.semantic_key.back())));
            else {auto path=assembly::InstancePath::decode(candidate.instance_path);const auto parent=assembly::InstancePath::decode(prefix);
                path.occurrence_ids.erase(path.occurrence_ids.begin(),path.occurrence_ids.begin()+parent.occurrence_ids.size());
                dialog->set_plane({path.encoded(),candidate.owner_id,candidate.semantic_key},QString::fromStdString(candidate.semantic_key));}
            viewer_->clear_selection();tree_->clearSelection();};
        feature_reference_end_=[this,dialog]{feature_reference_pick_={};feature_reference_end_={};dialog->end_input();dialog->clear_reference_highlights();dialog->changed();};
        state_->setText(row==1?tr("Vyberte zdrojový solid, těleso nebo komponentu."):row>=2?tr("Vyberte osu X, Y nebo Z vlastního počátku Pole."):dialog->derived_copy.pattern?tr("Vyberte osu kruhového Pole."):tr("Vyberte rovinu nebo rovinnou plochu zrcadlení."));
    };
    dialog->changed=[this,dialog,geometry,sources,prefix,document_id,graph,id,available]{
        if(!dialog->isVisible())return;
        const bool valid=dialog->resolve_pending_placement(geometry);
        auto origin=mirror_origin(dialog->pending);
        if(workspace_.open_part(document_id))primitive_origin_preview_mesh_.reset();else primitive_origin_preview_mesh_=origin;
        preserve_view_on_refresh_=true;refresh_scene();
        if(prefix.empty()&&workspace_.open_part(document_id)&&std::ranges::none_of(viewer_->mesh().original_references.axes,[&](const auto& axis) {
            return axis.reference.owner_id==dialog->pending.container_origin.id;
        })) {
            auto mesh=viewer_->mesh();append_derived_copy_references(mesh,origin);
            append_derived_copy_references(mesh.original_references,origin.original_references);viewer_->set_mesh(std::move(mesh));
        }
        if(!prefix.empty()) {
            if(workspace_.open_part(document_id)) {
                kernel::BodyResult input;
                const auto* part=workspace_.open_part(document_id);
                if(!part->session.calculated_boundaries().empty())for(const auto& id:available) {
                    const auto& outputs=part->session.calculated_boundaries().back().body_outputs;const auto found=outputs.find(id);
                    if(found!=outputs.end()){append_derived_copy_references(input.mesh,found->second->mesh);append_derived_copy_references(input.mesh.original_references,found->second->mesh.original_references);}
                }
                append_derived_copy_references(input.mesh,origin);append_derived_copy_references(input.mesh.original_references,origin.original_references);
                viewer_->set_mesh(workspace_.build_scene_with_part_override(workspace_.displayed_document_id(),assembly::InstancePath::decode(prefix),std::move(input)));
            } else if(derived_copy_assembly_preview_) {
                auto preview=*derived_copy_assembly_preview_;document::ConstructionObject origin;
                origin.id=dialog->pending.id;origin.entity_id=dialog->pending.feature_id;origin.container_origin=dialog->pending.container_origin;
                origin.kind=document::ConstructionKind::Point;origin.reference_valid=false;
                const auto& p=dialog->pending.placement;origin.origin={p.x,p.y,p.z};origin.rotation={p.rotation_x,p.rotation_y,p.rotation_z};preview.constructions.push_back(origin);
                viewer_->set_mesh(workspace_.build_scene_with_assembly_override(workspace_.displayed_document_id(),assembly::InstancePath::decode(prefix),preview));
            }
        }
        std::vector<kernel::ViewerEdge> edges;
        try {
            if(!valid)throw std::invalid_argument("Chybí reference umístění kontejneru.");
            document::PartDocument::resolve_copy_reference(dialog->derived_copy,dialog->pending.id,dialog->pending.placement,geometry);
            if(!sources.contains(dialog->derived_copy.source_id))throw std::invalid_argument("Vyberte zdrojový solid, těleso nebo komponentu.");
            if(const auto* part=workspace_.open_part(document_id)) {
                const auto* source=part->session.document().find_container(dialog->derived_copy.source_id);
                dialog->derived_copy.subtract_source=source&&source->combine_mode==document::CombineMode::Subtract;
                auto preview=part->session.document();auto updated=graph;document::BodyHistory body;
                if(!id.empty())body=*updated.find(id);
                body.scope.id=dialog->pending.id;body.name=dialog->pending.name;body.scope.placement=dialog->pending.placement;body.derived_copy=dialog->derived_copy;
                if(id.empty())static_cast<void>(updated.create_derived_copy(body));else updated.update_body(body);
                preview.set_body_history(std::move(updated));body_dialog_preview_=std::move(preview);
            }
            if(dialog->derived_copy.pattern) {
                const auto p=kernel::validated_pattern(*dialog->derived_copy.pattern);
                for(unsigned i=1;i<p.count;++i){const auto mesh=kernel::pattern_copy_mesh(sources.at(dialog->derived_copy.source_id).mesh,p,i);edges.insert(edges.end(),mesh.edges.begin(),mesh.edges.end());}
                if(p.circular){kernel::ViewerEdge axis;axis.overlay=true;axis.dash_dot=true;
                    axis.points={{p.origin.x-p.axis.x*50,p.origin.y-p.axis.y*50,p.origin.z-p.axis.z*50},{p.origin.x+p.axis.x*50,p.origin.y+p.axis.y*50,p.origin.z+p.axis.z*50}};edges.push_back(std::move(axis));}
            } else edges=kernel::mirrored_viewer_mesh(sources.at(dialog->derived_copy.source_id).mesh,dialog->derived_copy.resolved_plane).edges;
            dialog->set_status(tr("Náhled je připraven. OK vypočítá a uloží výsledek."));
        }catch(const std::exception& e){dialog->set_status(QString::fromUtf8(e.what()));}
        for(auto& edge:edges){edge.overlay=true;if(!prefix.empty())for(auto& p:edge.points)p=workspace_.occurrence_point_to_scene(workspace_.displayed_document_id(),assembly::InstancePath::decode(prefix),p);}
        viewer_->set_transient_edges(std::move(edges));
        if(id.empty()&&workspace_.open_assembly(document_id)) {
            auto* parent=tree_->topLevelItem(0);
            if(!prefix.empty())for(QTreeWidgetItemIterator i(tree_);*i;++i)
                if((*i)->data(0,Qt::UserRole+1).toString().toStdString()==prefix){parent=*i;break;}
            if(parent){auto* row=new QTreeWidgetItem(parent,{QString::fromStdString(dialog->pending.name)});
                row->setData(0,Qt::UserRole,QString::fromStdString(dialog->pending.id));row->setData(0,Qt::UserRole+3,"copy-draft");
                row->setIcon(0,resource_icon(dialog->derived_copy.pattern?"pattern":"mirror"));row->setForeground(0,QBrush(QColor("#4DD811")));}
        }
        std::set<viewer::EdgeKey> highlights;
        bool inspecting=false;
        for(int field=0;field<dialog->input_count();++field)if(field!=1&&dialog->inspected(field)&&!dialog->input_reference(field).owner_id.empty()) {
            inspecting=true;const auto ref=dialog->input_reference(field);auto path=assembly::InstancePath::decode(prefix);
            const auto local=assembly::InstancePath::decode(ref.instance_path);for(const auto& p:local.occurrence_ids)path=path.child(p);
            highlights.insert({ref.owner_id,ref.semantic_key,path.encoded()});if(ref.semantic_key=="plane")highlights.insert({ref.owner_id,"border",path.encoded()});
        }
        if(dialog->inspected(1)&&sources.contains(dialog->derived_copy.source_id))for(const auto& edge:sources.at(dialog->derived_copy.source_id).mesh.edges) {
            auto path=assembly::InstancePath::decode(prefix);for(const auto& p:assembly::InstancePath::decode(edge.reference.instance_path).occurrence_ids)path=path.child(p);
            highlights.insert({edge.reference.owner_id,edge.reference.semantic_key,path.encoded()});
        }
        viewer_->set_constraint_reference_highlights({},std::move(highlights));
        if(dialog->active_input()>=0)dialog->request_input(dialog->active_input());
        else if(!pending_primitive_reference_index_) {feature_reference_pick_={};set_primitive_properties_dimension_selection();}
        if(inspecting||dialog->inspected(1))feature_reference_end_=[this,dialog]{feature_reference_pick_={};feature_reference_end_={};dialog->end_input();dialog->changed();};
    };
    connect(dialog,&QDialog::finished,this,[this]{
        feature_reference_pick_={};feature_reference_end_={};pending_primitive_reference_index_.reset();primitive_reference_auto_advance_=false;
        primitive_reference_dialog_=nullptr;primitive_reference_geometry_={};primitive_origin_preview_mesh_.reset();
        properties_dialog_=nullptr;properties_dialog_instance_path_.clear();primitive_parameter_owner_id_.clear();
        body_dialog_preview_.reset();body_dialog_context_.reset();body_dialog_step_id_.clear();derived_copy_assembly_preview_.reset();
        local_origin_selection_dialog_=nullptr;local_origin_selection_active_=false;visible_local_origin_ids_.clear();visible_occurrence_origin_paths_.clear();selectable_local_origin_container_ids_.clear();
        viewer_->set_transient_edges({});viewer_->set_constraint_reference_highlights({},{});viewer_->set_candidate_filter({});viewer_->set_selection_contract({});viewer_->clear_selection();
        viewer_->set_candidate_priority({});
        viewer_->set_original_container_selection(false);
        tree_->setProperty("commandSelectionActive",false);preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
    });
    bind_local_origin_selection(dialog);dialog->show();dialog->changed();
    if(id.empty())start_primitive_reference_selection(0,true);
}

bool AssemblyWorkspaceWindow::accept_derived_copy_tree_reference(QTreeWidgetItem* item) {
    auto* dialog=dynamic_cast<DerivedCopyDialog*>(properties_dialog_);if(!dialog||dialog->active_input()<0||!feature_reference_pick_)return false;
    viewer::ViewerCandidate candidate;candidate.instance_path=item->data(0,Qt::UserRole+1).toString().toStdString();
    candidate.owner_id=item->data(0,Qt::UserRole).toString().toStdString();const auto kind=item->data(0,Qt::UserRole+3).toString();
    if(dialog->active_input()==1)candidate.kind=candidate.instance_path.empty()?viewer::CandidateKind::Container:viewer::CandidateKind::Occurrence;
    else {
        candidate.kind=dialog->derived_copy.pattern?viewer::CandidateKind::Axis:viewer::CandidateKind::Plane;candidate.geometry=viewer::CandidateGeometry::OriginalReference;
        if(kind=="origin-reference") {
            if(item->data(0,Qt::UserRole+6).isValid())candidate.owner_id=item->data(0,Qt::UserRole+6).toString().toStdString();
            candidate.semantic_key=item->data(0,Qt::UserRole+5).toString().toStdString();
        } else if(kind=="part-construction"||kind=="assembly-construction") {
            const auto* part=workspace_.open_part(workspace_.active_document_id());const auto* assembly=workspace_.open_assembly(workspace_.active_document_id());
            const auto* object=part?part->session.document().find_construction(candidate.owner_id):assembly?assembly->session.document().find_construction(candidate.owner_id):nullptr;
            if(object&&object->kind==(dialog->derived_copy.pattern?document::ConstructionKind::Axis:document::ConstructionKind::Plane)){
                candidate.owner_id=object->entity_id;candidate.semantic_key=dialog->derived_copy.pattern?"axis":"plane";}
        }
    }
    auto pick=feature_reference_pick_;pick(candidate);return true;
}

void AssemblyWorkspaceWindow::show_derived_source_properties(const std::string& id) {
    if(const auto* part=workspace_.open_part(workspace_.active_document_id())) {
        const auto& graph=part->session.document().body_history;auto source_id=id;const auto* body=graph.find(source_id);std::set<std::string> seen;
        while(body&&body->derived_copy&&seen.insert(body->scope.id).second){source_id=body->derived_copy->source_id;body=graph.find(source_id);}
        if(const auto* feature=part->session.document().find_container(source_id)) {
            show_parameter_dimensions(feature->id);show_primitive_properties(feature->feature_kind,feature->id);return;
        }
        if(graph.find_boolean(source_id)){show_body_boolean_properties(source_id);return;}
        if(body) {
            for(auto entry=body->entries.rbegin();entry!=body->entries.rend();++entry)
                if(const auto* feature=part->session.document().find_container(entry->id);feature&&feature->feature_kind!=document::FeatureKind::Sketch) {
                    show_parameter_dimensions(feature->id);show_primitive_properties(feature->feature_kind,feature->id);return;
                }
            show_body_properties(body->scope.id);
        }
    } else if(const auto* assembly=workspace_.open_assembly(workspace_.active_document_id())) {
        if(const auto* source=assembly->session.document().derived_source(id)) {
            auto path=assembly::InstancePath::decode(workspace_.active_occurrence_path()).child(source->occurrence_id);show_component_properties(path.encoded());
        }
    }
}
} // namespace zima::app
