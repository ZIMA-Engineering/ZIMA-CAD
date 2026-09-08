#include "assembly_workspace_window.hpp"
#include "section_properties_dialog.hpp"
#include "section_source.hpp"
#include <zima/viewer/mesh_view.hpp>
#include <zima/kernel/stable_id.hpp>
#include <QAction>
#include <QLabel>
#include <QMenu>
#include <QTreeWidget>
#include <QSignalBlocker>
#include <algorithm>
#include <cmath>
namespace zima::app {
namespace {
using V=zima::kernel::Vec3;
V add(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
V sub(V a,V b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
V mul(V a,double s){return {a.x*s,a.y*s,a.z*s};}
double dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
V cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
V unit(V a){const auto n=std::sqrt(dot(a,a));if(n<1e-12)throw std::runtime_error("Degenerate section camera");return mul(a,1/n);}
V plane_hit(V o,V d,V p,V n){const auto den=dot(d,n);if(std::abs(den)<1e-10)throw std::runtime_error("View is parallel to section sketch plane");return add(o,mul(d,dot(sub(p,o),n)/den));}
void plane_wire(zima::kernel::ViewerMesh& mesh,const zima::document::SectionDefinition& s){
    const auto f=zima::document::section_frame(s);
    const auto& line=s.sketch.segments.front();const auto* a=s.sketch.find_point(line.first_point_id);const auto* b=s.sketch.find_point(line.second_point_id);
    const double length=std::hypot(b->x-a->x,b->y-a->y),depth=std::max(length*.35,1.);
    const auto end=add(f.origin,mul(f.horizontal,length));
    zima::kernel::ViewerEdge edge;edge.overlay=true;edge.dash_dot=true;
    edge.points={sub(f.origin,mul(f.vertical,depth)),sub(end,mul(f.vertical,depth)),add(end,mul(f.vertical,depth)),add(f.origin,mul(f.vertical,depth)),sub(f.origin,mul(f.vertical,depth))};mesh.edges.push_back(edge);
    edge.dash_dot=false;edge.points={f.origin,end};mesh.edges.push_back(edge);
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
        auto sections=source_sections(&workspace_,section_document_id_,{});
        zima::document::SectionDefinition value;
        if(!id.empty()){const auto found=std::ranges::find(sections,id,&zima::document::SectionDefinition::id);if(found==sections.end())return;value=*found;}
        else{
            value.id=zima::kernel::make_stable_id();
            for(int i=0;;++i){const auto tag=i<26?std::string(1,static_cast<char>('A'+i)):std::to_string(i+1);value.name=tag+"–"+tag;if(std::ranges::none_of(sections,[&](const auto& s){return s.name==value.name;}))break;}
            const auto& mesh=viewer_->mesh();V center{};if(!mesh.vertices.empty()){for(auto v:mesh.vertices)center=add(center,v);center=mul(center,1./mesh.vertices.size());}
            const QPointF mid(viewer_->width()/2.,viewer_->height()/2.);
            const auto r=viewer_->ray_at(mid),rx=viewer_->ray_at(mid+QPointF(30,0)),ry=viewer_->ray_at(mid+QPointF(0,-30));
            if(!r||!rx||!ry)throw std::runtime_error("View camera is unavailable");
            const auto n=unit(r->second),origin=plane_hit(r->first,r->second,center,n);
            value.plane_origin=origin;value.plane_x=unit(sub(plane_hit(rx->first,rx->second,center,n),origin));const auto up=sub(plane_hit(ry->first,ry->second,center,n),origin);value.plane_y=unit(sub(up,mul(value.plane_x,dot(up,value.plane_x))));
            const auto span=std::max(1.,std::sqrt(dot(sub(plane_hit(rx->first,rx->second,center,n),origin),sub(plane_hit(rx->first,rx->second,center,n),origin)))*3);
            value.sketch=zima::sketcher::Sketch::create_default();static_cast<void>(value.sketch.add_segment(-span/2,0,span/2,0));
            if(auto* p=workspace_.open_part(section_document_id_)){auto doc=p->session.document();doc.sections.push_back(value);value=sections_for_part(doc).back();}
            else {auto doc=workspace_.open_assembly(section_document_id_)->session.document();doc.sections.push_back(value);value=sections_for_assembly(doc).back();}
        }
        section_preview_source_=workspace_.authoritative_viewer_mesh(section_document_id_);
        auto* dialog=new SectionPropertiesDialog(this,value,[this](auto next){
            if(section_line_picking_){section_dialog_->set_error(tr("Dokončete čáru dvěma kliknutími ve View."));return false;}
            auto all=source_sections(&workspace_,section_document_id_,{});
            if(std::ranges::any_of(all,[&](const auto& s){return s.id!=next.id&&s.name==next.name;})){section_dialog_->set_error(tr("Název řezu již existuje."));return false;}
            if(next.show_cut)for(auto& s:all)s.show_cut=false;
            const auto found=std::ranges::find(all,next.id,&zima::document::SectionDefinition::id);if(found==all.end())all.push_back(next);else *found=next;
            commit_sections(std::move(all));return true;
        },[this]{section_first_point_.reset();section_line_picking_=true;section_component_picking_=false;viewer_->clear_selection();viewer_->set_selection_contract({});state_->setText(tr("Řez: klikněte na začátek a konec čáry ve View."));},
        [this]{section_line_picking_=false;section_first_point_.reset();section_component_picking_=true;viewer_->clear_selection();viewer_->set_mesh(section_preview_source_,false);viewer_->set_selection_contract({workspace_.open_assembly(section_document_id_)?zima::viewer::CandidateKind::Occurrence:zima::viewer::CandidateKind::Container});viewer_->set_candidate_filter({});state_->setText(tr("Vyberte díl nebo těleso ve View; jeho řádek se označí v seznamu."));},[this]{preview_section();});
        dialog->set_initial_size(QSize(760,700));section_dialog_=dialog;properties_dialog_=dialog;
        connect(dialog,&QDialog::finished,this,[this,dialog]{section_dialog_.clear();if(properties_dialog_==dialog)properties_dialog_=nullptr;section_line_picking_=section_component_picking_=false;section_first_point_.reset();section_document_id_.clear();section_preview_source_={};viewer_->set_operation_direction_indicator({});preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();});
        viewer_->clear_selection();viewer_->set_selection_contract({});dialog->show();preview_section();
        if(draw)dialog->findChild<QPushButton*>("drawSectionLine")->click();
    }catch(const std::exception& e){section_document_id_.clear();state_->setText(QString::fromUtf8(e.what()));}
}
void AssemblyWorkspaceWindow::preview_section(){
    if(!section_dialog_)return;
    try{const auto s=section_dialog_->values();auto mesh=s.show_cut?zima::document::calculate_section(section_preview_source_,s).mesh:section_preview_source_;
        plane_wire(mesh,s);viewer_->set_mesh(std::move(mesh),false);const auto f=zima::document::section_frame(s);viewer_->set_operation_direction_indicator(zima::viewer::OperationDirectionIndicator{f.origin,f.normal,{},{},false});section_dialog_->set_error({});
    }catch(const std::exception& e){section_dialog_->set_error(QString::fromUtf8(e.what()));}
}
bool AssemblyWorkspaceWindow::section_ray(const V& origin,const V& direction,bool commit){
    if(!section_dialog_||!section_line_picking_)return false;
    try{const auto s=section_dialog_->values();const auto hit=plane_hit(origin,direction,s.plane_origin,cross(s.plane_x,s.plane_y));const auto delta=sub(hit,s.plane_origin);std::array<double,2> xy{dot(delta,s.plane_x),dot(delta,s.plane_y)};
        if(!section_first_point_){if(commit)section_first_point_=xy;return true;}
        const auto a=*section_first_point_;if(std::hypot(a[0]-xy[0],a[1]-xy[1])<.0001)return true;
        // Pointer motion only draws a wire; calculation is reserved for explicit edits/clicks.
        if(commit){section_dialog_->set_line(a[0],a[1],xy[0],xy[1]);section_first_point_.reset();section_line_picking_=false;state_->setText(tr("Čára řezu je připravena. OK uloží řez."));}
        else {auto mesh=section_preview_source_;zima::kernel::ViewerEdge edge;edge.overlay=true;edge.points={add(s.plane_origin,add(mul(s.plane_x,a[0]),mul(s.plane_y,a[1]))),hit};mesh.edges.push_back(edge);viewer_->set_mesh(std::move(mesh),false);}
    }catch(const std::exception& e){if(commit)section_dialog_->set_error(QString::fromUtf8(e.what()));}return true;
}
bool AssemblyWorkspaceWindow::section_confirmation(const zima::viewer::ViewerCandidate& candidate){
    if(!section_dialog_)return false;
    if(section_component_picking_){const auto s=section_dialog_->values();auto key=candidate.instance_path;if(key.empty()){const auto i=s.body_owners.find(candidate.owner_id);if(i!=s.body_owners.end())key=i->second;}
        section_dialog_->select_component(key);section_component_picking_=false;viewer_->set_selection_contract({});preview_section();}
    return true;
}
void AssemblyWorkspaceWindow::update_section_ui(){
    const auto id=workspace_.displayed_document_id();const bool model=workspace_.open_part(id)||workspace_.open_assembly(id);
    section_action_->setEnabled(model&&workspace_.active_document_id()==id&&!properties_dialog_&&active_sketch_id_.empty()&&!template_sketch());
    if(!model||template_sketch())return;
    const auto sections=source_sections(&workspace_,id,{});QSignalBlocker block(tree_);
    auto* root=tree_->topLevelItem(0);if(!root)return;
    auto* group=new QTreeWidgetItem(root,QStringList{tr("Řezy")});group->setData(0,Qt::UserRole+3,"document-sections");group->setExpanded(true);
    for(const auto& s:sections){auto* row=new QTreeWidgetItem(group,QStringList{QString::fromStdString(s.name)});row->setData(0,Qt::UserRole,QString::fromStdString(s.id));row->setData(0,Qt::UserRole+3,"document-section");row->setCheckState(0,s.show_plane?Qt::Checked:Qt::Unchecked);row->setToolTip(0,tr("Zaškrtnutí zobrazí rovinu řezu. Vlastnosti nastavují řez modelu."));}
    if(!tree_->property("sectionCheckConnected").toBool()){
        tree_->setProperty("sectionCheckConnected",true);
        connect(tree_,&QTreeWidget::itemChanged,this,[this](QTreeWidgetItem* item,int){if(refreshing_scene_||properties_dialog_||workspace_.active_document_id()!=workspace_.displayed_document_id()||item->data(0,Qt::UserRole+3)!="document-section")return;auto all=source_sections(&workspace_,workspace_.displayed_document_id(),{});for(auto& s:all)if(s.id==item->data(0,Qt::UserRole).toString().toStdString())s.show_plane=item->checkState(0)==Qt::Checked;commit_sections(std::move(all));preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();});
    }
    if(section_dialog_){preview_section();return;}
    if(properties_dialog_||!active_sketch_id_.empty())return;
    try{auto mesh=viewer_->mesh();bool changed=false;
        for(const auto& s:sections)if(s.show_cut){mesh=zima::document::calculate_section(mesh,s).mesh;changed=true;break;}
        for(const auto& s:sections)if(s.show_plane){plane_wire(mesh,s);changed=true;}
        if(changed)viewer_->set_mesh(std::move(mesh),false);
    }catch(const std::exception& e){state_->setText(QString::fromUtf8(e.what()));}
}
bool AssemblyWorkspaceWindow::section_context_menu(QTreeWidgetItem* item,const QPoint& position){
    if(!item||!item->data(0,Qt::UserRole+3).toString().startsWith("document-section"))return false;
    if(properties_dialog_||workspace_.active_document_id()!=workspace_.displayed_document_id())return true;
    QMenu menu(this);const auto id=item->data(0,Qt::UserRole).toString().toStdString();
    auto* create=menu.addAction(tr("Nový řez…"));QAction *edit=nullptr,*draw=nullptr,*remove=nullptr;
    if(!id.empty()){edit=menu.addAction(tr("Vlastnosti / přejmenovat…"));draw=menu.addAction(tr("Upravit čáru řezu…"));remove=menu.addAction(tr("Odstranit"));}
    const auto chosen=menu.exec(tree_->viewport()->mapToGlobal(position));
    if(chosen==create)show_section_properties({},true);else if(edit&&chosen==edit)show_section_properties(id);else if(draw&&chosen==draw)show_section_properties(id,true);
    else if(remove&&chosen==remove){auto all=source_sections(&workspace_,workspace_.displayed_document_id(),{});std::erase_if(all,[&](const auto& s){return s.id==id;});commit_sections(std::move(all));preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();}
    return true;
}
} // namespace zima::app
