#include "assembly_workspace_window.hpp"
#include "measurement_dialog.hpp"
#include "resource_icon.hpp"
#include <zima/workspace/measurement_edits.hpp>
#include <zima/document/physical_properties.hpp>
#include <zima/viewer/measurement.hpp>
#include <zima/viewer/mesh_view.hpp>
#include <QAction>
#include <QLabel>
#include <QMenu>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

namespace zima::app {
namespace {
using Ref=kernel::MeasurementReference;
using Geometry=measurement::MeasurementGeometry;
const std::vector<viewer::CandidateKind> measurement_kinds{
    viewer::CandidateKind::Vertex,viewer::CandidateKind::SketchPoint,
    viewer::CandidateKind::Edge,viewer::CandidateKind::SketchSegment,viewer::CandidateKind::SketchCurve,
    viewer::CandidateKind::SketchExternalReference,viewer::CandidateKind::Face,viewer::CandidateKind::Plane,
    viewer::CandidateKind::Axis,viewer::CandidateKind::SketchAxis,viewer::CandidateKind::Container,viewer::CandidateKind::Occurrence};
}
void AssemblyWorkspaceWindow::show_measurement(const std::string& saved_id){
    if(measurement_dialog_){measurement_dialog_->raise();return;}
    if(properties_dialog_||tree_edit_dialog_||!section_dialog_.isNull())return;
    const auto id=workspace_.displayed_document_id();
    const auto* part=workspace_.open_part(id);const auto* assembly=workspace_.open_assembly(id);
    if(!part&&!assembly)return;
    cancel_sketch_segment();
    workspace::MeasurementEdit edit;
    try {edit=workspace::prepare_measurement_edit(workspace_,id,saved_id,tr("Měření").toStdString());}
    catch(const std::exception& error){state_->setText(tr(error.what()));return;}
    const auto& initial=edit.initial;
    const auto saved_record=std::make_shared<std::string>();
    const auto units=part?part->session.document().document_units:assembly->session.document().document_units;
    auto* dialog=new MeasurementDialog(initial,
        [this](const Ref& ref){return resolve_measurement(ref);},
        [this](const Ref& ref){return measurement_label(ref);},
        [this,edit,saved_record](kernel::SavedMeasurement record){
            try {static_cast<void>(workspace::commit_measurement(workspace_,edit,std::move(record)));}
            catch(const std::exception& error){throw std::runtime_error(tr(error.what()).toStdString());}
            *saved_record=edit.initial.id;
        },document::length_unit_mm(units.at("Length")),QString::fromStdString(units.at("Length")),
        document::mass_unit_kg(units.at("Mass")),QString::fromStdString(units.at("Mass")),this);
    dialog->set_save_available(workspace_.active_document_id()==id,
        tr("Activate the displayed model before changing saved measurements."));
    measurement_dialog_=dialog;properties_dialog_=dialog;
    viewer_->clear_selection();tree_->clearSelection();tree_->setProperty("commandSelectionActive",true);
    viewer_->set_dimension_layout_editable(false);
    dialog->set_changed([this]{update_measurement_selection();});
    connect(dialog,&QDialog::finished,this,[this,dialog,saved_record]{
        if(measurement_dialog_!=dialog)return;
        measurement_dialog_=nullptr;if(properties_dialog_==dialog)properties_dialog_=nullptr;
        tree_->setProperty("commandSelectionActive",false);
        viewer_->set_candidate_filter({});viewer_->set_selection_contract({});
        viewer_->set_inspected_faces({});viewer_->set_constraint_reference_highlights({},{});
        viewer_->set_transient_edges({});viewer_->set_transient_points({});viewer_->set_transient_labels({});
        viewer_->clear_selection();preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
        if(!saved_record->empty())for(QTreeWidgetItemIterator it(tree_);*it;++it){
            auto* item=*it;
            if(item->data(0,Qt::UserRole+3)=="document-measurement"&&item->data(0,Qt::UserRole).toString().toStdString()==*saved_record){
                for(auto* parent=item->parent();parent;parent=parent->parent())parent->setExpanded(true);
                tree_->setCurrentItem(item);item->setSelected(true);tree_->scrollToItem(item);break;
            }
        }
    });
    dialog->show();
}
std::optional<Geometry> AssemblyWorkspaceWindow::resolve_measurement(const Ref& reference)const{
    const auto id=workspace_.displayed_document_id();
    if(reference.kind==kernel::MeasurementKind::Object&&reference.owner_id.empty()&&!reference.instance_path.empty()) {
        const auto scene=workspace_.authoritative_viewer_mesh(id);
        return workspace::resolve_measurement(workspace_,id,reference,scene);
    }
    return workspace::resolve_measurement(workspace_,id,reference,viewer_->mesh());
}

QString AssemblyWorkspaceWindow::measurement_label(const Ref& ref)const{
    QString type;
    switch(ref.kind){
    case kernel::MeasurementKind::Point:type=tr("Bod");break;
    case kernel::MeasurementKind::Curve:type=tr("Hrana / křivka");break;
    case kernel::MeasurementKind::Face:type=tr("Plocha");break;
    case kernel::MeasurementKind::Object:type=ref.owner_id.empty()?tr("Komponenta"):tr("Objekt");break;
    case kernel::MeasurementKind::Axis:type=tr("Osa");break;
    case kernel::MeasurementKind::Plane:type=tr("Rovina");break;
    }
    QString owner;
    for(QTreeWidgetItemIterator it(tree_);*it;++it){
        auto* item=*it;
        const auto id=item->data(0,Qt::UserRole).toString().toStdString();
        const auto path=item->data(0,Qt::UserRole+1).toString().toStdString();
        if((ref.owner_id.empty()?path==ref.instance_path&&!path.empty():id==ref.owner_id||id+":entity"==ref.owner_id)&&
            (ref.instance_path.empty()||path==ref.instance_path)){
            owner=item->text(0);break;
        }
    }
    if(owner.isEmpty())owner=QString::fromStdString(ref.owner_id.empty()?ref.instance_path:ref.owner_id);
    return type+QStringLiteral(" — ")+owner+(ref.kind==kernel::MeasurementKind::Object?QString{}:QStringLiteral(" / ")+QString::fromStdString(ref.semantic_key));
}
bool AssemblyWorkspaceWindow::accept_measurement(const viewer::ViewerCandidate& candidate){
    if(!measurement_dialog_)return false;
    if(const auto reference=viewer::measurement_reference(candidate))measurement_dialog_->select_reference(*reference);
    return true;
}
void AssemblyWorkspaceWindow::update_measurement_selection(){
    if(!measurement_dialog_)return;
    viewer_->set_selection_contract(measurement_kinds);
    viewer_->set_candidate_filter([this](const auto& candidate){
        if(!measurement_dialog_||!measurement_dialog_->entering())return false;
        const auto ref=viewer::measurement_reference(candidate);
        return ref.has_value();
    });
    viewer_->clear_selection();
    std::set<viewer::EdgeKey> references;std::vector<viewer::ViewerCandidate> faces;
    const auto& geometry=measurement_dialog_->geometries();
    for(int i=0;i<2;++i)if(geometry[i]&&measurement_dialog_->inspected()[i]){
        const auto& ref=geometry[i]->reference;
        if(ref.kind==kernel::MeasurementKind::Face||ref.kind==kernel::MeasurementKind::Plane)
            faces.push_back({viewer::CandidateKind::Face,0,0,ref.owner_id,ref.semantic_key,ref.instance_path,viewer::CandidateGeometry::OriginalReference});
        else if(ref.kind==kernel::MeasurementKind::Object){
            for(const auto& edge:viewer_->mesh().original_references.edges)
                if((ref.owner_id.empty()?edge.reference.instance_path.starts_with(ref.instance_path):edge.reference.owner_id==ref.owner_id&&edge.reference.instance_path==ref.instance_path))
                    references.insert(viewer::edge_key(edge.reference));
        }else references.insert({ref.owner_id,ref.semantic_key,ref.instance_path});
    }
    viewer_->set_inspected_faces(std::move(faces));viewer_->set_constraint_reference_highlights({},std::move(references));
    std::vector<kernel::ViewerEdge> edges;std::vector<kernel::Vec3> points;
    if(const auto& distance=measurement_dialog_->distance()){
        kernel::ViewerEdge edge;edge.points={distance->first,distance->second};edge.overlay=true;edges.push_back(edge);
        points={distance->first,distance->second};
    }
    viewer_->set_transient_edges(std::move(edges));viewer_->set_transient_points(std::move(points));
}
void AssemblyWorkspaceWindow::update_measurement_ui(){
    if(!measure_action_)return;
    const auto id=workspace_.displayed_document_id();
    const auto* part=workspace_.open_part(id);const auto* assembly=workspace_.open_assembly(id);
    measure_action_->setEnabled((part||assembly)&&(!properties_dialog_||measurement_dialog_)&&section_dialog_.isNull());
    if(!part&&!assembly)return;
    const auto& rows=part?part->session.document().measurements:assembly->session.document().measurements;
    auto* root=tree_->topLevelItem(0);if(!root)return;
    using Key=std::tuple<std::string,std::string,std::string>;std::set<Key> available;
    const auto add=[&](const auto& source){
        for(const auto& r:source.triangle_references)available.emplace(r.owner_id,r.semantic_key,r.instance_path);
        for(const auto& e:source.edges)available.emplace(e.reference.owner_id,e.reference.semantic_key,e.reference.instance_path);
        for(const auto& p:source.points)available.emplace(p.reference.owner_id,p.reference.semantic_key,p.reference.instance_path);
        for(const auto& a:source.axes)available.emplace(a.reference.owner_id,a.reference.semantic_key,a.reference.instance_path);
    };
    add(viewer_->mesh());add(viewer_->mesh().original_references);
    for(const auto& row:rows){
        auto* parent=root;QTreeWidgetItem* anchor{};
        for(QTreeWidgetItemIterator it(root);*it;++it){
            const auto key=(*it)->data(0,Qt::UserRole).toString().toStdString();
            if(!row.body_id.empty()&&key==row.body_id)parent=*it;
            if(!row.after_object_id.empty()&&key==row.after_object_id)anchor=*it;
        }
        if(anchor&&anchor->parent()==parent){}else anchor=nullptr;
        auto* item=new QTreeWidgetItem(QStringList{QString::fromStdString(row.name)});
        parent->insertChild(anchor?parent->indexOfChild(anchor)+1:parent->childCount(),item);
        item->setData(0,Qt::UserRole,QString::fromStdString(row.id));item->setData(0,Qt::UserRole+3,"document-measurement");
        item->setIcon(0,resource_icon("measure"));item->setFlags(item->flags()&~Qt::ItemIsUserCheckable);
        bool missing=false;
        for(const auto& ref:row.references){
            const bool found=ref.kind!=kernel::MeasurementKind::Object?available.contains({ref.owner_id,ref.semantic_key,ref.instance_path}):
                std::ranges::any_of(available,[&](const auto& key){return ref.owner_id.empty()?std::get<2>(key).starts_with(ref.instance_path):
                    (std::get<0>(key)==ref.owner_id||std::get<0>(key)==ref.owner_id+":entity")&&std::get<2>(key)==ref.instance_path;});
            missing|=!found;
        }
        item->setData(0,missing_reference_role,missing);
        if(missing){item->setForeground(0,QColor("#d85858"));item->setToolTip(0,tr("Měření má chybějící referenci. Otevřete vlastnosti a vyberte náhradu."));}
        else item->setToolTip(0,tr("Uložené měření. Vlastnosti znovu vyhodnotí reference v aktuálním modelu."));
    }
}
bool AssemblyWorkspaceWindow::measurement_context_menu(QTreeWidgetItem* item,const QPoint& position){
    if(!item||item->data(0,Qt::UserRole+3)!="document-measurement")return false;
    if(properties_dialog_)return true;
    const auto key=item->data(0,Qt::UserRole).toString().toStdString();QMenu menu(this);
    auto* edit=menu.addAction(tr("Vlastnosti…"));auto* remove=menu.addAction(tr("Odstranit"));
    remove->setEnabled(workspace_.active_document_id()==workspace_.displayed_document_id());
    const auto* selected=menu.exec(tree_->viewport()->mapToGlobal(position));
    if(selected==edit)show_measurement(key);
    else if(selected==remove){
        const auto id=workspace_.displayed_document_id();
        try {static_cast<void>(workspace::remove_measurement(workspace_,id,key));}
        catch(const std::exception& error){state_->setText(tr(error.what()));return true;}
        preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
    }
    return true;
}
} // namespace zima::app
