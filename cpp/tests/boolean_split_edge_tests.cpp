#include <zima/command_host/host.hpp>
#include <zima/workspace/primitive_operations.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <set>

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
            settings.templates = {fs::absolute("config/templates"), "start_part.prtz", "start_assembly.asmz", "Body"}; return settings; };
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
        auto value = document::PartDocument::create_box_container();
        value.box = {length, width, height}; value.placement.y = y; value.placement.z = z;
        value.combine_mode = subtract ? document::CombineMode::Subtract : document::CombineMode::Add;
        if (subtract) cavity = value.id;
        static_cast<void>(workspace::commit_primitive(live, kernel, live.active_document_id(), value, workspace::PrimitiveEditMode::Create));
    }
    void create(const std::string& name) {
        run("new", {{"type", "part"}, {"name", name}});
        if (name == "three-solids") {
            for (double y : {-32., 0., 32.}) box(100, 6, 50, y, 0, false);
        } else {
            box(100, 80, 50, 0, 0, false);
            if (name == "shell") {
                const auto source = state().session.document().history.front().id;
                cavity = run("shell.create", {{"thickness_mm", 6}, {"faces", Json::array({Json{{"owner", source}, {"key", "z_max"}}})}}).at("container");
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
        f.run("box.set", {{"container", f.cavity}, {"width_mm", "64"}});
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
    auto cutter = document::PartDocument::create_box_container();
    cutter.box = {4, 2, 4}; cutter.combine_mode = document::CombineMode::Subtract;
    cutter.placement.x = a.x; cutter.placement.y = (a.y + b.y) * .5; cutter.placement.z = a.z;
    static_cast<void>(workspace::commit_primitive(f.live, f.kernel, f.live.active_document_id(), cutter, workspace::PrimitiveEditMode::Create));
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
int main() { try {
    const auto root = fs::canonical(fs::temp_directory_path());
    const auto directory = root / ("zima-boolean-fragments-" + document::PartDocument::create_default().document_id);
    require(fs::create_directory(directory), "Cannot create test directory");
    for (const auto* name : {"open", "closed", "offset", "shell", "three-solids", "solid"}) exercise(directory, name);
    split_again(directory); assembly_targets(directory);
    require(directory.parent_path() == root, "Invalid test cleanup path"); fs::remove_all(directory);
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; } }
