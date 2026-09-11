#include "workspace_internal.hpp"
#include <zima/command_host/host.hpp>
#include "command_console.hpp"
#include <QDockWidget>
#include <QCursor>

namespace zima::app {
using namespace workspace_detail;
using commands::Json;
using commands::Result;
namespace {
QString file_name(const std::filesystem::path& path){
    const auto name=path.filename().u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(name.data()),static_cast<qsizetype>(name.size()));
}
const char* candidate_kind(viewer::CandidateKind kind) {
    using Kind=viewer::CandidateKind;
    switch(kind) {
        case Kind::Occurrence:return "occurrence";
        case Kind::Container:return "container";
        case Kind::Plane:return "plane";
        case Kind::Face:return "face";
        case Kind::Edge:return "edge";
        case Kind::Vertex:return "vertex";
        case Kind::Axis:return "axis";
        case Kind::SketchAxis:return "sketch_axis";
        case Kind::SketchSegment:return "sketch_segment";
        case Kind::SketchPoint:return "sketch_point";
        case Kind::Dimension:return "dimension";
        case Kind::SketchConstraint:return "sketch_constraint";
        case Kind::SketchCurve:return "sketch_curve";
        case Kind::SketchText:return "sketch_text";
        case Kind::SketchExternalReference:return "sketch_external_reference";
        case Kind::SketchTrimPiece:return "sketch_trim_piece";
        case Kind::TemplateRegion:return "template_region";
        case Kind::TemplateImage:return "template_image";
    }
    return "unknown";
}
}
void AssemblyWorkspaceWindow::report_operation_error(const QString& title,const QString& message) {
    if(console_executing_)console_operation_error_=title+QStringLiteral(": ")+message;
    else QMessageBox::critical(this,title,message);
}
Result AssemblyWorkspaceWindow::execute_console_command(const QString& text) {
    if(console_executing_)return Result::failure("busy",tr("Jiný příkaz právě probíhá.").toStdString());
    const QScopedValueRollback running(console_executing_,true);
    console_operation_error_.clear();console_status_operation_=false;
    auto result=Result::success();
    try {
        result=command_host_->execute_text(text.toStdString());
        if(const auto& change=command_host_->change())apply_console_change(*change);
        if(!console_operation_error_.isEmpty())result=Result::failure("operation_failed",console_operation_error_.toStdString());
    }catch(const std::exception& error){result=Result::failure("operation_failed",error.what());}
    if(console_status_operation_){
        auto message=QString::fromStdString(result.message);
        if(result.ok)if(const auto& change=command_host_->change()){
            const auto* state=workspace_.find(change->document_id);
            const auto name=std::visit([](const auto& value){return file_name(value.path);},*state);
            if(change->kind==command_host::ChangeKind::Copy)
                message=tr("Kopie uložena: %1").arg(QString::fromStdString(result.data.at("paths").at(0).get<std::string>()));
            else message=(change->kind==command_host::ChangeKind::Open?tr("Otevřeno: %1")
                :workspace_.open_assembly(change->document_id)?tr("Sestava uložena: %1")
                :workspace_.open_drawing(change->document_id)?tr("Výkres uložen: %1")
                :tr("Part uložen: %1")).arg(name);
        }
        finish_status_operation(message,result.ok);
    }
    return result;
}
void AssemblyWorkspaceWindow::apply_console_change(const command_host::Change& change){
    using Kind=command_host::ChangeKind;
    if(change.clear_selection && viewer_)viewer_->clear_selection();
    if(change.kind==Kind::Directory) {refresh_delete_file_actions();return;}
    if(change.kind==Kind::Copy) {refresh_delete_file_actions();return;}
    if(change.kind==Kind::Open||change.kind==Kind::New||change.kind==Kind::Activate||change.kind==Kind::Close){
        if(change.kind!=Kind::Open){
            if(workspace_.open_part(change.document_id))active_application_=ApplicationMode::Modeling;
            else if(workspace_.open_assembly(change.document_id))active_application_=ApplicationMode::Assembly;
            else active_application_=ApplicationMode::Drawing;
        }
        finish_document_switch(change.kind==Kind::Open);
    }else if(change.kind==Kind::Save){
        if(workspace_.open_drawing(change.document_id))drawing_workspace_->edit_workspace_document(change.document_id);
        refresh_tabs();
    }else{
        if(change.kind==Kind::History)cancel_sketch_segment();
        if(change.kind==Kind::Regenerate||change.kind==Kind::Model)preserve_view_on_refresh_=true;
        refresh_scene();refresh_tabs();
    }
}
void AssemblyWorkspaceWindow::create_command_console() {
    command_host::Options options;
    options.translate=[this](const char* text){return tr(text).toStdString();};
    options.settings=[this]{
        command_host::Settings result;result.templates=native_template_settings(application_settings_);
        for(auto it=application_settings_.units.cbegin();it!=application_settings_.units.cend();++it)
            result.units[it.key().toStdString()]=it.value().toStdString();
        return result;
    };
    options.interaction=[this]{
        command_host::Interaction state;
        state.editing=QApplication::activeModalWidget()!=nullptr||properties_dialog_||!active_sketch_id_.empty();
        for(auto* dialog:findChildren<QDialog*>())state.editing|=dialog->isVisible();
        state.editing|=tree_&&tree_->property("commandSelectionActive").toBool();
        state.template_document=template_sketch()!=nullptr;
        state.active_occurrence=active_occurrence_path_;state.active_sketch=active_sketch_id_;
        const auto candidate_json=[](const viewer::ViewerCandidate& candidate) -> Json {
            return {{"owner_id",candidate.owner_id},{"semantic_key",candidate.semantic_key},
                {"instance_path",candidate.instance_path},{"kind",candidate_kind(candidate.kind)},
                {"geometry",candidate.geometry==viewer::CandidateGeometry::OriginalReference?"original_reference":"display"}};
        };
        if(viewer_ && viewer_->isVisible() && !workspace_.open_drawing(workspace_.displayed_document_id())) {
            if(const auto& candidate=viewer_->confirmed_candidate())state.selection=candidate_json(*candidate);
            state.camera=viewer_->camera_state();
            const auto global=QCursor::pos();const auto position=viewer_->mapFromGlobal(global);
            // An editor or dialog over the View is not a geometry target.
            const bool inside=viewer_->rect().contains(position) && QApplication::widgetAt(global)==viewer_;
            state.pointer={{"inside_view",inside},{"coordinate_system","view_logical_pixels"},
                {"viewport_width",viewer_->width()},{"viewport_height",viewer_->height()},
                {"x",nullptr},{"y",nullptr},{"ray",nullptr}};
            if(inside) {
                state.pointer["x"]=position.x();state.pointer["y"]=position.y();
                if(const auto ray=viewer_->ray_at(position)) {
                    const auto vector=[](const kernel::Vec3& v){return Json::array({v.x,v.y,v.z});};
                    state.pointer["ray"]={{"origin",vector(ray->first)},{"direction",vector(ray->second)}};
                }
                // Consume the offered candidate; never run another picker.
                if(viewer_->last_pointer_position()==position)
                    if(const auto& hovered=viewer_->hovered_candidate())state.hover=candidate_json(*hovered);
            }
        }
        return state;
    };
    options.run_io=[](std::function<void()> task){run_background_task(std::move(task));};
    options.progress=[this](command_host::Activity activity,const std::filesystem::path& path){
        console_status_operation_=true;
        const auto name=file_name(path);
        const auto text=activity==command_host::Activity::Read?tr("Otevírám %1…").arg(name)
            : workspace_.open_assembly(workspace_.active_document_id())?tr("Ukládám sestavu %1…").arg(name)
            : workspace_.open_drawing(workspace_.active_document_id())?tr("Ukládám výkres %1…").arg(name)
            : tr("Ukládám Part %1…").arg(name);
        begin_status_operation(text);update_status_operation(text,-1,0);
    };
    options.fit=[this]{viewer_->fit_all();};
    command_host_=std::make_unique<command_host::Host>(workspace_,kernel_,working_directory_,std::move(options));
    console_dock_=new QDockWidget(tr("Konzole CADu"),this);
    console_dock_->setObjectName("commandConsoleDock");
    console_dock_->setFeatures(QDockWidget::DockWidgetClosable);
    console_dock_->setAllowedAreas(Qt::BottomDockWidgetArea);
    console_=new CommandConsole([this](const QString& text){return execute_console_command(text);},console_dock_);
    console_dock_->setWidget(console_);addDockWidget(Qt::BottomDockWidgetArea,console_dock_);console_dock_->hide();
    auto* toggle=console_dock_->toggleViewAction();toggle->setObjectName("showCommandConsoleAction");
    toggle->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    if(auto* menu=findChild<QMenu*>("viewMenu")){menu->addSeparator();menu->addAction(toggle);}
    connect(console_dock_,&QDockWidget::visibilityChanged,this,[this](bool visible){if(visible)console_->focus_input();});
}
} // namespace zima::app
