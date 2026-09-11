#include "sketch_command_support.hpp"
#include <zima/sketcher/curve_geometry.hpp>
#include <zima/sketcher/sketch_trim.hpp>
#include <set>
#include <tuple>

namespace zima::command_host {
using namespace sketch_commands;
namespace {
Json offset_data(const Sketch& sketch,const std::string& id) {
    const auto* value=sketch.find_offset(id);
    if(!value)throw workspace::SketchOperationError("offset_not_found","The native Sketch offset does not exist.");
    Json result={{"geometry",id},{"source",value->source_id},{"operation",value->operation_id},
        {"distance_mm",value->distance},{"flipped",value->flipped},{"tolerance_mm",value->tolerance},
        {"start",value->start},{"end",value->end},{"broken",value->broken}};
    const auto anchor=[](const auto& value)->Json{return value?Json{{"curve",value->curve_id},{"parameter",value->parameter}}:Json(nullptr);};
    result["start_anchor"]=anchor(value->start_anchor);result["end_anchor"]=anchor(value->end_anchor);return result;
}
std::vector<std::array<double,2>> intervals(const Json& data) {
    if(!data.is_array()||data.empty()||data.size()>1024)invalid("Retain requires 1 to 1024 nonoverlapping intervals.");
    std::vector<std::array<double,2>> result;
    for(const auto& range:data) {
        if(!range.is_array()||range.size()!=2)invalid("Each retained interval must be [start, end] inside [0, 1].");
        const double a=number(range[0]),b=number(range[1]);
        if(a<0||b>1||b-a<1e-12)invalid("Each retained interval must be [start, end] inside [0, 1].");result.push_back({a,b});
    }
    auto sorted=result;std::ranges::sort(sorted);
    for(std::size_t i=1;i<sorted.size();++i)if(sorted[i][0]<sorted[i-1][1])invalid("Retained intervals must not overlap.");
    return result;
}
}
void Host::add_sketch_query(commands::Command command,std::function<Json(const Sketch&,const Json&)> operation) {
    command.arguments.insert(command.arguments.begin(),{"sketch",true});command.arguments.push_back({"document",false});command.changes_state=false;
    dispatcher_.add(std::move(command),[this,operation=std::move(operation)](const Json& args) {
        try {
            const auto doc=args.value("document",workspace_.active_document_id());const auto id=args["sketch"].get<std::string>();
            auto result=operation(workspace::document_sketch(workspace_,doc,id),args);result["document"]=doc;result["sketch"]=id;
            if(const auto* part=workspace_.open_part(doc))result["revision"]=part->session.revision();else result["revision"]=workspace_.open_assembly(doc)->session.revision();
            return Result::success(std::move(result));
        }catch(const workspace::SketchOperationError& error){return Result::failure(error.code,tr(error.what()));}
         catch(const std::exception& error){return Result::failure("sketch_rejected",tr(error.what()));}
    });
}
void Host::register_sketch_curve_commands() {
    add_sketch_command({"sketch.offset.create",tr("Offset a native Sketch curve; distance is positive millimetres."),{{"source",true},{"distance_mm",true,Type::Number},{"flipped",false,Type::Boolean}}},[](Sketch& s,const Json& a) {
        const auto source=a["source"].get<std::string>();require_geometry(s,source);
        return offset_data(s,s.add_offset(source,number(a["distance_mm"]),a.value("flipped",false)));
    });
    add_sketch_command({"sketch.offset.set",tr("Edit all retained pieces of one Sketch offset operation."),{{"geometry",true},{"distance_mm",false,Type::Number},{"flipped",false,Type::Boolean},{"source",false}}},[](Sketch& s,const Json& a) {
        const auto id=a["geometry"].get<std::string>();static_cast<void>(offset_data(s,id));const auto original=*s.find_offset(id);
        if(a.contains("source")) {
            const auto source=a["source"].get<std::string>();require_geometry(s,source);
            for(auto& offset:s.offsets)if(offset.id==id)offset.source_id=source;
        }
        s.update_offset(id,a.value("distance_mm",original.distance),a.value("flipped",original.flipped));return offset_data(s,id);
    });
    add_sketch_query({"sketch.offset.get",tr("Read an offset's source, retained interval and repair state."),{{"geometry",true}}},[](const Sketch& s,const Json& a) {return offset_data(s,a["geometry"].get<std::string>());});
    add_sketch_command({"sketch.offset.free",tr("Detach an offset while preserving its current curve and identity."),{{"geometry",true}}},[](Sketch& s,const Json& a) {
        const auto id=a["geometry"].get<std::string>();s.free_offset(id);return Json{{"geometry",id}};
    });
    add_sketch_query({"sketch.curve.get",tr("Read a curve's complete supporting spline and visible interval."),{{"geometry",true},{"limit",false,Type::Integer}}},[](const Sketch& s,const Json& a) {
        const auto id=a["geometry"].get<std::string>();require_geometry(s,id);const auto limit=integer(a,"limit",4096,1,100000);
        const auto curve=s.supporting_curve(id);double start=0,end=1;
        if(const auto* offset=s.find_offset(id)){start=offset->start;end=offset->end;}
        for(const auto& trim:s.curve_trims)if(trim.id==id){start=trim.start;end=trim.end;}
        Json result={{"geometry",id},{"coordinates","sketch"},{"length_unit","mm"},{"start",start},{"end",end},
            {"degree",curve.degree},{"pole_count",curve.poles.size()},{"knot_count",curve.knots.size()},
            {"geometry_omitted_by_limit",curve.poles.size()+curve.weights.size()+curve.knots.size()>limit}};
        if(!result.at("geometry_omitted_by_limit").get<bool>()) {
            Json poles=Json::array();for(const auto& p:curve.poles)poles.push_back({p.x,p.y,p.z});
            result["support"]={{"degree",curve.degree},{"poles",poles},{"weights",curve.weights},{"knots",curve.knots}};
        }
        return result;
    });
    add_sketch_command({"sketch.curve.retain",tr("Retain explicit curve intervals while preserving the original supporting geometry."),{{"geometry",true},{"intervals",true,Type::Array}}},[](Sketch& s,const Json& a) {
        const auto id=a["geometry"].get<std::string>();require_geometry(s,id);const auto ranges=intervals(a["intervals"]);
        if(ranges.size()==1&&ranges[0]==std::array<double,2>{0,1})return Json{{"geometry",Json::array({id})}};
        return Json{{"geometry",s.retain_curve_intervals(id,ranges)}};
    });
    add_sketch_query({"sketch.trim.pieces",tr("List current intersection-bounded curve pieces for explicit trimming."),{{"geometry",false},{"include_axes",false,Type::Boolean},{"offset",false,Type::Integer},{"limit",false,Type::Integer}}},[](const Sketch& s,const Json& a) {
        const auto offset=integer(a,"offset",0,0,100000000),limit=integer(a,"limit",500,1,5000);const auto id=a.value("geometry",std::string{});
        if(!id.empty())require_geometry(s,id);Json items=Json::array();std::size_t total=0;
        for(const auto& piece:sketcher::sketch_trim_topology(s,a.value("include_axes",true))) {
            if(!id.empty()&&piece.geometry_id!=id)continue;if(total++<offset||items.size()>=limit)continue;
            Json item={{"geometry",piece.geometry_id},{"start",piece.start},{"end",piece.end},{"closed",piece.closed},{"point_count",piece.points.size()}};
            if(!piece.points.empty()){item["start_point"]=piece.points.front();item["end_point"]=piece.points.back();}items.push_back(std::move(item));
        }
        return Json{{"items",items},{"total",total},{"more",offset+items.size()<total},{"next_offset",offset+items.size()}};
    });
    add_sketch_command({"sketch.trim",tr("Remove explicitly identified current trim pieces; stale intervals are rejected."),{{"pieces",true,Type::Array},{"include_axes",false,Type::Boolean},{"snap_mm",false,Type::Number}}},[](Sketch& s,const Json& a) {
        if(a["pieces"].empty()||a["pieces"].size()>2048)invalid("Trim requires 1 to 2048 piece identities.");
        const auto topology=sketcher::sketch_trim_topology(s,a.value("include_axes",true));std::vector<sketcher::SketchTrimPiece> selected;
        std::set<std::tuple<std::string,double,double>> seen;
        for(const auto& value:a["pieces"]) {
            if(!value.is_object()||value.size()!=3||!value.contains("geometry")||!value["geometry"].is_string()||!value.contains("start")||!value.contains("end"))invalid("Each trim identity requires exactly geometry, start and end.");
            const auto id=value["geometry"].get<std::string>();const double start=number(value["start"]),end=number(value["end"]);
            if(!seen.emplace(id,start,end).second)invalid("A trim piece cannot be selected twice.");
            const auto found=std::ranges::find_if(topology,[&](const auto& p){return p.geometry_id==id && std::abs(p.start-start)<1e-12 && std::abs(p.end-end)<1e-12;});
            if(found==topology.end())throw workspace::SketchOperationError("stale_geometry","The requested trim piece no longer exists; read the current pieces.");selected.push_back(*found);
        }
        return Json{{"geometry_mapping",sketcher::apply_sketch_trim(s,selected,a.contains("snap_mm")?snap(a):1e-7).geometry_mapping}};
    });
    add_sketch_command({"sketch.mirror",tr("Mirror native Sketch entities about an explicitly identified Sketch axis."),{{"entities",true,Type::Array},{"axis",true},{"snap_mm",false,Type::Number}}},[](Sketch& s,const Json& a) {
        const auto entities=ids(a,"entities");if(entities.empty())invalid("Mirror requires native Sketch entities.");
        for(const auto& id:entities)if(!s.find_point(id))require_geometry(s,id);
        const auto result=s.mirror_geometry(entities,a["axis"].get<std::string>(),snap(a));return Json{{"geometry",result.geometry_ids},{"points",result.point_ids}};
    });
    add_sketch_command({"sketch.oriented_rectangle.create",tr("Create an oriented rectangle using a Sketch symmetry axis."),{{"first",true,Type::Array},{"guide",true,Type::Array},{"axis",true},{"snap_mm",false,Type::Number}}},[](Sketch& s,const Json& a) {
        const auto p=point(a,"first"),q=point(a,"guide");return Json{{"geometry",s.add_oriented_rectangle(p[0],p[1],q[0],q[1],a["axis"].get<std::string>(),snap(a))}};
    });
    add_sketch_command({"sketch.tangent_arc.create",tr("Create a circular arc tangent to an explicitly identified curve."),{{"start_point",true},{"end",true,Type::Array},{"tangent",true},{"reverse",false,Type::Boolean},{"construction",false,Type::Boolean},{"snap_mm",false,Type::Number}}},[](Sketch& s,const Json& a) {
        const auto p=point(a,"end");return Json{{"geometry",s.add_tangent_arc(a["start_point"].get<std::string>(),p[0],p[1],a["tangent"].get<std::string>(),a.value("reverse",false),a.value("construction",false),snap(a))}};
    });
    add_sketch_command({"sketch.common_tangent.create",tr("Create a common tangent segment using two curves and contact hints."),{{"first",true},{"first_hint",true,Type::Array},{"second",true},{"second_hint",true,Type::Array}}},[](Sketch& s,const Json& a) {
        return Json{{"geometry",s.add_common_tangent_segment(a["first"].get<std::string>(),point(a,"first_hint"),a["second"].get<std::string>(),point(a,"second_hint"))}};
    });
    add_sketch_command({"sketch.corner_fillet.create",tr("Round a Sketch corner without destroying its source segments."),{{"first",true},{"second",true},{"radius_mm",true,Type::Number},{"snap_mm",false,Type::Number}}},[](Sketch& s,const Json& a) {
        const auto value=s.add_corner_fillet(a["first"].get<std::string>(),a["second"].get<std::string>(),number(a["radius_mm"]),snap(a));
        return Json{{"geometry",value.arc_id}};
    });
}
} // namespace zima::command_host
