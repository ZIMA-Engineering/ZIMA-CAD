#include "assembly_workspace_window.hpp"
#include "symbol_properties_dialog.hpp"
#include "symbol_attachment_dialog.hpp"
#include "file_dialog.hpp"
#include <zima/symbols/definition.hpp>
#include <zima/workspace/symbol_operations.hpp>
#include <zima/workspace/measurement_operations.hpp>
#include <zima/document/file_path.hpp>
#include <zima/kernel/stable_id.hpp>
#include <zima/viewer/mesh_view.hpp>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QTreeWidgetItemIterator>
#include <QCursor>
#include <algorithm>
namespace zima::app {
const sketcher::Sketch* AssemblyWorkspaceWindow::symbol_document_sketch() const {
    const auto* part=workspace_.open_part(workspace_.active_document_id());
    if(!part||!part->symbol_definition)return nullptr;
    const auto& sketches=part->session.document().sketches;
    const auto found=std::ranges::find(sketches,active_sketch_id_,&sketcher::Sketch::id);
    return found!=sketches.end()?&*found:sketches.empty()?nullptr:&sketches.front();
}
void AssemblyWorkspaceWindow::save_symbol_document(bool copy) {
    const auto id=workspace_.active_document_id();auto* part=workspace_.open_part(id);
    if(!part||!part->symbol_definition)return;
    auto target=part->path;
    if(copy||target.empty()) {
        const auto selected=save_file(this,tr("Symbol"),QString::fromStdString(document::path_to_utf8(target)),
            tr("Symbol ZIMA-CAD (*.symz)"),"symz",application_settings_.translations);
        if(selected.isEmpty())return;target=std::filesystem::path(selected.toStdU16String());target.replace_extension(".symz");
    }
    try {workspace::save_symbol_document(workspace_,id,target,copy);refresh_tabs();}
    catch(const std::exception&){QMessageBox::warning(this,tr("Symbol"),tr("Uložení se nezdařilo"));}
}
void AssemblyWorkspaceWindow::initialize_symbol_handles() {
    viewer_->set_symbol_handle_callbacks([this]()->std::optional<viewer::SymbolHandles> {
        const auto owner=workspace_.active_document_id();
        if(!active_sketch_id_.empty()||(!workspace_.open_part(owner)&&!workspace_.open_assembly(owner)))return {};
        const symbols::Placement* value=nullptr;
        if(const auto* dialog=dynamic_cast<SymbolAttachmentDialog*>(properties_dialog_))value=&dialog->pending_placement();
        else if(!properties_dialog_) {
            const auto selected=viewer_->confirmed_candidate();
            if(!selected||selected->kind!=viewer::CandidateKind::Symbol||selected->instance_path!=workspace_.active_occurrence_path())return {};
            const auto& values=workspace::symbol_annotations(workspace_,owner);
            const auto found=std::ranges::find_if(values,[&](const auto& p){return p.symbol.id==selected->owner_id;});
            if(found!=values.end())value=&*found;
        }
        if(!value||!value->leader||!value->symbol.visible)return {};
        const auto to_scene=[&](kernel::Vec3 point) {
            const auto path=workspace_.active_occurrence_path();
            return path.empty()?point:workspace_.occurrence_point_to_scene(workspace_.displayed_document_id(),assembly::InstancePath::decode(path),point);
        };
        const auto contact=to_scene(value->frame.origin),grip=to_scene(value->frame.world({value->symbol.x,value->symbol.y,0}));
        const auto z=to_scene(value->frame.world({0,0,1}));
        return viewer::SymbolHandles{value->symbol.id,contact,grip,{z.x-contact.x,z.y-contact.y,z.z-contact.z}};
    },[this](const std::string& id,bool contact,kernel::Vec3 position) {
        if(!properties_dialog_)show_model_symbol_properties(id);
        auto* dialog=dynamic_cast<SymbolAttachmentDialog*>(properties_dialog_);if(!dialog)return;
        if(contact){dialog->begin_entry();return;}
        const auto path=workspace_.active_occurrence_path();
        if(!path.empty())position=workspace_.occurrence_point_from_scene(workspace_.displayed_document_id(),assembly::InstancePath::decode(path),position);
        const auto local=dialog->pending_placement().frame.local(position);dialog->set_anchor(local.x,local.y);
    });
}
void AssemblyWorkspaceWindow::start_symbol() {
    if(properties_dialog_||symbol_document_sketch())return;
    const auto path=open_file(this,tr("Vložit symbol"),application_settings_.resolved_paths.value("Symbols"),tr("Symbol ZIMA-CAD (*.symz)"),application_settings_.translations);
    if(path.isEmpty())return;
    try {
        const auto definition=symbols::Definition::load(std::filesystem::path(path.toStdU16String()));
        sketcher::SymbolInstance instance;instance.id=kernel::make_stable_id();instance.definition=definition.serialized();instance.variant=definition.default_variant;
        instance.use_cad_variant=!definition.variant_source.empty()&&template_sketch();
        if(active_sketch())show_symbol_properties({},std::move(instance));
        else {symbols::Placement placement;placement.symbol=std::move(instance);show_model_symbol_properties({},std::move(placement));}
    }catch(const std::exception&){QMessageBox::warning(this,tr("Symbol"),tr("Symbol nelze načíst. Zkontrolujte jeho definici."));}
}
void AssemblyWorkspaceWindow::show_symbol_properties(const std::string& id,std::optional<sketcher::SymbolInstance> initial) {
    if(!active_sketch()){show_model_symbol_properties(id);return;}
    const auto* sketch=active_sketch();if(!sketch||properties_dialog_)return;
    if(!initial){const auto found=std::ranges::find(sketch->symbols,id,&sketcher::SymbolInstance::id);if(found==sketch->symbols.end())return;initial=*found;}
    const auto owner=workspace_.active_document_id(),sketch_id=sketch->id;
    cancel_sketch_segment();clear_selected_sketch_geometry();viewer_->clear_selection();
    auto* dialog=new SymbolDialog(*initial,[this,owner,sketch_id,id](const auto& symbol){
        const auto* current=active_sketch();if(workspace_.active_document_id()!=owner||!current||current->id!=sketch_id)return;
        auto preview=*current;auto found=std::ranges::find(preview.symbols,id,&sketcher::SymbolInstance::id);
        if(found==preview.symbols.end())preview.symbols.push_back(symbol);else *found=symbol;
        show_sketch_drag_preview(preview);
    },[this,owner,sketch_id,id](auto symbol){
        if(workspace_.active_document_id()!=owner||active_sketch_id_!=sketch_id)throw std::runtime_error(QObject::tr("Skica již není aktivní.").toStdString());
        if(!mutate_active_sketch([&](auto& sketch){
            auto found=std::ranges::find(sketch.symbols,id,&sketcher::SymbolInstance::id);
            if(found==sketch.symbols.end())sketch.symbols.push_back(symbol);else *found=symbol;sketch.validate();
        }))throw std::runtime_error(QObject::tr("Skica již není aktivní.").toStdString());
    },this);
    properties_dialog_=dialog;symbol_anchor_=[dialog](double x,double y){dialog->set_anchor(x,y);};viewer_->set_selection_contract({});
    connect(dialog,&QDialog::finished,this,[this,dialog]{symbol_anchor_={};if(properties_dialog_==dialog)properties_dialog_=nullptr;preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();});dialog->show();
}
void AssemblyWorkspaceWindow::remove_symbol(const std::string& id) {
    if(properties_dialog_)return;
    if(!active_sketch()) {
        workspace::remove_symbol_annotation(workspace_,workspace_.active_document_id(),id);
        selected_symbol_.clear();viewer_->clear_selection();preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();return;
    }
    static_cast<void>(mutate_active_sketch([&](auto& sketch){std::erase_if(sketch.symbols,[&](const auto& symbol){return symbol.id==id;});}));
    selected_symbol_.clear();viewer_->clear_selection();preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
}
void AssemblyWorkspaceWindow::select_symbol(const std::string& id) {
    clear_selected_sketch_geometry();selected_symbol_=id;const QSignalBlocker blocked(tree_);tree_->clearSelection();
    QTreeWidgetItemIterator it(tree_);while(*it){auto* item=*it++;const auto kind=item->data(0,Qt::UserRole+3).toString();if((kind=="sketch-symbol"||kind=="model-symbol")&&item->data(0,Qt::UserRole).toString().toStdString()==id){item->setSelected(true);tree_->scrollToItem(item);break;}}
}
void AssemblyWorkspaceWindow::append_model_symbols_to_tree() {
    const auto id=workspace_.active_document_id();if(symbol_document_sketch()||(!workspace_.open_part(id)&&!workspace_.open_assembly(id)))return;
    const auto& values=workspace::symbol_annotations(workspace_,id);if(values.empty())return;
    QTreeWidgetItem* parent=tree_->topLevelItem(0);if(!parent)return;
    const auto path=workspace_.active_occurrence_path();
    if(!path.empty()) {
        const auto decoded=assembly::InstancePath::decode(path);QTreeWidgetItemIterator it(tree_);
        while(*it){auto* item=*it++;if(item->data(0,Qt::UserRole+1).toString().toStdString()==path&&
            item->data(0,Qt::UserRole).toString().toStdString()==decoded.occurrence_ids.back()){parent=item;break;}}
    }
    for(const auto& value:values){auto* item=new QTreeWidgetItem(parent,{QString::fromStdString(symbols::Definition::from_serialized(value.symbol.definition).name)});
        item->setIcon(0,symbol_action_->icon());item->setData(0,Qt::UserRole,QString::fromStdString(value.symbol.id));
        item->setData(0,Qt::UserRole+1,QString::fromStdString(path));item->setData(0,Qt::UserRole+3,"model-symbol");
        if(value.unresolved)item->setToolTip(0,tr("Reference není dostupná. Symbol zachovává poslední polohu."));}
}
void AssemblyWorkspaceWindow::show_model_symbol_properties(const std::string& id,std::optional<symbols::Placement> initial) {
    if(properties_dialog_||active_sketch()||symbol_document_sketch())return;
    const auto owner=workspace_.active_document_id(),path=workspace_.active_occurrence_path();
    if(!workspace_.open_part(owner)&&!workspace_.open_assembly(owner))return;
    if(!initial){const auto& values=workspace::symbol_annotations(workspace_,owner);
        const auto found=std::ranges::find_if(values,[&](const auto& value){return value.symbol.id==id;});if(found==values.end())return;initial=*found;}
    // Capture original analytic records once. Hover uses identity lookup, not
    // model traversal, tessellation or a second picker.
    using Key=std::tuple<std::string,std::string,std::string>;
    auto references=std::make_shared<std::map<Key,kernel::FaceReference>>();
    for(const auto& face:workspace::measurement_scene(workspace_,owner).original_references.triangle_references)
        if(face.valid()&&face.surface)references->try_emplace(Key{face.instance_path,face.owner_id,face.semantic_key},face);
    const auto source=[references,path](const viewer::ViewerCandidate& candidate)->std::optional<kernel::FaceReference>{
        if(candidate.kind!=viewer::CandidateKind::Face)return {};
        auto occurrence=assembly::InstancePath::decode(candidate.instance_path);const auto prefix=assembly::InstancePath::decode(path);
        if(occurrence.occurrence_ids.size()<prefix.occurrence_ids.size()||!std::equal(prefix.occurrence_ids.begin(),prefix.occurrence_ids.end(),occurrence.occurrence_ids.begin()))return {};
        occurrence.occurrence_ids.erase(occurrence.occurrence_ids.begin(),occurrence.occurrence_ids.begin()+prefix.occurrence_ids.size());
        const auto found=references->find({occurrence.encoded(),candidate.owner_id,candidate.semantic_key});return found==references->end()?std::nullopt:std::optional(found->second);
    };
    auto* dialog=new SymbolAttachmentDialog(*initial,[this,owner,path](auto value){
        if(workspace_.active_document_id()!=owner||workspace_.active_occurrence_path()!=path)throw std::runtime_error(tr("Dokument již není aktivní.").toStdString());
        workspace::store_symbol_annotation(workspace_,owner,value);
    },this);
    properties_dialog_=dialog;properties_dialog_instance_path_=path;
    dialog->changed=[this,dialog,owner,path,source]{
        const auto& value=dialog->pending_placement();
        auto preview=workspace_.open_part(owner)?workspace_.open_part(owner)->session.document().construction_viewer_mesh():workspace_.open_assembly(owner)->session.document().construction_viewer_mesh();
        std::erase_if(preview.edges,[&](const auto& edge){return edge.reference.owner_id==value.symbol.id;});
        auto symbol=value.viewer_mesh();preview.edges.insert(preview.edges.end(),symbol.edges.begin(),symbol.edges.end());
        construction_preview_mesh_=std::move(preview);preserve_view_on_refresh_=true;refresh_scene();
        viewer_->set_original_face_selection(true);viewer_->set_selection_contract({viewer::CandidateKind::Face});
        viewer_->set_candidate_filter([dialog,source](const auto& candidate){return dialog->active()&&source(candidate).has_value();});
        tree_->setProperty("commandSelectionActive",dialog->active());
        std::set<viewer::EdgeKey> highlights;
        if(dialog->inspected()&&value.reference){const auto& ref=*value.reference;
            auto full=assembly::InstancePath::decode(path);const auto local=assembly::InstancePath::decode(ref.instance_path);
            full.occurrence_ids.insert(full.occurrence_ids.end(),local.occurrence_ids.begin(),local.occurrence_ids.end());highlights.insert({ref.owner_id,ref.semantic_key,full.encoded()});}
        viewer_->set_constraint_reference_highlights({},std::move(highlights));
    };
    feature_reference_pick_=[this,dialog,source,owner,path](const auto& candidate){
        if(!dialog->active())return;const auto face=source(candidate);if(!face)return;
        const auto ray=viewer_->ray_at(viewer_->mapFromGlobal(QCursor::pos()));if(!ray)return;
        // The distance is from the already confirmed common-picker candidate.
        auto point=ray->first;point.x+=ray->second.x*candidate.distance;point.y+=ray->second.y*candidate.distance;point.z+=ray->second.z*candidate.distance;
        if(!path.empty())point=workspace_.occurrence_point_from_scene(workspace_.displayed_document_id(),assembly::InstancePath::decode(path),point);
        auto source_document=owner;if(!face->instance_path.empty())if(const auto address=workspace_.resolve_occurrence(owner,assembly::InstancePath::decode(face->instance_path)))source_document=address->source_document_id;
        try {dialog->set_surface(*face,source_document,point);}
        catch(const std::exception&){QMessageBox::warning(dialog,tr("Symbol"),tr("Symbol nelze připojit k vybrané geometrii."));}
    };
    feature_reference_end_=[dialog]{dialog->end_entry();};
    connect(dialog,&QDialog::finished,this,[this,dialog]{dialog->changed={};feature_reference_pick_={};feature_reference_end_={};
        properties_dialog_=nullptr;properties_dialog_instance_path_.clear();construction_preview_mesh_.reset();tree_->setProperty("commandSelectionActive",false);
        viewer_->set_candidate_filter({});viewer_->set_selection_contract({});viewer_->set_original_face_selection(false);viewer_->set_constraint_reference_highlights({},{});viewer_->clear_selection();
        preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();});
    dialog->show();dialog->changed();
}
}
