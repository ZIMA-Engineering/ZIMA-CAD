#include "assembly_workspace_window.hpp"
#include <zima/ui/numeric_value_lock.hpp>
#include <zima/viewer/mesh_view.hpp>
#include <QDialog>
#include <QLabel>
#include <QPointer>
#include <algorithm>
#include <type_traits>
namespace zima::app {
namespace {
std::pair<std::string,std::string> parameter_address(std::string owner,std::string key) {
    if(key.starts_with("placement-reference:")) {
        const auto separator=key.rfind(':');if(separator>20 && separator!=std::string::npos) {
            owner=key.substr(20,separator-20);key="parameter:placement:reference_offset:"+key.substr(separator+1);
        }
    }
    return {owner,key};
}
std::string normalized_key(std::string key) {
    if(key.starts_with("parameter:"))key.erase(0,10);
    if(key=="size")key="primary";
    if(key=="included_angle")key="angle";
    if(key=="thread_nominal_diameter")key="thread_diameter";
    if(key=="x"||key=="y"||key=="z"||key.starts_with("rotation_")||key.starts_with("reference_offset:"))key="placement:"+key;
    return key;
}
template<bool Const> struct LockTarget {
    using Set=std::conditional_t<Const,const std::set<std::string>,std::set<std::string>>;
    using Flag=std::conditional_t<Const,const bool,bool>;
    Set* values{};Flag* flag{};std::string key;
    bool get() const{return flag?*flag:values->contains(key);}
    void set(bool value) const requires(!Const) {if(flag)*flag=value;else if(value)values->insert(key);else values->erase(key);}
};
template<class Document> auto lock_target(Document& document,const std::string& owner,std::string key) {
    using Target=LockTarget<std::is_const_v<Document>>;std::optional<Target> result;key=normalized_key(std::move(key));
    const auto references=[&](auto& refs)->bool {
        const std::string prefix="placement:reference_offset:";if(!key.starts_with(prefix))return false;
        std::size_t index=0;const auto suffix=key.substr(prefix.size());if(suffix.empty())return true;
        for(char digit:suffix){if(digit<'0'||digit>'9')return true;index=index*10+digit-'0';}
        for(auto& ref:refs){
            if constexpr(requires{ref.orientation_only;})if(ref.orientation_only||ref.owner_id.empty())continue;
            if(index--==0){result=Target{nullptr,&ref.offset_locked,{}};break;}
        }
        return true;
    };
    const auto construction=[&](auto&& self,auto& object)->void {
        if(object.id==owner){if(!references(object.references))result=Target{&object.value_locks,nullptr,key};return;}
        for(auto& child:object.curve_points)self(self,child);
    };
    const auto feature=[&](auto& object){
        if(object.id==owner){
            if(references(object.placement.references))return;
            if(key.starts_with("placement:"))result=Target{&object.placement.value_locks,nullptr,key.substr(10)};
            else if(object.feature_kind==zima::document::FeatureKind::Sketch && key=="profile_offset")result=Target{&object.placement.value_locks,nullptr,key};
            else result=Target{&object.value_locks,nullptr,key};
        }
        if(object.feature_kind==zima::document::FeatureKind::Sweep3D)construction(construction,object.sweep3d.path);
    };
    for(auto& object:document.constructions)construction(construction,object);
    if constexpr(requires{document.history;})for(auto& object:document.history)feature(object);
    if constexpr(requires{document.cuts;})for(auto& cut:document.cuts)feature(cut.definition);
    if constexpr(requires{document.components;})for(auto& component:document.components)
        if(component.occurrence_id==owner){if(!references(component.placement_references))result=Target{&component.value_locks,nullptr,key};}
    if constexpr(std::is_const_v<Document> && requires{document.body_history;}) {
        if(const auto* body=document.body_history.find(owner)) {
            if(!references(body->scope.placement.references) && key.starts_with("placement:"))
                result=Target{&body->scope.placement.value_locks,nullptr,key.substr(10)};
        }
    }
    return result;
}
std::string represented_key(const zima::kernel::ViewerMesh& mesh,const std::string& owner,const std::string& key) {
    if(!key.starts_with("parameter:placement:rotation_"))return key;
    for(const auto& dimension:mesh.dimensions)
        if(dimension.reference.owner_id==owner && dimension.reference.semantic_key==key && !dimension.value_lock_key.empty())
            return "parameter:"+dimension.value_lock_key;
    return key;
}
QDoubleSpinBox* pending_field(QDialog* dialog,const std::string& owner,const std::string& key) {
    if(!dialog)return nullptr;
    for(auto* field:dialog->findChildren<QDoubleSpinBox*>()) {
        auto field_owner=field->property("zimaValueLockOwner");
        if(!field_owner.isValid())field_owner=dialog->property("zimaValueLockOwner");
        if(field_owner.toString().toStdString()==owner&&normalized_key(field->property("zimaValueLockKey").toString().toStdString())==normalized_key(key))return field;
    }
    return nullptr;
}
}
std::optional<bool> AssemblyWorkspaceWindow::parameter_value_locked(const std::string& source_owner,const std::string& source_key) {
    const auto [owner,key]=parameter_address(source_owner,represented_key(viewer_->mesh(),source_owner,source_key));
    if(!key.starts_with("parameter:"))return {};
    if(auto* field=pending_field(properties_dialog_,owner,key))return field->property("zimaValueLocked").toBool();
    if(const auto* part=workspace_.open_part(workspace_.active_document_id())){
        if(const auto target=lock_target(part->session.document(),owner,key))return target->get();
    } else if(const auto* assembly=workspace_.open_assembly(workspace_.active_document_id())) {
        if(const auto target=lock_target(assembly->session.document(),owner,key))return target->get();
    }
    return {};
}
void AssemblyWorkspaceWindow::toggle_parameter_value_lock(const std::string& source_owner,const std::string& source_key) {
    const auto [owner,key]=parameter_address(source_owner,represented_key(viewer_->mesh(),source_owner,source_key));
    if(auto* field=pending_field(properties_dialog_,owner,key)) {
        if(auto* action=field->findChild<QAction*>("valueLock:"+field->property("zimaValueLockKey").toString()))action->trigger();
        viewer_->update();return;
    }
    if(properties_dialog_)return;
    if(auto* part=workspace_.open_part(workspace_.active_document_id())) {
        auto next=part->session.document();
        if(const auto* current=next.body_history.find(owner)) {
            auto body=*current;const auto normalized=normalized_key(key);
            if(!normalized.starts_with("placement:"))return;
            const auto suffix=normalized.substr(10);
            if(suffix.starts_with("reference_offset:")) {
                std::size_t index=0;try{index=std::stoul(suffix.substr(17));}catch(...){return;}
                bool found=false;for(auto& reference:body.scope.placement.references)if(!reference.orientation_only&&!reference.owner_id.empty()) {
                    if(index--==0){reference.offset_locked=!reference.offset_locked;found=true;break;}
                }
                if(!found)return;
            } else {
                auto& locks=body.scope.placement.value_locks;if(locks.contains(suffix))locks.erase(suffix);else locks.insert(suffix);
            }
            next.body_history.update_body(std::move(body));
        } else {
            const auto target=lock_target(next,owner,key);if(!target)return;target->set(!target->get());
        }
        part->session.commit(std::move(next),part->session.calculated_boundaries());
        undo_action_->setEnabled(part->session.can_undo());redo_action_->setEnabled(part->session.can_redo());
    } else if(auto* assembly=workspace_.open_assembly(workspace_.active_document_id())) {
        auto next=assembly->session.document();const auto target=lock_target(next,owner,key);if(!target)return;
        target->set(!target->get());assembly->session.commit(std::move(next));
        undo_action_->setEnabled(assembly->session.can_undo());redo_action_->setEnabled(assembly->session.can_redo());
    }
    refresh_tabs();viewer_->update();
}
} // namespace zima::app
