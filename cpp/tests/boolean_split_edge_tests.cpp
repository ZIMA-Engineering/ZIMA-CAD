#include "profile_command_fixture.hpp"
#include "profile_solid_fixture.hpp"
#include <zima/workspace/profile_operations.hpp>
#include <zima/command_host/host.hpp>
#include <zima/workspace/primitive_operations.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <BRepTools.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRep_Builder.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS_Shape.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopoDS.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <sstream>

using namespace zima;
using commands::Json;
namespace fs = std::filesystem;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void near(double value, double expected, double tolerance = 1e-5) {
    if (!std::isfinite(value) || std::abs(value - expected) > tolerance)
        throw std::runtime_error("Expected " + std::to_string(expected) + ", got " + std::to_string(value));
}
using Key = std::pair<std::string, std::string>;
Key key(const kernel::EdgeReference& ref) { return {ref.owner_id, ref.semantic_key}; }
Json ref(const kernel::EdgeReference& value) { return {{"owner", value.owner_id}, {"key", value.semantic_key}}; }
Json ref(const kernel::VertexReference& value) { return {{"owner", value.owner_id}, {"key", value.semantic_key}}; }
bool longitudinal(const kernel::ViewerEdge& edge) {
    if (edge.points.size() < 2) return false;
    const auto a = edge.points.front(), b = edge.points.back();
    return std::abs(a.x - b.x) < 1e-6 && std::abs(a.z - b.z) < 1e-6 && std::abs(a.y - b.y) > 1;
}
struct Fixture {
    kernel::OcctKernel kernel;
    workspace::Workspace live;
    fs::path directory;
    command_host::Host host;
    std::string extrusion, cavity, sketch;
    Fixture(fs::path path) : directory(std::move(path)), host(live, kernel, directory, options()) {}
    static command_host::Options options() {
        command_host::Options value;
        value.settings = [] { command_host::Settings settings;
            settings.templates = {fs::absolute("config/templates"), "START_PART.prtz", "START_ASSEMBLY.asmz", "Body"}; return settings; };
        return value;
    }
    Json run(const char* command, Json args = Json::object()) {
        const auto result = host.execute({{"command", command}, {"arguments", args}});
        if (!result.ok) throw std::runtime_error(std::string(command) + ": " + result.code + ": " + result.message);
        return result.data;
    }
    workspace::PartState& state() { return *live.open_part(live.active_document_id()); }
    const kernel::BodyResult& result() { return state().session.calculated_boundaries().back(); }
    void box(double length, double width, double height, double y, double z, bool subtract) {
        auto fixture=state().session.document();
        auto value = zima::test::rectangular_feature(fixture,{length,width,height}); value.placement.y = y; value.placement.z = z;
        value.combine_mode = subtract ? document::CombineMode::Subtract : document::CombineMode::Add;
        if (subtract) cavity = value.id;
        workspace::commit_profile(live,kernel,live.active_document_id(),value,workspace::ProfileEditMode::Create,fixture.sketches.back());
    }
    void create(const std::string& name) {
        run("new", {{"type", "part"}, {"name", name}});
        if (name == "three-solids") {
            for (double y : {-32., 0., 32.}) box(100, 6, 50, y, 0, false);
        } else {
            box(100, 80, 50, 0, 0, false);
            if (name == "shell") {
                const auto source = state().session.document().history.front().id;
                cavity = run("shell.create", {{"thickness_mm", 6}, {"faces", Json::array({Json{{"owner", source}, {"key", test::profile_key(state().session.document(),source,"z_max")}}})}}).at("container");
            } else if (name != "solid") {
                const bool closed = name == "closed";
                box(88, 68, closed ? 38 : 50, name == "offset" ? 2 : 0, closed ? 0 : 6, true);
            }
        }
        near(result().volume, name == "three-solids" ? 90000 : name == "solid" ? 400000 : name == "closed" ? 172608 : 136704);
        sketch = run("sketch.create", {{"plane", "XZ"}, {"name", "Through profile"}}).at("sketch");
        // Local XZ Sketch Y is world -Z. Cut world X=-26..26, Z=-4..25.
        run("sketch.rectangle.create", {{"sketch", sketch}, {"first", {-26, -25}}, {"second", {26, 4}}});
        extrusion = run("extrusion.create", {{"sketch", sketch}, {"combine", "subtract"}, {"extent", "two_sides"},
            {"end_forward", "through_all"}, {"end_reverse", "through_all"}}).at("container");
        near(result().volume, name == "three-solids" ? 62856 : name == "solid" ? 279360 : name == "closed" ? 133296 : 118608);
    }
    std::vector<kernel::ViewerEdge> longitudinal_edges() {
        std::vector<kernel::ViewerEdge> edges;
        for (const auto& edge : result().mesh.edges)
            if (edge.reference.owner_id == extrusion && longitudinal(edge)) edges.push_back(edge);
        return edges;
    }
};
void unique_edges(const kernel::BodyResult& result) {
    std::set<Key> ids;
    for (const auto& edge : result.mesh.edges) {
        require(edge.reference.valid(), "Boolean output has an anonymous edge");
        require(ids.insert(key(edge.reference)).second, "Distinct Boolean fragments share an edge identity");
        require(edge.edge_treatment_endpoint_references.size() == 2, "Open fragment lacks persisted endpoints");
        require(edge.edge_treatment_endpoint_references[0] != edge.edge_treatment_endpoint_references[1], "Fragment endpoints share identity");
    }
}
void siblings_survive(Fixture& f, const std::vector<kernel::ViewerEdge>& source, const kernel::EdgeReference& treated) {
    for (const auto& sibling : source) {
        if (sibling.reference == treated) continue;
        const auto found = std::ranges::find(f.result().mesh.edges, sibling.reference, &kernel::ViewerEdge::reference);
        require(found != f.result().mesh.edges.end(), "Treating one fragment consumed a disconnected sibling");
        require(found->points == sibling.points, "Treating one fragment changed another fragment's geometry");
    }
}
void exercise(fs::path directory, const std::string& name) {
    Fixture f(directory); f.create(name); unique_edges(f.result());
    const auto original_volume = f.result().volume;
    const auto edges = f.longitudinal_edges();
    require(edges.size() == (name == "three-solids" ? 12 : name == "solid" ? 4 : 8), "Unexpected count of real longitudinal fragments");
    if (name == "three-solids") {
        auto operations = f.state().session.document().kernel_operations();
        std::reverse(operations.begin(), operations.begin() + 3);
        kernel::OcctKernel reordered;
        const auto rebuilt = reordered.evaluate_history(operations);
        near(rebuilt.back().volume, original_volume);
        std::set<Key> before, after;
        for (const auto& edge : f.result().mesh.edges) before.insert(key(edge.reference));
        for (const auto& edge : rebuilt.back().mesh.edges) after.insert(key(edge.reference));
        require(before == after, "Reordering independent solids replaced fragment identities");
    }
    for (const auto& edge : edges) {
        const auto query = f.run("edge_treatment.route", {{"seed", ref(edge.reference)}});
        require(query.at("edges").size() == 1 && query.at("endpoints_complete") == true && query.at("endpoints").size() == 2,
            "A fragment route crossed a cavity or lost its ends");
        for (const bool fillet : {true, false}) {
            const auto id = f.run(fillet ? "fillet.create" : "chamfer.create", {
                {fillet ? "radius_mm" : "distance_a_mm", .5}, {"routes", Json::array({Json{{"edges", Json::array({ref(edge.reference)})}}})}}).at("container");
            require(std::abs(f.result().volume - original_volume) > .01, "Treatment did not change the calculated solid");
            siblings_survive(f, edges, edge.reference);
            f.run("undo"); near(f.result().volume, original_volume);
            require(!f.state().session.document().find_container(id), "Treatment did not undo in one step");
        }
    }
    // A saved variable Fillet must resolve an explicit R1 at either wall after
    // a cold rebuild; storage and subsequent dimension changes use those IDs.
    const auto& selected = edges.front();
    const auto start = selected.edge_treatment_endpoint_references.front();
    const auto fillet = f.run("fillet.create", {{"mode", "linear"}, {"radius_mm", .25}, {"radius_end_mm", .5},
        {"routes", Json::array({Json{{"edges", Json::array({ref(selected.reference)})}, {"start", ref(start)}}})}}).at("container").get<std::string>();
    siblings_survive(f, edges, selected.reference);
    const auto value = f.result().volume;
    f.run("save");
    std::vector<kernel::BodyResult> saved;
    const auto loaded = document::PartDocument::load(directory / (name + ".prtz"), &saved);
    near(saved.back().volume, value);
    const auto* treatment = loaded.find_container(fillet);
    require(treatment && treatment->edge_treatment.routes[0][0] == selected.reference && treatment->edge_treatment.route_start_vertices[0] == start,
        "Native save/load lost the fragment or explicit R1 ancestry");
    kernel::OcctKernel cold;
    const auto rebuilt = cold.evaluate_history(loaded.kernel_operations());
    near(rebuilt.back().volume, value);
    f.run("regenerate"); near(f.result().volume, value);
    require(f.state().session.document().find_container(fillet)->edge_treatment.routes[0][0] == selected.reference, "Regeneration replaced a selected fragment");
    if (!f.cavity.empty() && name != "shell") {
        const auto input_before = f.run("edge_treatment.edges", {{"container", fillet}}).at("items");
        zima::test::resize_rectangular_commands([&](const char* n,commands::Json a){return f.run(n,std::move(a));},{{"container", f.cavity}, {"width_mm", "64"}});
        const auto input_after = f.run("edge_treatment.edges", {{"container", fillet}}).at("items");
        require(input_before.size() == input_after.size(), "Resizing cavity changed fragment count");
        for (std::size_t i = 0; i < input_before.size(); ++i)
            require(input_before[i].at("owner") == input_after[i].at("owner") && input_before[i].at("key") == input_after[i].at("key"),
                "Resizing cavity replaced stable fragment identity");
        const auto* retained = f.state().session.document().find_container(fillet);
        require(retained->edge_treatment.routes[0][0] == selected.reference && retained->edge_treatment.route_start_vertices[0] == start,
            "Source dimension edit replaced the downstream Fillet or R1 reference");
    }
    std::cout << name << ": unique fragments, isolated treatments, R1, save/reload, cold calculation and dimensions passed\n";
}

