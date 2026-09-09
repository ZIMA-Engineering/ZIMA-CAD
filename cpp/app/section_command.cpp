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
#include <algorithm>
#include <cmath>
namespace zima::app {
namespace {
void append(zima::kernel::ViewerReferenceGeometry& a,const zima::kernel::ViewerReferenceGeometry& b){
    const auto base=static_cast<std::uint32_t>(a.vertices.size());a.vertices.insert(a.vertices.end(),b.vertices.begin(),b.vertices.end());for(auto i:b.triangles)a.triangles.push_back(i+base);
    a.triangle_references.insert(a.triangle_references.end(),b.triangle_references.begin(),b.triangle_references.end());a.edges.insert(a.edges.end(),b.edges.begin(),b.edges.end());a.points.insert(a.points.end(),b.points.begin(),b.points.end());a.axes.insert(a.axes.end(),b.axes.begin(),b.axes.end());
}
void append(zima::kernel::ViewerMesh& a,const zima::kernel::ViewerMesh& b){
    const auto base=static_cast<std::uint32_t>(a.vertices.size());a.vertices.insert(a.vertices.end(),b.vertices.begin(),b.vertices.end());for(auto i:b.triangles)a.triangles.push_back(i+base);
    a.triangle_references.insert(a.triangle_references.end(),b.triangle_references.begin(),b.triangle_references.end());a.edges.insert(a.edges.end(),b.edges.begin(),b.edges.end());a.points.insert(a.points.end(),b.points.begin(),b.points.end());a.axes.insert(a.axes.end(),b.axes.begin(),b.axes.end());a.constraint_markers.insert(a.constraint_markers.end(),b.constraint_markers.begin(),b.constraint_markers.end());a.dimensions.insert(a.dimensions.end(),b.dimensions.begin(),b.dimensions.end());append(a.original_references,b.original_references);
}

}
void AssemblyWorkspaceWindow::commit_sections(std::vector<zima::document::SectionDefinition> sections){
    const auto id=section_document_id_.empty()?workspace_.displayed_document_id():section_document_id_;
    if(auto* p=workspace_.open_part(id)){auto next=p->session.document();next.sections=std::move(sections);p->session.commit(std::move(next),p->session.calculated_boundaries());}
    else if(auto* a=workspace_.open_assembly(id)){auto next=a->session.document();next.sections=std::move(sections);a->session.commit(std::move(next));}
    else throw std::runtime_error("Section source is no longer open");
}
void AssemblyWorkspaceWindow::show_section_properties(const std::string& id,bool draw){
    if(properties_dialog_||!active_sketch_id_.empty()||template_sketch()||workspace_.active_document_id()!=workspace_.displayed_document_id())return;
    section_document_id_=workspace_.displayed_document_id();
    if(!workspace_.open_part(section_document_id_)&&!workspace_.open_assembly(section_document_id_))return;
    try{
        auto sections=source_sections(&workspace_,section_document_id_,{});auto value=zima::document::create_section();
        if(!id.empty()){const auto found=std::ranges::find(sections,id,&zima::document::SectionDefinition::id);if(found==sections.end())return;value=*found;}
        else{
            for(int i=0;;++i){const auto tag=i<26?std::string(1,static_cast<char>('A'+i)):std::to_string(i+1);value.name=tag+"–"+tag;if(std::ranges::none_of(sections,[&](const auto& s){return s.name==value.name;}))break;}
            if(auto* p=workspace_.open_part(section_document_id_)){auto doc=p->session.document();doc.sections.push_back(value);value=sections_for_part(doc).back();}
            else {auto doc=workspace_.open_assembly(section_document_id_)->session.document();doc.sections.push_back(value);value=sections_for_assembly(doc).back();}
        }
        section_preview_source_=workspace_.authoritative_viewer_mesh(section_document_id_);section_camera_=viewer_->camera_state();
        primitive_reference_geometry_=section_preview_source_.original_references;
        if(auto* p=workspace_.open_part(section_document_id_)){const auto& doc=p->session.document();append(primitive_reference_geometry_,doc.origin_viewer_mesh().original_references);append(primitive_reference_geometry_,doc.construction_viewer_mesh().original_references);append(primitive_reference_geometry_,doc.history_origin_reference_geometry_before(value.id));append(primitive_reference_geometry_,doc.body_origin_reference_geometry());}
        else{const auto& doc=workspace_.open_assembly(section_document_id_)->session.document();append(primitive_reference_geometry_,doc.origin_viewer_mesh().original_references);append(primitive_reference_geometry_,doc.construction_viewer_mesh().original_references);}
        auto* dialog=new SectionPropertiesDialog(this,value,[this](auto next){
            // Validate the complete chain, including end extensions, before any
            // transaction. An inactive invalid section must not enter a file.
            static_cast<void>(zima::document::calculate_section(section_preview_source_,next));
            auto all=source_sections(&workspace_,section_document_id_,{});
            if(std::ranges::any_of(all,[&](const auto& s){return s.id!=next.id&&s.name==next.name;})){section_dialog_->set_error(tr("Název řezu již existuje."));return false;}
            if(next.show_cut)for(auto& s:all)s.show_cut=false;
            const auto found=std::ranges::find(all,next.id,&zima::document::SectionDefinition::id);if(found==all.end())all.push_back(next);else *found=next;
            commit_sections(std::move(all));return true;
        },[this]{
            pending_primitive_reference_index_.reset();section_dialog_->set_active_reference_index(std::nullopt);section_component_picking_=true;viewer_->clear_selection();viewer_->set_mesh(section_preview_source_,false);
            viewer_->set_selection_contract({workspace_.open_assembly(section_document_id_)?zima::viewer::CandidateKind::Occurrence:zima::viewer::CandidateKind::Container});viewer_->set_candidate_filter({});state_->setText(tr("Vyberte díl nebo těleso ve View; jeho řádek se označí v seznamu."));
        });
        dialog->set_initial_size(QSize(760,1040));section_dialog_=dialog;properties_dialog_=dialog;primitive_reference_dialog_=dialog;primitive_parameter_owner_id_=value.id;properties_dialog_instance_path_.clear();
        bind_local_origin_selection(dialog);
        dialog->request_placement=[this](std::size_t index){section_component_picking_=false;start_primitive_reference_selection(index);};
        dialog->edit_sketch=[this](unsigned){begin_section_sketch();};dialog->changed=[this]{preview_section();};
        connect(dialog,&QDialog::finished,this,[this,dialog]{
            section_dialog_.clear();if(properties_dialog_==dialog)properties_dialog_=nullptr;
            section_component_picking_=false;section_document_id_.clear();section_preview_source_={};section_sketch_undo_.clear();section_sketch_redo_.clear();
            feature_reference_pick_={};feature_reference_end_={};pending_primitive_reference_index_.reset();primitive_reference_auto_advance_=false;
            local_origin_selection_dialog_=nullptr;local_origin_selection_active_=false;visible_local_origin_ids_.clear();selectable_local_origin_container_ids_.clear();suspended_primitive_reference_index_.reset();suspended_construction_reference_index_.reset();
            primitive_reference_dialog_=nullptr;primitive_reference_geometry_={};primitive_origin_preview_mesh_.reset();parameter_dimension_preview_.reset();construction_dimension_object_id_.clear();primitive_parameter_owner_id_.clear();
            viewer_->set_constraint_reference_highlights({},{});viewer_->set_feature_preview_owners({});viewer_->set_transient_edges({});viewer_->set_transient_labels({});viewer_->set_transient_points({});viewer_->set_operation_direction_indicator({});
            viewer_->set_camera_state(section_camera_);preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
        });
        viewer_->clear_selection();viewer_->set_selection_contract({});dialog->show();preview_section();
        if(dialog->first_empty_position_index()<3)
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
    dialog->hide();properties_dialog_=nullptr;viewer_->set_operation_direction_indicator({});viewer_->set_transient_edges({});viewer_->set_transient_labels({});viewer_->set_transient_points({});
    active_sketch_id_=sweep_profile_sketch_draft_->id;selected_sketch_id_=active_sketch_id_;clear_selected_sketch_geometry();viewer_->clear_selection();tree_->clearSelection();preserve_view_on_refresh_=true;refresh_scene();align_active_sketch_view();
    state_->setText(tr("Nakreslete otevřenou čáru nebo lomenou čáru. Dokončit skicu vrátí vlastnosti řezu."));
}
void AssemblyWorkspaceWindow::cancel_section_sketch(){
    if(!section_dialog_||!sweep_profile_sketch_draft_||properties_dialog_)return;
    auto original=section_dialog_->values().sketch;auto finished=std::move(embedded_sketch_finished_);
    cancel_sketch_segment();active_sketch_id_.clear();clear_selected_sketch_geometry();viewer_->clear_selection();sweep_profile_sketch_draft_.reset();section_drag_sketches_.clear();finished(std::move(original));
}
bool AssemblyWorkspaceWindow::section_sketch_history(bool redo){
    if(!section_dialog_||!sweep_profile_sketch_draft_)return false;
    auto& from=redo?section_sketch_redo_:section_sketch_undo_;auto& to=redo?section_sketch_undo_:section_sketch_redo_;
    cancel_sketch_segment();if(!from.empty()){to.push_back(*sweep_profile_sketch_draft_);sweep_profile_sketch_draft_=std::move(from.back());from.pop_back();clear_selected_sketch_geometry();viewer_->clear_selection();preserve_view_on_refresh_=true;refresh_scene();}return true;
}
void AssemblyWorkspaceWindow::preview_section(){
    if(!section_dialog_||!section_dialog_->isVisible()||!active_sketch_id_.empty()||section_component_picking_)return;
    auto* dialog=section_dialog_.data();const bool placement_valid=dialog->resolve_pending_placement(primitive_reference_geometry_);const auto s=dialog->values();
    primitive_translation_dof_=zima::document::point_constraint_remaining_dof(s.placement.references,primitive_reference_geometry_);
    zima::document::PartDocument origin_doc;zima::document::ConstructionObject origin;origin.id=s.id;origin.entity_id=s.sketch.id;origin.container_origin=s.container_origin;origin.kind=zima::document::ConstructionKind::Point;
    origin.origin={s.placement.x,s.placement.y,s.placement.z};origin.rotation={s.placement.rotation_x,s.placement.rotation_y,s.placement.rotation_z};origin.reference_valid=false;origin_doc.constructions.push_back(origin);primitive_origin_preview_mesh_=origin_doc.construction_viewer_mesh(s.id);
    auto mesh=section_preview_source_;QString error;
    try{
        if(s.show_cut)mesh=zima::document::section_display_mesh(zima::document::calculate_section(section_preview_source_,s),s);
        else {
            for(const auto& active:source_sections(&workspace_,section_document_id_,{}))if(active.id!=s.id&&active.show_cut){mesh=zima::document::section_display_mesh(zima::document::calculate_section(section_preview_source_,active),active);break;}
        }
        const auto f=zima::document::section_frame(s);viewer_->set_operation_direction_indicator(zima::viewer::OperationDirectionIndicator{f.origin,f.normal,{},{},false});
    }
    catch(const std::exception& e){error=QString::fromUtf8(e.what());viewer_->set_operation_direction_indicator({});}
    // Placement candidates always refer to the complete persisted input even
    // when the transient cut hides that surface.
    if(pending_primitive_reference_index_||local_origin_selection_active_)mesh=section_preview_source_;
    if(s.show_plane&&(!s.show_cut||pending_primitive_reference_index_||local_origin_selection_active_)) {
        try {append(mesh,zima::document::section_surface_mesh(zima::document::calculate_section(section_preview_source_,s),s));}
        catch(const std::exception& e){error=QString::fromUtf8(e.what());}
    }
    append(mesh,sketch_viewer_mesh(s.sketch));
    if(const auto* part=workspace_.open_part(section_document_id_)) {
        append(mesh,active_part_origins(part->session.document()));
        append(mesh,selected_container_origins(part->session.document()));
    }else if(const auto* assembly=workspace_.open_assembly(section_document_id_))append(mesh,assembly->session.document().origin_viewer_mesh());
    append(mesh,*primitive_origin_preview_mesh_);viewer_->set_mesh(std::move(mesh),false);viewer_->set_feature_preview_owners({s.sketch.id,s.container_origin.id});std::set<zima::viewer::EdgeKey> highlights;for(const auto& ref:dialog->highlighted_reference_entries()){highlights.insert({ref.owner_id,ref.semantic_key,ref.instance_path});if(ref.semantic_key=="plane")highlights.insert({ref.owner_id,"border",ref.instance_path});}viewer_->set_constraint_reference_highlights({},std::move(highlights));
    if(!pending_primitive_reference_index_&&!local_origin_selection_active_)set_primitive_properties_dimension_selection();dialog->set_error(placement_valid?error:tr("Doplňte platné reference umístění řezu."));
}
bool AssemblyWorkspaceWindow::section_confirmation(const zima::viewer::ViewerCandidate& candidate){
    if(!section_dialog_||!section_component_picking_||!active_sketch_id_.empty())return false;
    const auto s=section_dialog_->values();auto key=candidate.instance_path;if(key.empty()){const auto i=s.body_owners.find(candidate.owner_id);if(i!=s.body_owners.end())key=i->second;}
    section_dialog_->select_component(key);section_component_picking_=false;viewer_->set_selection_contract({});preview_section();return true;
}
void AssemblyWorkspaceWindow::update_section_ui(){
    const auto id=workspace_.displayed_document_id();const bool model=workspace_.open_part(id)||workspace_.open_assembly(id);const bool sketch=section_dialog_&&sweep_profile_sketch_draft_&&!active_sketch_id_.empty();
    if(cancel_section_sketch_action_)cancel_section_sketch_action_->setVisible(sketch);
    section_action_->setEnabled(model&&workspace_.active_document_id()==id&&!properties_dialog_&&active_sketch_id_.empty()&&!template_sketch());
    if(sketch){undo_action_->setEnabled(!section_sketch_undo_.empty());redo_action_->setEnabled(!section_sketch_redo_.empty());return;}
    if(!model||template_sketch()||!active_sketch_id_.empty())return;
    const auto sections=source_sections(&workspace_,id,{});QSignalBlocker block(tree_);auto* root=tree_->topLevelItem(0);if(!root)return;
    auto* group=new QTreeWidgetItem(QStringList{tr("Řezy")});
    group->setData(0,Qt::UserRole+3,"document-sections");
    group->setFlags(group->flags()&~Qt::ItemIsUserCheckable);
    group->setIcon(0,resource_icon("sections"));
    root->insertChild(std::min(1,root->childCount()),group);group->setExpanded(true);
    const auto row=[&](const QString& name,const std::string& key,const char* type,bool active){
        auto* item=new QTreeWidgetItem(group,QStringList{name});
        item->setData(0,Qt::UserRole,QString::fromStdString(key));item->setData(0,Qt::UserRole+3,type);
        item->setFlags(item->flags()&~Qt::ItemIsUserCheckable);
        item->setIcon(0,resource_icon(key.empty()?"section-normal":"section-cut"));
        if(active){item->setForeground(0,QColor("#55BB77"));auto font=item->font(0);font.setBold(true);item->setFont(0,font);}
        return item;
    };
    row(tr("Bez řezu"),{},"document-section-normal",std::ranges::none_of(sections,[](const auto& s){return s.show_cut;}));
    TreeReferenceIndex references;
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
    try{const auto source=viewer_->mesh();auto mesh=source;bool changed=false;for(const auto& s:sections)if(s.show_cut){mesh=zima::document::section_display_mesh(zima::document::calculate_section(source,s),s);changed=true;break;}for(const auto& s:sections)if(s.show_plane&&!s.show_cut){append(mesh,zima::document::section_surface_mesh(zima::document::calculate_section(source,s),s));changed=true;}if(changed)viewer_->set_mesh(std::move(mesh),false);}
    catch(const std::exception& e){state_->setText(QString::fromUtf8(e.what()));}
}
bool AssemblyWorkspaceWindow::section_context_menu(QTreeWidgetItem* item,const QPoint& position){
    if(!item||!item->data(0,Qt::UserRole+3).toString().startsWith("document-section"))return false;
    if(properties_dialog_||!active_sketch_id_.empty()||workspace_.active_document_id()!=workspace_.displayed_document_id())return true;
    QMenu menu(this);const auto id=item->data(0,Qt::UserRole).toString().toStdString();const auto kind=item->data(0,Qt::UserRole+3).toString();
    QAction* create=kind=="document-sections"?menu.addAction(tr("Nový řez…")):nullptr;QAction *activate=nullptr,*edit=nullptr,*draw=nullptr,*remove=nullptr;
    if(kind!="document-sections")activate=menu.addAction(tr("Aktivní"));
    if(!id.empty()){edit=menu.addAction(tr("Vlastnosti / přejmenovat…"));draw=menu.addAction(tr("Upravit skicu řezu…"));remove=menu.addAction(tr("Odstranit"));}
    const auto chosen=menu.exec(tree_->viewport()->mapToGlobal(position));if(!chosen)return true;
    if(chosen==create)show_section_properties();else if(chosen==edit)show_section_properties(id);else if(chosen==draw)show_section_properties(id,true);
    else if(chosen==activate||chosen==remove){auto all=source_sections(&workspace_,workspace_.displayed_document_id(),{});if(chosen==remove)std::erase_if(all,[&](const auto& s){return s.id==id;});else for(auto& s:all)s.show_cut=s.id==id;commit_sections(std::move(all));preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();}return true;
}
} // namespace zima::app
