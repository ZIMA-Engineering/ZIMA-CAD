#include "sweep_test_support.hpp"
#include <zima/command_host/host.hpp>
#include <zima/workspace/sweep_operations.hpp>
#include <zima/kernel/stable_id.hpp>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>

using namespace zima;
using commands::Json;
namespace fs = std::filesystem;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void near(double value, double expected, double relative = 1e-8) {
    if (!std::isfinite(value) || std::abs(value - expected) > std::max(1e-5, std::abs(expected) * relative))
        throw std::runtime_error("Expected " + std::to_string(expected) + ", got " + std::to_string(value));
}
struct Fixture {
    workspace::Workspace live;
    command_host::Interaction interaction;
    fs::path directory;
    command_host::Host host;
    Fixture(const kernel::OcctKernel& kernel, const fs::path& directory)
        : directory(directory), host(live, kernel, this->directory, options()) {}
    command_host::Options options() {
        command_host::Options value;
        value.settings = [] { return command_host::Settings{{fs::absolute("config/templates"), "start_part.prtz", "start_assembly.asmz", "Body"}, {}}; };
        value.interaction = [this] { return interaction; };
        return value;
    }
    Json run(const std::string& command, Json args = Json::object()) {
        const auto result = host.execute({{"command", command}, {"arguments", std::move(args)}});
        if (!result.ok) throw std::runtime_error(command + ": " + result.code + ": " + result.message);
        return result.data;
    }
    workspace::PartState& state() { return *live.open_part(live.active_document_id()); }
    double volume() { return state().session.calculated_boundaries().back().volume; }
    void reject(const std::string& command, Json args, const char* code) {
        const auto before = state().session.document(); const auto revision = state().session.revision();
        const auto* cache = state().session.calculated_boundaries().data();
        const auto result = host.execute({{"command", command}, {"arguments", std::move(args)}});
        if (result.ok || result.code != code) throw std::runtime_error(command + " expected " + code + ", got " + result.code + ": " + result.message);
        require(state().session.revision() == revision && state().session.calculated_boundaries().data() == cache &&
            state().session.document().history == before.history && state().session.document().body_history == before.body_history && !host.change(),
            "Rejected Sweep command changed the document, history or calculated geometry");
    }
};
void verify_sweep_commands(const kernel::OcctKernel& kernel, const fs::path& directory, document::FeatureKind kind) {
    const bool helical = kind == document::FeatureKind::HelicalSweep;
    const std::string prefix = helical ? "helical" : kind == document::FeatureKind::Sweep2D ? "sweep2d" : "sweep3d";
    Fixture f(kernel, directory);
    const auto missing = f.host.execute({{"command", prefix + ".get"}, {"arguments", {{"container", "absent"}}}});
    require(missing.code == "unsupported_document", "Sweep query without a Part was not rejected");
    f.run("new", {{"type", "part"}, {"name", prefix}});
    const auto definition = test_support::sweep_fixture(kind); const auto id = definition.id;
    workspace::commit_sweep(f.live, kernel, f.live.active_document_id(), definition, workspace::SweepEditMode::Create);
    const auto body = f.state().session.document().body_history.active_body_id();
    const auto helix_volume = [](double pitch) { return std::numbers::pi * .25 * std::hypot(2 * std::numbers::pi * 10 * 10 / pitch, 10); };
    const double initial = helical ? helix_volume(5) : 80 * std::numbers::pi;
    near(f.volume(), initial, helical ? 1e-3 : 1e-8);
    const auto revision = f.state().session.revision(); const auto* cache = f.state().session.calculated_boundaries().data();
    const auto get = f.run(prefix + ".get", {{"container", id}});
    require(get.at("feature") == definition.feature_id && get.at("body") == body &&
        f.state().session.revision() == revision && f.state().session.calculated_boundaries().data() == cache && !f.host.change(), "Sweep query changed calculated data or lost ownership");
    const auto field = helical ? "pitch_mm" : "thickness_mm";
    Json patch = {{"container", id}, {field, helical ? 10.0 : .5}, {"name", "Upravené tažení"}};
    if (helical) patch["left_handed"] = true;
    else { patch["result_type"] = "thin"; patch["thin_mode"] = "symmetric"; }
    f.run(prefix + ".set", patch);
    const double changed = helical ? helix_volume(10) : 40 * std::numbers::pi;
    near(f.volume(), changed, helical ? 1e-3 : 1e-8);
    f.run("undo"); near(f.volume(), initial, helical ? 1e-3 : 1e-8);
    f.run("redo"); near(f.volume(), changed, helical ? 1e-3 : 1e-8);
    f.run(prefix + ".set", {{"container", id}, {"placement", {{"x", 7}, {"rotation_y", 30}}}});
    near(f.volume(), changed, helical ? 1e-3 : 1e-8);
    f.reject(prefix + ".set", {{"container", id}, {field, 0}, {"name", "Invalid"}}, "invalid_arguments");
    f.reject(prefix + ".set", {{"container", id}, {field, true}}, "invalid_arguments");
    f.reject(prefix + ".set", {{"container", id}, {"combine", "invalid"}}, "invalid_arguments");
    f.reject(prefix + ".set", {{"container", id}, {"placement", {{"unknown", 1}}}}, "parameter_not_editable");
    if (helical) f.reject(prefix + ".set", {{"container", id}, {"circle", "missing"}}, "sweep_rejected");
    f.interaction.editing = true;
    f.reject(prefix + ".set", {{"container", id}, {field, 2}}, "editing_in_progress");
    f.run(prefix + ".get", {{"container", id}}); f.interaction = {};
    auto locked = f.state().session.document(); locked.find_container(id)->value_locks.insert(helical ? "pitch" : "thickness");
    f.state().session.commit(std::move(locked), f.state().session.calculated_boundaries());
    f.reject(prefix + ".set", {{"container", id}, {field, 2}}, "value_locked");
    f.run("save"); std::vector<kernel::BodyResult> reopened;
    const auto saved = document::PartDocument::load(directory / (prefix + ".prtz"), &reopened);
    require(saved.history == f.state().session.document().history,
        "Sweep native persistence lost parameters or embedded profile identity");
    auto cold = saved;
    const auto recalculated = workspace::calculate_part_with_resolved_references(kernel, cold);
    require(cold.history == saved.history, "Cold calculation changed persisted Sweep frames or identities");
    near(recalculated.back().volume, changed, helical ? 1e-3 : 1e-8);
    near(reopened.back().volume, changed, helical ? 1e-3 : 1e-8);
    f.run("body.create", {{"name", "Other"}});
    f.reject(prefix + ".set", {{"container", id}, {"name", "Inactive"}}, "inactive_body");
    f.run("body.activate", {{"body", body}});
    if (kind == document::FeatureKind::Sweep3D) {
        const auto path_id = definition.sweep3d.path.id;
        const auto first = definition.sweep3d.path.curve_points.front().id;
        const auto last = definition.sweep3d.path.curve_points.back().id;
        const auto path = f.run("construction.get", {{"construction", path_id}});
        require(path.at("owning_feature") == id && path.at("coordinate_system") == "container" &&
            path.at("coordinate_owner") == id && path.at("body") == body && path.at("children").size() == 2,
            "Embedded path query lost its feature, frame or Body");
        const auto points = f.run("construction.list", {{"parent", path_id}});
        require(points.at("total") == 2 && points.at("items")[0].at("owning_feature") == id,
            "Embedded path child query lost ownership");
        require(f.run(prefix + ".get", {{"container", id}}).at("stations").size() == 2,
            "Sweep query omitted stable path stations");
        f.reject("construction.set", {{"construction", first}, {"values", {{"z", 1}}}}, "embedded_construction");
        const auto update = [&](Json value) { f.run("sweep3d.set", {{"container", id}, {"path", std::move(value)}}); };
        const auto entry = [](const std::string& point, Json fields = Json::object()) {
            fields["construction"] = point; return fields;
        };
        const auto reject_path = [&](Json path, const char* code) {
            f.reject("sweep3d.set", {{"container", id}, {"path", std::move(path)}}, code);
        };
        update({{"points", Json::array({entry(first), entry(last, {{"values", {{"z", 30}}}})})}});
        near(f.volume(), 60 * std::numbers::pi);
        f.run("undo"); near(f.volume(), changed);
        f.run("redo"); near(f.volume(), 60 * std::numbers::pi);
        update({{"curve_type", "interpolating_spline"}, {"points", Json::array({entry(first),
            Json{{"values", {{"z", 15}}}}, entry(last)})}});
        near(f.volume(), 60 * std::numbers::pi);
        const auto spline = f.run("construction.get", {{"construction", path_id}});
        const auto middle = spline.at("children")[1].get<std::string>();
        require(spline.at("curve_type") == "interpolating_spline" &&
            spline.at("children")[0] == first && spline.at("children")[2] == last,
            "Path insertion changed surviving point identities");
        update({{"curve_type", "polyline"}, {"rounding_enabled", true}, {"points", Json::array({entry(first),
            entry(middle, {{"values", {{"z", 20}}}, {"radius_mm", 5}}),
            entry(last, {{"values", {{"x", 10}, {"z", 20}}}})})}});
        const double rounded_volume = 2 * std::numbers::pi * (20 + 2.5 * std::numbers::pi);
        near(f.volume(), rounded_volume, 1e-6);
        const auto rounded = f.run(prefix + ".get", {{"container", id}});
        require(rounded.at("stations_valid") == true && rounded.at("stations").size() == 4 &&
            rounded.at("stations")[1].at("incoming") == true && rounded.at("stations")[2].at("point") == middle,
            "Rounded path stations did not distinguish incoming and outgoing branches");
        reject_path({{"points", Json::array({entry(first), entry(first)})}}, "invalid_arguments");
        reject_path({{"points", Json::array({entry(first), entry("foreign")})}}, "construction_not_found");
        reject_path({{"rounding_enabled", 1}}, "invalid_arguments");
        reject_path({{"origin_mm", {1, 2, 3}}}, "invalid_arguments");
        reject_path({{"points", Json::array({entry(middle), entry(last)})}}, "sweep_rejected");
        auto referenced = f.state().session.document();
        auto moved_body = *referenced.body_history.find(body);
        moved_body.scope.placement.references.clear();
        moved_body.scope.placement.x = 100;
        moved_body.scope.placement.rotation_z = 40;
        moved_body.scope.placement.absolute_rotation_z = 40;
        referenced.body_history.update_body(std::move(moved_body));
        auto& point = referenced.find_container(id)->sweep3d.path.curve_points.front();
        point.references.push_back({{}, definition.sweep3d.path.container_origin.id, "origin:plane:xy", 0, true});
        referenced.resolve_constructions();
        auto moved_geometry = workspace::calculate_part_with_resolved_references(kernel, referenced);
        f.state().session.commit(std::move(referenced), std::move(moved_geometry));
        near(f.volume(), rounded_volume, 1e-6);
        reject_path({{"points", Json::array({entry(first, {{"values", {{"z", 1}}}}),
            entry(middle), entry(last)})}}, "parameter_not_editable");
        update({{"points", Json::array({entry(first, {{"values", {{"x", 0}}}}), entry(middle), entry(last)})}});
        near(f.volume(), rounded_volume, 1e-6);
        f.run("save");
        const auto path_saved = document::PartDocument::load(directory / (prefix + ".prtz"));
        require(path_saved.history == f.state().session.document().history,
            "Embedded path save lost points, native references or profile frames");
        auto broken = f.state().session.document();
        auto& broken_points = broken.find_container(id)->sweep3d.path.curve_points;
        broken_points[1].origin = broken_points[0].origin;
        f.state().session.commit(std::move(broken), f.state().session.calculated_boundaries());
        const auto broken_revision = f.state().session.revision();
        const auto* broken_cache = f.state().session.calculated_boundaries().data();
        const auto inspected = f.run("sweep3d.get", {{"container", id}});
        require(inspected.at("stations_valid") == false && inspected.contains("stations_error") &&
            f.state().session.revision() == broken_revision && f.state().session.calculated_boundaries().data() == broken_cache,
            "Reading an invalid route changed the Part or hid its editable parameters");
        f.run("undo");
    }
}
}
int main() {
    try {
        const auto root = fs::canonical(fs::temp_directory_path());
        const auto directory = root / ("zima-sweep-commands-" + kernel::make_stable_id());
        require(fs::create_directory(directory), "Cannot create fixture directory");
        kernel::OcctKernel kernel;
        for (const auto kind : {document::FeatureKind::Sweep2D, document::FeatureKind::Sweep3D, document::FeatureKind::HelicalSweep}) verify_sweep_commands(kernel, directory, kind);
        require(directory.parent_path() == root, "Unexpected cleanup path"); fs::remove_all(directory);
        std::cout << "Sweep commands: independent volumes, native profiles, ownership, locks, atomic errors and Undo/Redo passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
