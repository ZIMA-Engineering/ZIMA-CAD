#include <zima/command_host/host.hpp>
#include <zima/workspace/drawing_dimension_operations.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <zima/document/dimension_layout_json.hpp>
#include <algorithm>
namespace zima::command_host {
namespace {
Json reference(const kernel::EdgeReference& r) {
    return {{"owner",r.owner_id},{"key",r.semantic_key},{"instance_path",r.instance_path}};
}
Json dimension_json(const drawing::DrawingSheet& sheet,const drawing::DrawingDimension& d,bool detail) {
    constexpr std::array kinds{"linear","radius","diameter","chain","angular"};
    const auto view=std::ranges::find(sheet.views,d.view_id,&drawing::DrawingView::id);
    const auto evaluation=view==sheet.views.end()?drawing::evaluate_drawing_dimension(drawing::DrawingView{},d):drawing::evaluate_drawing_dimension(*view,d);
    Json measured=Json::array();
    for(std::size_t i=0;i<evaluation.presentations.size();++i) {
        const auto index=evaluation.state==drawing::MeasurementState::Unresolved?evaluation.cached_segment_indices.at(i):i;
        const auto& value=evaluation.presentations[i];
        measured.push_back({{"segment",d.segments.at(index).id},{"value",value.value},{"unit",value.unit_suffix},
            {"text",drawing::drawing_dimension_text(d,value,evaluation.state==drawing::MeasurementState::Unresolved)},
            {"last_valid",evaluation.state==drawing::MeasurementState::Unresolved},
            {"angular_leaders",i<evaluation.angular_leaders.size()&&evaluation.angular_leaders[i]}});
    }
    Json result={{"dimension",d.id},{"sheet",sheet.id},{"view",d.view_id},{"kind",kinds.at(static_cast<std::size_t>(d.kind))},
        {"state",evaluation.state==drawing::MeasurementState::Resolved?"resolved":evaluation.state==drawing::MeasurementState::Hidden?"hidden":"unresolved"},
        {"measurements",std::move(measured)}};
    if(!detail)return result;
    constexpr std::array directions{"automatic","horizontal","vertical","parallel"};
    constexpr std::array attachments{"point","curve_point","line","center","tangent","intersection"};
    result["direction"]=directions.at(static_cast<std::size_t>(d.direction));result["anchor_attachment"]=d.anchor_attachment;
    result["parallel_reference"]=reference(d.parallel_reference);result["style"]=document::dimension_text_style_json(d.style);
    result["attachments"]=Json::array();result["segments"]=Json::array();
    result["resolved_attachments"]=evaluation.resolved_attachments;result["direction_resolved"]=evaluation.direction_resolved;
    for(const auto& a:d.attachments)result["attachments"].push_back({{"kind",attachments.at(static_cast<std::size_t>(a.kind))},
        {"reference",reference(a.reference)},{"other_reference",reference(a.other_reference)},{"parameter",a.parameter},{"side",a.side}});
    for(const auto& segment:d.segments)result["segments"].push_back({{"segment",segment.id},{"layout",document::dimension_layout_json(segment.layout)},
        {"last_presentation",segment.last_presentation?document::dimension_geometry_json(*segment.last_presentation):Json(nullptr)},
        {"last_angular_leaders",segment.last_angular_leaders}});
    return result;
}
}
void Host::register_drawing_dimension_commands() {
    using Type=commands::ArgumentType;
    dispatcher_.add({"drawing.dimension.list",tr("List measured drawing dimensions and their resolved or last valid values."),{{"view",false},{"sheet",false},{"limit",false,Type::Integer},{"document",false}},false},[this](const Json& args){
        const auto id=args.value("document",workspace_.active_document_id());const auto* state=workspace_.open_drawing(id);
        if(!state)return Result::failure("unsupported_document",tr("Drawing commands require an open Drawing."));
        const auto view=args.value("view",std::string{}),sheet=args.value("sheet",std::string{});const auto limit=args.value("limit",2000.0);
        if(limit<1||limit>10000)return Result::failure("invalid_arguments",tr("Drawing query limit must be from 1 to 10000."));
        if(!view.empty()&&!state->document().find_view(view))return Result::failure("view_not_found",tr("The drawing view does not exist."));
        if(!sheet.empty()&&!state->document().find_sheet(sheet))return Result::failure("sheet_not_found",tr("The drawing sheet does not exist."));
        Json rows=Json::array();std::size_t total=0;
        for(const auto& s:state->document().sheets)if(sheet.empty()||sheet==s.id)for(const auto& d:s.dimensions)if(view.empty()||view==d.view_id) {
            ++total;if(rows.size()<static_cast<std::size_t>(limit))rows.push_back(dimension_json(s,d,false));
        }
        return Result::success({{"document",id},{"revision",state->revision()},{"items",std::move(rows)},{"total",total}});
    });
    dispatcher_.add({"drawing.dimension.get",tr("Read measured dimension references, style, layout and reference validity."),{{"dimension",true},{"document",false}},false},[this](const Json& args){
        const auto id=args.value("document",workspace_.active_document_id());const auto* state=workspace_.open_drawing(id);
        if(!state)return Result::failure("unsupported_document",tr("Drawing commands require an open Drawing."));
        for(const auto& sheet:state->document().sheets)for(const auto& d:sheet.dimensions)if(d.id==args.at("dimension").get<std::string>()) {
            auto result=dimension_json(sheet,d,true);result["document"]=id;result["revision"]=state->revision();return Result::success(std::move(result));
        }
        return Result::failure("dimension_not_found",tr("The measured drawing dimension does not exist."));
    });
    dispatcher_.add({"drawing.dimension.delete",tr("Delete a measured drawing dimension with shared GUI history behavior."),{{"dimension",true},{"document",false}},true},[this](const Json& args){
        const auto checked=target(args);if(!checked.ok)return checked;
        const auto id=args.value("document",workspace_.active_document_id());auto* state=workspace_.open_drawing(id);
        if(!state)return Result::failure("unsupported_document",tr("Drawing commands require an open Drawing."));
        const auto dimension=args.at("dimension").get<std::string>();auto next=state->document();
        for(auto& sheet:next.sheets)if(workspace::erase_drawing_dimension(sheet,dimension)) {
            state->commit(std::move(next));change_=Change{ChangeKind::Model,id,true};
            return Result::success({{"document",id},{"dimension",dimension},{"revision",state->revision()}});
        }
        return Result::failure("dimension_not_found",tr("The measured drawing dimension does not exist."));
    });
}
}
