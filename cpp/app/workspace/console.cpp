#include "workspace_internal.hpp"
#include "command_console.hpp"
#include <QDockWidget>
#include <QDateTime>
#include <QCursor>
#include <QTreeWidgetItemIterator>

namespace zima::app {
using commands::Json;
using commands::Result;
namespace {
std::string path_text(const std::filesystem::path& path) {
    const auto utf8=path.generic_u8string();return {utf8.begin(),utf8.end()};
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
Json document_list(const workspace::Workspace& workspace) {
    Json result=Json::array();
    for(const auto& state:workspace.documents())std::visit([&](const auto& value) {
        using State=std::decay_t<decltype(value)>;
        const auto& document=[&]() -> const auto& {
            if constexpr(std::is_same_v<State,workspace::DrawingState>)return value.document;
            else return value.session.document();
        }();
        Json row={{"id",document.document_id},{"name",document.name},{"path",path_text(value.path)},
            {"active",document.document_id==workspace.active_document_id()},
            {"displayed",document.document_id==workspace.displayed_document_id()}};
        if constexpr(std::is_same_v<State,workspace::DrawingState>) {
            row["type"]="drawing";row["dirty"]=nullptr;
        } else {
            row["type"]=std::is_same_v<State,workspace::PartState>?"part":"assembly";
            row["dirty"]=value.session.is_dirty();row["revision"]=value.session.revision();
        }
        result.push_back(std::move(row));
    },state);
    return result;
}
}
void AssemblyWorkspaceWindow::report_operation_error(const QString& title,const QString& message) {
    if(console_executing_)console_operation_error_=title+QStringLiteral(": ")+message;
    else QMessageBox::critical(this,title,message);
}
Result AssemblyWorkspaceWindow::execute_console_command(const QString& text) {
    if(console_executing_)return Result::failure("busy",tr("Jiný příkaz právě probíhá.").toStdString());
    const QScopedValueRollback running(console_executing_,true);
    console_operation_error_.clear();
    auto result=command_dispatcher_.execute_text(text.toStdString());
    if(!console_operation_error_.isEmpty())return Result::failure("operation_failed",console_operation_error_.toStdString());
    return result;
}
void AssemblyWorkspaceWindow::create_command_console() {
    command_dispatcher_.set_guard([this](const commands::Command& command) {
        if(!command.changes_state)return Result::success();
        bool dialog_open=QApplication::activeModalWidget()!=nullptr;
        for(auto* dialog:findChildren<QDialog*>())dialog_open|=dialog->isVisible();
        if(dialog_open || properties_dialog_ || !active_sketch_id_.empty() ||
            (tree_ && tree_->property("commandSelectionActive").toBool()))
            return Result::failure("editing_in_progress",tr("Nejprve dokončete nebo zrušte otevřenou editaci.").toStdString());
        if(workspace_.active_document_id()!=workspace_.displayed_document_id())
            return Result::failure("active_occurrence",tr("Nejprve ukončete aktivaci komponenty v sestavě.").toStdString());
        return Result::success();
    });
    const auto target=[this](const Json& args) {
        if(workspace_.active_document_id().empty())return Result::failure("no_document",tr("Není otevřený dokument.").toStdString());
        if(args.contains("document") && args["document"]!=workspace_.active_document_id())
            return Result::failure("document_changed",tr("Požadovaný dokument není aktivní.").toStdString());
        return Result::success();
    };
    command_dispatcher_.add({"help",tr("Seznam příkazů a jejich argumentů.").toStdString(),{},false},
        [this](const Json&){return Result::success(command_dispatcher_.catalog());});
    command_dispatcher_.add({"documents",tr("Otevřené dokumenty a jejich identifikátory.").toStdString(),{},false},
        [this](const Json&){return Result::success(document_list(workspace_));});
    command_dispatcher_.add({"context",tr("Aktivní dokument, výskyt a potvrzený výběr.").toStdString(),{},false},
        [this](const Json&) {
            Json result={{"active_document",workspace_.active_document_id()},
                {"displayed_document",workspace_.displayed_document_id()},
                {"active_occurrence",active_occurrence_path_},{"active_sketch",active_sketch_id_},
                {"working_directory",path_text(working_directory_)},{"selection",nullptr},
                {"captured_at_unix_ms",QDateTime::currentMSecsSinceEpoch()},
                {"hover",nullptr},{"camera",nullptr},{"pointer",{{"inside_view",false}}}};
            const auto candidate_json=[](const viewer::ViewerCandidate& candidate) -> Json {
                return {{"owner_id",candidate.owner_id},{"semantic_key",candidate.semantic_key},
                    {"instance_path",candidate.instance_path},{"kind",candidate_kind(candidate.kind)},
                    {"geometry",candidate.geometry==viewer::CandidateGeometry::OriginalReference?"original_reference":"display"}};
            };
            if(viewer_ && viewer_->isVisible() && !workspace_.open_drawing(workspace_.displayed_document_id())) {
                if(const auto& candidate=viewer_->confirmed_candidate())result["selection"]=candidate_json(*candidate);
                result["camera"]=viewer_->camera_state();
                const auto global=QCursor::pos();const auto position=viewer_->mapFromGlobal(global);
                // An editor or dialog over the View is not a geometry target.
                const bool inside=viewer_->rect().contains(position) && QApplication::widgetAt(global)==viewer_;
                result["pointer"]={{"inside_view",inside},{"coordinate_system","view_logical_pixels"},
                    {"viewport_width",viewer_->width()},{"viewport_height",viewer_->height()},
                    {"x",nullptr},{"y",nullptr},{"ray",nullptr}};
                if(inside) {
                    result["pointer"]["x"]=position.x();result["pointer"]["y"]=position.y();
                    if(const auto ray=viewer_->ray_at(position)) {
                        const auto vector=[](const kernel::Vec3& v){return Json::array({v.x,v.y,v.z});};
                        result["pointer"]["ray"]={{"origin",vector(ray->first)},{"direction",vector(ray->second)}};
                    }
                    // Consume the offered candidate; never run another picker.
                    if(viewer_->last_pointer_position()==position)
                        if(const auto& hovered=viewer_->hovered_candidate())result["hover"]=candidate_json(*hovered);
                }
            }
            return Result::success(std::move(result));
        });
    command_dispatcher_.add({"tree",tr("Strom zobrazeného dokumentu bez výpočtu geometrie.").toStdString(),{},false},
        [this](const Json&) {
            Json rows=Json::array();bool truncated=false;
            for(QTreeWidgetItemIterator it(tree_);*it;++it) {
                if(rows.size()==2000){truncated=true;break;}
                auto* item=*it;int depth=0;for(auto* p=item->parent();p;p=p->parent())++depth;
                rows.push_back({{"depth",depth},{"label",item->text(0).toStdString()},
                    {"id",item->data(0,Qt::UserRole).toString().toStdString()},
                    {"instance_path",item->data(0,Qt::UserRole+1).toString().toStdString()},
                    {"type",item->data(0,Qt::UserRole+3).toString().toStdString()},
                    {"semantic_key",item->data(0,Qt::UserRole+5).toString().toStdString()}});
            }
            return Result::success({{"document",workspace_.displayed_document_id()},{"items",rows},{"truncated",truncated}});
        });
    command_dispatcher_.add({"open",tr("Otevřít nativní dokument: open cesta.").toStdString(),{{"path",true}},true},
        [this](const Json& args) {
            const auto path=QString::fromStdString(args["path"].get<std::string>());
            const auto suffix=QFileInfo(path).suffix().toLower();
            if(suffix!="prtz" && suffix!="asmz" && suffix!="drwz")
                return Result::failure("unsupported_format",tr("Konzole otevírá soubory prtz, asmz a drwz.").toStdString());
            const auto absolute=QDir(QString::fromStdString(working_directory_.string())).absoluteFilePath(path);
            if(!open_document_path(absolute))return Result::failure("open_failed",console_operation_error_.toStdString());
            return Result::success(document_list(workspace_));
        });
    command_dispatcher_.add({"new",tr("Nový dokument: new part|assembly|drawing název.").toStdString(),{{"type",true},{"name",true}},true},
        [this](const Json& args) {
            const auto kind=args["type"].get<std::string>();
            if(kind!="part" && kind!="assembly" && kind!="drawing")return Result::failure("invalid_arguments","Expected part, assembly or drawing.");
            const auto name=QString::fromStdString(args["name"].get<std::string>()).trimmed();
            if(name.isEmpty() || name=="." || name==".." || name.contains(QRegularExpression(R"([\\/:*?"<>|])")) || name.endsWith('.'))
                return Result::failure("invalid_name",tr("Zadejte název bez cesty a přípony.").toStdString());
            const auto error=create_document(QString::fromStdString(kind),name);
            if(!error.isEmpty())return Result::failure("create_failed",error.toStdString());
            return Result::success(document_list(workspace_));
        });
    command_dispatcher_.add({"save",tr("Uložit aktivní dokument; volitelně ověřit jeho ID.").toStdString(),{{"document",false}},true},
        [this,target](const Json& args) {
            auto check=target(args);if(!check.ok)return check;
            const auto* state=workspace_.find(workspace_.active_document_id());
            if(!state || std::visit([](const auto& value){return value.path.empty();},*state) || template_sketch())
                return Result::failure("path_required",tr("Nejprve určete cestu dokumentu pomocí Uložit jako.").toStdString());
            save_active_document();
            return Result::success(document_list(workspace_));
        });
    command_dispatcher_.add({"regenerate",tr("Výslovně regenerovat aktivní model.").toStdString(),{{"document",false}},true},
        [this,target](const Json& args) {
            auto check=target(args);if(!check.ok)return check;
            if(workspace_.open_drawing(workspace_.active_document_id()) || template_sketch())
                return Result::failure("unsupported_document",tr("Tento příkaz podporuje Part a Assembly.").toStdString());
            regenerate_active_document();
            if(!console_operation_error_.isEmpty())return Result::failure("calculation_failed",console_operation_error_.toStdString());
            if(const auto* part=workspace_.open_part(workspace_.active_document_id()))
                if(!part->session.calculated_boundaries().empty()) {
                    const auto& errors=part->session.calculated_boundaries().back().calculation_errors;
                    if(!errors.empty())return Result{false,"calculation_errors",tr("Některé prvky nebyly vypočteny.").toStdString(),errors};
                }
            return Result::success(document_list(workspace_));
        });
    for(const bool redo_command:{false,true})command_dispatcher_.add(
        {redo_command?"redo":"undo",redo_command?tr("Znovu provést změnu.").toStdString():tr("Vrátit změnu.").toStdString(),{{"document",false}},true},
        [this,target,redo_command](const Json& args) {
            auto check=target(args);if(!check.ok)return check;
            bool available=false;
            if(const auto* part=workspace_.open_part(workspace_.active_document_id()))available=redo_command?part->session.can_redo():part->session.can_undo();
            if(const auto* assembly=workspace_.open_assembly(workspace_.active_document_id()))available=redo_command?assembly->session.can_redo():assembly->session.can_undo();
            if(!available)return Result::failure("nothing_to_undo_redo",tr("Žádná změna není k dispozici.").toStdString());
            if(redo_command)redo();else undo();
            return Result::success(document_list(workspace_));
        });
    command_dispatcher_.add({"fit",tr("Přizpůsobit model velikosti pohledu.").toStdString(),{},true},
        [this,target](const Json& args) {
            auto check=target(args);if(!check.ok)return check;
            if(workspace_.open_drawing(workspace_.displayed_document_id()))return Result::failure("unsupported_document",tr("Tento příkaz podporuje Part a Assembly.").toStdString());
            viewer_->fit_all();return Result::success();
        });
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
