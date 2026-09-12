#include "sweep_test_support.hpp"
#include <zima/command_host/host.hpp>
#include <zima/workspace/sweep_operations.hpp>
#include <zima/workspace/sketch_operations.hpp>
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
void verify_creation(const kernel::OcctKernel& kernel, const fs::path& directory) {
    Fixture f(kernel, directory); f.run("new", {{"type", "part"}, {"name", "sweep-created"}});
    const auto path = f.run("construction.create", {{"kind", "curve3d"}, {"name", "Source path"},
        {"values", {{"x", 7}, {"y", -3}, {"z", 8}, {"rotation_y", 30}}},
        {"points", Json::array({Json{{"values", {{"z", 0}}}}, Json{{"values", {{"z", 20}}}}})}});
    const auto path_id = path.at("construction").get<std::string>();
    const auto sketch = f.run("sketch.create", {{"name", "Source profile"}, {"plane", "XY"}}).at("sketch").get<std::string>();
    const auto circle = f.run("sketch.circle.create", {{"sketch", sketch}, {"center", {0, 0}}, {"radius_mm", 2}}).at("geometry").get<std::string>();
    const auto before = f.state().session.document();
    const auto first = path.at("children")[0];
    const Json create = {{"source_path", path_id}, {"profiles", Json::array({Json{{"sketch", sketch}, {"point", first}}})}, {"name", "Created Sweep"}};
    auto dependency = before;
    auto consumer = document::PartDocument::create_construction(document::ConstructionKind::Point);
    const auto* original_path = dependency.find_construction(path_id);
    consumer.references.push_back({{}, original_path->curve_points.front().container_origin.id, "point", 0, false});
    dependency.insert_history_entry(document::PartHistoryKind::Construction, consumer.id);
    dependency.constructions.push_back(consumer);
    f.state().session.commit(std::move(dependency), f.state().session.calculated_boundaries());
    f.reject("sweep3d.create", create, "input_in_use"); f.run("undo");
    auto invalid = create; invalid["profiles"][0]["point"] = "foreign-point";
    f.reject("sweep3d.create", invalid, "invalid_profile");
    invalid = create; invalid["profiles"][0]["incoming"] = true;
    f.reject("sweep3d.create", invalid, "invalid_profile");
    const auto result = f.run("sweep3d.create", create);
    const auto id = result.at("container").get<std::string>();
    near(f.volume(), 80 * std::numbers::pi);
    const auto& created = f.state().session.document();
    require(created.history.size() == 1 && created.sketches.empty() && created.constructions.empty(),
        "Sweep creation duplicated its standalone inputs");
    const auto& feature = created.history.front();
    require(feature.id == id && feature.sweep3d.path.id == path_id && feature.sweep3d.path.origin == kernel::Vec3{} &&
        feature.placement.x == 7 && std::abs(feature.placement.rotation_y - 30) < 1e-10 && feature.sweep3d.profiles.front().sketch_id == sketch,
        "Sweep creation lost native identities or applied path placement twice");
    const auto owned = sketcher::Sketch::from_serialized(feature.sweep3d.profiles.front().sketch_serialized);
    require(owned.owner_container_id == id && owned.circles.front().id == circle,
        "Sweep creation changed original curve identity or Sketch ownership");
    kernel::ModelEnvelope bounds;
    for (const auto& point : f.state().session.calculated_boundaries().back().mesh.vertices) bounds.include(point);
    require(bounds.valid && std::abs(bounds.minimum.x - (7 - std::sqrt(3.0))) < .05 &&
        std::abs(bounds.maximum.z - (9 + 10 * std::sqrt(3.0))) < .05,
        "Created Sweep body does not occupy the original path frame");
    require(f.run("construction.get", {{"construction", path_id}}).at("owning_feature") == id,
        "Created path lost its owning feature query");
    for (const auto& referenced_owner : {feature.sweep3d.path.curve_points.front().container_origin.id, sketch}) {
        auto dependent = f.state().session.document();
        auto child = document::PartDocument::create_construction(document::ConstructionKind::Point);
        child.references.push_back({{}, referenced_owner, "point", 0, false});
        dependent.insert_history_entry(document::PartHistoryKind::Construction, child.id);
        dependent.constructions.push_back(child);
        f.state().session.commit(std::move(dependent), f.state().session.calculated_boundaries());
        f.reject("history.can_move", {{"object", child.id}, {"before", id}}, "history_dependency");
        f.reject("history.move", {{"object", child.id}, {"before", id}}, "history_dependency");
        f.run("undo");
    }
    f.run("undo");
    require(f.state().session.document().history == before.history && f.state().session.document().constructions == before.constructions &&
        std::ranges::equal(f.state().session.document().sketches, before.sketches, [](const auto& a, const auto& b) { return a.serialized() == b.serialized(); }) && f.state().session.document().body_history == before.body_history,
        "Sweep creation Undo did not restore all native inputs");
    f.run("redo"); near(f.volume(), 80 * std::numbers::pi); f.run("save");
    const auto saved = document::PartDocument::load(directory / "sweep-created.prtz");
    require(saved.history == f.state().session.document().history && saved.sketches.empty() && saved.constructions.empty(),
        "Created Sweep native persistence duplicated or lost adopted inputs");
}
void verify_creation_profiles(const kernel::OcctKernel& kernel, const fs::path& directory) {
    Fixture f(kernel, directory); f.run("new", {{"type", "part"}, {"name", "sweep-created-profiles"}});
    auto feature = test_support::sweep_fixture(document::FeatureKind::Sweep3D);
    auto section = sketcher::Sketch::create_default(); section.owner_container_id = feature.id;
    static_cast<void>(section.add_circle(0, 0, 3));
    feature.sweep3d.profiles.push_back({kernel::make_stable_id(), feature.sweep3d.path.curve_points.back().id,
        section.id, section.serialized()});
    feature.sweep3d.path.curve_points.front().references.push_back(
        {{}, feature.sweep3d.path.container_origin.id, "origin:plane:xy", 0, true});
    auto sources = test_support::standalone_sweep_sources(feature); sources.name = "sweep-created-profiles"; sources.document_id = f.live.active_document_id();
    f.state().session.commit(sources, {});
    Json profiles = Json::array();
    for (const auto& profile : feature.sweep3d.profiles)
        profiles.push_back({{"sketch", profile.sketch_id}, {"point", profile.point_id}});
    const Json create = {{"source_path", feature.sweep3d.path.id}, {"profiles", profiles}};
    f.run("history.suppress", {{"object", feature.sweep3d.path.id}, {"suppressed", true}});
    f.reject("sweep3d.create", create, "inactive_input"); f.run("undo");
    f.run("body.create", {{"name", "Other"}});
    f.reject("sweep3d.create", create, "inactive_body"); f.run("undo");
    const auto id = f.run("sweep3d.create", create).at("container").get<std::string>();
    // Frustum: L*pi*(r1*r1+r1*r2+r2*r2)/3.
    near(f.volume(), 380 * std::numbers::pi / 3, 1e-6);
    const auto& saved = f.state().session.document();
    require(saved.history.size() == 1 && saved.sketches.empty() && saved.constructions.empty() &&
        saved.find_container(id)->sweep3d.profiles.size() == 2 &&
        saved.find_container(id)->sweep3d.path.curve_points.front().references == feature.sweep3d.path.curve_points.front().references,
        "Multi-profile adoption lost a profile or path-local reference");
    f.run("undo");
    require(f.state().session.document().history == sources.history && f.state().session.document().constructions == sources.constructions,
        "Multi-profile Undo lost standalone inputs");
    // The same open source profile must survive a rejected Solid calculation
    // and remain available for successful Thin creation.
    feature.sweep3d.profiles.resize(1);
    section = sketcher::Sketch::create_default(); section.owner_container_id = feature.id;
    static_cast<void>(section.add_segment(-2, 0, 2, 0));
    feature.sweep3d.profiles.front().sketch_id = section.id;
    feature.sweep3d.profiles.front().sketch_serialized = section.serialized();
    sources = test_support::standalone_sweep_sources(feature); sources.name = "sweep-created-profiles"; sources.document_id = f.live.active_document_id();
    f.state().session.commit(sources, {});
    Json open = {{"source_path", feature.sweep3d.path.id},
        {"profiles", Json::array({Json{{"sketch", section.id}, {"point", feature.sweep3d.profiles.front().point_id}}})}};
    f.reject("sweep3d.create", open, "sweep_rejected");
    require(f.state().session.document().constructions == sources.constructions &&
        f.state().session.document().sketches.front().serialized() == sources.sketches.front().serialized(),
        "Failed Solid creation consumed the open profile or path");
    open["result_type"] = "thin"; open["thin_mode"] = "symmetric"; open["thickness_mm"] = .5;
    f.run("sweep3d.create", open); near(f.volume(), 40);
}
void verify_profile_management(const kernel::OcctKernel& kernel, const fs::path& directory, document::FeatureKind kind) {
    const bool planar = kind == document::FeatureKind::Sweep2D;
    const std::string prefix = planar ? "sweep2d" : "sweep3d";
    Fixture f(kernel, directory); f.run("new", {{"type", "part"}, {"name", prefix + "-profiles"}});
    // Put the adopted Sketch before the Sweep to exercise the shifted history
    // validation boundary when its standalone container is removed.
    const auto source_id = f.run("sketch.create", {{"name", "End profile"}, {"plane", "XY"}}).at("sketch").get<std::string>();
    f.run("sketch.circle.create", {{"sketch", source_id}, {"center", {0, 0}}, {"radius_mm", 3}});
    auto feature = test_support::sweep_fixture(kind); const auto id = feature.id;
    workspace::commit_sweep(f.live, kernel, f.live.active_document_id(), feature, workspace::SweepEditMode::Create);
    const auto query = [&] { return f.run(prefix + ".get", {{"container", id}}); };
    const auto details = query(); const auto first_profile = details.at("profiles")[0].at("profile").get<std::string>();
    const auto end = details.at("stations").back();
    require(details.at("stations_valid") == true && details.at("stations").size() == 2 &&
        details.at("station_coordinate_owner") == (planar ? details.at("body") : details.at("path")),
        "Sweep station query omitted native station IDs or coordinate ownership");
    const auto update = [&](Json profiles) { return f.run(prefix + ".set", {{"container", id}, {"profiles", std::move(profiles)}}); };
    const auto reject = [&](Json profiles, const char* code) {
        f.reject(prefix + ".set", {{"container", id}, {"profiles", std::move(profiles)}}, code);
    };
    const Json keep = {{"profile", first_profile}};
    const auto before = f.state().session.document();
    const auto result = update(Json::array({keep, Json{{"sketch", source_id}, {"point", end.at("point")}, {"incoming", end.at("incoming")}}}));
    near(f.volume(), 380 * std::numbers::pi / 3, 1e-6);
    const auto second_profile = result.at("profiles")[1].at("profile").get<std::string>();
    require(result.at("profiles")[1].at("sketch") == source_id && f.state().session.document().history.size() == 1 &&
        f.state().session.document().sketches.empty(), "Profile adoption duplicated or replaced native Sketch identity");
    f.run("undo"); near(f.volume(), 80 * std::numbers::pi);
    require(f.state().session.document().history == before.history &&
        f.state().session.document().sketches.front().serialized() == before.sketches.front().serialized(),
        "Profile adoption Undo did not restore the standalone Sketch");
    f.run("redo"); near(f.volume(), 380 * std::numbers::pi / 3, 1e-6);
    reject(Json::array(), "invalid_profile");
    reject(Json::array({keep, keep}), "invalid_sketch_owner");
    reject(Json::array({Json{{"profile", "foreign"}}}), "profile_not_found");
    reject(Json::array({Json{{"profile", first_profile}, {"incoming", 1}}}), "invalid_profile");
    reject(Json::array({Json{{"profile", first_profile}, {"sketch", source_id}}}), "invalid_profile");
    reject(Json::array({Json{{"profile", first_profile}, {"point", "foreign-point"}}}), "invalid_profile");
    reject(Json::array({Json{{"profile", second_profile}}}), "sweep_rejected");
    if (!planar) reject(Json::array({keep, Json{{"profile", second_profile}, {"point", details.at("stations")[0].at("point")},
        {"incoming", !details.at("stations")[0].at("incoming").get<bool>()}}}), "invalid_profile");
    update(Json::array({keep})); near(f.volume(), 80 * std::numbers::pi);
    require(query().at("profiles").size() == 1, "Removed profile remained owned by the Sweep");
    // An independently drawn Sketch after the feature may be adopted, but a
    // reference to that feature or later history must not create a cycle.
    const auto late = f.run("sketch.create", {{"name", "Dependent profile"}, {"plane", "XY"}}).at("sketch").get<std::string>();
    f.run("sketch.circle.create", {{"sketch", late}, {"center", {0, 0}}, {"radius_mm", 3}});
    auto dependent = f.state().session.document();
    const auto source = std::ranges::find(dependent.sketches, late, &sketcher::Sketch::id);
    dependent.find_container(source->owner_container_id)->placement.references.push_back(
        {{}, feature.container_origin.id, "origin:plane:xy", 0, true});
    f.state().session.commit(std::move(dependent), f.state().session.calculated_boundaries());
    const Json with_late = Json::array({keep, Json{{"sketch", late}, {"point", end.at("point")}, {"incoming", end.at("incoming")}}});
    reject(with_late, "history_dependency"); f.run("undo");
    update(with_late); near(f.volume(), 380 * std::numbers::pi / 3, 1e-6);
    const auto moved_profile = query().at("profiles")[1].at("profile");
    std::string middle;
    if (planar) {
        middle = f.run("sketch.point.create", {{"sketch", query().at("path_sketch")}, {"position", {0, 10}}}).at("point").get<std::string>();
    } else {
        f.run("sweep3d.set", {{"container", id}, {"path", {{"points", Json::array({
            Json{{"construction", feature.sweep3d.path.curve_points.front().id}}, Json{{"values", {{"z", 10}}}},
            Json{{"construction", feature.sweep3d.path.curve_points.back().id}}})}}}});
        middle = f.run("construction.get", {{"construction", feature.sweep3d.path.id}}).at("children")[1].get<std::string>();
    }
    const auto stations = query().at("stations");
    const auto station = std::ranges::find_if(stations, [&](const auto& entry) { return entry.at("point") == middle; });
    require(station != stations.end(), "New native path Point has no profile station");
    update(Json::array({keep, Json{{"profile", moved_profile}, {"point", middle}, {"incoming", station->at("incoming")}}}));
    // The first half is a frustum; after its new station the R3 section is inherited.
    near(f.volume(), 460 * std::numbers::pi / 3, 1e-6);
    require(query().at("profiles")[1].at("profile") == moved_profile && query().at("profiles")[1].at("point") == middle,
        "Moving a profile to a different station replaced its native identity");
    update(Json::array({keep})); near(f.volume(), 80 * std::numbers::pi);
    const auto& current = *f.state().session.document().find_container(id);
    const auto& profile = planar ? current.sweep2d.profiles.front() : current.sweep3d.profiles.front();
    auto sketch = sketcher::Sketch::from_serialized(profile.sketch_serialized);
    const auto sketch_id = sketch.id, circle_id = sketch.circles.front().id;
    for (int i = 0; i < 4; ++i) {
        const double a = i * std::numbers::pi / 2;
        const auto point = f.run("sketch.point.create", {{"sketch", sketch_id}, {"position", {2 * std::cos(a), 2 * std::sin(a)}}}).at("point");
        f.run("sketch.constraint.create", {{"sketch", sketch_id}, {"kind", "point_on_circle"},
            {"points", Json::array({point})}, {"geometry", Json::array({circle_id})}});
    }
    sketch = workspace::document_sketch(f.live, f.live.active_document_id(), sketch_id);
    const auto order = document::sweep3d_profile_correspondence(sketch);
    require(order.point_ids.size() == 4, "Circle profile has no native correspondence points");
    const auto start = order.point_ids[1];
    update(Json::array({Json{{"profile", first_profile}, {"start_point", start}}})); near(f.volume(), 80 * std::numbers::pi);
    require(query().at("profiles")[0].at("start_point") == start, "Profile correspondence start was not committed");
    reject(Json::array({Json{{"profile", first_profile}, {"start_point", "foreign-point"}}}), "sweep_rejected");
    f.run("save");
    const auto saved = document::PartDocument::load(directory / (prefix + "-profiles.prtz"));
    require(saved.history == f.state().session.document().history, "Profile list or correspondence did not persist");

}
void same_frame(const sketcher::Sketch& actual, const sketcher::Sketch& expected) {
    const auto vector = [](const auto& a, const auto& b) { near(a.x,b.x);near(a.y,b.y);near(a.z,b.z); };
    vector(actual.resolved_origin,expected.resolved_origin);vector(actual.resolved_normal,expected.resolved_normal);
    vector(actual.resolved_x_axis,expected.resolved_x_axis);vector(actual.resolved_y_axis,expected.resolved_y_axis);
    require(actual.id==expected.id && actual.points==expected.points && actual.segments==expected.segments &&
        actual.external_references==expected.external_references && actual.constraints==expected.constraints &&
        actual.arcs==expected.arcs && actual.bsplines==expected.bsplines && actual.circles==expected.circles,
        "Adopting the path changed its original local geometry, constraints or identity");
}
void verify_planar_creation(const kernel::OcctKernel& kernel, const fs::path& directory) {
    Fixture f(kernel,directory); f.run("new",{{"type","part"},{"name","sweep2d-created"}});
    unsigned scenario=0;
    for(const auto plane:{sketcher::SketchPlane::XY,sketcher::SketchPlane::XZ,sketcher::SketchPlane::YZ})
    for(const auto reference_count:{0,1,3})for(const auto back:{false,true})for(int turns=0;turns<4;++turns) {
        ++scenario;
        auto feature=test_support::sweep_fixture(document::FeatureKind::Sweep2D);
        auto sources=test_support::standalone_sweep_sources(feature);
        sources.document_id=f.live.active_document_id();sources.name="sweep2d-created";
        auto& source=sources.sketches.front();source.plane=plane;source.plane_offset=3;
        auto& p=sources.history.front().placement;
        p.x=7;p.y=-3;p.z=8;p.absolute_rotation_x=13;p.absolute_rotation_y=27;p.absolute_rotation_z=-19;
        p.orientation_back=back;p.orientation_quarter_turns=turns;
        p.rotation_offset_x=11;p.rotation_offset_y=23;p.rotation_offset_z=7;
        if(reference_count) {
            const std::string first=plane==sketcher::SketchPlane::XY?"origin:plane:xy":plane==sketcher::SketchPlane::XZ?"origin:plane:xz":"origin:plane:yz";
            const auto add=[&](const std::string& key,double offset,const std::string& role) {
                document::ConstructionReference r{{},sources.document_id+":origin",key,offset,true};
                r.orientation_drives_rotation=true;r.orientation_role=role;p.references.push_back(r);
            };
            add(first,5,"front");
            if(reference_count==3)for(const std::string key:{"origin:plane:xy","origin:plane:xz","origin:plane:yz"})
                if(key!=first)add(key,2,p.references.size()==1?"top":"none");
        }
        // Inputs are measured in a nontrivial owning Body frame as well.
        auto body=*sources.body_history.find(sources.body_history.active_body_id());
        body.scope.placement.x=100;body.scope.placement.absolute_rotation_z=40;
        sources.body_history.update_body(std::move(body));sources.set_body_history(sources.body_history);
        auto calculated=workspace::calculate_part_with_resolved_references(kernel,sources);
        const auto expected=sources.sketches.front();const auto original=sources;
        const auto profile=sources.sketches.back().id;
        const auto first_point=expected.segments.front().first_point_id;
        f.state().session.commit(std::move(sources),std::move(calculated));
        const Json create={{"source_path",expected.id},{"profiles",Json::array({Json{{"sketch",profile},{"point",first_point}}})}};
        try {
            const auto before=f.state().session.revision();
            const auto result=f.run("sweep2d.create",create);const auto id=result.at("container").get<std::string>();
            near(f.volume(),80*std::numbers::pi);
            const auto& part=f.state().session.document();const auto* adopted=part.find_container(id);
            require(adopted && part.sketches.empty() && part.history.size()==1 && result.at("path_sketch")==expected.id &&
                f.state().session.revision()==before+1,"2D adoption duplicated inputs or did not create one transaction");
            same_frame(sketcher::Sketch::from_serialized(adopted->sweep2d.path_sketch),expected);
            require(result.at("profiles")[0].at("sketch")==profile,"2D adoption replaced the profile identity");
            f.run("undo");require(f.state().session.document().history==original.history &&
                std::ranges::equal(f.state().session.document().sketches,original.sketches,[](const auto& a,const auto& b){return a.serialized()==b.serialized();}) && f.state().session.document().body_history==original.body_history,
                "2D creation Undo did not restore exact standalone inputs");
            f.run("redo");near(f.volume(),80*std::numbers::pi);
            if(scenario==72) {
                auto changed_source=original;changed_source.history.front().placement.references.front().offset+=4;
                static_cast<void>(workspace::calculate_part_with_resolved_references(kernel,changed_source));
                f.run("placement.set",{{"object",id},{"values",{{"reference_offset:0",9}}}});
                same_frame(sketcher::Sketch::from_serialized(f.state().session.document().find_container(id)->sweep2d.path_sketch),changed_source.sketches.front());
                near(f.volume(),80*std::numbers::pi);f.run("undo");
                f.run("save");auto saved=document::PartDocument::load(directory/"sweep2d-created.prtz");
                require(saved.history==f.state().session.document().history && saved.sketches.empty(),"2D creation save changed the owned path or profile");
                const auto cold=workspace::calculate_part_with_resolved_references(kernel,saved);
                near(cold.back().volume,80*std::numbers::pi);
                same_frame(sketcher::Sketch::from_serialized(saved.find_container(id)->sweep2d.path_sketch),expected);
            }
        } catch(const std::exception& error) {throw std::runtime_error("2D create frame scenario "+std::to_string(scenario)+": "+error.what());}
    }
    f.run("undo");
    const auto before=f.state().session.document();const auto path=before.sketches.front();const auto profile=before.sketches.back().id;
    const Json create={{"source_path",path.id},{"profiles",Json::array({Json{{"sketch",profile},{"point",path.segments.front().first_point_id}}})}};
    auto invalid=create;invalid["source_path"]="absent";f.reject("sweep2d.create",invalid,"sketch_not_found");
    invalid=create;invalid["profiles"][0]["point"]="absent";f.reject("sweep2d.create",invalid,"invalid_profile");
    invalid=create;invalid["profiles"][0]["sketch"]=path.id;f.reject("sweep2d.create",invalid,"invalid_sketch_owner");
    invalid=create;invalid["profiles"]=Json::array();f.reject("sweep2d.create",invalid,"invalid_profile");
    auto dependent=before;auto consumer=document::PartDocument::create_construction(document::ConstructionKind::Point);
    consumer.references.push_back({{},before.history.front().container_origin.id,"origin:point",0,false});
    dependent.constructions.push_back(consumer);dependent.insert_history_entry(document::PartHistoryKind::Construction,consumer.id);
    f.state().session.commit(std::move(dependent),f.state().session.calculated_boundaries());
    f.reject("sweep2d.create",create,"input_in_use");f.run("undo");
    auto suppressed=before;suppressed.sketches.front().suppressed=true;
    f.state().session.commit(std::move(suppressed),f.state().session.calculated_boundaries());
    f.reject("sweep2d.create",create,"inactive_input");f.run("undo");
    f.run("body.create",{{"name","Other"}});f.reject("sweep2d.create",create,"inactive_body");f.run("undo");
}
void verify_planar_curved_creation(const kernel::OcctKernel& kernel, const fs::path& directory) {
    Fixture f(kernel,directory);f.run("new",{{"type","part"},{"name","sweep2d-created-arc"}});
    auto feature=test_support::sweep_fixture(document::FeatureKind::Sweep2D);
    auto path=sketcher::Sketch::create_default();path.owner_container_id=feature.id;
    static_cast<void>(path.add_arc(10,0,0,0,10,10,false,1e-6,true));
    feature.sweep2d.path_sketch=path.serialized();
    const auto station=document::PartDocument::sweep2d_route(feature).stations.front();
    feature.sweep2d.profiles.front().point_id=station.point_id;
    feature.sweep2d.profiles.front().incoming=station.incoming;
    auto sources=test_support::standalone_sweep_sources(feature);sources.document_id=f.live.active_document_id();sources.name="sweep2d-created-arc";
    sources.resolve_constructions();const auto expected=sources.sketches.front();const auto profile=sources.sketches.back().id;
    f.state().session.commit(std::move(sources),{});
    const Json create={{"source_path",path.id},{"profiles",Json::array({Json{{"sketch",profile},{"point",station.point_id},{"incoming",station.incoming}}})}};
    const auto id=f.run("sweep2d.create",create).at("container").get<std::string>();
    near(f.volume(),20*std::numbers::pi*std::numbers::pi,2e-3);
    same_frame(sketcher::Sketch::from_serialized(f.state().session.document().find_container(id)->sweep2d.path_sketch),expected);
    f.run("save");auto saved=document::PartDocument::load(directory/"sweep2d-created-arc.prtz");
    require(saved.history==f.state().session.document().history,"Arc path changed during native save");
    const auto cold=workspace::calculate_part_with_resolved_references(kernel,saved);
    near(cold.back().volume,20*std::numbers::pi*std::numbers::pi,2e-3);
    f.run("undo");auto thin=create;thin["result_type"]="thin";thin["thin_mode"]="symmetric";thin["thickness_mm"]=.5;
    f.run("sweep2d.create",thin);near(f.volume(),10*std::numbers::pi*std::numbers::pi,2e-3);
}
void verify_path_plane(const kernel::OcctKernel& kernel, const fs::path& directory) {
    Fixture f(kernel, directory); f.run("new", {{"type", "part"}, {"name", "sweep-path-plane"}});
    const auto plane = f.run("construction.create", {{"kind", "plane"}, {"name", "Offset plane"},
        {"base_plane", "xy"}, {"offset_mm", 3}, {"values", {{"z", 10}}}});
    const auto feature = test_support::sweep_fixture(document::FeatureKind::Sweep2D); const auto id = feature.id;
    workspace::commit_sweep(f.live, kernel, f.live.active_document_id(), feature, workspace::SweepEditMode::Create);
    const auto get = [&] { return f.run("sweep2d.get", {{"container", id}}); };
    const auto set = [&](Json plane) { return f.run("sweep2d.set", {{"container", id}, {"path_plane", std::move(plane)}}); };
    const auto reject = [&](Json plane, const char* code) { f.reject("sweep2d.set", {{"container", id}, {"path_plane", std::move(plane)}}, code); };
    const Json external = {{"owner", plane.at("entity")}, {"key", "plane"}, {"offset_mm", 2}};
    set(external); near(f.volume(), 80 * std::numbers::pi);
    const auto path = [&] { return sketcher::Sketch::from_serialized(f.state().session.document().find_container(id)->sweep2d.path_sketch); };
    near(path().resolved_origin.z, 15); near(path().resolved_normal.z, 1);
    require(get().at("path_plane").at("owner") == plane.at("entity"), "Path plane query changed original owner");
    f.run("undo"); require(get().at("path_plane").is_null(), "Path plane Undo lost the original local plane");
    f.run("redo"); near(path().resolved_origin.z, 15);
    f.run("construction.set", {{"construction", plane.at("construction")}, {"offset_mm", 6}});
    f.run("regenerate"); near(path().resolved_origin.z, 18); near(f.volume(), 80 * std::numbers::pi);
    f.run("sweep2d.set", {{"container", id}, {"placement", {{"x", 7}, {"y", -3}, {"z", 8}, {"rotation_y", 30}}},
        {"path_plane", {{"owner", feature.container_origin.id}, {"key", "origin:plane:xy"}, {"offset_mm", 4}}}});
    near(path().resolved_origin.x, 9); near(path().resolved_origin.y, -3); near(path().resolved_origin.z, 8 + 2 * std::sqrt(3.0));
    near(path().resolved_normal.x, .5); near(path().resolved_normal.z, std::sqrt(3.0) / 2);
    double low = 1e100, high = -1e100;
    for (const auto& vertex : f.state().session.calculated_boundaries().back().mesh.vertices) {
        const double projection = (vertex.x - 7) * .5 + (vertex.z - 8) * std::sqrt(3.0) / 2;
        low = std::min(low, projection); high = std::max(high, projection);
    }
    require(std::abs(low - 2) < .05 && std::abs(high - 6) < .05, "Sweep body did not follow its rotated and offset path plane");
    reject({{"owner", "missing"}, {"key", "plane"}}, "invalid_reference_source");
    reject({{"owner", id}, {"key", "plane"}}, "invalid_reference_source");
    reject({{"owner", plane.at("entity")}, {"key", "plane"}, {"instance_path", "foreign"}}, "invalid_reference");
    reject({{"owner", plane.at("entity")}}, "invalid_arguments");
    reject({{"owner", plane.at("entity")}, {"key", "plane"}, {"offset_mm", true}}, "invalid_arguments");
    reject({{"owner", plane.at("entity")}, {"key", "plane"}, {"offset_mm", 1000001}}, "invalid_arguments");
    const auto later = f.run("construction.create", {{"kind", "plane"}, {"name", "Later plane"}, {"base_plane", "xy"}});
    reject({{"owner", later.at("entity")}, {"key", "plane"}}, "invalid_reference_source");
    auto locked = f.state().session.document(); locked.find_container(id)->sweep2d.path_plane->offset_locked = true;
    f.state().session.commit(std::move(locked), f.state().session.calculated_boundaries());
    reject({{"owner", feature.container_origin.id}, {"key", "origin:plane:xy"}, {"offset_mm", 5}}, "value_locked");
    f.run("undo"); // Remove the deliberately injected transient lock before native round-trip.
    f.run("save"); const auto saved = document::PartDocument::load(directory / "sweep-path-plane.prtz");
    require(saved.history == f.state().session.document().history, "Path plane reference or offset did not persist");
    set(Json::object()); require(get().at("path_plane").is_null(), "Clearing the explicit path plane failed");
    near(f.volume(), 80 * std::numbers::pi);
}
void verify_original_path_plane(const kernel::OcctKernel& kernel, const fs::path& directory) {
    Fixture f(kernel, directory); f.run("new", {{"type", "part"}, {"name", "sweep-original-plane"}});
    const auto box = f.run("box.create", {{"length_mm", "4"}, {"width_mm", "4"}, {"height_mm", "4"}}).at("container").get<std::string>();
    f.run("placement.set", {{"object", box}, {"values", {{"z", 40}}}});
    const auto faces = f.run("reference.list", {{"kind", "face"}, {"owner", box}}).at("items");
    Json top; double height = -1e100;
    for (const auto& item : faces) {
        const auto face = f.run("reference.get", {{"kind", "face"}, {"owner", box}, {"key", item.at("key")}});
        // The analytic plane axis does not encode the oriented face normal.
        // Select the physically highest horizontal face, independently of its
        // underlying surface axis sign or enumeration order.
        const auto z = face.at("surface").at("origin")[2].get<double>();
        if (std::abs(face.at("surface").at("axis")[2].get<double>()) > .9 && z > height) {
            top = {{"owner", box}, {"key", item.at("key")}, {"offset_mm", 3}};
            height = z;
        }
    }
    require(!top.is_null(), "Original top face missing from source box");
    const auto body = f.run("body.create", {{"name", "Sweep body"}}).at("body").get<std::string>();
    const auto feature = test_support::sweep_fixture(document::FeatureKind::Sweep2D);
    workspace::commit_sweep(f.live, kernel, f.live.active_document_id(), feature, workspace::SweepEditMode::Create);
    const auto set = [&](Json plane) { f.run("sweep2d.set", {{"container", feature.id}, {"path_plane", std::move(plane)}}); };
    const auto path = [&] { return sketcher::Sketch::from_serialized(f.state().session.document().find_container(feature.id)->sweep2d.path_sketch); };
    const double volume = f.volume(); set(top); near(f.volume(), volume);
    near(path().resolved_origin.z, height + 3); near(path().resolved_normal.z, 1);
    std::string offset_key; std::size_t row = 0;
    for (const auto& ref : f.state().session.document().body_history.find(body)->scope.placement.references) {
        if (ref.orientation_only) continue;
        if (ref.semantic_key == "origin:plane:xy" && ref.supports_offset) offset_key = "reference_offset:" + std::to_string(row);
        ++row;
    }
    require(!offset_key.empty(), "Body has no editable XY placement offset");
    f.run("placement.set", {{"object", body}, {"values", {{offset_key, 10}}}});
    near(f.state().session.document().body_history.find(body)->scope.placement.z, 10);
    near(path().resolved_origin.z, height - 7); near(f.volume(), volume);
    top["offset_mm"] = 4; set(top); near(path().resolved_origin.z, height - 6); near(f.volume(), volume);
    const auto edge = f.run("reference.list", {{"kind", "edge"}, {"owner", box}}).at("items")[0];
    f.reject("sweep2d.set", {{"container", feature.id}, {"path_plane", {{"owner", box}, {"key", edge.at("key")}}}}, "sweep_rejected");
    f.run("save"); const auto saved = document::PartDocument::load(directory / "sweep-original-plane.prtz");
    require(saved.history == f.state().session.document().history, "Original path plane lost source identity or Body frame on reload");
}
}
int main() {
    try {
        const auto root = fs::canonical(fs::temp_directory_path());
        const auto directory = root / ("zima-sweep-commands-" + kernel::make_stable_id());
        require(fs::create_directory(directory), "Cannot create fixture directory");
        kernel::OcctKernel kernel;
        for (const auto kind : {document::FeatureKind::Sweep2D, document::FeatureKind::Sweep3D, document::FeatureKind::HelicalSweep}) verify_sweep_commands(kernel, directory, kind);
        verify_creation(kernel, directory);
        verify_creation_profiles(kernel, directory);
        verify_planar_creation(kernel, directory);
        verify_planar_curved_creation(kernel, directory);
        verify_path_plane(kernel, directory);
        verify_original_path_plane(kernel, directory);
        for (const auto kind : {document::FeatureKind::Sweep2D, document::FeatureKind::Sweep3D}) verify_profile_management(kernel, directory, kind);
        require(directory.parent_path() == root, "Unexpected cleanup path"); fs::remove_all(directory);
        std::cout << "Sweep commands: independent volumes, native profiles, ownership, locks, atomic errors and Undo/Redo passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
