#include "assembly_workspace_window.hpp"
#include "cam_body_dialog.hpp"
#include "cam_machine_dialog.hpp"
#include "cam_tools_dialog.hpp"
#include "cam_operations_dialog.hpp"
#include "cam_calculation_dialog.hpp"
#include "cam_nc_dialog.hpp"
#include "cam_clamping_dialog.hpp"
#include "file_dialog.hpp"
#include "workspace/workspace_internal.hpp"
#include <zima/workspace/cam_operations.hpp>
#include <zima/workspace/calculated_body_source.hpp>
#include <zima/document/placement_reference_geometry.hpp>
#include <zima/document/cam_library.hpp>
#include <zima/kernel/stable_id.hpp>
#include <QScopedValueRollback>
#include <QTabBar>
#include <QSaveFile>

namespace zima::app {
using namespace workspace_detail;
namespace {
// These commands own the displayed input scene until their root dialog closes.
// Child editors share that lifetime; they must not release its tab guard.
void keep_cam_document(QTabBar* tabs,QDialog* root){
    const bool enabled=tabs->isEnabled();tabs->setEnabled(false);
    QObject::connect(root,&QDialog::finished,tabs,[tabs,enabled]{tabs->setEnabled(enabled);});
}
}
bool AssemblyWorkspaceWindow::active_cam_body()const {
    const auto* part=workspace_.open_part(workspace_.active_document_id());
    const auto* body=part?part->session.document().body_history.find(part->session.document().body_history.active_body_id()):nullptr;
    return body&&body->cam.has_value();
}
kernel::ViewerMesh AssemblyWorkspaceWindow::cam_display_mesh()const {
    kernel::ViewerMesh mesh;const auto* part=workspace_.open_part(workspace_.active_document_id());if(!part)return mesh;
    const auto& graph=part->session.document().body_history;const auto* body=graph.find(graph.active_body_id());if(!body||!body->cam)return mesh;
    for(const auto& id:graph.cam_sources(body->scope.id)) {
        const auto* source=graph.find(id);
        if(source&&source->cam){if(workspace::cam_result_state(part->session,id)==workspace::CamResultState::Current)append_mesh(mesh,source->cam->calculated->remaining->mesh);}
        else if(const auto* source=workspace::calculated_material_source(part->session,id))append_mesh(mesh,(*source)->mesh);
    }
    return mesh;
}
void AssemblyWorkspaceWindow::apply_cam_surface_styles(const kernel::SurfaceStyle& base,
    const std::map<std::string,kernel::SurfaceStyle>& instances,
    const std::map<std::string,kernel::SurfaceStyle>& owners,std::map<std::string,kernel::SurfaceStyle>& faces)const {
    const auto* part=workspace_.open_part(workspace_.active_document_id());if(!part)return;
    const auto& graph=part->session.document().body_history;const auto* active=graph.find(graph.active_body_id());
    const auto* setup=cam_setup_preview_?&*cam_setup_preview_:active&&active->cam?&*active->cam:nullptr;if(!setup)return;
    const auto path=workspace_.active_occurrence_path(),prefix=path+"\x1f";
    const auto apply=[&](const std::string& id,int alpha){
        const auto* body=graph.find(id);kernel::BodySnapshot snapshot;
        if(body&&body->cam){if(workspace::cam_result_state(part->session,id)!=workspace::CamResultState::Current)return;snapshot=body->cam->calculated->remaining;}
        else if(const auto* source=workspace::calculated_material_source(part->session,id))snapshot=*source;
        if(snapshot->mesh.triangle_references.empty())return;
        std::set<std::string> visited;
        for(const auto& face:snapshot->mesh.triangle_references){const auto owner=prefix+face.owner_id,key=owner+"\x1f"+face.semantic_key;
            if(!visited.insert(key).second)continue;
            auto style=base;if(const auto found=instances.find(path);found!=instances.end())style=found->second;
            if(const auto found=owners.find(owner);found!=owners.end())style=found->second;
            if(const auto found=faces.find(key);found!=faces.end())style=found->second;
            QColor color(QString::fromStdString(style.color));color.setAlpha(alpha);style.color=color.name(QColor::HexArgb).toStdString();faces[key]=std::move(style);
        }
    };
    // Display-only role overrides: authored colors/finish and native references
    // remain intact. Target/fixture roles win if the same Body serves both roles.
    apply(setup->stock_id,64);apply(setup->target_id,255);for(const auto& id:setup->fixture_ids)apply(id,255);
}
void AssemblyWorkspaceWindow::show_cam_body_properties(const std::string& id) {
    if(properties_dialog_){properties_dialog_->raise();return;}
    const auto document_id=workspace_.active_document_id();const auto* part=workspace_.open_part(document_id);
    // The setup editor is authored in its source Part. Assembly occurrences
    // continue to display shared Part geometry without owning its machining.
    if(!part||document_id!=workspace_.displayed_document_id())return;
    const auto& doc=part->session.document();const auto& graph=doc.body_history;
    if(id.empty()&&!graph.active_body_id().empty())return;
    workspace::BodyGraphEdit edit;std::vector<std::string> stocks,material;
    try{edit=workspace::prepare_cam_edit(doc,id);stocks=workspace::cam_source_candidates(part->session,edit,true);
        material=workspace::cam_source_candidates(part->session,edit,false);primitive_reference_geometry_=workspace::cam_placement_geometry(part->session,edit);}
    catch(const std::exception& e){state_->setText(tr(e.what()));return;}
    if(id.empty()&&(stocks.empty()||material.empty())){state_->setText(tr("CAM requires valid calculated stock and target Bodies. Regenerate the sources first."));return;}
    document::BodyHistory initial;
    if(!id.empty())initial=*graph.find(id);
    else {initial.scope.id=edit.object_id;initial.cam.emplace();initial.cam->machine.id=kernel::make_stable_id();initial.cam->machine.name=tr("Machine").toStdString();
        for(unsigned n=1;;++n){initial.name=tr("CAM setup").toStdString()+" "+std::to_string(n);if(std::ranges::none_of(graph.bodies(),[&](const auto& b){return b.name==initial.name;}))break;}}
    std::map<std::string,kernel::BodySnapshot> sources;std::map<std::string,QString> labels;
    for(const auto& source:stocks){const auto* b=graph.find(source);const auto* operation=graph.find_boolean(source);
        labels.emplace(source,QString::fromStdString(b?b->name:operation->name));
        sources.emplace(source,b&&b->cam?b->cam->calculated->remaining:*workspace::calculated_material_source(part->session,source));}
    // Keep labels of unresolved inputs available for replacement.
    for(const auto& b:graph.bodies())labels.try_emplace(b.scope.id,QString::fromStdString(b.name));
    auto* dialog=new CamBodyDialog(initial,[this,edit](auto value){static_cast<void>(workspace::commit_cam(workspace_,edit,std::move(value)));},this);
    dialog->set_sources(std::move(labels),{stocks.begin(),stocks.end()},{material.begin(),material.end()});
    const auto boundary=id.empty()?graph.insertion_cursor():std::size_t(std::ranges::find(graph.order(),id)-graph.order().begin());
    dialog->set_forbidden_owner([doc,boundary](const std::string& owner){
        if(owner==doc.document_id+":origin")return false;const auto* body=doc.body_owner_for_object(owner);
        const auto& order=doc.body_history.order();const auto found=std::ranges::find(order,body?body->scope.id:owner);
        return found==order.end()||std::size_t(found-order.begin())>=boundary;
    });
    cam_setup_preview_=*initial.cam;
    std::set<std::string> visible;
    for(const auto& [source,snapshot]:sources){const auto* b=graph.find(source);if(b?b->visible:graph.find_boolean(source)->visible)visible.insert(source);}
    const auto shown_sources=[visible,sources](const document::CamDefinition& definition){auto ids=visible;
        for(const auto& id:definition.fixture_ids)if(sources.contains(id))ids.insert(id);
        for(const auto& id:{definition.stock_id,definition.target_id})if(sources.contains(id))ids.insert(id);return ids;};
    const auto build_input=[this,sources,origin=doc.origin_viewer_mesh()](const std::set<std::string>& ids){kernel::ViewerMesh mesh;
        for(const auto& id:ids)append_mesh(mesh,sources.at(id)->mesh);append_mesh(mesh,origin);
        append_reference_geometry(mesh.original_references,primitive_reference_geometry_);return mesh;};
    auto shown=std::make_shared<std::set<std::string>>(shown_sources(*initial.cam));
    cam_setup_input_=std::make_shared<kernel::ViewerMesh>(build_input(*shown));
    keep_cam_document(tabs_,dialog);
    properties_dialog_=dialog;primitive_reference_dialog_=dialog;primitive_parameter_owner_id_=initial.scope.id;construction_dimension_object_id_=initial.scope.id;body_dialog_step_id_=initial.scope.id;
    preserve_view_on_refresh_=true;refresh_scene();
    auto base=std::make_shared<kernel::ViewerMesh>(viewer_->mesh()),offered=std::make_shared<kernel::ViewerMesh>(*base);
    for(const auto& [source,snapshot]:sources){const auto& mesh=snapshot->mesh;const auto offset=std::uint32_t(offered->original_references.vertices.size());
        offered->original_references.vertices.insert(offered->original_references.vertices.end(),mesh.vertices.begin(),mesh.vertices.end());
        for(auto index:mesh.triangles)offered->original_references.triangles.push_back(offset+index);
        offered->original_references.triangle_references.insert(offered->original_references.triangle_references.end(),mesh.triangles.size()/3,kernel::FaceReference{source,"container:display",{}});
        for(auto edge:mesh.edges){edge.reference={source,"container:display",{}};edge.display_owner_id=source;offered->original_references.edges.push_back(std::move(edge));}}
    const auto offered_references=std::make_shared<kernel::ViewerReferenceGeometry>(offered->original_references);
    const auto highlights=[this,dialog]{viewer_->set_constraint_reference_highlights({},highlighted_reference_edge_keys(*dialog));};
    dialog->set_reference_highlights_changed_callback(highlights);
    auto carrier=std::make_shared<document::PartDocument>(document::PartDocument::create_default());carrier->document_id=initial.scope.id;
    document::BodyHistoryGraph frame_graph;const auto frame_id=frame_graph.create_body("frame");carrier->set_body_history(frame_graph);const auto origin=carrier->origin_viewer_mesh();
    auto updating=std::make_shared<bool>(false),offering=std::make_shared<bool>(false);
    const auto reference_index=std::make_shared<document::PlacementReferenceGeometryIndex>(primitive_reference_geometry_);
    dialog->changed=[this,dialog,base,offered,offering,updating,reference_index,carrier,frame_id,origin,sources,highlights,shown,shown_sources,build_input,offered_references]{
        if(*updating)return;const QScopedValueRollback guard(*updating,true);
        const auto definition=*dialog->pending_value().cam;
        if(!cam_setup_preview_||cam_setup_preview_->stock_id!=definition.stock_id||cam_setup_preview_->target_id!=definition.target_id||cam_setup_preview_->fixture_ids!=definition.fixture_ids){
            cam_setup_preview_=definition;update_viewer_body_colors();
        }
        const auto wanted=shown_sources(definition);const bool geometry_changed=wanted!=*shown;
        if(geometry_changed){*shown=wanted;*cam_setup_input_=build_input(wanted);*base=*cam_setup_input_;*offered=*base;offered->original_references=*offered_references;}
        const bool active=dialog->active_input();if(*offering!=active||geometry_changed){viewer_->set_mesh(active?*offered:*base);*offering=active;}
        if(active){viewer_->set_original_container_selection(true);viewer_->set_selection_contract({viewer::CandidateKind::Container});
            viewer_->set_candidate_filter([dialog,shown](const auto& c){return c.kind==viewer::CandidateKind::Container&&c.instance_path.empty()&&shown->contains(c.owner_id)&&dialog->accepts_source(c.owner_id);});
            tree_->setProperty("commandSelectionActive",true);
            feature_reference_pick_=[this,dialog](const auto& c){if(c.kind==viewer::CandidateKind::Container&&c.instance_path.empty()&&dialog->accepts_source(c.owner_id)){
                const auto source=c.owner_id;viewer_->clear_selection();tree_->clearSelection();dialog->set_source(source);}};
            feature_reference_end_=[this,dialog]{viewer_->clear_selection();tree_->clearSelection();dialog->end_input(true);};
        }else {feature_reference_pick_={};feature_reference_end_={};viewer_->set_original_container_selection(false);
            if(!pending_primitive_reference_index_){viewer_->set_selection_contract({});viewer_->set_candidate_filter([](const auto&){return false;});tree_->setProperty("commandSelectionActive",false);}}
        auto p=dialog->pending_value().cam->work_frame;kernel::Vec3 rotation;bool oriented=false;
        const auto geometry=reference_index->select(p.references);const bool valid=document::resolve_placement(p,geometry,&rotation,&oriented);
        const auto constraint=document::point_constraint_state(p.references,geometry,{p.x,p.y,p.z});primitive_translation_dof_=constraint.remaining_dof;
        dialog->set_translation_constraint_state(constraint,{p.x,p.y,p.z});dialog->set_rotation_constraint_state(document::orientation_constraint_state(p.references,geometry,true,{p.x,p.y,p.z}));
        dialog->set_orientation_base_rotation(rotation,oriented);dialog->set_resolved_rotation({p.rotation_x,p.rotation_y,p.rotation_z},valid);
        auto frame=*carrier->body_history.find(frame_id);frame.scope.placement=p;carrier->body_history.update_body(frame);
        viewer_->set_transient_context_mesh(carrier->place_body_mesh(origin,frame_id));highlights();
        std::vector<kernel::ViewerEdge> wire;
        for(const auto& source:dialog->inspected_sources())if(const auto found=sources.find(source);found!=sources.end())for(auto edge:found->second->mesh.edges){edge.overlay=true;edge.color="#00D1FF";wire.push_back(std::move(edge));}
        viewer_->set_transient_edges(std::move(wire));
    };
    dialog->begin_source_input=[this]{set_local_origin_selection_mode(false);pending_primitive_reference_index_.reset();primitive_reference_auto_advance_=false;};
    dialog->set_reference_request_callback([this,dialog](std::size_t row){dialog->end_input();start_primitive_reference_selection(row);});
    dialog->edit_machine=[this,dialog]{
        set_local_origin_selection_mode(false);dialog->end_input();pending_primitive_reference_index_.reset();dialog->set_active_reference_index(std::nullopt);
        dialog->changed();dialog->hide();
        auto* machine=new CamMachineDialog(dialog->pending_value().cam->machine,[dialog](auto value){dialog->set_machine(std::move(value));},this);
        machine->import_machine=[this,machine]{
            const auto path=open_file(machine,tr("CAM machine library"),application_settings_.resolved_paths.value("CamMachines"),"*.prtz",application_settings_.qt_translations);if(path.isEmpty())return;
            try{machine->replace_machine(document::cam_library_machine(document::PartDocument::load(std::filesystem::u8path(path.toStdString()))));}
            catch(const std::exception& e){machine->show_submission_error(tr(e.what()));}
        };
        properties_dialog_=machine;primitive_reference_dialog_=nullptr;
        connect(machine,&QDialog::finished,dialog,[this,dialog]{properties_dialog_=dialog;primitive_reference_dialog_=dialog;dialog->show();dialog->raise();dialog->changed();});machine->show();
    };
    const auto open_child=[this,dialog](ui::PropertiesSubWindow* child){
        set_local_origin_selection_mode(false);dialog->end_input();pending_primitive_reference_index_.reset();dialog->set_active_reference_index(std::nullopt);
        dialog->changed();dialog->hide();properties_dialog_=child;primitive_reference_dialog_=nullptr;
        connect(child,&QDialog::finished,dialog,[this,dialog]{properties_dialog_=dialog;primitive_reference_dialog_=dialog;dialog->show();dialog->raise();dialog->changed();});child->show();
    };
    dialog->edit_tools=[this,dialog,open_child]{auto* child=new CamToolsDialog(*dialog->pending_value().cam,[dialog](auto value){dialog->set_tools(std::move(value));},this);
        child->import_tool=[this,child]{
            const auto path=open_file(child,tr("CAM tool library"),application_settings_.resolved_paths.value("CamTools"),"*.toolz",application_settings_.qt_translations);if(path.isEmpty())return;
            try{child->append_tool(document::cam_library_tool(document::PartDocument::load(std::filesystem::u8path(path.toStdString()))));}
            catch(const std::exception& e){child->show_submission_error(tr(e.what()));}
        };open_child(child);};
    dialog->edit_operations=[this,dialog,open_child,edit,base]{
        const auto* part=workspace_.open_part(edit.document_id);if(!part)return;const auto& doc=part->session.document();
        std::map<std::string,QString> labels;auto meshes=std::make_shared<std::map<std::string,kernel::ViewerMesh>>();
        auto offered=std::make_shared<kernel::ViewerMesh>(*base);
        for(const auto& id:workspace::cam_clamping_candidates(part->session,edit)){
            const auto& sketch=*std::ranges::find(doc.sketches,id,&sketcher::Sketch::id);const auto* body=doc.body_owner_for_object(id);
            labels[id]=QString::fromStdString(body->name+" / "+sketch.name);auto mesh=doc.place_body_mesh(sketch.viewer_mesh(false),body->scope.id);
            for(auto edge:mesh.edges){edge.reference.owner_id=id;edge.display_owner_id=id;offered->original_references.edges.push_back(std::move(edge));}
            meshes->emplace(id,std::move(mesh));
        }
        auto* child=new CamOperationsDialog(*dialog->pending_value().cam,[dialog](auto value){dialog->set_operations(std::move(value));},this,std::move(labels));
        auto armed=std::make_shared<std::optional<bool>>();
        child->changed=[this,child,base,offered,meshes,armed]{
            const bool active=child->active_input();if(!armed->has_value()||**armed!=active){viewer_->set_mesh(active?*offered:*base);*armed=active;}
            viewer_->set_original_container_selection(active);viewer_->set_selection_contract(active?std::vector{viewer::CandidateKind::Container}:std::vector<viewer::CandidateKind>{});
            viewer_->set_candidate_filter([child](const auto& c){return c.instance_path.empty()&&child->accepts_source(c.owner_id);});tree_->setProperty("commandSelectionActive",active);
            feature_reference_pick_=active?std::function<void(const viewer::ViewerCandidate&)>([child](const auto& c){child->set_source(c.owner_id);}):std::function<void(const viewer::ViewerCandidate&)>{};
            feature_reference_end_=[child]{child->end_input();};std::vector<kernel::ViewerEdge> wire;
            if(active)for(const auto& entry:*meshes)for(auto edge:entry.second.edges){edge.overlay=true;edge.color="#FFFFFF";wire.push_back(std::move(edge));}
            for(const auto& id:child->inspections())if(meshes->contains(id))for(auto edge:meshes->at(id).edges){edge.overlay=true;edge.color="#00D1FF";wire.push_back(std::move(edge));}
            viewer_->set_transient_edges(std::move(wire));
        };
        connect(child,&QDialog::finished,dialog,[this,base]{feature_reference_pick_={};feature_reference_end_={};viewer_->set_transient_edges({});viewer_->set_mesh(*base);viewer_->set_original_container_selection(false);tree_->setProperty("commandSelectionActive",false);});
        open_child(child);child->changed();
    };
    dialog->edit_clampings=[this,dialog,open_child,edit,base]{
        const auto* part=workspace_.open_part(edit.document_id);if(!part)return;const auto& doc=part->session.document();
        const auto candidates=workspace::cam_clamping_candidates(part->session,edit);
        std::map<std::string,QString> labels;auto meshes=std::make_shared<std::map<std::string,kernel::ViewerMesh>>();
        auto frames=std::make_shared<std::map<std::string,kernel::ProjectionRequest>>();auto offered=std::make_shared<kernel::ViewerMesh>(*base);
        for(const auto& id:candidates){const auto& sketch=*std::ranges::find(doc.sketches,id,&sketcher::Sketch::id);const auto* body=doc.body_owner_for_object(id);
            labels[id]=QString::fromStdString(body->name+" / "+sketch.name);auto mesh=doc.place_body_mesh(sketch.viewer_mesh(false),body->scope.id);
            frames->emplace(id,document::cam_sketch_request(sketch,body->scope.placement,{},false));
            for(auto edge:mesh.edges){edge.reference.owner_id=id;edge.display_owner_id=id;offered->original_references.edges.push_back(std::move(edge));}
            meshes->emplace(id,std::move(mesh));}
        auto* child=new CamClampingDialog(*dialog->pending_value().cam,std::move(labels),[dialog](auto value){dialog->set_clampings(std::move(value));},this);
        auto armed=std::make_shared<std::optional<bool>>();
        child->changed=[this,child,base,offered,meshes,frames,armed]{
            const bool active=child->active_input();if(!armed->has_value()||**armed!=active){viewer_->set_mesh(active?*offered:*base);*armed=active;}
            viewer_->set_original_container_selection(active);viewer_->set_selection_contract(active?std::vector{viewer::CandidateKind::Container}:std::vector<viewer::CandidateKind>{});
            viewer_->set_candidate_filter([child](const auto& c){return c.instance_path.empty()&&child->accepts_source(c.owner_id);});tree_->setProperty("commandSelectionActive",active);
            feature_reference_pick_=active?std::function<void(const viewer::ViewerCandidate&)>([child](const auto& c){child->set_source(c.owner_id);}):std::function<void(const viewer::ViewerCandidate&)>{};
            feature_reference_end_=[child]{child->end_input();};
            std::vector<kernel::ViewerEdge> wire;
            if(active)for(const auto& entry:*meshes)for(auto edge:entry.second.edges){edge.overlay=true;edge.color="#FFFFFF";wire.push_back(std::move(edge));}
            for(const auto& [id,positive]:child->inspections())if(meshes->contains(id)){
                const auto& mesh=meshes->at(id);for(auto edge:mesh.edges){edge.overlay=true;edge.color="#00D1FF";wire.push_back(std::move(edge));}
                const auto& frame=frames->at(id);auto n=frame.sketch_normal;if(!positive)n={-n.x,-n.y,-n.z};
                const auto p=frame.sketch_origin;const auto length=std::max(1.,document::viewer_mesh_bounds_diagonal(mesh)*.25);
                const kernel::Vec3 end{p.x+n.x*length,p.y+n.y*length,p.z+n.z*length};
                kernel::Vec3 side=std::abs(n.z)<.9?kernel::Vec3{-n.y,n.x,0}:kernel::Vec3{0,-n.z,n.y};
                const auto magnitude=std::sqrt(side.x*side.x+side.y*side.y+side.z*side.z);side={side.x/magnitude,side.y/magnitude,side.z/magnitude};
                kernel::ViewerEdge arrow;arrow.overlay=true;arrow.color="#00D1FF";arrow.points={p,end};wire.push_back(arrow);
                for(double sign:{-1.,1.}){arrow.points={end,{end.x-.2*length*n.x+sign*.1*length*side.x,end.y-.2*length*n.y+sign*.1*length*side.y,end.z-.2*length*n.z+sign*.1*length*side.z}};wire.push_back(arrow);}
            }
            viewer_->set_transient_edges(std::move(wire));
        };
        connect(child,&QDialog::finished,dialog,[this,base]{feature_reference_pick_={};feature_reference_end_={};viewer_->set_transient_edges({});viewer_->set_mesh(*base);viewer_->set_original_container_selection(false);tree_->setProperty("commandSelectionActive",false);});
        open_child(child);child->changed();
    };
    connect(dialog,&QDialog::finished,this,[this]{
        set_local_origin_selection_mode(false);
        properties_dialog_=nullptr;primitive_reference_dialog_=nullptr;primitive_reference_geometry_={};primitive_parameter_owner_id_.clear();construction_dimension_object_id_.clear();body_dialog_step_id_.clear();cam_setup_input_.reset();cam_setup_preview_.reset();
        pending_primitive_reference_index_.reset();primitive_reference_auto_advance_=false;feature_reference_pick_={};feature_reference_end_={};
        viewer_->set_transient_context_mesh({});viewer_->set_transient_edges({});viewer_->set_constraint_reference_highlights({},{});viewer_->set_original_container_selection(false);
        viewer_->set_candidate_filter({});viewer_->set_selection_contract({});viewer_->clear_selection();tree_->setProperty("commandSelectionActive",false);
        preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
    });
    bind_local_origin_selection(dialog);dialog->show();dialog->changed();if(id.empty())dialog->arm(0);
}
bool AssemblyWorkspaceWindow::accept_cam_tree_reference(QTreeWidgetItem* item){
    if(auto* dialog=dynamic_cast<CamOperationsDialog*>(properties_dialog_);item&&dialog&&dialog->active_input()){
        if(item->data(0,Qt::UserRole+3).toString()!="part-sketch")return false;
        return dialog->set_source(item->data(0,Qt::UserRole).toString().toStdString());
    }
    if(auto* dialog=dynamic_cast<CamClampingDialog*>(properties_dialog_);item&&dialog&&dialog->active_input()){
        if(item->data(0,Qt::UserRole+3).toString()!="part-sketch")return false;
        return dialog->set_source(item->data(0,Qt::UserRole).toString().toStdString());
    }
    auto* dialog=dynamic_cast<CamBodyDialog*>(properties_dialog_);if(!item||!dialog||!dialog->active_input()||!feature_reference_pick_)return false;
    const auto role=item->data(0,Qt::UserRole+3).toString();if(role!="part-body"&&role!="part-body-boolean")return false;
    viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Container;candidate.owner_id=item->data(0,Qt::UserRole).toString().toStdString();
    const auto pick=feature_reference_pick_;pick(candidate);return true;
}
void AssemblyWorkspaceWindow::calculate_cam_body(){
    if(properties_dialog_||!active_cam_body()||workspace_.active_document_id()!=workspace_.displayed_document_id())return;
    try{
        const auto document_id=workspace_.active_document_id();const auto* part=workspace_.open_part(document_id);
        const auto body_id=part->session.document().body_history.active_body_id();
        const auto state=workspace::cam_result_state(part->session,body_id);
        if(state==workspace::CamResultState::Current||state==workspace::CamResultState::Collision){show_cam_nc();return;}
        auto input=std::make_shared<const workspace::CamCalculationInput>(workspace::prepare_cam_calculation(workspace_,document_id,body_id));
        auto published=std::make_shared<bool>(false);
        auto* dialog=new CamCalculationDialog(*input,[this,input,published](auto result){
            if(!workspace::publish_cam_calculation(workspace_,*input,std::move(result)))throw std::runtime_error("CAM inputs changed. Repeat the calculation.");*published=true;
        },this);keep_cam_document(tabs_,dialog);properties_dialog_=dialog;rebuild_application_toolbar();
        connect(dialog,&QDialog::finished,this,[this,input,published]{properties_dialog_=nullptr;refresh_tabs();rebuild_application_toolbar();
            const auto* part=workspace_.open_part(input->document_id);
            if(*published&&workspace_.active_document_id()==input->document_id&&part&&part->session.document().body_history.active_body_id()==input->body_id)show_cam_nc();});
        dialog->show();
    }catch(const std::exception& e){state_->setText(tr(e.what()));}
}
void AssemblyWorkspaceWindow::show_cam_nc(){
    if(properties_dialog_||!active_cam_body()||workspace_.active_document_id()!=workspace_.displayed_document_id())return;
    try{
        const auto document_id=workspace_.active_document_id();const auto* part=workspace_.open_part(document_id);const auto& doc=part->session.document();
        const auto body_id=doc.body_history.active_body_id();const auto state=workspace::cam_result_state(part->session,body_id);
        if(state!=workspace::CamResultState::Current&&state!=workspace::CamResultState::Collision){state_->setText(tr("Calculate CAM before inspecting the NC program."));return;}
        const auto result=doc.body_history.find(body_id)->cam->calculated;
        auto input=std::make_shared<kernel::ViewerMesh>(cam_display_mesh());auto remaining=std::make_shared<kernel::ViewerMesh>(result->remaining->mesh);
        const auto& definition=*doc.body_history.find(body_id)->cam;
        auto context=definition.fixture_ids;context.push_back(definition.target_id);
        for(const auto& id:context)if(const auto* material=workspace::calculated_material_source(part->session,id))append_mesh(*remaining,(*material)->mesh);
        auto carrier=std::make_shared<document::PartDocument>(document::PartDocument::create_default());document::BodyHistoryGraph graph;
        const auto frame_id=graph.create_body("CAM presentation frame");auto frame=*graph.find(frame_id);frame.scope.placement=result->work_frame;graph.update_body(std::move(frame));carrier->set_body_history(std::move(graph));
        auto shown=std::make_shared<std::optional<bool>>();
        std::map<std::string,QString> clamping_names;for(const auto& c:definition.clampings)clamping_names[c.id]=QString::fromStdString(c.name);
        auto* dialog=new CamNcDialog(result,[this,carrier,frame_id,input,remaining,shown](auto edges,bool show_remaining){
            if(!shown->has_value()||**shown!=show_remaining){viewer_->set_mesh(show_remaining?*remaining:*input);*shown=show_remaining;update_viewer_body_colors();}
            kernel::ViewerMesh overlay;overlay.edges=std::move(edges);viewer_->set_transient_edges(carrier->place_body_mesh(std::move(overlay),frame_id).edges);
        },this,std::move(clamping_names));keep_cam_document(tabs_,dialog);properties_dialog_=dialog;rebuild_application_toolbar();
        dialog->export_program=[this,dialog](const std::string& text){
            const auto path=save_file(dialog,tr("Export generic NC text…"),QString::fromStdString(document::path_to_utf8(working_directory_/"program.nc.txt")),"*.txt","txt",application_settings_.qt_translations);if(path.isEmpty())return;
            QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(text.data(),qint64(text.size()))!=qint64(text.size())||!file.commit())
                dialog->show_submission_error(tr("The generic NC text could not be saved."));
        };
        connect(dialog,&QDialog::finished,this,[this]{properties_dialog_=nullptr;viewer_->set_transient_edges({});preserve_view_on_refresh_=true;refresh_scene();rebuild_application_toolbar();});dialog->show();
    }catch(const std::exception& e){state_->setText(tr(e.what()));}
}
} // namespace zima::app
