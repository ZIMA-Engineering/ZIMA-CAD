#include <zima/command_host/host.hpp>
#include <zima/document/placement_json.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
using namespace zima;
using commands::Json;
namespace fs = std::filesystem;
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void near(double actual, double expected) { require(std::isfinite(actual) && std::abs(actual-expected) < 1e-7, "Independent curve geometry check failed"); }
Json point(double x, double y, double z = 0) { return {{"values", {{"x",x},{"y",y},{"z",z}}}}; }
void verify(const kernel::OcctKernel& kernel, fs::path directory, bool assembly) {
    workspace::Workspace live; command_host::Options options;
    options.settings = [] { return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}}; };
    command_host::Host host(live,kernel,directory,options);
    const auto run = [&](const char* command, Json args = Json::object()) {
        auto result=host.execute({{"command",command},{"arguments",std::move(args)}});
        if (!result.ok) throw std::runtime_error(std::string(command)+": "+result.code+": "+result.message);
        return result.data;
    };
    const std::string name=assembly?"curve-assembly":"curve-part";
    run("new",{{"type",assembly?"assembly":"part"},{"name",name}});
    const auto document=live.active_document_id();
    if (!assembly) run("box.create",{{"length_mm","10"},{"width_mm","10"},{"height_mm","10"}});
    const auto objects = [&]() -> const std::vector<document::ConstructionObject>& {
        return assembly ? live.open_assembly(document)->session.document().constructions
            : live.open_part(document)->session.document().constructions;
    };
    const auto revision = [&]() { return assembly ? live.open_assembly(document)->session.revision() : live.open_part(document)->session.revision(); };
    const auto get = [&](const std::string& id) { return run("construction.get",{{"construction",id}}); };
    const auto set = [&](const std::string& id, Json args) { args["construction"]=id; return run("construction.set",std::move(args)); };
    const auto reject = [&](const char* command, Json args, const char* code) {
        const auto before=objects(); const auto before_revision=revision();
        const auto result=host.execute({{"command",command},{"arguments",std::move(args)}});
        if (result.ok || result.code!=code) throw std::runtime_error(std::string(command)+" expected "+code+", got "+result.code+": "+result.message);
        require(objects()==before && revision()==before_revision && !host.change(),"Rejected curve operation partly committed");
    };
    const auto count=run("construction.list").at("total").get<std::size_t>();
    auto middle=point(10,0); middle["radius_mm"]=2;
    const auto made=run("construction.create",{{"kind","curve3d"},{"name","Dráha žluťoučká"},
        {"curve_type","polyline"},{"rounding_enabled",true},
        {"values",{{"x",20},{"rotation_z",90}}},{"points",{point(0,0),middle,point(10,10)}}});
    const auto id=made.at("construction").get<std::string>();
    const auto original=objects().back();
    const auto a=original.curve_points[0].id,b=original.curve_points[1].id,c=original.curve_points[2].id;
    std::set<std::string> ids{id,original.entity_id,original.container_origin.id};
    for (const auto& child:original.curve_points) {
        require(child.parent_construction_id==id && child.entity_parent_id==child.container_origin.id,"Child lost parent or topology ancestry");
        require(ids.insert(child.id).second && ids.insert(child.entity_id).second && ids.insert(child.container_origin.id).second,"Native identities alias");
    }
    require(get(b).at("coordinate_owner")==id && get(b).at("origin_mm")==Json::array({10,0,0}),"Local child coordinates changed on creation");
    auto route=document::curve3d_route(original);
    require(route.segments.size()==3 && route.segments[1].arc_midpoint.has_value(),"Rounded polyline lacks exact arc");
    near(route.segments[0].end.x,8); near(route.segments[0].end.y,0);
    near(route.segments[1].end.x,10); near(route.segments[1].end.y,2);
    // Radius 2, quarter circle centred at (8,2): midpoint is (8+sqrt(2),2-sqrt(2)).
    near(route.segments[1].arc_midpoint->x,8+std::sqrt(2.0)); near(route.segments[1].arc_midpoint->y,2-std::sqrt(2.0));
    require(route.segments[0].source_id=="curve:segment:"+a+":"+b && route.segments[1].source_id=="curve:rounding:"+b,"Route lost source point identities");
    run("undo"); require(run("construction.list").at("total")==count,"Curve creation is not one Undo step");
    run("redo"); require(objects().back()==original,"Redo changed curve identities/parameters");
    const auto before_noop=revision();
    require(set(id,{{"points",Json::array({Json{{"construction",a}},Json{{"construction",b}},Json{{"construction",c}}})}}).at("changed")==false && revision()==before_noop,"Point-list no-op created a transaction");
    set(b,{{"radius_mm",3},{"name","Roh"}});
    near(document::curve3d_route(objects().back()).segments[0].end.x,7);
    run("undo");require(objects().back()==original,"Child properties did not undo atomically");run("redo");
    const auto retained=objects().back();
    // Reverse the complete point list, retain all IDs and add a new point.
    set(id,{{"rounding_enabled",false},{"points",Json::array({Json{{"construction",c}},Json{{"construction",b}},Json{{"construction",a}},point(-10,0)})}});
    const auto reordered=objects().back(); const auto d=reordered.curve_points.back().id;
    require(reordered.curve_points[0]==retained.curve_points[2] && reordered.curve_points[1]==retained.curve_points[1] && reordered.curve_points[2]==retained.curve_points[0],"Reorder replaced point parameters/identity");
    require(get(d).at("parent_construction")==id && get(d).at("origin_mm")==Json::array({-10,0,0}),"Appended point has wrong frame");
    run("undo");require(objects().back()==retained,"Point-list edit was not one Undo step");run("redo");require(objects().back()==reordered,"Point-list Redo changed new IDs");
    set(id,{{"points",Json::array({Json{{"construction",a}},Json{{"construction",b}},Json{{"construction",c}}})}});
    require(host.execute({{"command","construction.get"},{"arguments",{{"construction",d}}}}).code=="construction_not_found","Omitted point remained in curve");
    run("undo"); require(objects().back()==reordered,"Removed point did not restore with original ID");run("redo");
    set(id,{{"curve_type","interpolating_spline"}});
    set(a,{{"tangent","+x"},{"values",{{"rotation_z",90}}}});
    route=document::curve3d_route(objects().back());
    const auto& poles=route.segments[0].bezier_control_points;
    require(poles.size()==4 && route.segments[0].source_id=="curve:segment:"+a+":"+b,"Spline lost exact cubic or source identity");
    near(poles[0].x,0);near(poles[0].y,0);near(poles[1].x,0);require(poles[1].y>0,"Local tangent rotation ignored");
    near(poles[3].x,10);near(poles[3].y,0);
    set(a,{{"tangent","-x"}});route=document::curve3d_route(objects().back());
    require(route.segments[0].bezier_control_points[1].y<0,"Tangent flip ignored");
    set(a,{{"tangent_enabled",false}});require(get(a).at("tangent")=="-x" && get(a).at("tangent_enabled")==false,"Disabling tangent erased selected direction");
    set(a,{{"tangent_enabled",true}});require(get(a).at("tangent")=="-x","Enabling tangent lost retained direction");
    set(a,{{"tangent","automatic"}});require(get(a).at("tangent_enabled")==false,"Automatic tangent remained driven");
    set(a,{{"tangent_enabled",true}});require(get(a).at("tangent")=="+x","GUI-compatible default tangent missing");
    for (const auto* mode:{"+y","-y","+z","-z"}) {set(c,{{"tangent",mode}});require(get(c).at("tangent")==mode,"Axis tangent not persisted");}
    reject("construction.set",{{"construction",id},{"rounding_enabled",true}},"parameter_not_editable");
    reject("construction.set",{{"construction",b},{"radius_mm",1}},"parameter_not_editable");
    set(id,{{"curve_type","polyline"},{"rounding_enabled",true}});
    for (Json bad:{Json(-1),Json(1000000001.0),Json("2"),Json(true),Json(std::numeric_limits<double>::infinity())})
        reject("construction.set",{{"construction",b},{"name","partial"},{"radius_mm",bad}},"invalid_arguments");
    reject("construction.set",{{"construction",b},{"radius_mm",10}},"construction_rejected");
    reject("construction.set",{{"construction",a},{"radius_mm",1}},"parameter_not_editable");
    reject("construction.set",{{"construction",id},{"tangent","+x"}},"invalid_arguments");
    reject("construction.set",{{"construction",a},{"tangent","x"}},"invalid_arguments");
    reject("construction.set",{{"construction",id},{"curve_type","bspline"}},"invalid_arguments");
    const auto invalid_points=std::vector<Json>{Json::array(),Json::array({point(0,0)}),Json::array({point(0,0),"bad"}),
        Json::array({point(0,0),Json{{"values",{{"x",1}}},{"owner","fake"}}}),
        Json::array({Json{{"construction",a}},Json{{"construction",a}}}),
        Json::array({Json{{"construction",original.entity_id}},point(1,0)}),
        Json::array({Json{{"construction",a}},Json{{"construction",b},{"values",{{"x",0},{"y",0}}}}})};
    for (std::size_t i=0;i<invalid_points.size();++i)
        reject("construction.set",{{"construction",id},{"name","partial"},{"points",invalid_points[i]}},i==5?"construction_not_found":i==6?"construction_rejected":"invalid_arguments");
    reject("construction.create",{{"kind","point"},{"name","wrong"},{"points",{point(0,0),point(1,0)}}},"invalid_arguments");
    // Child locks must survive list edits and remain binding through either entry point.
    if (assembly) {auto* state=live.open_assembly(document);auto next=state->session.document();next.find_construction(b)->value_locks.insert("radius");state->session.commit(std::move(next));}
    else {auto* state=live.open_part(document);auto next=state->session.document();next.find_construction(b)->value_locks.insert("radius");state->session.commit(std::move(next),state->session.calculated_boundaries());}
    reject("construction.set",{{"construction",b},{"radius_mm",2}},"value_locked");
    reject("construction.set",{{"construction",id},{"points",Json::array({Json{{"construction",a}},Json{{"construction",b},{"radius_mm",2}},Json{{"construction",c}}})}},"value_locked");
    // Saving needs no sidecar, GUI or body recalculation.
    run("save");
    const auto saved=assembly?assembly::AssemblyDocument::load(directory/(name+".asmz")).constructions
        :document::PartDocument::load(directory/(name+".prtz")).constructions;
    require(saved==objects(),"Native curve roundtrip lost topology, locks, local coordinates or tangents");
    if (!assembly) {const auto& cache=live.open_part(document)->session.calculated_boundaries();require(cache.size()==1,"Curve edits changed body calculation count");near(cache.back().volume,1000);}
    const auto source_plane=run("construction.create",{{"kind","plane"},{"name","Reference plane"},{"base_plane","yz"}});
    const auto framed=run("construction.create",{{"kind","curve3d"},{"name","Referenced frame"},
        {"points",{point(0,0),point(10,2),point(20,10)}}});
    const auto framed_id=framed.at("construction").get<std::string>();
    const auto framed_a=framed.at("children")[0].get<std::string>(),framed_b=framed.at("children")[1].get<std::string>(),framed_c=framed.at("children")[2].get<std::string>();
    const auto attach=[&](auto& next) {
        auto* child=next.find_construction(framed_b);
        child->references={{{},source_plane.at("entity").get<std::string>(),"plane",10,true}};
        child->definition=document::ConstructionDefinition::PointReference;
        next.resolve_constructions();require(next.find_construction(framed_id)->reference_valid,"Referenced curve fixture failed");
    };
    if(assembly) {auto* state=live.open_assembly(document);auto next=state->session.document();attach(next);state->session.commit(std::move(next));}
    else {auto* state=live.open_part(document);auto next=state->session.document();attach(next);state->session.commit(std::move(next),state->session.calculated_boundaries());}
    const auto stored_reference=get(framed_b).at("references");
    // World X is constrained. After rotating the curve 90 degrees, local X
    // becomes free and local Y constrained. Validate in the proposed frame.
    set(framed_id,{{"values",{{"rotation_z",90}}},{"points",Json::array({Json{{"construction",framed_a}},
        Json{{"construction",framed_b},{"values",{{"x",7}}}},Json{{"construction",framed_c}}})}});
    near(get(framed_b).at("origin_mm")[0],7);near(get(framed_b).at("origin_mm")[1],-10);
    require(get(framed_b).at("references")==stored_reference,"Frame edit replaced exact plane reference");
    reject("construction.set",{{"construction",framed_id},{"values",{{"rotation_z",0}}},
        {"points",Json::array({Json{{"construction",framed_a}},Json{{"construction",framed_b},{"values",{{"x",9}}}},Json{{"construction",framed_c}}})}},"parameter_not_editable");
    set(framed_id,{{"values",{{"rotation_z",0}}},{"points",Json::array({Json{{"construction",framed_a}},
        Json{{"construction",framed_b},{"values",{{"y",7}}}},Json{{"construction",framed_c}}})}});
    near(get(framed_b).at("origin_mm")[0],10);near(get(framed_b).at("origin_mm")[1],7);
    run("undo");near(get(framed_b).at("origin_mm")[0],7);near(get(framed_b).at("origin_mm")[1],-10);run("redo");
    // A new point starts at zero before its patch; frame preparation must not
    // validate that unfinished route (it would temporarily coincide with A).
    set(framed_id,{{"points",Json::array({Json{{"construction",framed_a}},point(3,4),
        Json{{"construction",framed_b},{"values",{{"y",4}}}},Json{{"construction",framed_c}}})}});
    require(get(framed_id).at("child_count")==4 && get(framed_b).at("origin_mm")==Json::array({10,4,0}),"Combined new/referenced point edit failed");
    const auto own_frame=[&](auto& next) {
        auto* curve=next.find_construction(framed_id);auto* child=next.find_construction(framed_b);
        child->references={{{},curve->container_origin.id,"origin:plane:yz",10,true}};
        next.resolve_constructions();require(next.find_construction(framed_id)->reference_valid,"Own-origin reference fixture failed");
    };
    if(assembly) {auto* state=live.open_assembly(document);auto next=state->session.document();own_frame(next);state->session.commit(std::move(next));}
    else {auto* state=live.open_part(document);auto next=state->session.document();own_frame(next);state->session.commit(std::move(next),state->session.calculated_boundaries());}
    const auto children=get(framed_id).at("children");
    set(framed_id,{{"values",{{"rotation_z",90}}},{"points",Json::array({Json{{"construction",children[0]}},Json{{"construction",children[1]}},
        Json{{"construction",framed_b},{"values",{{"y",5}}}},Json{{"construction",framed_c}}})}});
    near(get(framed_b).at("origin_mm")[0],10);near(get(framed_b).at("origin_mm")[1],5);
    run("save");
    const auto saved_references=assembly?assembly::AssemblyDocument::load(directory/(name+".asmz")).constructions
        :document::PartDocument::load(directory/(name+".prtz")).constructions;
    require(saved_references==objects(),"Native save lost referenced curve edits");
}
}
int main() {try {
    const auto root=fs::canonical(fs::temp_directory_path());
    const auto directory=root/("zima-curve-commands-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(directory),"Cannot create test directory");
    kernel::OcctKernel kernel;verify(kernel,directory,false);verify(kernel,directory,true);
    require(directory.parent_path()==root,"Unexpected cleanup target");fs::remove_all(directory);
    std::cout<<"3D curve commands: exact geometry, stable point edits, tangents, radii, locks, atomic errors, Undo and Part/Assembly persistence passed\n";return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
