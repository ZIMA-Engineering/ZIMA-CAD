#include "assembly_workspace_window.hpp"
#include <zima/ui/numeric_value_lock.hpp>
#include <zima/viewer/mesh_view.hpp>
#include <QDialog>
#include <QLabel>
#include <zima/workspace/value_lock_operations.hpp>
namespace zima::app {
namespace {
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
        if(field_owner.toString().toStdString()==owner&&zima::workspace::value_lock_address({},field->property("zimaValueLockKey").toString().toStdString()).second==zima::workspace::value_lock_address({},key).second)return field;
    }
    return nullptr;
}
}
std::optional<bool> AssemblyWorkspaceWindow::parameter_value_locked(const std::string& source_owner,const std::string& source_key) {
    const auto [owner,key]=zima::workspace::value_lock_address(source_owner,represented_key(viewer_->mesh(),source_owner,source_key));
    if(!source_key.starts_with("parameter:")&&!source_key.starts_with("placement-reference:"))return {};
    if(auto* field=pending_field(properties_dialog_,owner,key))return field->property("zimaValueLocked").toBool();
    try {return zima::workspace::value_locked(workspace_,workspace_.active_document_id(),owner,key);}
    catch(const zima::workspace::ValueLockError&){}
    return {};
}
void AssemblyWorkspaceWindow::toggle_parameter_value_lock(const std::string& source_owner,const std::string& source_key) {
    const auto [owner,key]=zima::workspace::value_lock_address(source_owner,represented_key(viewer_->mesh(),source_owner,source_key));
    if(auto* field=pending_field(properties_dialog_,owner,key)) {
        if(auto* action=field->findChild<QAction*>("valueLock:"+field->property("zimaValueLockKey").toString()))action->trigger();
        viewer_->update();return;
    }
    if(properties_dialog_)return;
    try {
        const auto current=zima::workspace::value_locked(workspace_,workspace_.active_document_id(),owner,key);
        if(!current)return;
        static_cast<void>(zima::workspace::set_value_lock(workspace_,workspace_.active_document_id(),owner,key,!*current));
        if(const auto* part=workspace_.open_part(workspace_.active_document_id())) {
            undo_action_->setEnabled(part->session.can_undo());redo_action_->setEnabled(part->session.can_redo());
        }else if(const auto* assembly=workspace_.open_assembly(workspace_.active_document_id())) {
            undo_action_->setEnabled(assembly->session.can_undo());redo_action_->setEnabled(assembly->session.can_redo());
        }
    }catch(const zima::workspace::ValueLockError& error){state_->setText(tr(error.what()));return;}
    refresh_tabs();viewer_->update();
}
} // namespace zima::app
