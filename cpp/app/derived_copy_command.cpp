#include "assembly_workspace_window.hpp"
#include "derived_copy_dialog.hpp"
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
struct MirrorSource {QString name;kernel::ViewerMesh mesh;};
}

void AssemblyWorkspaceWindow::show_derived_copy_properties(const std::string& id,bool pattern) {
    if(properties_dialog_){properties_dialog_->raise();return;}
    const auto document_id=workspace_.active_document_id();auto* part=workspace_.open_part(document_id);auto* assembly=workspace_.open_assembly(document_id);
    if(!part&&!assembly)return;
    const auto selected=viewer_->confirmed_candidate();
    document::HistoryContainer initial;initial.id=id.empty()?kernel::make_stable_id():id;
    initial.feature_id=initial.id+":entity";initial.container_origin=document::create_container_origin(initial.id);initial.name=pattern?"Pole":"Zrcadlo";
    document::DerivedCopyParameters parameters;
    if(pattern){parameters.pattern=kernel::PatternRequest{};parameters.reference={{},initial.container_origin.id,"origin:axis:z"};}
    document::BodyHistoryGraph graph;
    std::map<std::string,MirrorSource> sources;
    kernel::ViewerReferenceGeometry geometry;
    std::vector<std::string> available;
    if(part) {
        const auto& document=part->session.document();graph=document.body_history;
        if(graph.bodies().empty()&&!document.history_order.empty()) {
            static_cast<void>(graph.create_body("Těleso 1"));for(const auto& entry:document.history_order)graph.insert(entry);graph.activate({});
        }
        const auto boundary=id.empty()?graph.insertion_cursor():static_cast<std::size_t>(std::distance(graph.order().begin(),std::ranges::find(graph.order(),id)));
        available=graph.available_before(boundary);
        if(!id.empty()) {const auto* body=graph.find(id);if(!body||!body->derived_copy)return;parameters=*body->derived_copy;initial.name=body->name;initial.placement=body->scope.placement;}
        if(!part->session.calculated_boundaries().empty()) {
            const auto& result=part->session.calculated_boundaries().back();geometry=result.mesh.original_references;
            for(const auto& source:available) {
                const auto found=result.body_outputs.find(source);
                if(found==result.body_outputs.end()&&graph.bodies().size()!=1)continue;
                const auto* body=graph.find(source);
                sources.emplace(source,MirrorSource{QString::fromStdString(body?body->name:graph.find_boolean(source)->name),
                    found==result.body_outputs.end()?result.mesh:found->second.mesh});
            }
        }
        append_derived_copy_references(geometry,document.origin_viewer_mesh().original_references);
        append_derived_copy_references(geometry,document.body_origin_reference_geometry());
        append_derived_copy_references(geometry,document.construction_viewer_mesh().original_references);
        append_derived_copy_references(geometry,document.history_origin_reference_geometry_before({}));
        // Reference owners must precede the Mirror in this document history.
        for(auto& ref:geometry.triangle_references)if(const auto* owner=document.body_owner_for_object(ref.owner_id);
            owner&&std::ranges::find(available,owner->scope.id)==available.end())ref={};
    } else {
        const auto& document=assembly->session.document();geometry=document.build_scene().original_references;
        append_derived_copy_references(geometry,document.origin_viewer_mesh().original_references);
        append_derived_copy_references(geometry,document.construction_viewer_mesh().original_references);
        if(!id.empty()){const auto* c=document.find_occurrence(id);if(!c||!c->derived_copy)return;parameters=*c->derived_copy;initial.name=c->name;initial.placement=c->copy_placement;}
        auto preview=document;bool downstream=false;
        for(auto& component:preview.components){if(component.occurrence_id==id)downstream=true;if(downstream)component.visible=false;}
        derived_copy_assembly_preview_=std::move(preview);
        for(const auto& source:document.components) {
            if(source.occurrence_id==id)break;
            if(source.suppressed)continue;
            auto isolated=document;isolated.components={source};isolated.dependencies.clear();isolated.constructions.clear();
            sources.emplace(source.occurrence_id,MirrorSource{QString::fromStdString(source.name),isolated.build_scene()});
        }
    }
    const auto unavailable=[&](const auto& reference) {
        if(part){const auto* owner=part->session.document().body_owner_for_object(reference.owner_id);
            return owner&&std::ranges::find(available,owner->scope.id)==available.end();}
        const auto path=assembly::InstancePath::decode(reference.instance_path);
        return !path.occurrence_ids.empty()&&!sources.contains(path.occurrence_ids.front());
    };
    for(auto& ref:geometry.triangle_references)if(unavailable(ref))ref={};
    std::erase_if(geometry.edges,[&](const auto& e){return unavailable(e.reference);});
    std::erase_if(geometry.points,[&](const auto& p){return unavailable(p.reference);});
    std::erase_if(geometry.axes,[&](const auto& a){return unavailable(a.reference);});
    if(sources.empty()){derived_copy_assembly_preview_.reset();state_->setText(tr("Nejprve vytvořte zdrojové těleso nebo vložte komponentu."));return;}
    const auto prefix=active_occurrence_path_;
    const auto source_id=[this,document_id,prefix,sources](const viewer::ViewerCandidate& candidate)->std::string {
        if(const auto* part=workspace_.open_part(document_id)) {
            if(candidate.instance_path!=prefix)return {};
            if(sources.contains(candidate.owner_id))return candidate.owner_id;
            if(const auto* body=part->session.document().body_owner_for_object(candidate.owner_id);body&&sources.contains(body->scope.id))return body->scope.id;
            if(part->session.document().body_history.bodies().empty()&&sources.size()==1)return sources.begin()->first;
            return {};
        }
        const auto path=assembly::InstancePath::decode(candidate.instance_path),parent=assembly::InstancePath::decode(prefix);
        if(path.occurrence_ids.size()!=parent.occurrence_ids.size()+1||!std::equal(parent.occurrence_ids.begin(),parent.occurrence_ids.end(),path.occurrence_ids.begin()))return {};
        const auto& source=path.occurrence_ids.back();return sources.contains(source)?source:std::string{};
    };
    if(id.empty()&&selected)parameters.source_id=source_id(*selected);
    auto* dialog=new DerivedCopyDialog(initial,parameters,[this,document_id,graph,id,geometry](auto value,auto mirror) mutable {
        if(!document::resolve_placement(value.placement,geometry))throw std::invalid_argument("Chybí reference umístění kontejneru.");
        document::PartDocument::resolve_copy_reference(mirror,value.id,value.placement,geometry);
        if(auto* part=workspace_.open_part(document_id)) {
            auto next=part->session.document();auto updated=graph;document::BodyHistory body;
            if(!id.empty())body=*updated.find(id);
            body.scope.id=value.id;body.scope.placement=value.placement;body.name=value.name;body.derived_copy=mirror;
            if(id.empty())static_cast<void>(updated.create_derived_copy(body));else updated.update_body(body);
            updated.activate({});next.set_body_history(std::move(updated));
            auto result=calculate_part_with_resolved_references(next,&part->session.calculated_boundaries());
            part->session.commit(std::move(next),std::move(result));
        } else if(auto* assembly=workspace_.open_assembly(document_id)) {
            auto next=assembly->session.document();const auto* source=next.find_occurrence(mirror.source_id);
            if(!source)throw std::invalid_argument("Zdrojová komponenta není dostupná.");
            auto result=*source;result.occurrence_id=value.id;result.name=value.name;result.derived_copy=mirror;
            result.copy_placement=value.placement;result.placement={};result.placement_references.clear();result.grounded=true;
            if(id.empty())next.components.push_back(std::move(result));else *next.find_occurrence(id)=std::move(result);
            next.calculate_derived_copies(kernel_);assembly->session.commit(std::move(next));
        }
    },this);
    if(sources.contains(parameters.source_id))dialog->set_source(parameters.source_id,sources.at(parameters.source_id).name);
    properties_dialog_=dialog;properties_dialog_instance_path_=prefix;primitive_parameter_owner_id_=initial.id;
    primitive_reference_dialog_=dialog;primitive_reference_geometry_=geometry;
    if(part){body_dialog_context_=available;body_dialog_step_id_=initial.id;}

    dialog->request_placement=[this](std::size_t index){feature_reference_pick_={};feature_reference_end_={};start_primitive_reference_selection(index);};
    dialog->request_input=[this,dialog,geometry,source_id,prefix,sources](int row) {
        pending_primitive_reference_index_.reset();primitive_reference_auto_advance_=false;set_local_origin_selection_mode(false);dialog->arm(row);
        const auto accepts=[dialog,geometry,source_id,prefix,row](const viewer::ViewerCandidate& candidate) {
            if(row==1)return (candidate.kind==viewer::CandidateKind::Occurrence||candidate.kind==viewer::CandidateKind::Container)&&!source_id(candidate).empty();
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
        tree_->setProperty("commandSelectionActive",true);viewer_->set_candidate_filter(accepts);
        viewer_->set_selection_contract(row?std::vector<viewer::CandidateKind>{viewer::CandidateKind::Occurrence,viewer::CandidateKind::Container}:
            dialog->derived_copy.pattern ? std::vector<viewer::CandidateKind>{viewer::CandidateKind::Axis,viewer::CandidateKind::Edge} :
            std::vector<viewer::CandidateKind>{viewer::CandidateKind::Plane,viewer::CandidateKind::Face});
        feature_reference_pick_=[this,dialog,source_id,prefix,row,accepts,sources](const auto& candidate){if(!accepts(candidate))return;
            feature_reference_pick_={};feature_reference_end_={};
            if(row){const auto id=source_id(candidate);dialog->set_source(id,sources.at(id).name);}
            else {auto path=assembly::InstancePath::decode(candidate.instance_path);const auto parent=assembly::InstancePath::decode(prefix);
                path.occurrence_ids.erase(path.occurrence_ids.begin(),path.occurrence_ids.begin()+parent.occurrence_ids.size());
                dialog->set_plane({path.encoded(),candidate.owner_id,candidate.semantic_key},QString::fromStdString(candidate.semantic_key));}
            viewer_->clear_selection();tree_->clearSelection();};
        feature_reference_end_=[this,dialog]{feature_reference_pick_={};feature_reference_end_={};dialog->end_input();dialog->clear_reference_highlights();dialog->changed();};
        state_->setText(row?tr("Vyberte zdrojové těleso nebo komponentu."):tr("Vyberte rovinu nebo rovinnou plochu zrcadlení."));
    };
    dialog->changed=[this,dialog,geometry,sources,prefix,document_id,graph,id,available]{
        if(!dialog->isVisible())return;
        const bool valid=dialog->resolve_pending_placement(geometry);
        auto origin=mirror_origin(dialog->pending);primitive_origin_preview_mesh_=origin;
        preserve_view_on_refresh_=true;refresh_scene();
        if(!prefix.empty()) {
            if(workspace_.open_part(document_id)) {
                kernel::BodyResult input;
                for(const auto& [id,source]:sources){append_derived_copy_references(input.mesh,source.mesh);append_derived_copy_references(input.mesh.original_references,source.mesh.original_references);}
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
            if(!sources.contains(dialog->derived_copy.source_id))throw std::invalid_argument("Vyberte zdrojové těleso nebo komponentu.");
            if(const auto* part=workspace_.open_part(document_id)) {
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
        if(dialog->inspected(0)&&!dialog->derived_copy.reference.owner_id.empty()) {
            const auto& ref=dialog->derived_copy.reference;auto path=assembly::InstancePath::decode(prefix);
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
        if(dialog->inspected(0)||dialog->inspected(1))feature_reference_end_=[this,dialog]{feature_reference_pick_={};feature_reference_end_={};dialog->end_input();dialog->changed();};
    };
    connect(dialog,&QDialog::finished,this,[this]{
        feature_reference_pick_={};feature_reference_end_={};pending_primitive_reference_index_.reset();primitive_reference_auto_advance_=false;
        primitive_reference_dialog_=nullptr;primitive_reference_geometry_={};primitive_origin_preview_mesh_.reset();
        properties_dialog_=nullptr;properties_dialog_instance_path_.clear();primitive_parameter_owner_id_.clear();
        body_dialog_preview_.reset();body_dialog_context_.reset();body_dialog_step_id_.clear();derived_copy_assembly_preview_.reset();
        local_origin_selection_dialog_=nullptr;local_origin_selection_active_=false;visible_local_origin_ids_.clear();selectable_local_origin_container_ids_.clear();
        viewer_->set_transient_edges({});viewer_->set_constraint_reference_highlights({},{});viewer_->set_candidate_filter({});viewer_->set_selection_contract({});viewer_->clear_selection();
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
            auto path=assembly::InstancePath::decode(active_occurrence_path_).child(source->occurrence_id);show_component_properties(path.encoded());
        }
    }
}
} // namespace zima::app
