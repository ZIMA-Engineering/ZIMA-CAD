#include <zima/command_host/host.hpp>
#include <zima/workspace/drawing_sources.hpp>
#include <zima/workspace/section_operations.hpp>
#include <zima/document/placement_json.hpp>
#include <zima/workspace/placement_edit.hpp>
#include <cmath>
#include <algorithm>
#include <set>
namespace zima::command_host {
namespace {
struct QueryError : std::runtime_error {
    const char* code;
    QueryError(const char* code,const char* message):std::runtime_error(message),code(code){}
};
using EditError=workspace::SectionOperationError;
void patch_section(document::SectionDefinition& section,const Json& args,const workspace::Workspace& live,const std::string& id) {
    if(args.contains("name"))section.name=args.at("name").get<std::string>();
    if(args.contains("plane")) {
        const auto plane=args.at("plane").get<std::string>();
        if(plane!="XY"&&plane!="XZ"&&plane!="YZ")throw EditError("invalid_arguments","Sketch plane must be XY, XZ or YZ.");
        section.sketch.plane=plane=="XY"?sketcher::SketchPlane::XY:plane=="XZ"?sketcher::SketchPlane::XZ:sketcher::SketchPlane::YZ;
    }
    if(args.contains("reversed"))section.reversed=args.at("reversed").get<bool>();
    if(args.contains("show_plane"))section.show_plane=args.at("show_plane").get<bool>();
    if(args.contains("show_cut"))section.show_cut=args.at("show_cut").get<bool>();
    if(args.contains("path_mm")) {
        const auto& path=args.at("path_mm");
        if(path.size()<2||path.size()>10000)throw EditError("invalid_arguments","A Section path requires 2 to 10000 points.");
        std::vector<std::array<double,2>> points;
        for(const auto& point:path) {
            if(!point.is_array()||point.size()!=2)throw EditError("invalid_arguments","Section points require two finite coordinates in millimetres.");
            for(const auto& value:point)if(!value.is_number()||!std::isfinite(value.get<double>())||std::abs(value.get<double>())>1000000)
                throw EditError("invalid_arguments","Section points require two finite coordinates in millimetres.");
            points.push_back({point[0].get<double>(),point[1].get<double>()});
        }
        for(std::size_t i=1;i<points.size();++i) {
            const auto a=points[i-1],b=points[i];
            if(std::hypot(b[0]-a[0],b[1]-a[1])<=1e-7)throw EditError("invalid_arguments","Řez obsahuje nulovou úsečku.");
            static_cast<void>(section.sketch.add_segment(a[0],a[1],b[0],b[1],1e-7));
        }
    }
    if(args.contains("placement")) {
        if(args.at("placement").empty())throw EditError("invalid_arguments","Specify at least one placement parameter.");
        const auto geometry=workspace::section_placement_geometry(live,id);
        for(const auto& [key,value]:args.at("placement").items()) {
            if(!value.is_number()||!std::isfinite(value.get<double>()))throw EditError("invalid_arguments","Placement parameters must be finite JSON numbers.");
            if(!workspace::assign_placement_dimension(section.placement,geometry,key,value.get<double>()))
                throw EditError("parameter_not_editable","The placement parameter is unknown, constrained or locked.");
        }
    }
    if(args.contains("components")) {
        if(args.at("components").size()>10000)throw EditError("invalid_arguments","A Section component patch is too large.");
        std::set<std::string> touched;
        for(const auto& patch:args.at("components")) {
            if(!patch.is_object()||!patch.contains("component")||!patch.at("component").is_string())
                throw EditError("invalid_arguments","A Section component patch requires an exact component ID.");
            const auto key=patch.at("component").get<std::string>();
            if(!touched.insert(key).second)throw EditError("invalid_arguments","A Section component may be changed only once per patch.");
            if(!section.component_names.contains(key)&&!section.components.contains(key))
                throw EditError("component_not_found","The requested component does not belong to this Section.");
            for(const auto& [name,value]:patch.items())if(name!="component"&&name!="mode"&&name!="custom_hatch"&&name!="hatch")
                throw EditError("invalid_arguments","Unknown Section component property.");
            auto& component=section.components[key];
            if(patch.contains("mode")) {
                if(!patch.at("mode").is_string())throw EditError("invalid_arguments","Neplatný režim komponenty řezu.");
                const auto mode=patch.at("mode").get<std::string>();
                if(mode!="cut_hatch"&&mode!="cut_only"&&mode!="uncut")throw EditError("invalid_arguments","Neplatný režim komponenty řezu.");
                component.mode=mode=="cut_hatch"?0:mode=="cut_only"?1:2;
            }
            if(patch.contains("custom_hatch")&&!patch.at("custom_hatch").is_boolean())
                throw EditError("invalid_arguments","custom_hatch must be a boolean.");
            const bool custom=patch.value("custom_hatch",patch.contains("hatch")||component.custom_hatch);
            if(patch.contains("hatch")&&!custom)throw EditError("invalid_arguments","Custom hatch values require custom_hatch enabled.");
            if(custom&&!component.custom_hatch)component.hatch=document::section_component_hatch(section,key);
            component.custom_hatch=custom;
            if(patch.contains("hatch")) {
                if(!patch.at("hatch").is_object()||patch.at("hatch").empty())throw EditError("invalid_arguments","Specify at least one hatch property.");
                for(const auto& [name,value]:patch.at("hatch").items()) {
                    if(name=="pattern") {
                        if(!value.is_string())throw EditError("invalid_arguments","Unknown Section hatch pattern.");
                        const auto pattern=value.get<std::string>();
                        if(pattern!="parallel"&&pattern!="cross"&&pattern!="dashed")throw EditError("invalid_arguments","Unknown Section hatch pattern.");
                        component.hatch.pattern=pattern=="parallel"?0:pattern=="cross"?1:2;
                    }else {
                        if(!value.is_number()||!std::isfinite(value.get<double>()))throw EditError("invalid_arguments","Hatch parameters must be finite numbers.");
                        if(name=="angle_degrees")component.hatch.angle=value.get<double>();
                        else if(name=="spacing_mm")component.hatch.spacing_mm=value.get<double>();
                        else if(name=="offset_mm")component.hatch.offset_mm=value.get<double>();
                        else throw EditError("invalid_arguments","Unknown Section hatch property.");
                    }
                }
            }
        }
    }
}
struct Source {
    std::string id;
    std::uint64_t revision;
    std::vector<document::SectionDefinition> sections;
};
Source source(const workspace::Workspace& live,const Json& args) {
    const auto id=args.value("document",live.active_document_id());
    if(const auto* part=live.open_part(id))return {id,part->session.revision(),workspace::sections_for_part(part->session.document())};
    if(const auto* assembly=live.open_assembly(id))return {id,assembly->session.revision(),workspace::sections_for_assembly(assembly->session.document())};
    throw QueryError("unsupported_document","Section queries require an open Part or Assembly.");
}
const document::SectionDefinition& find(const Source& source,const Json& args) {
    const auto id=args.at("object").get<std::string>();
    const auto found=std::ranges::find(source.sections,id,&document::SectionDefinition::id);
    if(found==source.sections.end())throw QueryError("section_not_found","The requested Section does not exist in this document.");
    return *found;
}
struct Page {std::size_t offset,limit;};
Page page(const Json& args) {
    const auto offset=args.value("offset",0LL),limit=args.value("limit",2000LL);
    if(offset<0||offset>100000000||limit<1||limit>10000)
        throw QueryError("invalid_arguments","Section query offset or limit is outside the supported range.");
    return {static_cast<std::size_t>(offset),static_cast<std::size_t>(limit)};
}
Json vec(kernel::Vec3 value){return Json::array({value.x,value.y,value.z});}
Json row(const document::SectionDefinition& section) {
    return {{"object",section.id},{"name",section.name},{"sketch",section.sketch.id},{"origin",section.container_origin.id},
        {"reversed",section.reversed},{"show_plane",section.show_plane},{"show_cut",section.show_cut},
        {"reference_valid",section.placement.reference_valid}};
}
Json details(const document::SectionDefinition& section) {
    auto result=row(section);result["placement"]=section.placement;
    result["length_unit"]="mm";result["angle_unit"]="degrees";
    result["sketch_frame"]={{"origin",vec(section.plane_origin)},{"x",vec(section.plane_x)},{"y",vec(section.plane_y)}};
    result["path_mm"]=Json::array();result["frames"]=Json::array();
    result["valid"]=section.placement.reference_valid;
    result["error"]=section.placement.reference_valid?"":"Section placement has unresolved references.";
    // Derive only the chain and its local frames from stored ZIMA data. There
    // is deliberately no source mesh construction, placement solve or OCCT.
    try {
        result["path_mm"]=document::section_path(section);
        for(const auto& frame:document::section_frames(section))result["frames"].push_back({{"origin",vec(frame.origin)},
            {"horizontal",vec(frame.horizontal)},{"vertical",vec(frame.vertical)},{"normal",vec(frame.normal)}});
    }catch(const std::exception& error){result["valid"]=false;result["error"]=error.what();}
    return result;
}
Json component(const document::SectionDefinition& section,const std::string& key) {
    const auto named=section.component_names.find(key);const auto setting=section.components.find(key);
    const auto value=setting==section.components.end()?document::SectionComponent{}:setting->second;
    const auto hatch=document::section_component_hatch(section,key);
    return {{"component",key},{"name",named==section.component_names.end()?std::string{}:named->second},
        {"available",named!=section.component_names.end()},{"stored",setting!=section.components.end()},
        {"mode",value.mode==0?"cut_hatch":value.mode==1?"cut_only":"uncut"},{"custom_hatch",value.custom_hatch},
        {"hatch",{{"angle_degrees",hatch.angle},{"spacing_mm",hatch.spacing_mm},{"offset_mm",hatch.offset_mm},
            {"pattern",hatch.pattern==0?"parallel":hatch.pattern==1?"cross":"dashed"}}}};
}
}
void Host::register_section_commands() {
    using Type=commands::ArgumentType;
    for(const bool create:{true,false}) {
        std::vector<commands::Argument> args{{"name",false},{"plane",false},{"reversed",false,Type::Boolean},
            {"show_plane",false,Type::Boolean},{"show_cut",false,Type::Boolean},{"placement",false,Type::Object},
            {"components",false,Type::Array},{"document",false}};
        if(create)args.insert(args.begin(),{"path_mm",true,Type::Array});else args.insert(args.begin(),{"object",true});
        dispatcher_.add({create?"section.create":"section.set",create?tr("Create a Section from a complete open polyline."):
            tr("Change Section properties through the same transaction as Properties OK."),std::move(args),true},[this,create](const Json& args) {
            const auto checked=target(args);if(!checked.ok)return checked;
            if(interaction().template_document)return Result::failure("unsupported_document",tr("Section operations require an open Part or Assembly."));
            try {
                const auto id=workspace_.active_document_id();
                const auto edit=workspace::prepare_section_edit(workspace_,id,create?std::string{}:args.at("object").get<std::string>());
                auto value=edit.initial;patch_section(value,args,workspace_,id);
                const bool changed=workspace::commit_section(workspace_,edit,std::move(value));
                const auto current=source(workspace_,{{"document",id}});
                auto result=details(find(current,{{"object",edit.initial.id}}));
                result["document"]=id;result["revision"]=current.revision;result["changed"]=changed;result["body_calculated"]=false;
                if(changed)change_=Change{ChangeKind::Model,id,true};
                return Result::success(std::move(result));
            }catch(const EditError& error){return Result::failure(error.code,tr(error.what()));}
             catch(const std::exception& error){return Result::failure("section_edit_rejected",tr(error.what()));}
        });
    }
    dispatcher_.add({"section.sketch.edit",tr("Edit a complete Section Sketch with one atomic batch of native Sketch commands."),
        {{"object",true},{"operations",true,Type::Array},{"document",false}},true},[this](const Json& args) {
        const auto checked=target(args);if(!checked.ok)return checked;
        if(interaction().template_document)return Result::failure("unsupported_document",tr("Section operations require an open Part or Assembly."));
        const auto& operations=args.at("operations");
        if(operations.empty()||operations.size()>1000)return Result::failure("invalid_arguments",tr("A Section Sketch batch requires 1 to 1000 operations."));
        try {
            const auto id=workspace_.active_document_id();
            const auto edit=workspace::prepare_section_edit(workspace_,id,args.at("object").get<std::string>());
            auto value=edit.initial;const auto batch=sketch_draft_dispatcher(value.sketch);auto results=Json::array();
            for(std::size_t i=0;i<operations.size();++i) {
                auto result=batch.execute(operations[i]);
                if(!result.ok){result.message=tr(result.message.c_str());result.data={{"operation_index",i}};return result;}
                results.push_back(std::move(result.data));
            }
            const bool changed=workspace::commit_section(workspace_,edit,std::move(value));
            const auto current=source(workspace_,{{"document",id}});
            auto result=details(find(current,{{"object",edit.initial.id}}));
            result["document"]=id;result["revision"]=current.revision;result["changed"]=changed;result["body_calculated"]=false;
            result["results"]=std::move(results);
            if(changed)change_=Change{ChangeKind::Model,id,true};
            return Result::success(std::move(result));
        }catch(const EditError& error){return Result::failure(error.code,tr(error.what()));}
         catch(const std::exception& error){return Result::failure("section_edit_rejected",tr(error.what()));}
    });
    for(const bool remove:{false,true})dispatcher_.add({remove?"section.delete":"section.activate",
        remove?tr("Remove a saved Section through the same transaction as the tree action."):
            tr("Activate a saved Section, or the unsectioned display when object is omitted."),
        {{"object",remove},{"document",false}},true},[this,remove](const Json& args) {
        const auto checked=target(args);if(!checked.ok)return checked;
        if(interaction().template_document)return Result::failure("unsupported_document",tr("Section operations require an open Part or Assembly."));
        try {
            const auto id=workspace_.active_document_id(),object=args.value("object",std::string{});
            const bool changed=remove?workspace::remove_section(workspace_,id,object):workspace::activate_section(workspace_,id,object);
            const auto revision=workspace_.open_part(id)?workspace_.open_part(id)->session.revision():workspace_.open_assembly(id)->session.revision();
            if(changed)change_=Change{ChangeKind::Model,id,true};
            return Result::success({{"document",id},{"object",object},{"changed",changed},{"revision",revision}});
        }catch(const workspace::SectionOperationError& error){return Result::failure(error.code,tr(error.what()));}
         catch(const std::exception& error){return Result::failure("section_action_rejected",tr(error.what()));}
    });
    const std::vector<commands::Argument> query{{"offset",false,Type::Integer},{"limit",false,Type::Integer},{"document",false}};
    dispatcher_.add({"section.list",tr("List saved Sections without calculating or activating a document."),query,false},[this](const Json& args) {
        try {
            const auto window=page(args);const auto data=source(workspace_,args);auto items=Json::array();
            for(std::size_t i=window.offset;i<data.sections.size()&&items.size()<window.limit;++i)items.push_back(row(data.sections[i]));
            return Result::success({{"document",data.id},{"revision",data.revision},{"items",std::move(items)},{"total",data.sections.size()}});
        }catch(const QueryError& error){return Result::failure(error.code,tr(error.what()));}
         catch(const std::exception& error){return Result::failure("section_query_failed",tr(error.what()));}
    });
    dispatcher_.add({"section.get",tr("Read a saved Section, its owned Sketch and cutting frames from persisted data."),
        {{"object",true},{"document",false}},false},[this](const Json& args) {
        try {
            const auto data=source(workspace_,args);auto result=details(find(data,args));result["document"]=data.id;result["revision"]=data.revision;
            result["error"]=tr(result.at("error").get<std::string>().c_str());
            return Result::success(std::move(result));
        }catch(const QueryError& error){return Result::failure(error.code,tr(error.what()));}
         catch(const std::exception& error){return Result::failure("section_query_failed",tr(error.what()));}
    });
    auto components=query;components.insert(components.begin(),{"object",true});
    dispatcher_.add({"section.components",tr("Read exact Section component paths, cut modes and effective hatch settings."),std::move(components),false},[this](const Json& args) {
        try {
            const auto window=page(args);const auto data=source(workspace_,args);const auto& section=find(data,args);
            std::set<std::string> keys;
            for(const auto& [id,name]:section.component_names)keys.insert(id);
            for(const auto& [id,setting]:section.components)keys.insert(id);
            auto items=Json::array();std::size_t index=0;
            for(const auto& key:keys)if(index++>=window.offset&&items.size()<window.limit)items.push_back(component(section,key));
            return Result::success({{"document",data.id},{"object",section.id},{"revision",data.revision},{"items",std::move(items)},{"total",keys.size()}});
        }catch(const QueryError& error){return Result::failure(error.code,tr(error.what()));}
         catch(const std::exception& error){return Result::failure("section_query_failed",tr(error.what()));}
    });
}
}