void split_again(const fs::path& directory) {
    Fixture f(directory); f.create("split-again");
    const auto baseline = f.longitudinal_edges();
    const auto selected = std::ranges::find_if(baseline, [](const auto& edge) {
        return edge.points.front().z < 0 && edge.points.front().y > 0;
    });
    require(selected != baseline.end(), "Missing lower front-wall fragment");
    const auto original = selected->reference;
    const auto a = selected->points.front(), b = selected->points.back();
    auto fixture=f.state().session.document();
    auto cutter = zima::test::rectangular_feature(fixture,{4,2,4}); cutter.combine_mode = document::CombineMode::Subtract;
    cutter.placement.x = a.x; cutter.placement.y = (a.y + b.y) * .5; cutter.placement.z = a.z;
    workspace::commit_profile(f.live,f.kernel,f.live.active_document_id(),cutter,workspace::ProfileEditMode::Create,fixture.sketches.back());
    unique_edges(f.result());
    require(std::ranges::find(f.result().mesh.edges, original, &kernel::ViewerEdge::reference) == f.result().mesh.edges.end(),
        "A split parent still selects an arbitrary surviving child");
    std::vector<kernel::ViewerEdge> children;
    for (const auto& edge : f.result().mesh.edges)
        if (edge.reference.owner_id == cutter.id && longitudinal(edge) &&
            std::abs(edge.points.front().x - a.x) < 1e-6 && std::abs(edge.points.front().z - a.z) < 1e-6) children.push_back(edge);
    require(children.size() == 2, "Second cut did not retain both fragment children");
    for (const auto& child : children) {
        near(*child.measured_length, 2);
        require(child.reference.semantic_key.find(original.semantic_key) != std::string::npos, "Second split lost recoverable parent ancestry");
        f.run("fillet.create", {{"radius_mm", .2}, {"routes", Json::array({Json{{"edges", Json::array({ref(child.reference)})}}})}});
        siblings_survive(f, children, child.reference); f.run("undo");
    }
    const auto revision = f.state().session.revision();
    const Json stale_args = {{"distance_a_mm", .2}, {"routes", Json::array({Json{{"edges", Json::array({ref(original)})}}})}};
    const auto rejected = f.host.execute({{"command", "chamfer.create"}, {"arguments", stale_args}});
    require(!rejected.ok && rejected.code == "invalid_reference" && f.state().session.revision() == revision,
        "An obsolete parent selected a child or committed a partial treatment");
    std::cout << "Repeated split: child ancestry, sibling isolation and obsolete-parent rejection passed\n";
}

