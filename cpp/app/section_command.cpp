#include <zima/workspace/section_operations.hpp>
#include "assembly_workspace_window.hpp"
#include "section_properties_dialog.hpp"
#include "section_source.hpp"
#include "resource_icon.hpp"
#include "reference_tree_policy.hpp"
#include <zima/viewer/mesh_view.hpp>
#include <QAction>
#include <QLabel>
#include <QMenu>
#include <QTreeWidget>
#include <QSignalBlocker>
#include <QStyle>
#include <QCryptographicHash>
#include <algorithm>
#include <cmath>
namespace zima::app {
namespace {
zima::kernel::ViewerMesh section_sketch_overlay(const zima::kernel::ViewerMesh& sketch,const std::string& owner,bool editing=true) {
    zima::kernel::ViewerMesh result;
    for(auto edge:sketch.edges)if(!edge.construction&&edge.reference.semantic_key.starts_with("segment:")) {
        edge.overlay=true;edge.color=editing?"#FFFFFF":"#AD6E2E";edge.reference={owner,"section:sketch",{}};
        edge.display_owner_id=owner;
        result.edges.push_back(std::move(edge));
    }
    return result;
}
void append(zima::kernel::ViewerReferenceGeometry& a,const zima::kernel::ViewerReferenceGeometry& b){
    const auto base=static_cast<std::uint32_t>(a.vertices.size());a.vertices.insert(a.vertices.end(),b.vertices.begin(),b.vertices.end());for(auto i:b.triangles)a.triangles.push_back(i+base);
    a.triangle_references.insert(a.triangle_references.end(),b.triangle_references.begin(),b.triangle_references.end());a.edges.insert(a.edges.end(),b.edges.begin(),b.edges.end());a.points.insert(a.points.end(),b.points.begin(),b.points.end());a.axes.insert(a.axes.end(),b.axes.begin(),b.axes.end());
}
void append(zima::kernel::ViewerMesh& a,const zima::kernel::ViewerMesh& b){
    const auto base=static_cast<std::uint32_t>(a.vertices.size());a.vertices.insert(a.vertices.end(),b.vertices.begin(),b.vertices.end());for(auto i:b.triangles)a.triangles.push_back(i+base);
    a.triangle_references.insert(a.triangle_references.end(),b.triangle_references.begin(),b.triangle_references.end());a.edges.insert(a.edges.end(),b.edges.begin(),b.edges.end());a.points.insert(a.points.end(),b.points.begin(),b.points.end());a.axes.insert(a.axes.end(),b.axes.begin(),b.axes.end());a.constraint_markers.insert(a.constraint_markers.end(),b.constraint_markers.begin(),b.constraint_markers.end());a.dimensions.insert(a.dimensions.end(),b.dimensions.begin(),b.dimensions.end());append(a.original_references,b.original_references);
}

}
const AssemblyWorkspaceWindow::SectionPreviewCache& AssemblyWorkspaceWindow::cached_section_preview(
    const zima::kernel::ViewerMesh& source,const zima::document::SectionDefinition& section) {
    const auto document=workspace_.displayed_document_id();
    if(section_cache_document_!=document){section_preview_cache_.clear();section_cache_document_=document;}
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const auto scalar=[&](const auto& value){hash.addData(QByteArrayView(reinterpret_cast<const char*>(&value),sizeof(value)));};
    const auto text=[&](const std::string& value){scalar(value.size());hash.addData(QByteArrayView(value.data(),value.size()));};
    const auto point=[&](const auto& value){scalar(value.x);scalar(value.y);scalar(value.z);};
    const auto reference=[&](const auto& ref){
        text(ref.owner_id);text(ref.semantic_key);text(ref.instance_path);
        if constexpr(requires{ref.display_owner_id;})text(ref.display_owner_id);
        if constexpr(requires{ref.surface;}) {
            scalar(ref.surface.get());scalar(ref.measured_area.has_value());if(ref.measured_area)scalar(*ref.measured_area);
            scalar(ref.surface_result);scalar(ref.sheet_role);scalar(ref.sheet_thickness);text(ref.sheet_owner);
        }
    };
    scalar(source.vertices.size());for(const auto& p:source.vertices)point(p);
    scalar(source.triangles.size());for(auto index:source.triangles)scalar(index);
    scalar(source.triangle_references.size());for(const auto& ref:source.triangle_references)reference(ref);
    scalar(source.edges.size());for(const auto& edge:source.edges){
        reference(edge.reference);scalar(edge.points.size());for(const auto& p:edge.points)point(p);
        scalar(edge.construction);scalar(edge.overlay);scalar(edge.infinite);scalar(edge.dash_dot);scalar(edge.parameter_seam);
        text(edge.display_owner_id);text(edge.color);scalar(edge.filled_text);scalar(edge.surface_result);
        scalar(edge.measured_length.has_value());if(edge.measured_length)scalar(*edge.measured_length);
        scalar(edge.edge_treatment_owner_ids.size());for(const auto& owner:edge.edge_treatment_owner_ids)text(owner);
        scalar(edge.edge_treatment_side_directions.size());for(const auto& side:edge.edge_treatment_side_directions){scalar(side.size());for(const auto& p:side)point(p);}
        scalar(edge.edge_treatment_side_references.size());for(const auto& ref:edge.edge_treatment_side_references)reference(ref);
        scalar(edge.edge_treatment_endpoint_references.size());for(const auto& ref:edge.edge_treatment_endpoint_references)reference(ref);
        scalar(edge.exact_spline.has_value());if(edge.exact_spline){const auto& spline=*edge.exact_spline;scalar(spline.degree);
            scalar(spline.poles.size());for(const auto& p:spline.poles)point(p);
            scalar(spline.knots.size());for(auto v:spline.knots)scalar(v);
            scalar(spline.weights.size());for(auto v:spline.weights)scalar(v);}
    }
    scalar(source.points.size());for(const auto& p:source.points){point(p.position);reference(p.reference);text(p.label);scalar(p.always_visible);scalar(p.construction);text(p.sketch_text_key);scalar(p.surface_result);}
    auto geometry=section;geometry.name.clear();geometry.show_cut=false;geometry.show_plane=false;
    geometry.sketch.dimension_layouts.clear();
    text(zima::document::serialize_sections({geometry}));
    const auto key=hash.result().toHex().toStdString();
    const auto existing=section_preview_cache_.find(section.id);
    if(existing!=section_preview_cache_.end()&&existing->second.key==key)return existing->second;
    auto result=zima::document::calculate_section(source,section);
    SectionPreviewCache next;next.key=key;next.plane=zima::document::section_surface_mesh(result,section);
    for(auto& edge:next.plane.edges)edge.reference={section.id,"section:sketch",{}};
    next.clipped=zima::document::section_display_mesh(std::move(result),section);
    if(section_preview_cache_.size()>=8&&!section_preview_cache_.contains(section.id))section_preview_cache_.clear();
    setProperty("sectionCalculationCount",QVariant::fromValue<qulonglong>(++section_calculation_count_));
    return section_preview_cache_.insert_or_assign(section.id,std::move(next)).first->second;
}
void AssemblyWorkspaceWindow::show_section_properties(const std::string& id,bool draw){
    if(properties_dialog_||!active_sketch_id_.empty()||template_sketch()||workspace_.active_document_id()!=workspace_.displayed_document_id())return;
    section_document_id_=workspace_.displayed_document_id();
    if(!workspace_.open_part(section_document_id_)&&!workspace_.open_assembly(section_document_id_))return;
    try{
        const auto edit=workspace::prepare_section_edit(workspace_,section_document_id_,id);
        auto value=edit.initial;
        section_preview_source_=workspace_.authoritative_viewer_mesh(section_document_id_);section_camera_=viewer_->camera_state();
        primitive_reference_geometry_=section_preview_source_.original_references;
        if(auto* p=workspace_.open_part(section_document_id_)){const auto& doc=p->session.document();append(primitive_reference_geometry_,doc.origin_viewer_mesh().original_references);append(primitive_reference_geometry_,doc.construction_viewer_mesh().original_references);append(primitive_reference_geometry_,doc.history_origin_reference_geometry_before(value.id));append(primitive_reference_geometry_,doc.body_origin_reference_geometry());}
        else{const auto& doc=workspace_.open_assembly(section_document_id_)->session.document();append(primitive_reference_geometry_,doc.origin_viewer_mesh().original_references);append(primitive_reference_geometry_,doc.construction_viewer_mesh().original_references);}
        auto* dialog=new SectionPropertiesDialog(this,value,[this,edit](auto next){
            static_cast<void>(workspace::commit_section(workspace_,edit,std::move(next)));return true;
        },[this]{
            pending_primitive_reference_index_.reset();section_dialog_->set_active_reference_index(std::nullopt);section_component_picking_=true;viewer_->clear_selection();viewer_->set_mesh(section_preview_source_,false);
            viewer_->set_selection_contract({workspace_.open_assembly(section_document_id_)?zima::viewer::CandidateKind::Occurrence:zima::viewer::CandidateKind::Container});viewer_->set_candidate_filter({});state_->setText(tr("Vyberte díl nebo těleso ve View; jeho řádek se označí v seznamu."));
        });
        dialog->set_initial_size(QSize(660,900));section_dialog_=dialog;properties_dialog_=dialog;primitive_reference_dialog_=dialog;primitive_parameter_owner_id_=value.id;properties_dialog_instance_path_.clear();
        bind_local_origin_selection(dialog);
        dialog->request_placement=[this](std::size_t index){section_component_picking_=false;start_primitive_reference_selection(index);};
        dialog->edit_sketch=[this](unsigned){begin_section_sketch();};dialog->changed=[this]{preview_section();};
        connect(dialog->findChild<QCheckBox*>("sectionShowPlane"),&QCheckBox::toggled,this,[this]{
            static_cast<void>(finish_active_reference_selection());set_local_origin_selection_mode(false);preview_section();
        });
        connect(dialog,&QDialog::finished,this,[this,dialog]{
            section_dialog_.clear();if(properties_dialog_==dialog)properties_dialog_=nullptr;
            tree_->setProperty("commandSelectionActive",false);
            section_component_picking_=false;section_document_id_.clear();section_preview_source_={};section_sketch_undo_.clear();section_sketch_redo_.clear();
            feature_reference_pick_={};feature_reference_end_={};pending_primitive_reference_index_.reset();primitive_reference_auto_advance_=false;
            local_origin_selection_dialog_=nullptr;local_origin_selection_active_=false;visible_local_origin_ids_.clear();visible_occurrence_origin_paths_.clear();selectable_local_origin_container_ids_.clear();suspended_primitive_reference_index_.reset();suspended_construction_reference_index_.reset();
            primitive_reference_dialog_=nullptr;primitive_reference_geometry_={};primitive_origin_preview_mesh_.reset();parameter_dimension_preview_.reset();construction_dimension_object_id_.clear();primitive_parameter_owner_id_.clear();
            viewer_->set_constraint_reference_highlights({},{});viewer_->set_feature_preview_owners({});viewer_->set_transient_edges({});viewer_->set_transient_labels({});viewer_->set_transient_points({});viewer_->set_operation_direction_indicator({});
            viewer_->set_camera_state(section_camera_);preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
        });
        construction_dimension_object_id_.clear();
        tree_->clearSelection();viewer_->set_feature_selected_edges({});
        viewer_->set_constraint_reference_highlights({},{});
        viewer_->clear_selection();viewer_->set_selection_contract({});dialog->show();preview_section();
        if(!value.show_plane&&dialog->first_empty_position_index()<3)
            start_primitive_reference_selection(dialog->first_empty_position_index(),true);
        // Creation always starts with placement. The separate edit-sketch tree
        // action may enter the sketch directly for an existing section.
        if(draw&&!id.empty())begin_section_sketch();
    }catch(const std::exception& e){section_document_id_.clear();state_->setText(QString::fromUtf8(e.what()));}
}
void AssemblyWorkspaceWindow::begin_section_sketch(){
    if(!section_dialog_||!active_sketch_id_.empty())return;auto* dialog=section_dialog_.data();
    if(!dialog->resolve_pending_placement(primitive_reference_geometry_)){dialog->set_error(tr("Doplňte platné reference umístění řezu."));return;}
    sweep_profile_sketch_draft_=dialog->values().sketch;section_sketch_undo_.clear();section_sketch_redo_.clear();
    embedded_sketch_finished_=[this,dialog](auto sketch){
        properties_dialog_=dialog;primitive_reference_dialog_=dialog;dialog->set_sketch(0,sketch);bind_local_origin_selection(dialog);dialog->show();dialog->raise();
        section_sketch_undo_.clear();section_sketch_redo_.clear();viewer_->set_camera_state(section_camera_);preserve_view_on_refresh_=true;refresh_scene();preview_section();
    };
    feature_reference_pick_={};feature_reference_end_={};pending_primitive_reference_index_.reset();primitive_reference_auto_advance_=false;dialog->set_active_reference_index(std::nullopt);dialog->clear_reference_highlights();section_component_picking_=false;
    set_local_origin_selection_mode(false);local_origin_selection_dialog_=nullptr;primitive_reference_dialog_=nullptr;viewer_->set_constraint_reference_highlights({},{});primitive_origin_preview_mesh_.reset();parameter_dimension_preview_.reset();
    viewer_->set_feature_preview_owners({});viewer_->set_feature_selected_edges({});
    construction_dimension_object_id_.clear();tree_->setProperty("commandSelectionActive",false);
    dialog->hide();properties_dialog_=nullptr;viewer_->set_operation_direction_indicator({});viewer_->set_transient_edges({});viewer_->set_transient_labels({});viewer_->set_transient_points({});
    active_sketch_id_=sweep_profile_sketch_draft_->id;selected_sketch_id_=active_sketch_id_;clear_selected_sketch_geometry();viewer_->clear_selection();tree_->clearSelection();preserve_view_on_refresh_=true;refresh_scene();align_active_sketch_view();
    state_->setText(tr("Nakreslete otevřenou čáru nebo lomenou čáru. Dokončit skicu vrátí vlastnosti řezu."));
}
bool AssemblyWorkspaceWindow::section_sketch_history(bool redo){
    if(!section_dialog_||!sweep_profile_sketch_draft_)return false;
    auto& from=redo?section_sketch_redo_:section_sketch_undo_;auto& to=redo?section_sketch_undo_:section_sketch_redo_;
    cancel_sketch_segment();if(!from.empty()){to.push_back(*sweep_profile_sketch_draft_);sweep_profile_sketch_draft_=std::move(from.back());from.pop_back();clear_selected_sketch_geometry();viewer_->clear_selection();preserve_view_on_refresh_=true;refresh_scene();}return true;
}
void AssemblyWorkspaceWindow::preview_section(){
    if(!section_dialog_||!section_dialog_->isVisible()||!active_sketch_id_.empty()||section_component_picking_)return;
    auto* dialog=section_dialog_.data();const bool placement_valid=dialog->resolve_pending_placement(primitive_reference_geometry_);dialog->refresh_axis_points(primitive_reference_geometry_);const auto s=dialog->values();
    primitive_translation_dof_=zima::document::point_constraint_remaining_dof(s.placement.references,primitive_reference_geometry_);
    zima::document::PartDocument origin_doc;zima::document::ConstructionObject origin;origin.id=s.id;origin.entity_id=s.sketch.id;origin.container_origin=s.container_origin;origin.kind=zima::document::ConstructionKind::Point;
    origin.origin={s.placement.x,s.placement.y,s.placement.z};origin.rotation={s.placement.rotation_x,s.placement.rotation_y,s.placement.rotation_z};origin.reference_valid=false;origin_doc.constructions.push_back(origin);primitive_origin_preview_mesh_=origin_doc.construction_viewer_mesh(s.id);
    auto mesh=section_preview_source_;QString error;
    const auto sketch=sketch_viewer_mesh(s.sketch);
    append(mesh,section_sketch_overlay(sketch,s.id));
    mesh.dimensions.insert(mesh.dimensions.end(),sketch.dimensions.begin(),sketch.dimensions.end());
    if(s.show_plane) {
        try{append(mesh,cached_section_preview(section_preview_source_,s).plane);}
        catch(const std::exception& e){error=QString::fromUtf8(e.what());}
    }
    const bool picking=pending_primitive_reference_index_.has_value()||local_origin_selection_active_;
    if(picking) {
    if(const auto* part=workspace_.open_part(section_document_id_)) {
        append(mesh,active_part_origins(part->session.document()));
        append(mesh,selected_container_origins(part->session.document()));
    }else if(const auto* assembly=workspace_.open_assembly(section_document_id_))append(mesh,assembly->session.document().origin_viewer_mesh());
    append(mesh,*primitive_origin_preview_mesh_); }
    if(!picking)viewer_->set_operation_direction_indicator({});
    viewer_->set_mesh(std::move(mesh),false);viewer_->set_feature_preview_owners(picking?std::set<std::string>{s.container_origin.id}:std::set<std::string>{});std::set<zima::viewer::EdgeKey> highlights;for(const auto& ref:dialog->highlighted_reference_entries()){highlights.insert({ref.owner_id,ref.semantic_key,ref.instance_path});if(ref.semantic_key=="plane")highlights.insert({ref.owner_id,"border",ref.instance_path});}viewer_->set_constraint_reference_highlights({},std::move(highlights));
    if(!pending_primitive_reference_index_&&!local_origin_selection_active_)set_primitive_properties_dimension_selection();dialog->set_error(placement_valid?error:tr("Doplňte platné reference umístění řezu."));
}
bool AssemblyWorkspaceWindow::section_confirmation(const zima::viewer::ViewerCandidate& candidate){
    if(!section_dialog_||!section_component_picking_||!active_sketch_id_.empty())return false;
    const auto s=section_dialog_->values();auto key=candidate.instance_path;if(key.empty()){const auto i=s.body_owners.find(candidate.owner_id);if(i!=s.body_owners.end())key=i->second;}
    section_dialog_->select_component(key);section_component_picking_=false;viewer_->set_selection_contract({});preview_section();return true;
}
void AssemblyWorkspaceWindow::update_section_ui(){
    const auto id=workspace_.displayed_document_id();const bool model=workspace_.open_part(id)||workspace_.open_assembly(id);const bool sketch=section_dialog_&&sweep_profile_sketch_draft_&&!active_sketch_id_.empty();
    section_action_->setEnabled(model&&workspace_.active_document_id()==id&&!properties_dialog_&&active_sketch_id_.empty()&&!template_sketch());
    if(sketch){undo_action_->setEnabled(!section_sketch_undo_.empty());redo_action_->setEnabled(!section_sketch_redo_.empty());return;}
    if(!model||template_sketch()||!active_sketch_id_.empty())return;
    const auto sections=source_sections(&workspace_,id,{});QSignalBlocker block(tree_);auto* root=tree_->topLevelItem(0);if(!root)return;
    auto* group=new QTreeWidgetItem(QStringList{tr("Řezy")});
    group->setData(0,Qt::UserRole+3,"document-sections");
    group->setFlags(group->flags()&~Qt::ItemIsUserCheckable);
    group->setIcon(0,resource_icon("sections"));
    root->insertChild(std::min(1,root->childCount()),group);group->setExpanded(section_tree_expanded_[id]);
    const auto row=[&](const QString& name,const std::string& key,const char* type,bool active){
        auto* item=new QTreeWidgetItem(group,QStringList{name});
        item->setData(0,Qt::UserRole,QString::fromStdString(key));item->setData(0,Qt::UserRole+3,type);
        item->setFlags(item->flags()&~Qt::ItemIsUserCheckable);
        item->setIcon(0,resource_icon(key.empty()?"section-normal":"section-cut"));
        if(active){item->setForeground(0,QColor("#55BB77"));auto font=item->font(0);font.setBold(true);item->setFont(0,font);}
        return item;
    };
    row(tr("Bez řezu"),{},"document-section-normal",std::ranges::none_of(sections,[](const auto& s){return s.show_cut;}));
    zima::workspace::ReferenceIndex references;
    if (const auto* part=workspace_.open_part(id)) {
        // An unfinished Part can have history but no calculated body yet.
        // Tree validation consumes only persisted geometry and must not
        // require a solid or trigger its calculation.
        if (!part->session.calculated_boundaries().empty())
            references.add_geometry(workspace_.authoritative_viewer_mesh(id).original_references);
        references.add_geometry(part->session.document().body_origin_reference_geometry());
        references.add_geometry(part->session.document().history_origin_reference_geometry_before({}));
        references.add_geometry(part->session.document().origin_viewer_mesh().original_references);
        references.add_geometry(part->session.document().construction_viewer_mesh().original_references);
    } else {
        references.add_geometry(workspace_.authoritative_viewer_mesh(id).original_references);
    }
    for(const auto& s:sections){auto* item=row(QString::fromStdString(s.name),s.id,"document-section",s.show_cut);tree_reference_state_.apply(item,id,s.id,placement_reference_issue(s.placement,references));}
    // Tree presentation changes must never commit a section or destroy the
    // emitting item inside QTreeWidget::itemChanged.
    if(section_dialog_){preview_section();return;}if(properties_dialog_)return;
    try {
        const auto editing=std::ranges::find(sections,construction_dimension_object_id_,&zima::document::SectionDefinition::id);
        const bool dimension_edit=editing!=sections.end()&&workspace_.active_document_id()==id;
        const bool needs_source=std::ranges::any_of(sections,[&](const auto& s){return s.show_plane||(!dimension_edit&&s.show_cut);});
        const auto source=needs_source?workspace_.authoritative_viewer_mesh(id):zima::kernel::ViewerMesh{};
        auto mesh=viewer_->mesh();bool changed=false;
        if(!dimension_edit)for(const auto& s:sections)if(s.show_cut){
            mesh=cached_section_preview(source,s).clipped;
            mesh.original_references=source.original_references;changed=true;break;
        }
        for(const auto& s:sections)if(s.show_plane){
            append(mesh,cached_section_preview(source,s).plane);
            append(mesh,section_sketch_overlay(sketch_viewer_mesh(s.sketch),s.id,dimension_edit&&s.id==editing->id));changed=true;
        }
        if(dimension_edit) {
            const auto sketch=sketch_viewer_mesh(editing->sketch);
            if(!editing->show_plane)append(mesh,section_sketch_overlay(sketch,editing->id));
            mesh.dimensions.insert(mesh.dimensions.end(),sketch.dimensions.begin(),sketch.dimensions.end());changed=true;
        }
        if(changed)viewer_->set_mesh(std::move(mesh),false);
        if(dimension_edit){viewer_->set_container_inspection({});viewer_->set_selection_contract({zima::viewer::CandidateKind::Dimension});}
    }
    catch(const std::exception& e){state_->setText(QString::fromUtf8(e.what()));}
}
bool AssemblyWorkspaceWindow::section_context_menu(QTreeWidgetItem* item,const QPoint& position){
    if(!item||!item->data(0,Qt::UserRole+3).toString().startsWith("document-section"))return false;
    if(properties_dialog_||!active_sketch_id_.empty()||workspace_.active_document_id()!=workspace_.displayed_document_id())return true;
    QMenu menu(this);const auto id=item->data(0,Qt::UserRole).toString().toStdString();const auto kind=item->data(0,Qt::UserRole+3).toString();
    QAction* create=kind=="document-sections"?menu.addAction(tr("Nový řez…")):nullptr;QAction *activate=nullptr,*edit=nullptr,*draw=nullptr,*remove=nullptr;
    if(kind!="document-sections") {
        activate=menu.addAction(tr("Aktivní"));
        activate->setObjectName("activateSectionAction");
        const auto sections=source_sections(&workspace_,workspace_.displayed_document_id(),{});
        const bool active=id.empty()?std::ranges::none_of(sections,[](const auto& s){return s.show_cut;}):
            std::ranges::any_of(sections,[&](const auto& s){return s.id==id&&s.show_cut;});
        if(active)activate->setIcon(resource_icon("active-check"));
    }
    QAction* plane{};bool plane_visible=false;
    if(!id.empty()){
        edit=menu.addAction(tr("Vlastnosti"));draw=menu.addAction(tr("Upravit"));
        const auto sections=source_sections(&workspace_,workspace_.displayed_document_id(),{});
        const auto found=std::ranges::find(sections,id,&zima::document::SectionDefinition::id);
        plane_visible=found!=sections.end()&&found->show_plane;
        plane=menu.addAction(plane_visible?tr("Skrýt rovinu řezu"):tr("Zobrazit rovinu řezu"));plane->setObjectName("sectionPlaneVisibilityAction");
        remove=menu.addAction(resource_icon("delete"),tr("Odstranit"));
    }
    const auto chosen=exec_tree_menu(menu,item,position);if(!chosen)return true;
    if(chosen==create)show_section_properties();else if(chosen==edit)show_section_properties(id);else if(chosen==draw)show_parameter_dimensions(id);
    else if(chosen==activate||chosen==remove||chosen==plane){
        try {
            if(chosen==plane)static_cast<void>(workspace::set_section_plane_visible(workspace_,workspace_.displayed_document_id(),id,!plane_visible));
            else if(chosen==remove)static_cast<void>(workspace::remove_section(workspace_,workspace_.displayed_document_id(),id));
            else static_cast<void>(workspace::activate_section(workspace_,workspace_.displayed_document_id(),id));
            preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
        }catch(const std::exception& error){state_->setText(tr(error.what()));}
    }return true;
}
} // namespace zima::app
