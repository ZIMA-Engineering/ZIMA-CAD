#include "construction_query_test_support.hpp"
#include <zima/document/file_path.hpp>
#include <iostream>
#include <cmath>
#include <limits>
using namespace zima;
using commands::Json;
namespace fs = std::filesystem;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
commands::Result run(command_host::Host& host, const char* command, Json args = Json::object()) {
    auto result = host.execute({{"command", command}, {"arguments", std::move(args)}});
    if (!result.ok) throw std::runtime_error(std::string(command) + ": " + result.code + ": " + result.message);
    return result;
}
void verify(const kernel::OcctKernel& kernel, fs::path directory) {
    workspace::Workspace live; command_host::Interaction interaction;
    command_host::Options options; options.interaction = [&] { return interaction; };
    command_host::Host host(live, kernel, directory, options);
    require(host.execute_text("construction.list").code == "unsupported_document", "Empty query accepted");
    auto native = test::construction_query_fixture();
    const auto id = native.document_id, body = native.body_history.active_body_id();
    const auto curve = native.constructions[3];
    // A real calculated snapshot makes accidental cache replacement observable.
    const auto calculated = kernel.make_box({2, 3, 4});
    live.add_part(native, {calculated}, directory / "construction.prtz"); live.activate(id);
    auto* state = live.open_part(id);
    auto edited = state->session.document(); edited.name = "Neuložená změna";
    state->session.commit(edited, state->session.calculated_boundaries());
    const auto revision = state->session.revision(), generation = state->session.data_generation();
    const auto* cache = state->session.calculated_boundaries().data();
    const auto before = state->session.document().constructions;
    const auto all = run(host, "construction.list").data;
    require(all.at("total") == 7 && all.at("items").size() == 7, "List omitted constructions or child Points");
    require(all.at("items")[0].at("parent") == body && all.at("items")[0].at("origin") == native.constructions[0].container_origin.id, "Body ownership or original identity lost");
    const auto page = run(host, "construction.list", {{"parent", curve.id}, {"limit", 2}}).data;
    require(page.at("total") == 3 && page.at("next_offset") == 2, "Child pagination failed");
    const auto last = run(host, "construction.list", {{"parent", curve.id}, {"limit", 2}, {"offset", 2}}).data;
    require(last.at("items")[0].at("construction") == curve.curve_points[2].id && last.at("more") == false && last.at("next_offset").is_null(), "Last page is wrong");
    require(run(host, "construction.list", {{"offset", 100}}).data.at("items").empty(), "Offset beyond end failed");
    require(run(host, "construction.list", {{"kind", "point"}}).data.at("total") == 4, "Point filter omitted owned Points");
    require(run(host, "construction.list", {{"parent", body}}).data.at("total") == 4, "Parent filter is recursive");
    auto get = [&](const std::string& object) { return run(host, "construction.get", {{"construction", object}}).data; };
    const auto point = get(native.constructions[0].id);
    require(point.at("origin_mm") == Json::array({3, 4, 5}) && point.at("coordinate_system") == "body" && point.at("coordinate_owner") == body, "Query silently transformed body coordinates");
    require(point.at("rotation_degrees") == Json::array({10, 20, 30}) && point.at("value_locks") == Json::array({"placement:x"}), "Angles/locks lost");
    require(get(native.constructions[1].id).at("direction") == Json::array({0, 0, 1}), "Stored axis direction lost");
    const auto plane = get(native.constructions[2].id);
    require(plane.at("reference_valid") == false && plane.at("entity_origin_mm") == Json::array({3, 4, 12}) && plane.at("offset_mm") == 7, "Invalid plane lost last stored position or offset");
    require(plane.at("references")[0].at("instance_path") == "repeat/leaf" && plane.at("references")[0].at("offset_locked") == true, "Exact reference or lock lost");
    test::check_construction_child(get(curve.curve_points[1].id), curve, body);
    const auto bounded = run(host, "construction.get", {{"construction", curve.id}, {"limit", 1}}).data;
    require(bounded.at("children").size() == 1 && bounded.at("children_truncated") == true && bounded.at("child_count") == 3, "Curve details copied all children");
    for (const auto* command : {"construction.list", "construction.get"}) {
        for (const Json& invalid : std::vector<Json>{-1, 0, 5001, true, "5"}) {
            Json args = {{"limit", invalid}};
            if (std::string(command) == "construction.get") args["construction"] = curve.id;
            require(host.execute({{"command", command}, {"arguments", args}}).code == "invalid_arguments", "Invalid limit accepted");
        }
    }
    require(host.execute_text("construction.list triangle").code == "invalid_arguments", "Invalid kind accepted");
    require(host.execute_text("construction.get " + curve.entity_id).code == "construction_not_found", "Entity was guessed as container");
    interaction.editing = true; interaction.active_occurrence = "repeated/active";
    get(curve.id); run(host, "construction.list");
    require(!host.change() && state->session.revision() == revision && state->session.data_generation() == generation &&
        state->session.calculated_boundaries().data() == cache && state->session.document().constructions == before && state->session.is_dirty(), "Queries changed model/history/cache");
    interaction = {};
    auto assembly = assembly::AssemblyDocument::create_default(); assembly.constructions = native.constructions;
    live.add_assembly(assembly, directory / "construction.asmz"); live.activate(assembly.document_id);
    test::check_construction_child(get(curve.curve_points[1].id), curve, {});
    require(get(native.constructions[0].id).at("coordinate_system") == "document", "Assembly coordinates mislabeled");
    require(run(host, "construction.get", {{"construction", curve.id}, {"document", id}}).data.at("body") == body && live.active_document_id() == assembly.document_id, "Explicit inactive source query changed activation");
    require(host.execute_text("construction.get missing").code == "construction_not_found", "Missing ID accepted");
    require(host.execute({{"command", "construction.list"}, {"arguments", {{"document", "missing"}}}}).code == "unsupported_document", "Missing document accepted");
    // Native persistence is the only data source needed by the queries.
    native.save(directory / "native.prtz"); assembly.save(directory / "native.asmz");
    workspace::Workspace reopened;
    reopened.add_part(document::PartDocument::load(directory / "native.prtz"));
    reopened.add_assembly(assembly::AssemblyDocument::load(directory / "native.asmz"));
    command_host::Host reopened_host(reopened, kernel, directory);
    reopened.activate(id);
    test::check_construction_child(run(reopened_host, "construction.get", {{"construction", curve.curve_points[1].id}}).data, curve, body);
    const auto missing_plane_id=native.constructions[2].id;
    const auto reopened_plane=run(reopened_host,"construction.get",{{"construction",missing_plane_id}}).data;
    require(reopened_plane.at("reference_valid")==false&&reopened_plane.at("entity_origin_mm")==Json::array({3,4,12}),"Native Part lost last plane geometry or repaired a missing reference during read");
    reopened.activate(assembly.document_id);
    const auto reopened_assembly_plane=run(reopened_host,"construction.get",{{"construction",missing_plane_id}}).data;
    require(reopened_assembly_plane.at("reference_valid")==false&&reopened_assembly_plane.at("entity_origin_mm")==Json::array({3,4,12}),"Native Assembly lost last plane geometry or repaired a missing reference during read");
    test::check_construction_child(run(reopened_host, "construction.get", {{"construction", curve.curve_points[1].id}}).data, curve, {});

}
void near(double a, double b) {
    if (std::abs(a-b)>1e-7) throw std::runtime_error("Expected " + std::to_string(b) + ", got " + std::to_string(a));
}
void edit_verify(const kernel::OcctKernel& kernel, fs::path directory) {
    workspace::Workspace live; command_host::Interaction interaction; command_host::Options options;
    options.settings=[] { return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}}; };
    options.interaction=[&] { return interaction; }; command_host::Host host(live,kernel,directory,options);
    run(host,"new",{{"type","part"},{"name","construction-edit"}});
    const auto document=live.active_document_id(); auto* state=live.open_part(document);
    run(host,"box.create",{{"length_mm","10"},{"width_mm","20"},{"height_mm","30"}});
    const auto body=state->session.document().body_history.active_body_id();
    const auto shape=state->session.calculated_boundaries().back().kernel_shape;
    const auto create=[&](const char* type, Json extra=Json::object()) {
        extra["kind"]=type;extra["name"]="  Konstrukce žluťoučká  ";
        return run(host,"construction.create",std::move(extra)).data;
    };
    const auto get=[&](const std::string& id) {return run(host,"construction.get",{{"construction",id}}).data;};
    const auto set=[&](const std::string& id, Json args) {args["construction"]=id;return run(host,"construction.set",std::move(args)).data;};
    const auto point=create("point",{{"values",{{"x",1},{"y",2},{"z",3}}}});
    const auto point_id=point.at("construction").get<std::string>();
    require(point.at("body")==body&&point.at("name")=="Konstrukce žluťoučká"&&point.at("origin_mm")==Json::array({1,2,3}),"Point creation lost values/owner/name");
    require(point.at("entity_parent")==point.at("origin")&&point.at("origin")!=point.at("construction"),"Point ancestry collapsed");
    const auto axis=create("axis",{{"direction_axis","x"},{"display_size_mm",25.25},{"values",{{"rotation_z",90}}}});
    const auto axis_id=axis.at("construction").get<std::string>();
    near(axis.at("direction")[0],0);near(axis.at("direction")[1],1);near(axis.at("direction")[2],0);
    run(host,"placement.set",{{"object",axis_id},{"values",{{"rotation_z",180}}}});
    near(get(axis_id).at("direction")[0],-1);near(get(axis_id).at("direction")[1],0);
    run(host,"undo");
    set(axis_id,{{"direction_axis","z"},{"values",{{"rotation_y",90},{"rotation_z",0}}}});
    near(get(axis_id).at("direction")[0],1);near(get(axis_id).at("direction")[2],0);
    run(host,"undo");
    const auto plane=create("plane",{{"base_plane","yz"},{"offset_mm",10},{"values",{{"x",1},{"y",2},{"z",3},{"rotation_z",90}}}});
    const auto plane_id=plane.at("construction").get<std::string>();
    near(plane.at("entity_origin_mm")[0],1);near(plane.at("entity_origin_mm")[1],12);near(plane.at("entity_origin_mm")[2],3);
    require(state->session.calculated_boundaries().back().kernel_shape==shape,"Creating constructions recalculated body");
    const auto plane_origin=plane.at("origin");
    set(plane_id,{{"base_plane","xy"},{"offset_mm",5},{"values",{{"rotation_x",90},{"rotation_z",0},{"x",4}}}});
    const auto edited=get(plane_id);
    near(edited.at("entity_origin_mm")[0],4);near(edited.at("entity_origin_mm")[1],-3);near(edited.at("entity_origin_mm")[2],3);
    require(edited.at("origin")==plane_origin&&edited.at("entity")==plane.at("entity"),"Edit replaced native identities");
    run(host,"undo");require(get(plane_id).at("base_plane")=="yz"&&get(plane_id).at("offset_mm")==10,"Multi-field edit did not undo in one step");run(host,"redo");
    const auto revision=state->session.revision();const auto* cache=state->session.calculated_boundaries().data();
    require(set(plane_id,{{"offset_mm",5}}).at("changed")==false&&state->session.revision()==revision&&state->session.calculated_boundaries().data()==cache&&!host.change(),"No-op changed history/cache");
    const auto snapshot=state->session.document().constructions;
    const auto reject=[&](const char* command, Json args, const char* code) {
        const auto revision=state->session.revision();const auto* cache=state->session.calculated_boundaries().data();
        const auto before=state->session.document().constructions;
        const auto result=host.execute({{"command",command},{"arguments",std::move(args)}});
        if(result.ok||result.code!=code) throw std::runtime_error(std::string(command)+" expected "+code+", got "+result.code+": "+result.message);
        require(state->session.revision()==revision&&state->session.document().constructions==before&&state->session.calculated_boundaries().data()==cache&&!host.change(),"Rejected construction partly committed");
    };
    for(const Json& invalid:std::vector<Json>{-0.1,0,1000001,true,"bad"})
        reject("construction.set",{{"construction",axis_id},{"name","partial"},{"display_size_mm",invalid}},"invalid_arguments");
    for(Json patch:std::vector<Json>{Json::object(),{{"name","  "}},{{"offset_mm",2}},{{"direction_axis","bad"}},{{"values",Json::object()}},{{"values",{{"x",2},{"z",true}}}},{{"values",{{"x",1},{"unknown",3}}}}}) {
        const bool unknown=patch.contains("values")&&patch["values"].contains("unknown");
        patch["construction"]=axis_id;reject("construction.set",patch,unknown?"parameter_not_editable":"invalid_arguments");
    }
    reject("construction.set",{{"construction",axis_id},{"display_size_mm",std::numeric_limits<double>::infinity()}},"invalid_arguments");
    reject("construction.create",{{"kind","curve3d"},{"name","not-yet"}},"invalid_arguments");
    reject("construction.create",{{"kind","point"},{"name","partial"},{"offset_mm",1}},"invalid_arguments");
    reject("construction.set",{{"construction",point.at("entity")},{"name","wrong identity"}},"construction_not_found");
    require(state->session.document().constructions==snapshot,"Invalid input changed source");
    interaction.editing=true;reject("construction.set",{{"construction",point_id},{"name","blocked"}},"editing_in_progress");interaction={};
    interaction.active_occurrence="sub/part";reject("construction.create",{{"kind","point"},{"name","blocked"}},"active_occurrence");interaction={};
    interaction.template_document=true;reject("construction.create",{{"kind","point"},{"name","blocked"}},"unsupported_document");interaction={};
    auto locked=state->session.document();locked.find_construction(axis_id)->value_locks.insert("length");locked.find_construction(plane_id)->value_locks.insert("offset");
    locked.find_construction(point_id)->value_locks.insert("placement:y");
    state->session.commit(std::move(locked),state->session.calculated_boundaries());
    reject("construction.set",{{"construction",axis_id},{"name","partial"},{"display_size_mm",55}},"value_locked");
    reject("construction.set",{{"construction",plane_id},{"base_plane","yz"},{"offset_mm",7}},"value_locked");
    reject("construction.set",{{"construction",point_id},{"name","partial"},{"values",{{"x",9},{"y",8}}}},"parameter_not_editable");
    auto referenced=state->session.document();auto* referenced_point=referenced.find_construction(point_id);
    referenced_point->references={{{},referenced.body_history.find(body)->origin().id,"origin:plane:xy",3,true}};
    referenced_point->definition=document::ConstructionDefinition::PointReference;
    referenced.resolve_constructions();require(referenced.find_construction(point_id)->reference_valid,"Reference fixture did not resolve");state->session.commit(std::move(referenced),state->session.calculated_boundaries());
    reject("construction.set",{{"construction",point_id},{"name","partial"},{"values",{{"z",9}}}},"parameter_not_editable");
    const auto refs=get(point_id).at("references");set(point_id,{{"name","Renamed reference point"},{"values",{{"x",6}}}});
    require(get(point_id).at("references")==refs,"Editing properties changed exact references");
    auto broken=state->session.document();broken.find_construction(point_id)->references[0].owner_id="missing";
    state->session.commit(std::move(broken),state->session.calculated_boundaries());
    reject("construction.set",{{"construction",point_id},{"name","partial"}},"construction_rejected");run(host,"undo");
    run(host,"body.activate");
    reject("construction.create",{{"kind","point"},{"name","no body"}},"inactive_body");
    reject("construction.set",{{"construction",axis_id},{"name","no body"}},"inactive_body");
    run(host,"body.activate",{{"body",body}});
    // A local point in a Body rotated 90 degrees around Z is independently
    // expected at (100-y, x, z) in the document, not at its unchanged local value.
    run(host,"placement.set",{{"object",body},{"values",{{"reference_offset:2",100},{"rotation_z",90}}}});
    const auto moved_shape=state->session.calculated_boundaries().back().kernel_shape;
    const auto local=create("point",{{"values",{{"x",7},{"y",8},{"z",9}}}});
    require(local.at("origin_mm")==Json::array({7,8,9})&&local.at("coordinate_owner")==body,"Construction leaked document coordinates");
    const auto scene=state->session.document().construction_viewer_mesh();bool found=false;
    for(const auto& candidate:scene.original_references.points) if(candidate.reference.owner_id==local.at("origin").get<std::string>()&&candidate.reference.semantic_key=="point") {
        near(candidate.position.x,92);near(candidate.position.y,7);near(candidate.position.z,9);found=true;
    }
    require(found,"Created point missing from native scene reference geometry");
    require(state->session.calculated_boundaries().back().kernel_shape==moved_shape,"Construction recomputed calculated body");
    auto curve=test::construction_query_fixture().constructions[3];curve.suppressed=false;
    auto with_curve=state->session.document();with_curve.insert_history_entry(document::PartHistoryKind::Construction,curve.id);
    with_curve.constructions.push_back(curve);with_curve.resolve_constructions();
    state->session.commit(std::move(with_curve),state->session.calculated_boundaries());
    const auto child_id=curve.curve_points[1].id;const auto sibling=get(curve.curve_points[0].id);
    set(child_id,{{"name","Edited curve point"},{"values",{{"y",4}}}});
    require(get(child_id).at("coordinate_owner")==curve.id&&get(child_id).at("origin_mm")[1]==4&&get(curve.curve_points[0].id).at("origin_mm")==sibling.at("origin_mm"),"Child edit affected sibling/frame");
    require(get(child_id).at("radius_mm")==0.125&&get(child_id).at("tangent")=="-y","Child edit lost curve parameters");
    reject("construction.set",{{"construction",child_id},{"name","partial"},{"values",{{"x",0},{"y",0}}}},"construction_rejected");
    reject("placement.set",{{"object",child_id},{"values",{{"x",0},{"y",0}}}},"placement_rejected");
    require(state->session.calculated_boundaries().back().kernel_shape==moved_shape,"Curve point edit recalculated solid");
    run(host,"save");const auto saved=document::PartDocument::load(directory/"construction-edit.prtz");
    require(saved.constructions==state->session.document().constructions,"Native Part lost construction properties");
    run(host,"new",{{"type","assembly"},{"name","construction-assembly"}});
    const auto assembly_id=live.active_document_id();
    const auto ap=create("plane",{{"base_plane","xy"},{"offset_mm",12.5},{"values",{{"x",3}}}});
    const auto ap_id=ap.at("construction").get<std::string>();
    require(ap.at("coordinate_system")=="document"&&ap.at("coordinate_owner")==assembly_id,"Assembly frame ownership wrong");
    near(ap.at("entity_origin_mm")[2],12.5);
    set(ap_id,{{"offset_mm",15}});run(host,"undo");near(get(ap_id).at("offset_mm"),12.5);run(host,"redo");
    run(host,"save");require(assembly::AssemblyDocument::load(directory/"construction-assembly.asmz").constructions==live.open_assembly(assembly_id)->session.document().constructions,"Native Assembly lost construction parameters");
    require(host.execute({{"command","construction.set"},{"arguments",{{"construction",axis_id},{"document",document},{"name","inactive"}}}}).code=="document_changed","Inactive document mutation accepted");
    // Creation is a single Undo step too, including its history ownership.
    run(host,"undo");run(host,"undo");require(run(host,"construction.list").data.at("total")==0,"Creation did not undo as one operation");
    run(host,"redo");require(get(ap_id).at("entity")==ap.at("entity"),"Redo allocated new topology identity");
}
}
int main() { try {
    const auto root = fs::canonical(fs::temp_directory_path());
    const auto directory = root / ("zima-constructions-" + document::PartDocument::create_default().document_id);
    require(fs::create_directory(directory), "Cannot create fixture directory");
    kernel::OcctKernel kernel; verify(kernel, directory); edit_verify(kernel, directory);
    require(directory.parent_path() == root, "Unexpected cleanup path"); fs::remove_all(directory);
    std::cout << "Construction queries and edits: geometry, identities, ownership, atomic rejection, locks, Undo/Redo and native persistence passed\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; } }
