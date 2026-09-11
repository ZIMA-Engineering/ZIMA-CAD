#include "sketch_command_support.hpp"
#include <algorithm>

namespace zima::command_host {
using namespace sketch_commands;
void Host::add_sketch_command(commands::Command command,std::function<Json(Sketch&,const Json&)> operation) {
    command.arguments.insert(command.arguments.begin(),{"sketch",true});command.arguments.push_back({"document",false});command.changes_state=true;
    dispatcher_.add(std::move(command),[this,operation=std::move(operation)](const Json& args) {
        const auto check=target(args);if(!check.ok)return check;
        if(interaction().template_document)return Result::failure("unsupported_document",tr("Sketch commands require an ordinary Part or Assembly."));
        try {
            const auto doc=workspace_.active_document_id();Json result;
            const bool changed=workspace::mutate_document_sketch(workspace_,doc,args["sketch"].get<std::string>(),
                [&](Sketch& sketch){result=operation(sketch,args);});
            result["document"]=doc;result["sketch"]=args["sketch"];result["changed"]=changed;
            if(const auto* part=workspace_.open_part(doc))result["revision"]=part->session.revision();
            else result["revision"]=workspace_.open_assembly(doc)->session.revision();
            result["body_calculated"]=false;
            if(changed)change_=Change{ChangeKind::Model,doc,true};
            return Result::success(std::move(result));
        } catch(const workspace::SketchOperationError& error){return Result::failure(error.code,tr(error.what()));}
          catch(const std::exception& error){return Result::failure("sketch_rejected",tr(error.what()));}
    });
}
void Host::register_sketch_commands() {
    const auto query=[this](commands::Command command,std::function<Json(const Json&)> operation) {
        dispatcher_.add(std::move(command),[this,operation=std::move(operation)](const Json& args) {
            try {return Result::success(operation(args));}
            catch(const workspace::SketchOperationError& error){return Result::failure(error.code,tr(error.what()));}
            catch(const std::exception& error){return Result::failure("sketch_rejected",tr(error.what()));}
        });
    };
    query({"sketch.list",tr("List persisted Sketches without calculating geometry."),{{"document",false},{"offset",false,Type::Integer},{"limit",false,Type::Integer}},false},[this](const Json& args) {
        const auto offset=integer(args,"offset",0,0,100000000),limit=integer(args,"limit",100,1,1000);
        const auto doc=args.value("document",workspace_.active_document_id());Json items=Json::array();std::size_t total=0;
        workspace::visit_document_sketches(workspace_,doc,[&](const auto& sketch){if(total++>=offset&&items.size()<limit)items.push_back(metadata(sketch));return true;});
        const auto next=offset+items.size();return Json{{"document",doc},{"items",items},{"total",total},{"more",next<total},{"next_offset",next}};
    });
    query({"sketch.get",tr("Read Sketch metadata and geometry counts."),{{"sketch",true},{"document",false}},false},[this](const Json& args) {
        const auto doc=args.value("document",workspace_.active_document_id());auto result=metadata(workspace::document_sketch(workspace_,doc,args["sketch"].get<std::string>()));result["document"]=doc;return result;
    });
    query({"sketch.entities",tr("List stable Sketch entity IDs and kinds."),{{"sketch",true},{"document",false},{"offset",false,Type::Integer},{"limit",false,Type::Integer}},false},[this](const Json& args) {
        const auto offset=integer(args,"offset",0,0,100000000),limit=integer(args,"limit",500,1,5000);
        const auto sketch=workspace::document_sketch(workspace_,args.value("document",workspace_.active_document_id()),args["sketch"].get<std::string>());
        Json items=Json::array();std::size_t total=0;
        const auto add=[&](const auto& values,const char* kind){for(const auto& value:values){if(total++>=offset&&items.size()<limit)items.push_back({{"id",value.id},{"kind",kind}});}};
        add(sketch.points,"point");add(sketch.segments,"segment");add(sketch.circles,"circle");add(sketch.arcs,"arc");
        add(sketch.ellipses,"ellipse");add(sketch.elliptical_arcs,"elliptical_arc");add(sketch.bsplines,"bspline");add(sketch.texts,"text");
        add(sketch.external_references,"external_reference");add(sketch.constraints,"constraint");add(sketch.dimensions,"dimension");add(sketch.corner_radii,"corner_radius");
        return Json{{"items",items},{"total",total},{"more",offset+items.size()<total},{"next_offset",offset+items.size()}};
    });
    query({"sketch.entity.get",tr("Read one persisted Sketch entity; lengths use millimetres."),{{"sketch",true},{"entity",true},{"document",false}},false},[this](const Json& args) {
        const auto sketch=workspace::document_sketch(workspace_,args.value("document",workspace_.active_document_id()),args["sketch"].get<std::string>());
        const auto data=Json::parse(sketch.serialized());
        for(const auto* key:{"points","segments","circles","arcs","ellipses","elliptical_arcs","bsplines","texts","external_references","constraints","dimensions","corner_radii"})
            if(data.contains(key))for(const auto& item:data.at(key))if(item.value("id",std::string{})==args["entity"].get<std::string>()) {
                if(item.dump().size()>65536)throw workspace::SketchOperationError("result_too_large","The entity exceeds the 64 KiB query limit.");
                return Json{{"collection",key},{"entity",item}};
            }
        throw workspace::SketchOperationError("entity_not_found","The Sketch entity does not exist.");
    });
    query({"sketch.create",tr("Create a standalone Sketch using the standard container factory."),{{"name",true},{"plane",false},{"document",false}},true},[this](const Json& args) {
        const auto check=target(args);if(!check.ok)throw workspace::SketchOperationError("document_changed","Sketch creation requires the active document.");
        if(interaction().template_document)throw workspace::SketchOperationError("unsupported_document","Sketch commands require an ordinary Part or Assembly.");
        const auto plane=args.value("plane",std::string{"XY"});if(plane!="XY"&&plane!="XZ"&&plane!="YZ")invalid("Sketch plane must be XY, XZ or YZ.");
        const auto doc=workspace_.active_document_id();
        const auto id=workspace::create_document_sketch(workspace_,kernel_,doc,args["name"].get<std::string>(),plane=="XY"?sketcher::SketchPlane::XY:plane=="XZ"?sketcher::SketchPlane::XZ:sketcher::SketchPlane::YZ);
        change_=Change{ChangeKind::Model,doc,true};auto result=metadata(workspace::document_sketch(workspace_,doc,id));result["document"]=doc;result["changed"]=true;return result;
    });
    add_sketch_command({"sketch.point.create",tr("Add a native point in Sketch coordinates (mm)."),{{"position",true,Type::Array},{"construction",false,Type::Boolean},{"snap_mm",false,Type::Number}}},[](Sketch& s,const Json& a) {
        const auto p=point(a,"position");return Json{{"point",s.add_point(p[0],p[1],snap(a),a.value("construction",false))}};
    });
    add_sketch_command({"sketch.point.move",tr("Move a native point subject to its constraints."),{{"point",true},{"position",true,Type::Array}}},[](Sketch& s,const Json& a) {
        const auto id=a["point"].get<std::string>();require_point(s,id);const auto p=point(a,"position");const auto* before=s.find_point(id);
        if(before->x!=p[0]||before->y!=p[1])if(!s.move_point(id,p[0],p[1]))throw workspace::SketchOperationError("constrained_geometry","The point cannot move to the requested position.");return Json{{"point",id}};
    });
    add_sketch_command({"sketch.point.fixed",tr("Set a native point's fixed state."),{{"point",true},{"fixed",true,Type::Boolean}}},[](Sketch& s,const Json& a) {
        const auto id=a["point"].get<std::string>();require_point(s,id);s.set_point_fixed(id,a["fixed"].get<bool>());return Json{{"point",id}};
    });
    add_sketch_command({"sketch.point.delete",tr("Remove a native point and its dependent Sketch geometry."),{{"point",true}}},[](Sketch& s,const Json& a) {
        const auto id=a["point"].get<std::string>();require_point(s,id);s.remove_point(id);return Json{{"point",id}};
    });
    add_sketch_command({"sketch.segment.create",tr("Add a native segment between two Sketch points (mm)."),{{"first",true,Type::Array},{"second",true,Type::Array},{"construction",false,Type::Boolean},{"snap_mm",false,Type::Number}}},[](Sketch& s,const Json& a) {
        const auto p=point(a,"first"),q=point(a,"second");return Json{{"geometry",s.add_segment(p[0],p[1],q[0],q[1],snap(a),a.value("construction",false))}};
    });
    add_sketch_command({"sketch.segment.centerline",tr("Set a native segment's unbounded centerline state."),{{"segment",true},{"centerline",true,Type::Boolean}}},[](Sketch& s,const Json& a) {
        const auto id=a["segment"].get<std::string>();require_geometry(s,id);s.set_segment_centerline(id,a["centerline"].get<bool>());return Json{{"geometry",id}};
    });
    add_sketch_command({"sketch.circle.create",tr("Add an exact circle with radius in millimetres."),{{"center",true,Type::Array},{"radius_mm",true,Type::Number},{"construction",false,Type::Boolean},{"snap_mm",false,Type::Number}}},[](Sketch& s,const Json& a) {
        const auto p=point(a,"center");return Json{{"geometry",s.add_circle(p[0],p[1],number(a["radius_mm"]),a.value("construction",false),snap(a))}};
    });
    add_sketch_command({"sketch.arc.create",tr("Add an exact circular arc through start and end points."),{{"center",true,Type::Array},{"start",true,Type::Array},{"end",true,Type::Array},{"clockwise",false,Type::Boolean},{"construction",false,Type::Boolean},{"snap_mm",false,Type::Number}}},[](Sketch& s,const Json& a) {
        const auto p=point(a,"center"),q=point(a,"start"),r=point(a,"end");return Json{{"geometry",s.add_arc(p[0],p[1],q[0],q[1],r[0],r[1],a.value("construction",false),snap(a),a.value("clockwise",false))}};
    });
    for(const bool arc:{false,true}) {
        std::vector<commands::Argument> args={{"center",true,Type::Array},{"major",true,Type::Array},{"minor",true,Type::Array}};
        if(arc){args.push_back({"start",true,Type::Array});args.push_back({"end",true,Type::Array});args.push_back({"reversed",false,Type::Boolean});}
        args.push_back({"construction",false,Type::Boolean});args.push_back({"snap_mm",false,Type::Number});
        add_sketch_command({arc?"sketch.elliptical_arc.create":"sketch.ellipse.create",tr("Add exact elliptical geometry from its center and axis endpoints."),args},[arc](Sketch& s,const Json& a) {
            const auto p=point(a,"center"),q=point(a,"major"),r=point(a,"minor");
            if(!arc)return Json{{"geometry",s.add_ellipse(p[0],p[1],q[0],q[1],r[0],r[1],a.value("construction",false),snap(a))}};
            const auto start=point(a,"start"),end=point(a,"end");return Json{{"geometry",s.add_elliptical_arc(p[0],p[1],q[0],q[1],r[0],r[1],start[0],start[1],end[0],end[1],a.value("reversed",false),a.value("construction",false),snap(a))}};
        });
    }
    add_sketch_command({"sketch.bspline.create",tr("Add a native B-spline from control or interpolation points."),{{"points",true,Type::Array},{"degree",false,Type::Integer},{"closed",false,Type::Boolean},{"interpolating",false,Type::Boolean},{"construction",false,Type::Boolean},{"snap_mm",false,Type::Number}}},[](Sketch& s,const Json& a) {
        if(a["points"].size()<2 || a["points"].size()>4096)invalid("A B-spline requires 2 to 4096 points.");
        std::vector<std::array<double,2>> points;for(const auto& data:a["points"]) {if(!data.is_array()||data.size()!=2)invalid("Each B-spline point must be [x, y].");points.push_back({number(data[0]),number(data[1])});}
        const auto degree=integer(a,"degree",3,1,25);
        return Json{{"geometry",s.add_bspline(points,static_cast<unsigned>(degree),a.value("closed",false),a.value("construction",false),snap(a),a.value("interpolating",false))}};
    });
    add_sketch_command({"sketch.rectangle.create",tr("Add a constrained native rectangle from opposite corners."),{{"first",true,Type::Array},{"second",true,Type::Array},{"snap_mm",false,Type::Number}}},[](Sketch& s,const Json& a) {
        const auto p=point(a,"first"),q=point(a,"second");return Json{{"geometry",s.add_rectangle(p[0],p[1],q[0],q[1],snap(a))}};
    });
    add_sketch_command({"sketch.polygon.create",tr("Add a regular polygon using a center and rim point."),{{"center",true,Type::Array},{"rim",true,Type::Array},{"sides",true,Type::Integer},{"snap_mm",false,Type::Number}}},[](Sketch& s,const Json& a) {
        const auto p=point(a,"center"),q=point(a,"rim");const auto result=s.add_regular_polygon(p[0],p[1],q[0],q[1],static_cast<unsigned>(integer(a,"sides",3,3,1024)),snap(a));return Json{{"geometry",result.segment_ids}};
    });
    add_sketch_command({"sketch.geometry.construction",tr("Set the auxiliary state of native Sketch geometry."),{{"geometry",true},{"construction",true,Type::Boolean}}},[](Sketch& s,const Json& a) {
        const auto id=a["geometry"].get<std::string>();require_geometry(s,id);s.set_geometry_construction(id,a["construction"].get<bool>());return Json{{"geometry",id}};
    });
    add_sketch_command({"sketch.geometry.delete",tr("Delete native Sketch geometry using the Sketcher's dependency rules."),{{"geometry",true}}},[](Sketch& s,const Json& a) {
        const auto id=a["geometry"].get<std::string>();require_geometry(s,id);s.remove_geometry(id);return Json{{"geometry",id}};
    });
    add_sketch_command({"sketch.translate",tr("Translate native Sketch points and geometry subject to constraints."),{{"delta",true,Type::Array},{"points",false,Type::Array},{"geometry",false,Type::Array}}},[](Sketch& s,const Json& a) {
        const auto p=point(a,"delta");const auto points=ids(a,"points"),geometry=ids(a,"geometry");
        if(points.empty()&&geometry.empty())invalid("Translation requires at least one native point or geometry ID.");
        for(const auto& id:points)require_point(s,id);for(const auto& id:geometry)require_geometry(s,id);
        if(p[0]!=0||p[1]!=0)if(!s.translate_selection(points,geometry,p[0],p[1]))throw workspace::SketchOperationError("constrained_geometry","The selection cannot translate by the requested displacement.");return Json::object();
    });
}
} // namespace zima::command_host
