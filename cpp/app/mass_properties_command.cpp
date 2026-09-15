#include "workspace/workspace_internal.hpp"
#include "mass_properties_dialog.hpp"
#include <zima/workspace/body_properties_edits.hpp>
#include <QTreeWidgetItemIterator>
namespace zima::app {
using namespace workspace_detail;
void AssemblyWorkspaceWindow::show_mass_properties(const std::string& object) {
    if(mass_properties_dialog_){mass_properties_dialog_->raise();return;}
    if(properties_dialog_||tree_edit_dialog_||!section_dialog_.isNull())return;
    const auto id=workspace_.displayed_document_id();
    workspace::BodyPropertiesEdit edit;
    try{edit=workspace::prepare_body_properties_edit(workspace_,id,object,tr("Měření tělesa").toStdString());}
    catch(const std::exception& e){state_->setText(tr(e.what()));return;}
    cancel_sketch_segment();const auto& part=*workspace_.open_part(id);const auto& doc=part.session.document();
    auto input=std::make_shared<kernel::ViewerMesh>();
    try {
        for(const auto& source:document::body_properties_inputs(doc,part.session.calculated_boundaries(),edit.initial)) {
            auto mesh=source.body->mesh;
            if(!edit.initial.body_id.empty())mesh=doc.place_body_mesh(std::move(mesh),edit.initial.body_id);
            append_mesh(*input,std::move(mesh));
        }
    }catch(const std::exception&){} // A broken record remains editable and removable.
    const auto preview=[this,input](const document::BodyProperties& row){
        auto shown=row;shown.visible=true;
        auto mesh=*input;append_mesh(mesh,document::body_properties_origin(shown,tr("Těžiště").toStdString()));
        viewer_->set_mesh(std::move(mesh),false);
        viewer_->set_editing_origin_visible(true);
        viewer_->set_candidate_filter([](const auto&){return false;});
        viewer_->confirm_origin(row.id+":origin",{});
    };
    const auto* body=doc.body_history.find(edit.initial.body_id);
    const auto scope=body?QString::fromStdString(body->name):tr("Celý díl");
    auto* dialog=new MassPropertiesDialog(edit.initial,doc.document_units,scope,
        [this,edit](auto value){try{static_cast<void>(workspace::commit_body_properties(workspace_,edit,std::move(value)));}
            catch(const std::exception& e){throw std::runtime_error(tr(e.what()).toStdString());}},preview,this);
    mass_properties_dialog_=dialog;properties_dialog_=dialog;
    dialog->setProperty("centroidOriginId",QString::fromStdString(edit.initial.id+":origin"));
    viewer_->clear_selection();tree_->clearSelection();tree_->setProperty("commandSelectionActive",true);
    viewer_->set_dimension_layout_editable(false);
    connect(dialog,&QDialog::finished,this,[this,dialog,id,object=edit.initial.id](int result){
        if(mass_properties_dialog_!=dialog)return;
        mass_properties_dialog_=nullptr;if(properties_dialog_==dialog)properties_dialog_=nullptr;
        tree_->setProperty("commandSelectionActive",false);viewer_->set_candidate_filter({});viewer_->set_selection_contract({});
        viewer_->set_editing_origin_visible(false);
        viewer_->clear_selection();preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
        if(result==QDialog::Accepted&&dialog->property("bodyPropertiesSaved").toBool())for(QTreeWidgetItemIterator it(tree_);*it;++it) {
            if((*it)->data(0,Qt::UserRole+3)=="body-properties"&&(*it)->data(0,Qt::UserRole).toString().toStdString()==object) {
                for(auto* parent=(*it)->parent();parent;parent=parent->parent())parent->setExpanded(true);
                tree_->setCurrentItem(*it);(*it)->setSelected(true);tree_->scrollToItem(*it);break;
            }
        }
    });
    preserve_view_on_refresh_=true;refresh_scene();preview(edit.initial);
    // The information feature consumes the solid immediately before its row.
    // Suppress downstream tree rows visually only for this edit session.
    for(QTreeWidgetItemIterator it(tree_);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==edit.initial.id) {
        auto* row=*it;row->setForeground(0,QColor("#53bb39"));auto* parent=row->parent();
        if(parent)for(int i=parent->indexOfChild(row)+1;i<parent->childCount();++i)parent->child(i)->setForeground(0,QColor("#888888"));
        break;
    }
    dialog->show();
}
void AssemblyWorkspaceWindow::update_mass_properties_ui() {
    const auto id=workspace_.displayed_document_id();const auto* part=workspace_.open_part(id);
    if(mass_properties_action_) {
        mass_properties_action_->setVisible(part!=nullptr);
        mass_properties_action_->setEnabled(part&&workspace_.active_document_id()==id&&!properties_dialog_&&active_sketch_id_.empty()&&section_dialog_.isNull());
    }
    if(!part||!active_sketch_id_.empty())return;auto* root=tree_->topLevelItem(0);if(!root)return;
    for(const auto& row:part->session.document().body_properties) {
        auto* parent=root;QTreeWidgetItem* anchor{};
        for(QTreeWidgetItemIterator it(root);*it;++it) {
            const auto key=(*it)->data(0,Qt::UserRole).toString().toStdString();
            const auto kind=(*it)->data(0,Qt::UserRole+3).toString();
            if(!row.body_id.empty()&&key==row.body_id&&kind=="part-body")parent=*it;
            if(!row.after_object_id.empty()&&key==row.after_object_id&&
               (kind=="part-container"||kind=="part-sketch"||kind=="part-construction"||kind=="part-body"||kind=="part-body-boolean"))anchor=*it;
        }
        if(anchor&&anchor->parent()!=parent)anchor=nullptr;
        int position=anchor?parent->indexOfChild(anchor)+1:0;
        if(anchor)while(position<parent->childCount()) {
            const auto kind=parent->child(position)->data(0,Qt::UserRole+3).toString();
            if(kind!="body-properties"&&kind!="document-measurement")break;++position;
        }
        if(!anchor)for(int i=0;i<parent->childCount();++i) {
            const auto kind=parent->child(i)->data(0,Qt::UserRole+3).toString();
            if(kind=="part-insert-here"||kind=="part-body-insert-here"||kind=="part-container"||kind=="part-sketch"||kind=="part-construction"||kind=="part-body"||kind=="part-body-boolean"){position=i;break;}
        }
        auto* item=new QTreeWidgetItem(QStringList{QString::fromStdString(row.name)});parent->insertChild(position,item);
        item->setData(0,Qt::UserRole,QString::fromStdString(row.id));item->setData(0,Qt::UserRole+3,"body-properties");
        item->setIcon(0,resource_icon("body-properties"));item->setFlags(item->flags()&~Qt::ItemIsUserCheckable);
        item->setData(0,missing_reference_role,!row.error.empty());
        if(!row.error.empty()){item->setForeground(0,QColor("#d85858"));item->setToolTip(0,tr(row.error.c_str()));}
        else item->setToolTip(0,tr("Měření tělesa v tomto místě historie."));
        if(!row.visible&&row.error.empty())item->setForeground(0,QColor("#888888"));
        auto* origin=new QTreeWidgetItem(item,QStringList{tr("Těžiště")});origin->setIcon(0,resource_icon("origin"));
        origin->setData(0,Qt::UserRole,QString::fromStdString(row.id+":origin"));origin->setData(0,Qt::UserRole+3,"body-properties-origin");
        origin->setData(0,Qt::UserRole+5,QString::fromStdString(row.id));origin->setFlags(origin->flags()&~Qt::ItemIsUserCheckable);
        if(row.integrals){const auto c=row.integrals->centroid;origin->setToolTip(0,QString("X: %1; Y: %2; Z: %3 mm").arg(c.x).arg(c.y).arg(c.z));}
    }
}
bool AssemblyWorkspaceWindow::mass_properties_context_menu(QTreeWidgetItem* item,const QPoint& position) {
    if(!item||!item->data(0,Qt::UserRole+3).toString().startsWith("body-properties"))return false;
    if(properties_dialog_)return true;
    const bool origin=item->data(0,Qt::UserRole+3)=="body-properties-origin";
    const auto object=item->data(0,origin?Qt::UserRole+5:Qt::UserRole).toString().toStdString();
    const auto* part=workspace_.open_part(workspace_.displayed_document_id());if(!part)return true;
    const auto found=std::ranges::find(part->session.document().body_properties,object,&document::BodyProperties::id);
    if(found==part->session.document().body_properties.end())return true;
    const bool visible=found->visible;
    QMenu menu(this);auto* edit=menu.addAction(tr("Vlastnosti…"));
    auto* visibility=menu.addAction(visible?tr("Skrýt"):tr("Zobrazit"));visibility->setObjectName("bodyPropertiesVisibilityAction");
    auto* remove=menu.addAction(tr("Odstranit"));
    const auto* picked=menu.exec(tree_->viewport()->mapToGlobal(position));
    if(picked==edit)show_mass_properties(object);
    else if(picked==visibility) {
        try{auto edit=workspace::prepare_body_properties_edit(workspace_,workspace_.displayed_document_id(),object);auto value=edit.initial;value.visible=!visible;
            static_cast<void>(workspace::commit_body_properties(workspace_,edit,std::move(value)));}
        catch(const std::exception& e){state_->setText(tr(e.what()));return true;}
        preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
    }
    else if(picked==remove) {
        try{workspace::remove_body_properties(workspace_,workspace_.displayed_document_id(),object);}
        catch(const std::exception& e){state_->setText(tr(e.what()));return true;}
        preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
    }return true;
}
}
