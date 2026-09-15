#include <zima/command_host/host.hpp>
#include <zima/workspace/drawing_balloon_operations.hpp>
#include <zima/workspace/drawing_operations.hpp>
#include <algorithm>
namespace zima::command_host {
namespace {
Json balloon_json(const drawing::DrawingSheet& sheet,const drawing::DrawingBalloon& b) {
    auto j=Json::parse(drawing::serialize_balloons({b})).at(0);j["balloon"]=b.id;j["sheet"]=sheet.id;
    const auto e=drawing::evaluate_balloon(sheet,b);j["item_number"]=e.item_number;j["unresolved"]=e.unresolved;return j;
}
void patch_balloon(drawing::DrawingBalloon& b,const Json& args) {
    if(args.contains("view"))b.view_id=args["view"].get<std::string>();
    if(args.contains("reference")) {
        const auto& r=args["reference"];
        for(const auto& [key,value]:r.items())if((key!="owner"&&key!="key"&&key!="instance_path")||!value.is_string())throw std::invalid_argument("Invalid balloon reference.");
        b.attachment.reference={r.at("owner").get<std::string>(),r.at("key").get<std::string>(),r.at("instance_path").get<std::string>()};
    }
    if(args.contains("parameter"))b.attachment.parameter=args["parameter"].get<double>();
    if(args.contains("position")){const auto& p=args["position"];if(p.size()!=2)throw std::invalid_argument("Balloon position requires two paper coordinates.");b.position={p.at(0).get<double>(),p.at(1).get<double>()};}
    if(args.contains("diameter"))b.diameter=args["diameter"].get<double>();
    if(args.contains("text_height"))b.text_height=args["text_height"].get<double>();
    if(args.contains("visible"))b.visible=args["visible"].get<bool>();
}
}
void Host::register_drawing_balloon_commands() {
    using Type=commands::ArgumentType;
    for(bool one:{false,true})dispatcher_.add({one?"drawing.balloon.get":"drawing.balloon.list",
        tr("Read stored drawing balloons and their first-level BOM links."),one?std::vector<commands::Argument>{{"balloon",true},{"document",false}}:std::vector<commands::Argument>{{"view",false},{"sheet",false},{"document",false}},false},[this,one](const Json& args){
        const auto id=args.value("document",workspace_.active_document_id());const auto* state=workspace_.open_drawing(id);
        if(!state)return Result::failure("unsupported_document",tr("Drawing commands require an open Drawing."));
        const auto wanted=args.value("balloon",std::string{}),view=args.value("view",std::string{}),sheet=args.value("sheet",std::string{});
        if(!view.empty()&&!state->document().find_view(view))return Result::failure("view_not_found",tr("The drawing view does not exist."));
        if(!sheet.empty()&&!state->document().find_sheet(sheet))return Result::failure("sheet_not_found",tr("The drawing sheet does not exist."));
        auto rows=Json::array();for(const auto& s:state->document().sheets)if(sheet.empty()||sheet==s.id)for(const auto& b:s.balloons)if((view.empty()||view==b.view_id)&&(!one||wanted==b.id))rows.push_back(balloon_json(s,b));
        if(one&&rows.empty())return Result::failure("balloon_not_found",tr("The balloon does not exist."));
        auto result=one?rows.at(0):Json{{"items",rows},{"total",rows.size()}};result["document"]=id;result["revision"]=state->revision();return Result::success(std::move(result));
    });
    for(const auto operation:{"create","set","delete","show_all","erase_all"}) {
        const std::string op=operation;const bool create=op=="create",bulk=op=="show_all"||op=="erase_all";
        std::vector<commands::Argument> arguments{{create||bulk?"view":"balloon",true},{"document",false}};
        if(create||op=="set") {
            if(!create)arguments.push_back({"view",false});
            arguments.insert(arguments.end(),{{"reference",create,Type::Object},{"parameter",false,Type::Number},{"position",create,Type::Array},{"diameter",false,Type::Number},{"text_height",false,Type::Number},{"visible",false,Type::Boolean}});
        }
        dispatcher_.add({"drawing.balloon."+op,tr("Edit first-level BOM balloons with shared GUI history behavior."),arguments,true},[this,op,create,bulk](const Json& args){
            const auto checked=target(args);if(!checked.ok)return checked;
            const auto id=args.value("document",workspace_.active_document_id());auto* state=workspace_.open_drawing(id);
            if(!state)return Result::failure("unsupported_document",tr("Drawing commands require an open Drawing."));
            try {
                auto next=state->document();drawing::DrawingSheet* owner{};auto value=drawing::make_drawing_balloon();
                if(create||bulk) {
                    const auto view=args.at("view").get<std::string>();value.view_id=view;
                    for(auto& sheet:next.sheets)if(std::ranges::any_of(sheet.views,[&](const auto& v){return v.id==view;}))owner=&sheet;
                    if(!owner)return Result::failure("view_not_found",tr("The drawing view does not exist."));
                }else {
                    for(auto& sheet:next.sheets)for(const auto& b:sheet.balloons)if(b.id==args.at("balloon").get<std::string>()){owner=&sheet;value=b;}
                    if(!owner)return Result::failure("balloon_not_found",tr("The balloon does not exist."));
                }
                const auto sheet_id=owner->id;bool changed=false;
                if(bulk) {
                    auto pending=*owner;
                    if(op=="show_all")drawing::show_all_balloons(pending,value.view_id);else drawing::erase_all_balloons(pending,value.view_id);
                    changed=workspace::set_drawing_balloons(next,sheet_id,pending.balloons);
                }else if(op=="delete")changed=workspace::erase_drawing_balloon(*owner,value.id);
                else {patch_balloon(value,args);changed=workspace::edit_drawing_balloon(next,sheet_id,value,create);}
                if(changed){state->commit(std::move(next));change_=Change{ChangeKind::Model,id,true};}
                Json result{{"document",id},{"sheet",sheet_id},{"revision",state->revision()},{"changed",changed}};
                if(!bulk&&op!="delete")for(const auto& b:state->document().find_sheet(sheet_id)->balloons)if(b.id==value.id){result.update(balloon_json(*state->document().find_sheet(sheet_id),b));break;}
                return Result::success(std::move(result));
            }catch(const workspace::DrawingOperationError& e){return Result::failure(e.code,tr(e.what()));}
             catch(const std::exception& e){return Result::failure("invalid_arguments",tr(e.what()));}
        });
    }
}
}
