#include "assembly_workspace_window.hpp"
#include "render_body_dialog.hpp"
#include "render_object_dialog.hpp"
#include "workspace/workspace_internal.hpp"
#include <zima/workspace/render_operations.hpp>
#include <zima/workspace/calculated_body_source.hpp>
#include <zima/document/placement_reference_geometry.hpp>
#include <zima/kernel/stable_id.hpp>
#include <zima/viewer/view_theme.hpp>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QSignalBlocker>
#include <QTreeWidgetItemIterator>
#include <numbers>
#include <limits>

namespace zima::app {
using namespace workspace_detail;
namespace {
using kernel::Vec3;
QVector3D vector(Vec3 p){return {float(p.x),float(p.y),float(p.z)};}
Vec3 vector(QVector3D p){return {p.x(),p.y(),p.z()};}
}
bool AssemblyWorkspaceWindow::active_render_body()const {
    const auto* part=workspace_.open_part(workspace_.active_document_id());
    const auto* body=part?part->session.document().body_history.find(part->session.document().body_history.active_body_id()):nullptr;
    return body&&body->render.has_value();
}
void AssemblyWorkspaceWindow::remember_document_camera() {
    const auto& id=workspace_.active_document_id();
    if(!id.empty()&&viewer_)document_camera_states_[id]={viewer_->camera_state(),viewer_->camera_field_of_view(),viewer_->projection_mode()==viewer::ProjectionMode::Orthographic,viewer_->camera_viewport_anchor()};
}
void AssemblyWorkspaceWindow::restore_document_camera(const std::string& id) {
    const auto found=document_camera_states_.find(id);
    if(found==document_camera_states_.end()||!viewer_)return;
    const auto& camera=found->second;
    viewer_->set_projection_mode(camera.orthographic?viewer::ProjectionMode::Orthographic:viewer::ProjectionMode::Perspective);
    viewer_->set_camera_state(camera.pose,camera.field_of_view);
    viewer_->set_camera_viewport_anchor(camera.viewport_anchor);
    orthographic_camera_action_->setChecked(camera.orthographic);
    perspective_camera_action_->setChecked(!camera.orthographic);
}
const document::RenderObject* AssemblyWorkspaceWindow::active_render_object(const std::string& id)const {
    const auto* part=workspace_.open_part(workspace_.active_document_id());
    return part?workspace::find_render_object(part->session.document(),part->session.document().body_history.active_body_id(),id):nullptr;
}
document::RenderObject AssemblyWorkspaceWindow::render_camera_from_view()const {
    document::RenderObject camera;camera.id=kernel::make_stable_id();camera.name=tr("Camera").toStdString()+" 1";
    camera.orthographic=viewer_->projection_mode()==viewer::ProjectionMode::Orthographic;
    const double w=viewer_->width(),h=viewer_->height();
    const auto center=viewer_->ray_at({w/2,h/2}),top=viewer_->ray_at({w/2,0}),bottom=viewer_->ray_at({w/2,h});
    if(!center||!top||!bottom)throw std::runtime_error("The current View camera is unavailable.");
    auto origin=vector(center->first),forward=vector(center->second).normalized();
    const auto state=viewer_->camera_state();
    auto up=QQuaternion(state[0],state[1],state[2],state[3]).conjugated().rotatedVector({0,1,0});
    up=(up-forward*QVector3D::dotProduct(up,forward)).normalized();
    if(camera.orthographic) {
        camera.orthographic_height_mm=(vector(top->first)-vector(bottom->first)).length();
        // Orthographic picker origins lie on the distant clipping plane.
        // Move along the same rays to just before the material; framing is
        // unchanged and the physical camera marker stays near its scene.
        double nearest=std::numeric_limits<double>::infinity();
        for(const auto& point:viewer_->mesh().vertices)
            nearest=std::min(nearest,double(QVector3D::dotProduct(vector(point)-origin,forward)));
        if(std::isfinite(nearest))origin+=forward*float(nearest-camera.orthographic_height_mm*.25);
    }
    else {
        // Intersect two picker rays behind the near plane to recover the eye.
        // This also follows fly navigation and the View's resize/pan anchoring.
        const auto direction=vector(top->second),delta=vector(top->first)-origin;
        const double cosine=QVector3D::dotProduct(forward,direction),denominator=1-cosine*cosine;
        if(denominator<1e-10)throw std::runtime_error("The current View camera is unavailable.");
        const double t=(QVector3D::dotProduct(delta,forward)-cosine*QVector3D::dotProduct(delta,direction))/denominator;
        origin+=forward*float(t);
        const double angle=std::acos(std::clamp(double(QVector3D::dotProduct(direction,vector(bottom->second))),-1.,1.));
        camera.field_of_view_degrees=angle*180/std::numbers::pi;
    }
    // Camera local +X is forward, +Z is up and -Y is image right.
    const auto rotation=euler_degrees_from_frame_columns(vector(forward),vector(QVector3D::crossProduct(up,forward)),vector(up));
    camera.placement.x=origin.x();camera.placement.y=origin.y();camera.placement.z=origin.z();
    camera.placement.rotation_x=rotation.x;camera.placement.rotation_y=rotation.y;camera.placement.rotation_z=rotation.z;
    camera.placement.absolute_rotation_x=rotation.x;camera.placement.absolute_rotation_y=rotation.y;camera.placement.absolute_rotation_z=rotation.z;
    return camera;
}
void AssemblyWorkspaceWindow::view_from_render_camera(const document::RenderObject& object) {
    if(object.kind!=document::RenderObjectKind::Camera)return;
    const auto& p=object.placement;QMatrix4x4 rotation;
    rotation.rotate(float(p.rotation_z),0,0,1);rotation.rotate(float(p.rotation_y),0,1,0);rotation.rotate(float(p.rotation_x),1,0,0);
    auto eye=Vec3{p.x,p.y,p.z},forward=vector(rotation.mapVector({1,0,0})),up=vector(rotation.mapVector({0,0,1}));
    const auto prefix=workspace_.active_occurrence_path();if(!prefix.empty()) {
        const auto transform=[&](Vec3 v){return workspace_.occurrence_point_to_scene(workspace_.displayed_document_id(),assembly::InstancePath::decode(prefix),v);};
        const auto f=transform({eye.x+forward.x,eye.y+forward.y,eye.z+forward.z}),u=transform({eye.x+up.x,eye.y+up.y,eye.z+up.z});eye=transform(eye);
        forward={f.x-eye.x,f.y-eye.y,f.z-eye.z};up={u.x-eye.x,u.y-eye.y,u.z-eye.z};
    }
    viewer_->set_camera_frame(eye,forward,up,object.orthographic?viewer::ProjectionMode::Orthographic:viewer::ProjectionMode::Perspective,
        object.orthographic_height_mm,object.field_of_view_degrees);
    orthographic_camera_action_->setChecked(object.orthographic);perspective_camera_action_->setChecked(!object.orthographic);fly_camera_action_->setChecked(false);
}
kernel::ViewerMesh AssemblyWorkspaceWindow::render_object_wire(const document::RenderObject& object)const {
    kernel::ViewerMesh mesh;if(!object.visible)return mesh;
    const auto& p=object.placement;
    QMatrix4x4 matrix;matrix.translate(float(p.x),float(p.y),float(p.z));
    matrix.rotate(float(p.rotation_z),0,0,1);matrix.rotate(float(p.rotation_y),0,1,0);matrix.rotate(float(p.rotation_x),1,0,0);
    const auto stroke=[&](std::vector<Vec3> points) {
        kernel::ViewerEdge edge;edge.reference={object.id,"render:marker:"+std::to_string(mesh.edges.size()),{}};
        edge.display_owner_id=object.id;
        edge.color=!p.reference_valid?"#FF5E52":"#F5CD50";
        for(const auto point:points)edge.points.push_back(vector(matrix.map(vector(point))));mesh.edges.push_back(std::move(edge));
    };
    constexpr double size=12;
    if(object.kind==document::RenderObjectKind::Camera) {
        const double half=object.orthographic?size*.45:size*std::tan(object.field_of_view_degrees*std::numbers::pi/360);
        const double aspect=16./9;
        stroke({{size,-half*aspect,-half},{size,half*aspect,-half},{size,half*aspect,half},{size,-half*aspect,half},{size,-half*aspect,-half}});
        for(const double y:{-half*aspect,half*aspect})for(const double z:{-half,half})stroke({{0,object.orthographic?y:0,object.orthographic?z:0},{size,y,z}});
        stroke({{size,-half*.4,half},{size,0,half*1.4},{size,half*.4,half}});
    } else {
        for(int plane=0;plane<3;++plane){std::vector<Vec3> circle;for(int i=0;i<=32;++i){const double a=i*2*std::numbers::pi/32;
            const double c=std::cos(a)*size*.25,s=std::sin(a)*size*.25;circle.push_back(plane==0?Vec3{0,c,s}:plane==1?Vec3{c,0,s}:Vec3{c,s,0});}stroke(std::move(circle));}
        if(object.kind==document::RenderObjectKind::Sun){stroke({{0,0,0},{size,0,0},{size*.75,size*.12,0}});stroke({{size,0,0},{size*.75,-size*.12,0}});}
    }
    mesh.constraint_markers.push_back({{p.x,p.y,p.z},object.name+(p.reference_valid?"":" !"),{object.id,"render:label",{}},{}});
    mesh.original_references.edges=mesh.edges;return mesh;
}
kernel::ViewerMesh AssemblyWorkspaceWindow::render_display_mesh()const {
    const auto* part=workspace_.open_part(workspace_.active_document_id());if(!part)return {};
    const auto& doc=part->session.document();const auto id=doc.body_history.active_body_id();
    const auto* body=doc.body_history.find(id);if(!body||!body->render)return {};
    kernel::ViewerMesh mesh;
    try {for(const auto& [source,snapshot]:workspace::read_render_sources(part->session,id))append_mesh(mesh,snapshot->mesh);}
    catch(const std::exception&){mesh=part->session.body_context_mesh();}
    for(const auto& object:body->render->objects) {
        if(render_object_preview_&&render_object_preview_->id==object.id)continue;
        append_mesh(mesh,render_object_wire(object));
        if(construction_dimension_object_id_==object.id&&!properties_dialog_)for(int axis=0;axis<3;++axis) {
            kernel::ViewerDimension d;d.label_only=true;d.reference={object.id,"render:parameter:"+std::to_string(axis),{}};
            const auto& p=object.placement;d.value=axis==0?p.x:axis==1?p.y:p.z;d.label_prefix=std::string(1,char('X'+axis))+" = ";
            d.witness_first=d.witness_second=d.line_first=d.line_second={p.x,p.y,p.z};d.label_position=Vec3{p.x,p.y+5*(axis+1),p.z};mesh.dimensions.push_back(std::move(d));
        }
    }
    if(render_object_preview_) {
        append_mesh(mesh,doc.origin_viewer_mesh());
        auto references=workspace::render_placement_geometry(part->session,id);
        kernel::ViewerMesh frame;frame.original_references=std::move(references);
        append_mesh(frame,local_origin_display_mesh(frame.original_references,visible_local_origin_ids_));append_mesh(mesh,std::move(frame));
    }
    return mesh;
}
void AssemblyWorkspaceWindow::select_render_object(std::string id,bool inspect) {
    if(!active_render_object(id)||properties_dialog_)return;
    if(!inspect&&!construction_dimension_object_id_.empty()&&construction_dimension_object_id_!=id) {
        construction_dimension_object_id_.clear();preserve_view_on_refresh_=true;refresh_scene();
    }
    if(inspect){construction_dimension_object_id_=id;preserve_view_on_refresh_=true;refresh_scene();}
    const QSignalBlocker blocked(tree_);
    for(QTreeWidgetItemIterator it(tree_);*it;++it)if((*it)->data(0,Qt::UserRole+3)=="render-object"&&(*it)->data(0,Qt::UserRole).toString().toStdString()==id) {
        tree_->clearSelection();tree_->setCurrentItem(*it);(*it)->setSelected(true);break;
    }
    viewer_->confirm_reference(id,{},workspace_.active_occurrence_path(),viewer::CandidateKind::Container);
}
void AssemblyWorkspaceWindow::remove_render_object(const std::string& id) {
    if(properties_dialog_||!active_render_object(id))return;
    const auto* part=workspace_.open_part(workspace_.active_document_id());const auto& doc=part->session.document();
    try{static_cast<void>(workspace::erase_render_object(workspace_,workspace::prepare_render_edit(doc,doc.body_history.active_body_id()),id));
        if(construction_dimension_object_id_==id)construction_dimension_object_id_.clear();preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();}
    catch(const std::exception& e){state_->setText(tr(e.what()));}
}

void AssemblyWorkspaceWindow::show_render_body_properties(const std::string& id) {
    if(properties_dialog_){properties_dialog_->raise();return;}
    const auto* part=workspace_.open_part(workspace_.active_document_id());if(!part)return;
    const auto& graph=part->session.document().body_history;
    if(id.empty()&&!graph.active_body_id().empty())return;
    workspace::BodyGraphEdit edit;std::vector<std::string> candidates;
    try{edit=workspace::prepare_render_edit(part->session.document(),id);candidates=workspace::render_source_candidates(part->session,edit);}
    catch(const std::exception& e){state_->setText(tr(e.what()));return;}
    if(id.empty()&&candidates.empty()){state_->setText(tr("Render requires valid calculated Bodies. Regenerate the sources first."));return;}
    std::map<std::string,kernel::BodySnapshot> sources;std::map<std::string,QString> labels;std::set<std::string> available;
    for(const auto& source:candidates) {
        sources.emplace(source,*workspace::calculated_material_source(part->session,source));available.insert(source);
        const auto* body=graph.find(source);const auto* operation=graph.find_boolean(source);
        labels.emplace(source,QString::fromStdString(body?body->name:operation->name));
    }
    document::BodyHistory initial;
    if(!id.empty())initial=*graph.find(id);
    else {
        initial.scope.id=edit.object_id;initial.render.emplace();
        for(unsigned n=1;;++n){initial.name=tr("Render Body").toStdString()+" "+std::to_string(n);
            if(std::ranges::none_of(graph.bodies(),[&](const auto& b){return b.name==initial.name;}))break;}
        try{auto camera=render_camera_from_view();initial.render->active_camera_id=camera.id;initial.render->objects.push_back(camera);
            document::RenderObject sun;sun.id=kernel::make_stable_id();sun.kind=document::RenderObjectKind::Sun;sun.name=tr("Sun").toStdString()+" 1";
            sun.placement=camera.placement;sun.placement.rotation_y+=25;sun.placement.absolute_rotation_y+=25;
            sun.intensity=1200;sun.angular_radius_degrees=2;initial.render->objects.push_back(sun);
        }catch(const std::exception& e){state_->setText(tr(e.what()));return;}
        const auto theme=viewer::view_theme(viewer_->palette());
        initial.render->settings.background_top={theme.top.redF(),theme.top.greenF(),theme.top.blueF()};
        initial.render->settings.background_bottom={theme.bottom.redF(),theme.bottom.greenF(),theme.bottom.blueF()};
    }
    const auto boundary=id.empty()?graph.insertion_cursor():static_cast<std::size_t>(std::ranges::find(graph.order(),id)-graph.order().begin());
    std::vector<std::string> context;for(const auto& source:graph.available_before(boundary))if(sources.contains(source)) {
        const auto* b=graph.find(source);if(b?b->visible:graph.find_boolean(source)->visible)context.push_back(source);
    }
    auto* dialog=new RenderBodyDialog(initial,[this,edit](auto value){static_cast<void>(workspace::commit_render(workspace_,edit,std::move(value)));},this);
    dialog->set_sources(std::move(labels),std::move(available));
    properties_dialog_=dialog;body_dialog_context_=context;body_dialog_step_id_=initial.scope.id;
    const auto prefix=workspace_.active_occurrence_path();properties_dialog_instance_path_=prefix;
    preserve_view_on_refresh_=true;
    if(prefix.empty())refresh_scene();else refresh_scene([&]{kernel::BodyResult input;for(const auto& source:context)append_mesh(input.mesh,sources.at(source)->mesh);
        return workspace_.build_scene_with_part_override(workspace_.displayed_document_id(),assembly::InstancePath::decode(prefix),std::move(input));});
    auto base=std::make_shared<kernel::ViewerMesh>(viewer_->mesh()),offered=std::make_shared<kernel::ViewerMesh>(*base);
    const auto transform=[this,prefix](auto p){return prefix.empty()?p:workspace_.occurrence_point_to_scene(workspace_.displayed_document_id(),assembly::InstancePath::decode(prefix),p);};
    for(const auto& [source,snapshot]:sources) {
        const auto& mesh=snapshot->mesh;const auto offset=std::uint32_t(offered->original_references.vertices.size());
        for(auto p:mesh.vertices)offered->original_references.vertices.push_back(transform(p));
        for(auto i:mesh.triangles)offered->original_references.triangles.push_back(offset+i);
        offered->original_references.triangle_references.insert(offered->original_references.triangle_references.end(),mesh.triangles.size()/3,kernel::FaceReference{source,"container:display",prefix});
        for(auto e:mesh.edges){e.reference={source,"container:display",prefix};e.display_owner_id=source;
            for(auto& p:e.points)p=transform(p);if(e.exact_spline)for(auto& p:e.exact_spline->poles)p=transform(p);
            if(e.annotation)kernel::transform_annotation(*e.annotation,transform);offered->original_references.edges.push_back(std::move(e));}
    }
    auto offering=std::make_shared<std::optional<bool>>();
    dialog->changed=[this,dialog,sources,prefix,base,offered,offering,transform] {
        const bool active=dialog->active_input();if(!*offering||**offering!=active){viewer_->set_mesh(active?*offered:*base);*offering=active;}
        const auto accepts=[sources,prefix](const viewer::ViewerCandidate& c){return c.kind==viewer::CandidateKind::Container&&c.instance_path==prefix&&sources.contains(c.owner_id);};
        viewer_->set_original_container_selection(active);viewer_->set_selection_contract(active?std::vector{viewer::CandidateKind::Container}:std::vector<viewer::CandidateKind>{});
        viewer_->set_candidate_filter([active,accepts](const auto& c){return active&&accepts(c);});tree_->setProperty("commandSelectionActive",active);
        feature_reference_pick_={};if(active)feature_reference_pick_=[this,dialog,accepts](const auto& c){if(accepts(c)){const auto id=c.owner_id;viewer_->clear_selection();tree_->clearSelection();feature_reference_pick_={};dialog->set_source(id);}};
        std::vector<kernel::ViewerEdge> inspection;
        for(const auto& id:dialog->inspected_sources())if(const auto found=sources.find(id);found!=sources.end())for(auto edge:found->second->mesh.edges) {
            edge.overlay=true;edge.color="#00D1FF";for(auto& p:edge.points)p=transform(p);
            if(edge.exact_spline)for(auto& p:edge.exact_spline->poles)p=transform(p);if(edge.annotation)kernel::transform_annotation(*edge.annotation,transform);inspection.push_back(std::move(edge));
        }
        viewer_->set_transient_edges(std::move(inspection));
        feature_reference_end_=[this,dialog]{feature_reference_pick_={};feature_reference_end_={};dialog->end_input();};
    };
    connect(dialog,&QDialog::finished,this,[this] {
        properties_dialog_=nullptr;properties_dialog_instance_path_.clear();body_dialog_context_.reset();body_dialog_step_id_.clear();
        feature_reference_pick_={};feature_reference_end_={};viewer_->set_transient_edges({});viewer_->set_original_container_selection(false);
        viewer_->set_candidate_filter({});viewer_->set_selection_contract({});viewer_->clear_selection();tree_->setProperty("commandSelectionActive",false);
        preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
    });dialog->show();dialog->changed();
}
bool AssemblyWorkspaceWindow::accept_render_tree_reference(QTreeWidgetItem* item) {
    auto* dialog=dynamic_cast<RenderBodyDialog*>(properties_dialog_);if(!item||!dialog||!dialog->active_input()||!feature_reference_pick_)return false;
    const auto role=item->data(0,Qt::UserRole+3).toString();if(role!="part-body"&&role!="part-body-boolean")return false;
    viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Container;candidate.owner_id=item->data(0,Qt::UserRole).toString().toStdString();candidate.instance_path=workspace_.active_occurrence_path();
    const auto pick=feature_reference_pick_;pick(candidate);return true;
}
void AssemblyWorkspaceWindow::show_render_object_properties(document::RenderObjectKind kind,const std::string& id) {
    if(properties_dialog_||!active_render_body())return;
    const auto* part=workspace_.open_part(workspace_.active_document_id());const auto& doc=part->session.document();const auto body_id=doc.body_history.active_body_id();
    workspace::BodyGraphEdit edit;
    try{edit=workspace::prepare_render_edit(doc,body_id);primitive_reference_geometry_=workspace::render_placement_geometry(part->session,body_id);}
    catch(const std::exception& e){state_->setText(tr(e.what()));return;}
    document::RenderObject initial;
    if(!id.empty()){const auto* found=active_render_object(id);if(!found)return;initial=*found;}
    else {
        if(kind==document::RenderObjectKind::Camera){try{initial=render_camera_from_view();}catch(const std::exception& e){state_->setText(tr(e.what()));return;}}
        initial.id=kernel::make_stable_id();initial.kind=kind;
        for(unsigned n=1;;++n){initial.name=render_object_name(kind).toStdString()+" "+std::to_string(n);
            if(std::ranges::none_of(doc.body_history.find(body_id)->render->objects,[&](const auto& o){return o.name==initial.name;}))break;}
    }
    auto* dialog=new RenderObjectDialog(initial,[this,edit](auto value){static_cast<void>(workspace::commit_render_object(workspace_,edit,std::move(value)));},this);
    dialog->view_from_camera=[this,dialog]{if(dialog->changed)dialog->changed();if(render_object_preview_)view_from_render_camera(*render_object_preview_);};
    properties_dialog_=dialog;primitive_reference_dialog_=dialog;primitive_parameter_owner_id_=initial.id;construction_dimension_object_id_=initial.id;render_object_preview_=initial;
    const auto highlights=[this,dialog]{viewer_->set_constraint_reference_highlights({},highlighted_reference_edge_keys(*dialog));};
    dialog->set_reference_highlights_changed_callback(highlights);
    auto updating=std::make_shared<bool>(false);
    auto carrier=std::make_shared<document::PartDocument>(document::PartDocument::create_default());carrier->document_id=initial.id;
    document::BodyHistoryGraph frame_graph;const auto frame_id=frame_graph.create_body("frame");carrier->set_body_history(frame_graph);const auto origin=carrier->origin_viewer_mesh();
    dialog->changed=[this,dialog,updating,highlights,carrier,frame_id,origin] {
        if(*updating)return;const QScopedValueRollback guard(*updating,true);
        auto value=dialog->pending_value();auto& p=value.placement;Vec3 base;bool oriented=false;
        const auto geometry=document::PlacementReferenceGeometryIndex(primitive_reference_geometry_).select(p.references);
        const bool valid=document::resolve_placement(p,geometry,&base,&oriented);
        const auto constraint=document::point_constraint_state(p.references,geometry,{p.x,p.y,p.z});primitive_translation_dof_=constraint.remaining_dof;
        dialog->set_translation_constraint_state(constraint,{p.x,p.y,p.z});dialog->set_rotation_constraint_state(document::orientation_constraint_state(p.references,geometry,true,{p.x,p.y,p.z}));
        dialog->set_orientation_base_rotation(base,oriented);dialog->set_resolved_rotation({p.rotation_x,p.rotation_y,p.rotation_z},valid);
        render_object_preview_=value;value.visible=true;auto preview=render_object_wire(value);
        auto frame=*carrier->body_history.find(frame_id);frame.scope.placement=p;carrier->body_history.update_body(frame);append_mesh(preview,carrier->place_body_mesh(origin,frame_id));
        viewer_->set_transient_context_mesh(std::move(preview));highlights();
    };
    dialog->set_reference_request_callback([this](std::size_t row){start_primitive_reference_selection(row);});
    connect(dialog,&QDialog::finished,this,[this,dialog] {
        if(properties_dialog_!=dialog)return;
        properties_dialog_=nullptr;primitive_reference_dialog_=nullptr;primitive_reference_geometry_={};primitive_parameter_owner_id_.clear();construction_dimension_object_id_.clear();
        pending_primitive_reference_index_.reset();primitive_reference_auto_advance_=false;render_object_preview_.reset();
        viewer_->set_transient_context_mesh({});viewer_->set_constraint_reference_highlights({},{});viewer_->set_candidate_filter({});viewer_->set_selection_contract({});viewer_->clear_selection();
        tree_->setProperty("commandSelectionActive",false);preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
    });
    bind_local_origin_selection(dialog);preserve_view_on_refresh_=true;refresh_scene();dialog->show();dialog->changed();set_primitive_properties_dimension_selection();
}
} // namespace zima::app
