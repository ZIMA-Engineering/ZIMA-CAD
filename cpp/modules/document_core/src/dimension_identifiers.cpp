#include <zima/document/dimension_identifiers.hpp>
#include <zima/document/part_document.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

namespace zima::document {
void DimensionIdentifiers::synchronize(const std::vector<DimensionParameter>& parameters) {
    for (const auto& parameter : parameters) {
        if (parameter.owner_id.empty() || parameter.semantic_key.empty())
            throw std::runtime_error("Dimension parameter has no ZIMA identity");
        const Key key{parameter.owner_id, parameter.semantic_key};
        if (!numbers_.contains(key)) {
            if (next_ == std::numeric_limits<std::uint64_t>::max())
                throw std::runtime_error("Dimension identifier sequence exhausted");
            numbers_.emplace(key, next_++);
        }
    }
}
void DimensionIdentifiers::retain(const DimensionIdentifiers& previous) {
    std::map<std::uint64_t, Key> used;
    for (const auto& [key, number] : numbers_) used.emplace(number, key);
    for (const auto& [key, number] : previous.numbers_) {
        if ((numbers_.contains(key) && numbers_.at(key) != number) ||
            (used.contains(number) && used.at(number) != key))
            throw std::runtime_error("Conflicting document dimension identifiers");
        numbers_.emplace(key, number);
        used.emplace(number, key);
    }
    next_ = std::max(next_, previous.next_);
}
std::string DimensionIdentifiers::identifier(const std::string& owner,
                                             const std::string& key) const {
    auto found = numbers_.find({owner, key});
    // Point and generic placement editors expose the same persisted slot with
    // their existing semantic prefixes. Never allocate a second number for it.
    if (found == numbers_.end() && key.starts_with("parameter:placement:"))
        found = numbers_.find({owner, "parameter:" + key.substr(20)});
    if (found == numbers_.end() && key.starts_with("parameter:") &&
        !key.starts_with("parameter:placement:"))
        found = numbers_.find({owner, "parameter:placement:" + key.substr(10)});
    return found == numbers_.end() ? std::string{} : "d" + std::to_string(found->second);
}
std::string DimensionIdentifiers::serialized() const {
    auto entries = nlohmann::json::array();
    for (const auto& [key, number] : numbers_)
        entries.push_back({{"owner", key.first}, {"key", key.second}, {"number", number}});
    return nlohmann::json{{"next", next_}, {"entries", entries}}.dump();
}
DimensionIdentifiers DimensionIdentifiers::from_serialized(const std::string& data) {
    const auto root = nlohmann::json::parse(data);
    DimensionIdentifiers result;
    if (!root.at("next").is_number_unsigned())
        throw std::runtime_error("Invalid dimension sequence");
    result.next_ = root.at("next").get<std::uint64_t>();
    std::set<std::uint64_t> used;
    if (!result.next_) throw std::runtime_error("Invalid dimension sequence");
    for (const auto& entry : root.at("entries")) {
        const Key key{entry.at("owner").get<std::string>(), entry.at("key").get<std::string>()};
        if (!entry.at("number").is_number_unsigned())
            throw std::runtime_error("Invalid dimension number");
        const auto number = entry.at("number").get<std::uint64_t>();
        if (key.first.empty() || key.second.empty() || !number || number >= result.next_ ||
            !used.insert(number).second || !result.numbers_.emplace(key, number).second)
            throw std::runtime_error("Invalid document dimension identifier");
    }
    return result;
}
namespace {
void sketch_parameters(std::vector<DimensionParameter>& out,
                       const zima::sketcher::Sketch& sketch) {
    for (const auto& dimension : sketch.dimensions)
        out.push_back({sketch.id, "dimension:" + dimension.id, sketch.name});
    for (const auto& radius : sketch.corner_radii)
        out.push_back({sketch.id, "corner_dimension:" + radius.id, sketch.name});
}
void embedded_sketch(std::vector<DimensionParameter>& out, const std::string& data) {
    if (!data.empty())
        sketch_parameters(out, zima::sketcher::Sketch::from_serialized(data));
}
void placement_parameters(std::vector<DimensionParameter>& out,
                          const std::string& id, const std::string& name,
                          const std::vector<ConstructionReference>& references,
                          bool point = false) {
    for (const auto* key : {"x", "y", "z", "rotation_x", "rotation_y", "rotation_z"}) {
        const auto prefix = point && std::string_view(key).size() == 1
            ? "parameter:" : "parameter:placement:";
        out.push_back({id, std::string(prefix) + key, name});
    }
    std::size_t index = 0;
    for (const auto& reference : references) {
        if (reference.orientation_only ||
            (reference.owner_id.empty() && reference.semantic_key.empty())) continue;
        out.push_back({id, "parameter:placement:reference_offset:" +
            std::to_string(index++), name});
    }
}
} // namespace

void append_dimension_parameters(std::vector<DimensionParameter>& out,
                                 const ConstructionObject& object) {
    placement_parameters(out, object.id, object.name, object.references,
                         object.kind == ConstructionKind::Point);
    const auto add = [&](const char* key) {
        out.push_back({object.id, std::string("parameter:") + key, object.name});
    };
    if (!object.parent_construction_id.empty()) add("radius");
    if (object.kind == ConstructionKind::Plane) add("offset");
    if (object.kind == ConstructionKind::Axis) add("length");
    for (const auto& point : object.curve_points)
        append_dimension_parameters(out, point);
}
void append_dimension_parameters(std::vector<DimensionParameter>& out,
                                 const HistoryContainer& feature) {
    placement_parameters(out, feature.id, feature.name, feature.placement.references);
    const auto add = [&](std::initializer_list<const char*> keys) {
        for (const auto* key : keys)
            out.push_back({feature.id, std::string("parameter:") + key, feature.name});
    };
    // Enumerate parameter slots, never visible dimensions or nonzero values.
    // Names are the existing semantic keys used by the feature editors.
    switch (feature.feature_kind) {
    case FeatureKind::Box: case FeatureKind::Pyramid:
        add({"length", "width", "height"}); break;
    case FeatureKind::Cylinder:
        add({"radius", "height"}); break;
    case FeatureKind::Sphere:
        add({"radius"}); break;
    case FeatureKind::Cone:
        add({"bottom_radius", "top_radius", "height"}); break;
    case FeatureKind::Wedge:
        add({"length", "width", "height", "top_offset"}); break;
    case FeatureKind::Sketch:
        add({"profile_offset"}); break;
    case FeatureKind::Extrusion:
        add({"profile_offset", "length_forward", "length_reverse", "thin_thickness"}); break;
    case FeatureKind::Revolution:
        add({"profile_offset", "angle", "length_reverse", "thin_thickness"}); break;
    case FeatureKind::Fillet:
        add({"primary", "secondary"}); break;
    case FeatureKind::Chamfer:
        add({"primary", "secondary", "treatment_angle"}); break;
    case FeatureKind::Shell:
        add({"thickness"}); break;
    case FeatureKind::Hole:
        add({"diameter", "bore_length", "entrance_chamfer", "exit_chamfer",
             "drill_point_angle", "thread_diameter", "thread_pitch", "thread_length"}); break;
    case FeatureKind::Thread:
        add({"bore_diameter", "bore_length", "thread_designation", "pitch", "thread_length",
             "length_reverse", "chamfer_depth", "chamfer_angle", "drill_point_angle",
             "runout_pitch_factor"}); break;
    case FeatureKind::ShaftThread:
        add({"nominal_diameter", "root_diameter", "pitch", "length", "runout_pitch_factor"}); break;
    case FeatureKind::DrillPoint:
        add({"angle"}); break;
    case FeatureKind::Sweep2D:
        add({"thickness"});
        for (const auto& sketch : feature.sweep2d.sketches) embedded_sketch(out, sketch);
        break;
    case FeatureKind::HelicalSweep:
        add({"pitch"});
        for (const auto& sketch : feature.helical.sketches) embedded_sketch(out, sketch);
        break;
    case FeatureKind::Sweep3D:
        add({"thickness"});
        if (!feature.sweep3d.path.id.empty())
            append_dimension_parameters(out, feature.sweep3d.path);
        for (const auto& profile : feature.sweep3d.profiles)
            embedded_sketch(out, profile.sketch_serialized);
        break;
    case FeatureKind::ImportedStep: break;
    }
    if (feature.feature_kind == FeatureKind::Hole || feature.feature_kind == FeatureKind::Thread) {
        embedded_sketch(out, feature.hole.sketch_serialized);
        embedded_sketch(out, feature.hole.chamfer_sketch_serialized);
        embedded_sketch(out, feature.hole.tip_sketch_serialized);
    }
}
std::vector<DimensionParameter> PartDocument::dimension_parameters() const {
    std::vector<DimensionParameter> result;
    for (const auto& sketch : sketches) {
        sketch_parameters(result, sketch);
        if (sketch.owner_container_id.empty())
            result.push_back({sketch.id, "parameter:profile_offset", sketch.name});
    }
    for (const auto& feature : history) append_dimension_parameters(result, feature);
    for (const auto& object : constructions) append_dimension_parameters(result, object);
    // An owned sketch can also be present in its feature's persisted input.
    std::set<DimensionIdentifiers::Key> seen;
    std::erase_if(result, [&](const auto& parameter) {
        return !seen.emplace(parameter.owner_id, parameter.semantic_key).second;
    });
    return result;
}
void PartDocument::synchronize_dimension_identifiers() {
    dimension_identifiers.synchronize(dimension_parameters());
}
} // namespace zima::document
