#include <zima/command_host/host.hpp>
#include <zima/workspace/drawing_dimension_operations.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <zima/document/dimension_layout_json.hpp>
#include <algorithm>
#include <zima/workspace/drawing_operations.hpp>
namespace zima::command_host {
namespace {
[[noreturn]] void invalid_edit(){throw workspace::DrawingOperationError("invalid_arguments","Invalid measured drawing dimension parameters.");}
void keys(const Json& row,const std::set<std::string>& allowed) {
    if(!row.is_object())invalid_edit();for(const auto& [key,value]:row.items())if(!allowed.contains(key))invalid_edit();
}
template<class Enum,std::size_t N> Enum enumeration(const Json& row,const std::array<const char*,N>& names) {
    if(!row.is_string())invalid_edit();const auto found=std::ranges::find(names,row.get<std::string>());
    if(found==names.end())invalid_edit();return static_cast<Enum>(found-names.begin());
}
double number(const Json& value){if(!value.is_number()||!std::isfinite(value.get<double>()))invalid_edit();return value.get<double>();}
drawing::Point2 point(const Json& value){if(!value.is_array()||value.size()!=2)invalid_edit();return {number(value[0]),number(value[1])};}
kernel::EdgeReference read_reference(const Json& row) {
    keys(row,{"owner","key","instance_path"});
    for(const auto* key:{"owner","key","instance_path"})if(!row.contains(key)||!row[key].is_string())invalid_edit();
    kernel::EdgeReference result{row["owner"].get<std::string>(),row["key"].get<std::string>(),row["instance_path"].get<std::string>()};
    if(!result.valid())invalid_edit();return result;
}
drawing::DimensionAttachment read_attachment(const Json& row) {
    keys(row,{"kind","reference","other_reference","parameter","side"});
    if(!row.contains("kind")||!row.contains("reference"))invalid_edit();
    drawing::DimensionAttachment a;a.kind=enumeration<drawing::DimensionAttachmentKind>(row["kind"],std::array{"point","curve_point","line","center","tangent","intersection"});
    a.reference=read_reference(row["reference"]);if(row.contains("other_reference"))a.other_reference=read_reference(row["other_reference"]);
    if(row.contains("parameter"))a.parameter=number(row["parameter"]);
    if(row.contains("side")){if(!row["side"].is_number_integer()||(row["side"]!=1&&row["side"]!=-1))invalid_edit();a.side=row["side"].get<int>();}
    return a;
}
void patch_dimension(drawing::DrawingDimension& value,const Json& args) {
    if(args.contains("kind")) {
        const auto kind=enumeration<drawing::DrawingDimensionKind>(args["kind"],std::array{"linear","radius","diameter","chain","angular"});
        if(kind!=value.kind) {
            if(!args.contains("attachments"))invalid_edit();
            if(kind==drawing::DrawingDimensionKind::Angular&&value.style.suffix=="mm")value.style.suffix="°";
            else if(kind!=drawing::DrawingDimensionKind::Angular&&value.style.suffix=="°")value.style.suffix="mm";
            value.kind=kind;value.anchor_attachment=0;
            for(auto& segment:value.segments){segment.layout={};segment.last_presentation.reset();segment.last_angular_leaders=false;}
        }
    }
    if(args.contains("attachments")) {
        if(args["attachments"].size()>4096)invalid_edit();value.attachments.clear();
        for(const auto& item:args["attachments"])value.attachments.push_back(read_attachment(item));
        drawing::resize_dimension_segments(value);
    }
    if(args.contains("direction"))value.direction=enumeration<drawing::DimensionDirection>(args["direction"],std::array{"automatic","horizontal","vertical","parallel"});
    if(args.contains("parallel_reference"))value.parallel_reference=read_reference(args["parallel_reference"]);
    if(args.contains("anchor_attachment")){if(args["anchor_attachment"]<0||args["anchor_attachment"]>4095)invalid_edit();value.anchor_attachment=args["anchor_attachment"].get<std::size_t>();}
    if(args.contains("style")) {
        auto style=document::dimension_text_style_json(value.style);
        for(const auto& [key,item]:args["style"].items()) {
            if(!style.contains(key)||(key=="decimals"?!item.is_number_integer():!item.is_string()))invalid_edit();
            if(key=="decimals"&&(item<0||item>12))invalid_edit();style[key]=item;
        }
        value.style=document::dimension_text_style_from_json(style);
    }
    if(args.contains("layouts")) {
        if(args["layouts"].size()!=value.segments.size())invalid_edit();
        for(std::size_t i=0;i<value.segments.size();++i) {
            const auto& row=args["layouts"][i];keys(row,{"text_along","text_outward","line_offset","arrows_reversed","radius_rotation_degrees","radius_center_line_hidden"});
            auto layout=document::dimension_layout_json(value.segments[i].layout);
            for(const auto& [key,item]:row.items()) {
                if(key=="arrows_reversed"||key=="radius_center_line_hidden"){if(!item.is_boolean())invalid_edit();}else static_cast<void>(number(item));
                layout[key]=item;
            }
            value.segments[i].layout=document::dimension_layout_from_json(layout);
        }
    }
}
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
    for(int operation=0;operation<3;++operation) {
        const bool creating=operation==0,extending=operation==2;
        std::vector<commands::Argument> arguments;
        arguments.push_back({creating?"view":"dimension",true});
        if(extending) {
            arguments.push_back({"attachment",true,Type::Object});arguments.push_back({"at_first",false,Type::Boolean});arguments.push_back({"position",false,Type::Array});
        } else {
            if(!creating)arguments.push_back({"view",false});
            arguments.insert(arguments.end(),{{"kind",false},{"attachments",creating,Type::Array},{"direction",false},{"parallel_reference",false,Type::Object},
                {"anchor_attachment",false,Type::Integer},{"style",false,Type::Object},{"layouts",false,Type::Array},{"placements",false,Type::Array}});
        }
        arguments.push_back({"document",false});
        dispatcher_.add({creating?"drawing.dimension.create":extending?"drawing.dimension.extend":"drawing.dimension.set",
            tr(creating?"Create a measured dimension from original drawing references.":extending?"Extend a measured dimension chain while preserving existing segment identities.":"Edit measured dimension references, style and placement."),arguments,true},[this,creating,extending](const Json& args){
            const auto checked=target(args);if(!checked.ok)return checked;
            const auto id=args.value("document",workspace_.active_document_id());auto* state=workspace_.open_drawing(id);
            if(!state)return Result::failure("unsupported_document",tr("Drawing commands require an open Drawing."));
            try {
                auto next=state->document();drawing::DrawingSheet* owner=nullptr;drawing::DrawingDimension value;
                if(creating) {
                    const auto view=args.at("view").get<std::string>();
                    for(auto& sheet:next.sheets)if(std::ranges::any_of(sheet.views,[&](const auto& v){return v.id==view;})){owner=&sheet;break;}
                    if(!owner)return Result::failure("view_not_found",tr("The drawing view does not exist."));
                    const auto kind=args.contains("kind")?enumeration<drawing::DrawingDimensionKind>(args["kind"],std::array{"linear","radius","diameter","chain","angular"}):drawing::DrawingDimensionKind::Linear;
                    value=drawing::make_drawing_dimension(view,kind);
                } else {
                    const auto wanted=args.at("dimension").get<std::string>();
                    for(auto& sheet:next.sheets)for(const auto& d:sheet.dimensions)if(d.id==wanted){owner=&sheet;value=d;}
                    if(!owner)return Result::failure("dimension_not_found",tr("The measured drawing dimension does not exist."));
                }
                if(!extending&&args.contains("view"))value.view_id=args["view"].get<std::string>();
                const auto target_view=std::ranges::find(owner->views,value.view_id,&drawing::DrawingView::id);
                if(target_view==owner->views.end())throw workspace::DrawingOperationError("view_not_found","The dimension view must belong to its drawing sheet.");
                if(extending) {
                    const bool at_first=args.value("at_first",false);drawing::extend_dimension_chain(value,at_first,read_attachment(args["attachment"]));drawing::validate_drawing_dimension(value);
                    if(args.contains("position"))drawing::place_drawing_dimension(*target_view,value,at_first?0:value.segments.size()-1,point(args["position"]));
                } else {
                    patch_dimension(value,args);
                    drawing::validate_drawing_dimension(value);
                    if(args.contains("placements")) {
                        if(args["placements"].size()!=value.segments.size())invalid_edit();
                        for(std::size_t i=0;i<value.segments.size();++i)if(!args["placements"][i].is_null())
                            drawing::place_drawing_dimension(*target_view,value,i,point(args["placements"][i]));
                    }
                }
                const auto dimension=value.id;const bool changed=workspace::edit_drawing_dimension(next,owner->id,std::move(value),creating);
                if(changed){state->commit(std::move(next));change_=Change{ChangeKind::Model,id,true};}
                for(const auto& sheet:state->document().sheets)for(const auto& d:sheet.dimensions)if(d.id==dimension) {
                    auto result=dimension_json(sheet,d,true);result["document"]=id;result["revision"]=state->revision();result["changed"]=changed;return Result::success(std::move(result));
                }
                return Result::failure("dimension_not_found",tr("The measured drawing dimension does not exist."));
            } catch(const workspace::DrawingOperationError& error){return Result::failure(error.code,tr(error.what()));}
              catch(const std::exception& error){return Result::failure("invalid_arguments",tr(error.what()));}
        });
    }
}
}
