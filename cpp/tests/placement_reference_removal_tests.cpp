#include "profile_command_fixture.hpp"
#include <zima/command_host/host.hpp>
#include <zima/workspace/sweep_operations.hpp>
#include "sweep_test_support.hpp"
#include <cmath>
#include <iostream>

using namespace zima;
using commands::Json;
namespace fs = std::filesystem;
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void near(double value, double expected) {
    if (!std::isfinite(value) || std::abs(value - expected) > 1e-6)
        throw std::runtime_error("Expected " + std::to_string(expected) + ", got " + std::to_string(value));
}
Json run(command_host::Host& host, const char* name, Json args = Json::object()) {
    const auto result = host.execute({{"command", name}, {"arguments", std::move(args)}});
    if (!result.ok) throw std::runtime_error(std::string(name) + ": " + result.code + ": " + result.message);
    return result.data;
}
document::ConstructionReference missing(const char* owner) {
    document::ConstructionReference ref; ref.owner_id = owner; ref.semantic_key = "point"; return ref;
}
void constructions(const kernel::OcctKernel& kernel, fs::path directory, bool assembly_mode) {
    workspace::Workspace live; std::string id;
    const auto file = directory / (assembly_mode ? "remove.asmz" : "remove.prtz");
    if (assembly_mode) { auto doc = assembly::AssemblyDocument::create_default(); id = doc.document_id; live.add_assembly(std::move(doc), file); }
    else { auto doc = document::PartDocument::create_default(); id = doc.document_id; live.add_part(std::move(doc), {}, file); }
    live.activate(id);
    command_host::Interaction interaction; command_host::Options options; options.interaction = [&] { return interaction; };
    command_host::Host host(live, kernel, directory, options);
    const auto object = run(host, "construction.create", {{"kind", "point"}, {"name", "Linked point"}}).at("construction").get<std::string>();
    const auto value = [&] { return assembly_mode ? *live.open_assembly(id)->session.document().find_construction(object)
        : *live.open_part(id)->session.document().find_construction(object); };
    const auto revision = [&] { return assembly_mode ? live.open_assembly(id)->session.revision() : live.open_part(id)->session.revision(); };
    const auto set = [&](int index, const char* key, double offset) {
        run(host, "construction.reference.set", {{"construction", object}, {"index", index},
            {"reference", {{"owner", id + ":origin"}, {"key", key}}}, {"offset_mm", offset}, {"derive_orientation", false}});
    };
    const auto remove = [&](int index) { return run(host, "placement.reference.remove", {{"object", object}, {"index", index}}); };
    set(0, "origin:plane:xy", 9); set(1, "origin:plane:yz", 3);
    const auto bound = value(); const auto rev = revision();
    require(remove(0).at("body_calculated") == false && revision() == rev + 1, "Construction removal calculated a body or used multiple commits");
    require(value().references.size() == 1 && value().references.front() == bound.references[1], "Removal changed the surviving reference");
    near(value().origin.x, 3); near(value().origin.z, 9);
    const auto freed = value(); run(host, "undo"); require(value() == bound, "Construction removal Undo lost the source");
    run(host, "redo"); require(value() == freed, "Construction removal Redo changed identity");
    const auto stable = revision(); require(remove(4).at("changed") == false && revision() == stable && !host.change(), "Empty removal created history");
    const auto reject = [&](Json args, const char* code) {
        const auto before = value(); const auto before_revision = revision();
        const auto result = host.execute({{"command", "placement.reference.remove"}, {"arguments", std::move(args)}});
        require(!result.ok && (std::string(code).empty() || result.code == code), "Invalid removal was accepted or reported the wrong error");
        require(value() == before && revision() == before_revision && !host.change(), "Rejected removal partially changed the document");
    };
    for (const auto index : {-1LL, 5LL, 4294967296LL}) reject({{"object", object}, {"index", index}}, "invalid_arguments");
    reject({{"object", "missing"}, {"index", 0}}, "placement_not_found");
    interaction.editing = true; reject({{"object", object}, {"index", 0}}, "editing_in_progress"); interaction = {};
    run(host, "placement.set", {{"object", object}, {"values", {{"z", 17}}}}); near(value().origin.z, 17);
    // Simulate the persisted state after losing a source: retain the last
    // usable frame. Removing one of two broken sources must remain atomic.
    const auto inject = [&](bool two) {
        auto broken = value(); broken.references = {missing("lost-one")}; if (two) broken.references.push_back(missing("lost-two"));
        broken.reference_valid = false;
        if (assembly_mode) { auto next = live.open_assembly(id)->session.document(); *next.find_construction(object) = broken; live.open_assembly(id)->session.commit(std::move(next)); }
        else { auto* state = live.open_part(id); auto next = state->session.document(); *next.find_construction(object) = broken; state->session.commit(std::move(next), state->session.calculated_boundaries()); }
    };
    inject(true); reject({{"object", object}, {"index", 0}}, "");
    inject(false); const auto broken = value(); remove(0);
    require(value().reference_valid && value().references.empty() && value().origin == broken.origin, "A missing source could not be removed without moving the point");
    run(host, "undo"); require(value() == broken, "Undo did not restore the exact broken source"); run(host, "redo"); run(host, "save");
    const auto stored = assembly_mode ? *assembly::AssemblyDocument::load(file).find_construction(object)
        : *document::PartDocument::load(file).find_construction(object);
    require(stored == value(), "Native reload lost repaired reference state");
}
void body(const kernel::OcctKernel& kernel, fs::path directory) {
    workspace::Workspace live; auto doc = document::PartDocument::create_default(); const auto id = doc.document_id;
    live.add_part(std::move(doc), {}, directory / "body.prtz"); live.activate(id); command_host::Host host(live, kernel, directory);
    zima::test::rectangular_commands([&](const char* n,commands::Json a){return run(host, n,std::move(a));},{{"length_mm", "10"}, {"width_mm", "10"}, {"height_mm", "10"}});
    const auto object = run(host, "body.create", {{"name", "Framed body"}}).at("body").get<std::string>();
    const auto state = [&]() -> workspace::PartState& { return *live.open_part(id); };
    const auto bound = *state().session.document().body_history.find(object);
    const auto baseline = state().session.calculated_boundaries().back();
    require(bound.scope.placement.references.front().semantic_key == "origin:plane:xy", "Unexpected Body origin fixture");
    run(host, "placement.reference.remove", {{"object", object}, {"index", 0}});
    const auto freed = *state().session.document().body_history.find(object);
    require(freed.scope.placement.references.size() == 3, "Body removal lost unrelated origin planes or retained paired TOP");
    near(state().session.calculated_boundaries().back().volume, 1000);
    run(host, "undo"); require(*state().session.document().body_history.find(object) == bound, "Body removal Undo failed");
    run(host, "redo"); require(*state().session.document().body_history.find(object) == freed, "Body removal Redo failed");
    run(host, "placement.set", {{"object", object}, {"values", {{"z", 7}}}});
    const auto min_z = [](const auto& mesh) { return std::ranges::min(mesh.vertices, {}, &kernel::Vec3::z).z; };
    near(min_z(state().session.calculated_boundaries().back().mesh) - min_z(baseline.mesh), 7);
    near(state().session.calculated_boundaries().back().volume, 1000); run(host, "save");
    require(document::PartDocument::load(directory / "body.prtz").body_history == state().session.document().body_history, "Native Body lost removal or unlocked translation");
}
void sweeps(const kernel::OcctKernel& kernel, fs::path directory) {
    for (const auto kind : {document::FeatureKind::Sweep2D, document::FeatureKind::Sweep3D, document::FeatureKind::HelicalSweep}) {
        workspace::Workspace live; auto doc = document::PartDocument::create_default(); const auto id = doc.document_id;
        const auto file = directory / (std::to_string(static_cast<int>(kind)) + ".prtz");
        live.add_part(std::move(doc), {}, file); live.activate(id); command_host::Host host(live, kernel, directory);
        auto feature = test_support::sweep_fixture(kind); const auto object = feature.id;
        workspace::commit_sweep(live, kernel, id, std::move(feature), workspace::SweepEditMode::Create);
        const auto state = [&]() -> workspace::PartState& { return *live.open_part(id); };
        const auto current = [&] { return *state().session.document().find_container(object); };
        run(host, "placement.reference.set", {{"object", object}, {"index", 0}, {"reference", {{"owner", id + ":origin"}, {"key", "origin:plane:yz"}}}, {"offset_mm", 5}, {"derive_orientation", false}});
        const auto bound = current(); const auto volume = state().session.calculated_boundaries().back().volume;
        require(run(host, "placement.reference.remove", {{"object", object}, {"index", 0}}).at("body_calculated") == true, "Sweep removal bypassed its body transaction");
        require(current().placement.references.empty(), "Sweep retained the removed source"); near(state().session.calculated_boundaries().back().volume, volume);
        const auto freed = current(); run(host, "undo"); require(current() == bound, "Sweep removal Undo failed"); run(host, "redo"); require(current() == freed, "Sweep removal Redo changed profile or path identities");
        if (kind == document::FeatureKind::Sweep3D) {
            auto next = state().session.document(); auto& point = next.find_container(object)->sweep3d.path.curve_points.back();
            const auto point_id = point.id; point.references = {missing("lost-path-source")}; point.reference_valid = false;
            state().session.commit(std::move(next), state().session.calculated_boundaries());
            run(host, "placement.reference.remove", {{"object", point_id}, {"index", 0}});
            require(current().sweep3d.path.reference_valid && current().sweep3d.path.curve_points.back().references.empty(), "Owned path point source could not be removed");
            near(state().session.calculated_boundaries().back().volume, volume);
        }
        run(host, "save"); std::vector<kernel::BodyResult> cache; const auto stored = document::PartDocument::load(file, &cache);
        require(*stored.find_container(object) == current() && !cache.empty(), "Native Sweep lost repaired path or placement"); near(cache.back().volume, volume);
    }
}
}
int main() {
    try {
        const auto root = fs::canonical(fs::temp_directory_path());
        const auto directory = root / ("zima-reference-removal-" + document::PartDocument::create_default().document_id);
        require(fs::create_directory(directory), "Cannot create test directory"); kernel::OcctKernel kernel;
        constructions(kernel, directory, false); constructions(kernel, directory, true); body(kernel, directory); sweeps(kernel, directory);
        require(fs::canonical(directory).parent_path() == root, "Unsafe test cleanup"); fs::remove_all(directory);
        std::cout << "Reference removal: construction, Body, Sweep, missing sources, native files and Undo/Redo passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