void assembly_targets(const fs::path& directory) {
    Fixture f(directory);
    f.run("new", {{"type", "part"}, {"name", "assembly-split-source"}});
    f.box(100, 80, 50, 0, 0, false); f.box(88, 68, 50, 0, 6, true);
    const auto source = f.live.active_document_id(); f.run("save");
    const auto source_revision = f.state().session.revision();
    f.run("new", {{"type", "assembly"}, {"name", "assembly-split"}});
    const auto assembly_id = f.live.active_document_id();
    const auto first = f.run("component.insert", {{"source", source}}).at("occurrence").get<std::string>();
    const auto second = f.run("component.insert", {{"source", source}}).at("occurrence").get<std::string>();
    const auto first_path = assembly::InstancePath{}.child(first).encoded();
    const auto second_path = assembly::InstancePath{}.child(second).encoded();
    f.run("component.set", {{"instance_path", second_path}, {"placement", {{"y_mm", 100}}}});
    const auto sketch = f.run("sketch.create", {{"plane", "XZ"}, {"name", "Assembly through profile"}}).at("sketch");
    f.run("sketch.rectangle.create", {{"sketch", sketch}, {"first", {-26, -25}}, {"second", {26, 4}}});
    const auto cut = f.run("extrusion.create", {{"sketch", sketch}, {"targets", {first, second}}, {"extent", "two_sides"},
        {"end_forward", "through_all"}, {"end_reverse", "through_all"}}).at("container");
    auto* state = f.live.open_assembly(assembly_id);
    const auto check = [&](const assembly::AssemblyDocument& doc) {
        near(doc.find_occurrence(first)->calculated_source->volume, 118608);
        near(doc.find_occurrence(second)->calculated_source->volume, 118608);
        require(doc.find_cut(cut)->target_occurrence_ids.size() == 2, "Assembly cut lost one target");
        std::set<std::string> paths;
        for (const auto& edge : doc.build_scene().original_references.edges)
            if (!edge.reference.instance_path.empty()) paths.insert(edge.reference.instance_path);
        require(paths.contains(first_path) && paths.contains(second_path), "Repeated Part references lost occurrence identity");
    };
    check(state->session.document()); f.run("save");
    check(assembly::AssemblyDocument::load(directory / "assembly-split.asmz"));
    f.run("regenerate"); check(state->session.document());
    require(f.live.open_part(source)->session.revision() == source_revision, "Assembly cut changed source Part history");
    near(f.live.open_part(source)->session.calculated_boundaries().back().volume, 136704);
    std::cout << "Assembly: one through cut across two separated hollow occurrences, save/reload and source ownership passed\n";
}
}
namespace {
std::set<Key> form_edges(const kernel::BodyResult& result) {
    std::set<Key> ids;
    for (const auto& edge:result.mesh.edges) {
        require(edge.reference.valid(),"FORM contains an anonymous body edge");
        require(ids.insert(key(edge.reference)).second,"FORM body edges share identity");
    }
    return ids;
}
void form_case(const fs::path& directory) {
    const auto original=document::PartDocument::load("cpp/tests/fixtures/boolean/form.prtz");
    kernel::OcctKernel cold;
    const auto operations=original.kernel_operations();
    const auto calculated=cold.evaluate_history(operations);
    require(calculated.size()==4,"FORM fixture history changed");
    for (std::size_t index=0;index<calculated.size();++index) {
        const auto& boundary=calculated[index];
        require(boundary.calculation_errors.empty(),"FORM calculation reported an error");
        require(std::ranges::all_of(boundary.mesh.triangle_references,[](const auto& face){return face.valid();}),
            "FORM contains an anonymous body face");
        // Intermediate packets deliberately retire their kernel snapshots.
        // Explicitly calculate that prefix to inspect its actual B-Rep.
        const auto prefix=boundary.kernel_shape.empty()
            ? cold.evaluate_history(std::vector<kernel::HistoryOperation>(operations.begin(),operations.begin()+index+1))
            : std::vector<kernel::BodyResult>{};
        const auto& snapshot=prefix.empty()?boundary:prefix.back();
        TopoDS_Shape shape;BRep_Builder builder;std::istringstream stream(snapshot.kernel_shape);
        BRepTools::Read(shape,stream,builder);
        require(!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid(),"FORM is not a valid B-Rep");
        TopTools_IndexedMapOfShape actual_faces;TopExp::MapShapes(shape,TopAbs_FACE,actual_faces);
        std::set<Key> face_ids;
        for(const auto& face:boundary.mesh.triangle_references)face_ids.emplace(face.owner_id,face.semantic_key);
        require(face_ids.size()==static_cast<std::size_t>(actual_faces.Extent()),"Distinct FORM body faces share an identity");
        GProp_GProps volume,area;BRepGProp::VolumeProperties(shape,volume);BRepGProp::SurfaceProperties(shape,area);
        // Re-reading rational OCCT trims changes the independent integral by
        // up to 3e-5 mm3; this does not change the calculated/persisted result.
        near(volume.Mass(),boundary.volume,1e-4);near(area.Mass(),boundary.surface_area,1e-4);
        static_cast<void>(form_edges(boundary));
    }
    const auto& input=calculated.back();
    const auto is_front=[](const auto& face) {
        return face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&
            std::abs(face.surface->origin.y)<1e-6&&std::abs(face.surface->axis.y)>.99;
    };
    const auto oldest=std::ranges::find_if(calculated.front().mesh.triangle_references,is_front);
    const auto merged=std::ranges::find_if(calculated[2].mesh.triangle_references,is_front);
    require(oldest!=calculated.front().mesh.triangle_references.end()&&
        merged!=calculated[2].mesh.triangle_references.end()&&*merged==*oldest,
        "FORM coplanar union replaced its oldest source face identity");
    // Disconnected cap remnants are separate faces. Connected coplanar
    // regions must have no internal Boolean join in the actual B-Rep.
    const auto sweep=cold.evaluate_history({operations.begin(),operations.begin()+3}).back();
    TopoDS_Shape sweep_shape;BRep_Builder sweep_builder;
    std::istringstream sweep_stream(sweep.kernel_shape);BRepTools::Read(sweep_shape,sweep_stream,sweep_builder);
    TopTools_IndexedDataMapOfShapeListOfShape adjacency;
    TopExp::MapShapesAndAncestors(sweep_shape,TopAbs_EDGE,TopAbs_FACE,adjacency);
    TopTools_IndexedDataMapOfShapeListOfShape solids;
    TopExp::MapShapesAndAncestors(sweep_shape,TopAbs_FACE,TopAbs_SOLID,solids);
    for(int i=1;i<=adjacency.Extent();++i) {
        const auto& faces=adjacency.FindFromIndex(i);if(faces.Extent()!=2)continue;
        TopTools_ListIteratorOfListOfShape it(faces);
        const auto first_face=it.Value();it.Next();
        if(first_face.IsSame(it.Value()))continue;
        const auto& first_solids=solids.FindFromKey(first_face);
        const auto& second_solids=solids.FindFromKey(it.Value());
        if(first_solids.Extent()!=1||second_solids.Extent()!=1||!first_solids.First().IsSame(second_solids.First()))continue;
        const BRepAdaptor_Surface a(TopoDS::Face(first_face));
        const BRepAdaptor_Surface b(TopoDS::Face(it.Value()));
        if(a.GetType()!=GeomAbs_Plane||b.GetType()!=GeomAbs_Plane)continue;
        if(std::abs(a.Plane().Location().Y())>1e-6||std::abs(a.Plane().Axis().Direction().Y())<.99)continue;
        auto normal_a=a.Plane().Axis().Direction(),normal_b=b.Plane().Axis().Direction();
        if(!a.Plane().Position().Direct())normal_a.Reverse();
        if(!b.Plane().Position().Direct())normal_b.Reverse();
        if(first_face.Orientation()==TopAbs_REVERSED)normal_a.Reverse();
        if(it.Value().Orientation()==TopAbs_REVERSED)normal_b.Reverse();
        require(normal_a.Dot(normal_b)<1-1e-12||
            a.Plane().Distance(b.Plane().Location())>1e-7,
            "FORM Sweep retained an internal coplanar join edge");
    }
    for(const auto& edge:input.mesh.edges)if(!edge.parameter_seam&&!edge.overlay&&!edge.construction)for(const auto& point:edge.points) {
        const bool on_mesh=std::ranges::any_of(input.mesh.vertices,[&](const auto& vertex) {
            return std::hypot(point.x-vertex.x,point.y-vertex.y,point.z-vertex.z)<1e-9;
        });
        require(on_mesh,"FORM displayed edge sample does not match its supporting face mesh");
    }
    near(input.volume,18497.255433974693);near(input.surface_area,8563.080366200782);
    const auto ids=form_edges(input);
    const auto file=directory/"form.prtz";original.save(file,calculated);
    Fixture f(directory);f.run("open",{{"path",file.generic_string()}});
    require(form_edges(f.result())==ids,"FORM native reload replaced edge identities");
    f.run("regenerate");require(form_edges(f.result())==ids,"FORM regeneration replaced edge identities");
    const std::string cutter="01a1128b1f7a74f1b43389f983258e0d";
    const std::array lengths{4.71456615785608,10.,7.83706245248815,6.43410760843518,23.4521390383139,18.7992868721026};
    const auto edges=f.result().mesh.edges;
    std::array<int,6> counts{};
    for (const auto& edge:edges) {
        if(edge.reference.owner_id!=cutter||!edge.measured_length)continue;
        const auto length=std::ranges::find_if(lengths,[&](double value){return std::abs(value-*edge.measured_length)<1e-5;});
        if(length==lengths.end())continue;
        ++counts[static_cast<std::size_t>(length-lengths.begin())];
        const auto route=f.run("edge_treatment.route",{{"seed",ref(edge.reference)}});
        require(!route.at("edges").empty(),"FORM edge route is not selectable");
        for(const double radius:{.05,.1}) {
            const auto id=f.run("fillet.create",{{"radius_mm",radius},
                {"routes",Json::array({Json{{"edges",Json::array({ref(edge.reference)})}}})}}).at("container").get<std::string>();
            require(std::abs(f.result().volume-input.volume)>1e-6,"FORM Fillet did not alter geometry");
            require(f.result().calculation_errors.empty(),"FORM Fillet calculation failed");
            const auto treated_volume=f.result().volume;
            f.run("undo");near(f.result().volume,input.volume);require(form_edges(f.result())==ids,"Fillet Undo lost input identity");
            f.run("redo");near(f.result().volume,treated_volume);
            f.run("save");std::vector<kernel::BodyResult> saved;
            const auto reopened=document::PartDocument::load(file,&saved);
            require(reopened.find_container(id)->edge_treatment.routes[0][0]==edge.reference,"Saved FORM Fillet lost its selected edge");
            near(saved.back().volume,treated_volume);
            const auto revision=f.state().session.revision();
            f.run("regenerate");near(f.result().volume,treated_volume);
            if(f.state().session.revision()!=revision)f.run("undo");
            f.run("undo");near(f.result().volume,input.volume);
        }
    }
    require(std::ranges::all_of(counts,[](int count){return count==2;}),"FORM no longer covers all twelve formerly ambiguous edges");
    std::map<Key,kernel::FaceReference> faces;
    for(const auto& face:input.mesh.triangle_references)
        if(face.semantic_key.starts_with("boolean:subtract:split-face:from:")&&face.surface&&
            face.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&
            std::abs(face.surface->origin.y)<1e-6&&std::abs(face.surface->axis.y)>.99)
            faces.emplace(Key{face.owner_id,face.semantic_key},face);
    require(faces.size()==2,"FORM does not expose both disconnected front faces");
    for(const auto& [id,face]:faces) {
        const auto shell=f.run("surface_shell.create",{{"faces",Json::array({Json{{"owner",face.owner_id},{"key",face.semantic_key}}})}}).at("container").get<std::string>();
        near(f.result().volume,0);
        require(f.result().surface_area<input.surface_area,"FORM surface extraction did not remove its selected front face");
        f.run("save");std::vector<kernel::BodyResult> saved;
        const auto reopened=document::PartDocument::load(file,&saved);
        require(reopened.find_container(shell)->shell.removed_faces==std::vector<kernel::FaceReference>{face},"FORM saved the wrong front face");
        const auto area=f.result().surface_area;f.run("undo");near(f.result().volume,input.volume);
        f.run("redo");near(f.result().surface_area,area);f.run("undo");
    }
    auto changed=*f.state().session.document().find_container(cutter);
    changed.feature.sides[1].length=51.;
    workspace::commit_profile(f.live,f.kernel,f.live.active_document_id(),changed,workspace::ProfileEditMode::Replace);
    near(f.result().volume,input.volume);require(form_edges(f.result())==ids,"Changing cutter length replaced surviving FORM edge identities");
    f.run("undo");require(form_edges(f.result())==ids,"Length Undo changed FORM edge identities");
    std::cout<<"FORM: valid B-Rep, all face/edge identities, twelve edges at two radii, front-face extraction, native reload, regeneration, Undo/Redo and feature length passed\n";
}
}
int main(int argc, char** argv) { try {
    const auto root = fs::canonical(fs::temp_directory_path());
    const auto directory = root / ("zima-boolean-fragments-" + document::PartDocument::create_default().document_id);
    require(fs::create_directory(directory), "Cannot create test directory");
    if(argc>1&&std::string(argv[1])=="--form-only") {
        form_case(directory);
        require(directory.parent_path()==root,"Invalid test cleanup path");fs::remove_all(directory);return 0;
    }
    for (const auto* name : {"open", "closed", "offset", "shell", "three-solids", "solid"}) exercise(directory, name);
    split_again(directory);
    if(argc<2||std::string(argv[1])!="--part-only")assembly_targets(directory);
    require(directory.parent_path() == root, "Invalid test cleanup path"); fs::remove_all(directory);
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; } }
