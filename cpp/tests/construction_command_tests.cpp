#include "construction_query_test_support.hpp"
#include <zima/document/file_path.hpp>
#include <iostream>
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
    reopened.activate(assembly.document_id);
    test::check_construction_child(run(reopened_host, "construction.get", {{"construction", curve.curve_points[1].id}}).data, curve, {});
}
}
int main() { try {
    const auto root = fs::canonical(fs::temp_directory_path());
    const auto directory = root / ("zima-constructions-" + document::PartDocument::create_default().document_id);
    require(fs::create_directory(directory), "Cannot create fixture directory");
    kernel::OcctKernel kernel; verify(kernel, directory);
    require(directory.parent_path() == root, "Unexpected cleanup path"); fs::remove_all(directory);
    std::cout << "Construction queries: identities, ownership, local frames, bounded output, native persistence and unchanged model/cache passed\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; } }
