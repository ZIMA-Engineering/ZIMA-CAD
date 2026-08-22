#include <zima/document/part_document.hpp>
#include <zima/document/versioned_file.hpp>
#include <zima/document/viewer_packet_json.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <unordered_map>

namespace zima::document {
namespace {

std::string make_id() {
    std::mt19937_64 generator(
        static_cast<std::mt19937_64::result_type>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<unsigned long long> distribution;
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16)
           << distribution(generator) << std::setw(16) << distribution(generator);
    return stream.str();
}

using IniSections = std::map<std::string, std::map<std::string, std::string>>;

std::string trim_ini(std::string value) {
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r");
    return value.substr(first, last - first + 1);
}

IniSections read_ini(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Cannot open document: " + path.string());
    }
    IniSections result;
    std::string line;
    std::string section;
    while (std::getline(input, line)) {
        line = trim_ini(std::move(line));
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = trim_ini(line.substr(1, line.size() - 2));
            if (section.empty()) throw std::runtime_error("Invalid INI section");
            result.try_emplace(section);
            continue;
        }
        const auto separator = line.find('=');
        if (section.empty() || separator == std::string::npos) {
            throw std::runtime_error("Invalid INI document line");
        }
        result[section][trim_ini(line.substr(0, separator))] =
            trim_ini(line.substr(separator + 1));
    }
    if (!input.eof()) throw std::runtime_error("Cannot read document: " + path.string());
    return result;
}

const std::string& ini_required(
    const IniSections& ini, const std::string& section, const std::string& key) {
    const auto found_section = ini.find(section);
    if (found_section == ini.end()) {
        throw std::runtime_error("Missing INI section [" + section + "]");
    }
    const auto found = found_section->second.find(key);
    if (found == found_section->second.end()) {
        throw std::runtime_error("Missing INI key " + section + "." + key);
    }
    return found->second;
}

std::string ini_value(
    const IniSections& ini, const std::string& section, const std::string& key,
    std::string fallback = {}) {
    const auto found_section = ini.find(section);
    if (found_section == ini.end()) return fallback;
    const auto found = found_section->second.find(key);
    return found == found_section->second.end() ? fallback : found->second;
}

std::string json_text(const nlohmann::json& value) {
    return value.is_string() ? value.get<std::string>() :
        value.is_boolean() ? (value.get<bool>() ? "true" : "false") :
        value.dump();
}

std::string feature_entity_kind(const std::string& type) {
    if (type == "box" || type == "cylinder" || type == "sphere" ||
        type == "cone" || type == "pyramid" || type == "wedge") return type;
    if (type == "extrusion") return "protrusion";
    if (type == "revolution") return "revolve";
    return type;
}

// The persisted `combine_mode` field must match Python's `CombineMode` enum
// values ("0", "+", "-"), while the JSON `combine` field keeps the readable
// "add"/"subtract" spelling used elsewhere in this file.
std::string combine_mode_value(const std::string& combine) {
    if (combine == "subtract") return "-";
    if (combine == "add") return "+";
    return "0";
}

std::string feature_container_type(const std::string& type) {
    const auto kind = feature_entity_kind(type);
    std::string result;
    result.reserve(kind.size());
    for (const auto character : kind) {
        result.push_back(static_cast<char>(std::toupper(
            static_cast<unsigned char>(character))));
    }
    return result;
}

void add_common_entity_fields(
    std::map<std::string, std::string>& section,
    const std::string& id, const std::string& name, const std::string& kind,
    const std::string& combine, bool suppressed,
    const Placement& placement = {}) {
    section["id"] = id;
    section["name"] = name;
    section["kind"] = kind;
    section["combine_mode"] = combine;
    section["x"] = std::to_string(placement.x);
    section["y"] = std::to_string(placement.y);
    section["z"] = std::to_string(placement.z);
    section["rx"] = std::to_string(placement.rotation_x);
    section["ry"] = std::to_string(placement.rotation_y);
    section["rz"] = std::to_string(placement.rotation_z);
    section["user_visible"] = "true";
    section["suppressed"] = suppressed ? "true" : "false";
    section["tree_exposure"] = "public";
    section["show_auxiliary_geometry"] = "false";
}

void add_json_parameters(
    std::map<std::string, std::string>& section, const nlohmann::json& value) {
    for (auto it = value.begin(); it != value.end(); ++it) {
        if (it.key() == "id" || it.key() == "name" || it.key() == "type" ||
            it.key() == "combine" || it.key() == "suppressed" ||
            it.key() == "container_origin" || it.key() == "placement") {
            continue;
        }
        section["param." + it.key()] = json_text(it.value());
    }
}

nlohmann::json read_part_ini(const std::filesystem::path& path) {
    const auto ini = read_ini(path);
    if (ini_value(ini, "Document", "format_version") != "11") {
        throw std::runtime_error("Unsupported ZIMA-CAD Part document format");
    }
    nlohmann::json root = {
        {"format", "zima-cad-cpp"},
        {"format_version", 36},
        {"document_id", ini_required(ini, "Document", "document_id")},
        {"type", ini_value(ini, "Document", "type", "part")},
        {"name", ini_value(ini, "Document", "name", "Nový díl")},
        {"family_table", ini_value(ini, "Document", "family_table",
            "{\"columns\":[],\"instances\":[]}")},
        {"user_parameters", nlohmann::json::object()},
        {"user_parameter_order", nlohmann::json::array()},
        {"user_parameter_labels", nlohmann::json::object()},
        {"user_parameter_values", nlohmann::json::object()},
        {"relations", nlohmann::json::array()},
        {"document_units", nlohmann::json::object()},
        {"document_precision", nlohmann::json::object()},
        {"physical_parameters", nlohmann::json::object()},
        {"physical_parameter_units", nlohmann::json::object()},
        {"material_parameter_descriptions", nlohmann::json::object()},
        {"history", nlohmann::json::array()},
        {"sketches", nlohmann::json::array()},
        {"constructions", nlohmann::json::array()},
        {"history_order", nlohmann::json::array()},
        {"history_cursor", 0},
        {"calculated_boundaries", nlohmann::json::array()},
    };
    const auto copy_section = [&](const char* section_name, const char* root_name) {
        const auto found = ini.find(section_name);
        if (found == ini.end()) return;
        for (const auto& [key, value] : found->second) root[root_name][key] = value;
    };
    copy_section("DocumentUnits", "document_units");
    copy_section("DocumentPrecision", "document_precision");
    copy_section("MaterialProperties", "physical_parameters");
    if (ini.contains("Material")) {
        const auto material_name = ini_value(ini, "Material", "Name");
        if (!material_name.empty()) {
            root["physical_parameters"]["MATERIAL_NAME"] = material_name;
        }
    }
    copy_section("MaterialUnits", "physical_parameter_units");
    if (const auto found = ini.find("Relations"); found != ini.end() &&
        found->second.contains("Data")) {
        root["relations"] = nlohmann::json::parse(found->second.at("Data"));
    }
    if (const auto found = ini.find("UserParameters"); found != ini.end()) {
        if (const auto order = found->second.find("Order");
            order != found->second.end()) {
            std::stringstream values(order->second);
            std::string value;
            while (std::getline(values, value, ',')) {
                value = trim_ini(std::move(value));
                if (!value.empty()) root["user_parameter_order"].push_back(value);
            }
        }
    }
    const auto read_language_map = [&](const char* section_name, const char* root_name) {
        const auto found = ini.find(section_name);
        if (found == ini.end()) return;
        for (const auto& [key, value] : found->second) {
            const auto separator = key.rfind('\\');
            const auto parameter = separator == std::string::npos
                ? key : key.substr(0, separator);
            const auto language = separator == std::string::npos
                ? std::string{} : key.substr(separator + 1);
            root[root_name][parameter][language] = value;
            if (std::string_view(root_name) == "user_parameter_values" &&
                language.empty()) {
                root["user_parameters"][parameter] = value;
            }
        }
    };
    read_language_map("UserParameterLabels", "user_parameter_labels");
    read_language_map("UserParameterValues", "user_parameter_values");
    read_language_map("MaterialDescriptions", "material_parameter_descriptions");

    const auto containers = ini_value(ini, "Containers", "items");
    std::stringstream ids(containers);
    std::string id;
    while (std::getline(ids, id, ',')) {
        id = trim_ini(std::move(id));
        if (id.empty()) continue;
        const auto section = "Container." + id;
        const auto role = ini_value(ini, section, "param.cpp_kind");
        if (role == "history") {
            root["history"].push_back(nlohmann::json::parse(
                ini_required(ini, section, "param.cpp_history")));
            root["history_order"].push_back({
                {"kind", "feature"}, {"id", id}});
        } else if (role == "sketch") {
            auto native_sketch = ini_value(ini, "Entity." + id + ":sketch", "param.cpp_sketch");
            if (native_sketch.empty()) {
                native_sketch = ini_value(ini, "Entity." + id, "param.cpp_sketch");
            }
            if (native_sketch.empty()) {
                native_sketch = ini_required(ini, section, "param.cpp_sketch");
            }
            root["sketches"].push_back(nlohmann::json::parse(native_sketch));
            root["history_order"].push_back({
                {"kind", "sketch"}, {"id", id}});
        } else if (role == "construction") {
            root["constructions"].push_back(nlohmann::json::parse(
                ini_required(ini, section, "param.cpp_construction")));
            root["history_order"].push_back({
                {"kind", "construction"}, {"id", id}});
        }
    }
    if (const auto cursor = ini_value(ini, "Document", "history_cursor");
        !cursor.empty()) {
        root["history_cursor"] = std::stoull(cursor);
    }
    if (const auto found = ini.find("CachedBodies"); found != ini.end() &&
        ini_value(ini, "CachedBodies", "encoding") ==
            "zima-cpp-body-results-json") {
        const auto data = ini_value(ini, "CachedBodies", "data");
        if (!data.empty()) root["calculated_boundaries"] = nlohmann::json::parse(data);
    }
    return root;
}

void write_part_ini(
    const nlohmann::json& root, const std::filesystem::path& path) {
    IniSections ini;
    ini["Document"] = {
        {"format_version", "11"},
        {"type", "part"},
        {"document_id", root.at("document_id").get<std::string>()},
        {"name", root.at("name").get<std::string>()},
        {"family_table", root.at("family_table").get<std::string>()},
        {"history_cursor", std::to_string(root.at("history_cursor").get<std::size_t>())},
    };
    const auto copy_object = [&](const char* section_name, const char* root_name) {
        for (auto it = root.at(root_name).begin(); it != root.at(root_name).end(); ++it) {
            ini[section_name][it.key()] = json_text(it.value());
        }
    };
    copy_object("DocumentUnits", "document_units");
    copy_object("DocumentPrecision", "document_precision");
    copy_object("MaterialProperties", "physical_parameters");
    ini["Material"]["Name"] = root.at("physical_parameters").value("MATERIAL_NAME", "");
    ini.erase("MaterialProperties");
    for (auto it = root.at("physical_parameters").begin();
         it != root.at("physical_parameters").end(); ++it) {
        if (it.key() != "MATERIAL_NAME") ini["MaterialProperties"][it.key()] =
            json_text(it.value());
    }
    copy_object("MaterialUnits", "physical_parameter_units");
    for (auto it = root.at("material_parameter_descriptions").begin();
         it != root.at("material_parameter_descriptions").end(); ++it) {
        for (auto language = it.value().begin(); language != it.value().end(); ++language) {
            ini["MaterialDescriptions"][it.key() +
                (language.key().empty() ? "" : "\\" + language.key())] =
                json_text(language.value());
        }
    }
    if (!root.at("relations").empty()) ini["Relations"]["Data"] =
        root.at("relations").dump();
    std::string order;
    for (const auto& value : root.at("user_parameter_order")) {
        if (!order.empty()) order += ", ";
        order += value.get<std::string>();
    }
    ini["UserParameters"]["Order"] = order;
    for (auto it = root.at("user_parameter_labels").begin();
         it != root.at("user_parameter_labels").end(); ++it) {
        for (auto language = it.value().begin(); language != it.value().end(); ++language) {
            ini["UserParameterLabels"][it.key() +
                (language.key().empty() ? "" : "\\" + language.key())] =
                json_text(language.value());
        }
    }
    for (auto it = root.at("user_parameter_values").begin();
         it != root.at("user_parameter_values").end(); ++it) {
        for (auto language = it.value().begin(); language != it.value().end(); ++language) {
            ini["UserParameterValues"][it.key() +
                (language.key().empty() ? "" : "\\" + language.key())] =
                json_text(language.value());
        }
    }
    for (auto it = root.at("user_parameters").begin();
         it != root.at("user_parameters").end(); ++it) {
        const auto key = it.key();
        if (!ini["UserParameterValues"].contains(key)) {
            ini["UserParameterValues"][key] = json_text(it.value());
        }
    }
    std::vector<std::string> container_ids;
    std::vector<PartHistoryEntry> order_entries;
    for (const auto& entry : root.at("history_order")) {
        order_entries.push_back({
            entry.at("kind") == "feature" ? PartHistoryKind::Feature
                : entry.at("kind") == "sketch" ? PartHistoryKind::Sketch
                : PartHistoryKind::Construction,
            entry.at("id").get<std::string>()});
    }
    for (const auto& entry : order_entries) {
        container_ids.push_back(entry.id);
        if (entry.kind == PartHistoryKind::Feature) {
            const auto& value = *std::find_if(root.at("history").begin(),
                root.at("history").end(), [&](const auto& item) {
                    return item.at("id") == entry.id;
                });
            const auto type = value.at("type").get<std::string>();
            auto& container = ini["Container." + entry.id];
            add_common_entity_fields(container, entry.id, value.at("name"),
                "container", combine_mode_value(value.at("combine").get<std::string>()),
                value.at("suppressed").get<bool>(), value.value("placement", nlohmann::json::object()).is_object()
                    ? Placement{value.value("placement", nlohmann::json::object()).value("x", 0.0),
                        value.value("placement", nlohmann::json::object()).value("y", 0.0),
                        value.value("placement", nlohmann::json::object()).value("z", 0.0),
                        value.value("placement", nlohmann::json::object()).value("rotation_x", 0.0),
                        value.value("placement", nlohmann::json::object()).value("rotation_y", 0.0),
                        value.value("placement", nlohmann::json::object()).value("rotation_z", 0.0)}
                    : Placement{});
            container["TYPE"] = feature_container_type(type);
            container["param.cpp_kind"] = "history";
            container["param.cpp_history"] = value.dump();
            const auto feature_id = value.at("feature_id").get<std::string>();
            auto& feature = ini["Entity." + feature_id];
            add_common_entity_fields(feature, feature_id, value.at("name"),
                feature_entity_kind(type), combine_mode_value(value.at("combine").get<std::string>()),
                value.at("suppressed").get<bool>());
            feature["tree_exposure"] = "internal";
            feature["param.cpp_feature_id"] = feature_id;
            add_json_parameters(feature, value);
            feature["param.unit"] = "mm";
            feature["param.operation"] = value.at("combine").get<std::string>();
            if (type == "sphere" || type == "cylinder") {
                feature["param.diameter"] = std::to_string(
                    2.0 * value.at("radius").get<double>());
            } else if (type == "cone") {
                feature["param.bottom_diameter"] = std::to_string(
                    2.0 * value.at("bottom_radius").get<double>());
                feature["param.top_diameter"] = std::to_string(
                    2.0 * value.at("top_radius").get<double>());
            } else if (type == "revolution") {
                feature["param.angle"] = std::to_string(
                    value.at("angle_degrees").get<double>());
            } else if (type == "fillet") {
                feature["param.radius"] = std::to_string(
                    value.at("size").get<double>());
            } else if (type == "chamfer") {
                feature["param.distance"] = std::to_string(
                    value.at("size").get<double>());
            }
            ini["Children." + entry.id]["items"] = feature_id;
        } else if (entry.kind == PartHistoryKind::Sketch) {
            const auto& value = *std::find_if(root.at("sketches").begin(),
                root.at("sketches").end(), [&](const auto& item) {
                    return item.at("id") == entry.id;
                });
            auto& container = ini["Container." + entry.id];
            add_common_entity_fields(container, entry.id, value.at("name"),
                "container", "0", value.at("suppressed").get<bool>());
            container["TYPE"] = "SKETCH";
            container["param.cpp_kind"] = "sketch";
            const auto sketch_entity_id = entry.id + ":sketch";
            auto& sketch = ini["Entity." + sketch_entity_id];
            add_common_entity_fields(sketch, sketch_entity_id, value.at("name"), "sketch",
                "0", value.at("suppressed").get<bool>());
            sketch["tree_exposure"] = "internal";
            sketch["param.plane"] = value.value("plane", "xy");
            sketch["param.role"] = "PROFILE";
            sketch["param.cpp_sketch"] = value.dump();
            sketch["param.sketch_data"] =
                "{\"version\":3,\"points\":{},\"geometry\":{},\"constraints\":{},\"dimensions\":{}}";
            ini["Children." + entry.id]["items"] = sketch_entity_id;
        } else {
            const auto& value = *std::find_if(root.at("constructions").begin(),
                root.at("constructions").end(), [&](const auto& item) {
                    return item.at("id") == entry.id;
                });
            const auto kind = value.at("type").get<std::string>();
            auto& container = ini["Container." + entry.id];
            add_common_entity_fields(container, entry.id, value.at("name"),
                "container", "0", value.at("suppressed").get<bool>());
            container["TYPE"] = kind == "point" ? "POINT" : kind == "axis" ? "AXIS" : "PLANE";
            container["param.cpp_kind"] = "construction";
            container["param.cpp_construction"] = value.dump();
            const auto construction_entity_id = entry.id + ":construction";
            auto& construction = ini["Entity." + construction_entity_id];
            add_common_entity_fields(construction, construction_entity_id, value.at("name"), kind,
                "0", value.at("suppressed").get<bool>());
            construction["tree_exposure"] = "internal";
            add_json_parameters(construction, value);
            ini["Children." + entry.id]["items"] = construction_entity_id;
        }
    }
    std::string items;
    for (const auto& id : container_ids) {
        if (!items.empty()) items += ",";
        items += id;
    }
    ini["Containers"]["items"] = items;
    if (!root.at("calculated_boundaries").empty()) {
        ini["CachedBodies"] = {
            {"encoding", "zima-cpp-body-results-json"},
            {"data", root.at("calculated_boundaries").dump()},
        };
    }
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot write document: " + path.string());
        for (const auto& [section, values] : ini) {
            output << "[" << section << "]\n";
            for (const auto& [key, value] : values) output << key << "=" << value << "\n";
            output << "\n";
        }
        if (!output) throw std::runtime_error("Document write failed: " + path.string());
    }
    try {
        static_cast<void>(PartDocument::load(temporary));
    } catch (...) {
        std::error_code error;
        std::filesystem::remove(temporary, error);
        throw;
    }
    archive_existing_file(path);
    std::filesystem::rename(temporary, path);
}

void require_positive(double value, const char* field) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::runtime_error(std::string(field) + " must be finite and positive");
    }
}

void require_finite(double value, const char* field) {
    if (!std::isfinite(value)) {
        throw std::runtime_error(std::string(field) + " must be finite");
    }
}

void validate_placement(const Placement& placement) {
    require_finite(placement.x, "placement x");
    require_finite(placement.y, "placement y");
    require_finite(placement.z, "placement z");
    require_finite(placement.rotation_x, "rotation x");
    require_finite(placement.rotation_y, "rotation y");
    require_finite(placement.rotation_z, "rotation z");
}

void require_default_sketch_feature_placement(const Placement& placement) {
    if (placement.x != 0.0 || placement.y != 0.0 || placement.z != 0.0 ||
        placement.rotation_x != 0.0 || placement.rotation_y != 0.0 ||
        placement.rotation_z != 0.0) {
        throw std::runtime_error(
            "Sketch feature placement is defined by its source Sketch");
    }
}

std::string stable_profile_id(std::string prefix, std::vector<std::string> ids) {
    std::sort(ids.begin(), ids.end());
    for (const auto& id : ids) prefix += ":" + std::to_string(id.size()) + ":" + id;
    return prefix;
}

void validate_extrusion_direction(ExtrusionDirection direction) {
    switch (direction) {
        case ExtrusionDirection::Forward:
        case ExtrusionDirection::Reverse:
        case ExtrusionDirection::Symmetric:
            return;
    }
    throw std::runtime_error("Invalid Extrusion direction");
}

using ProfileLoop = zima::kernel::ExtrusionRequest::ProfileLoop;
using ProfileRegion = zima::kernel::ExtrusionRequest::ProfileRegion;
using ProfilePolygon = std::vector<std::array<double, 2>>;

ProfilePolygon sampled_profile_loop(
    const ProfileLoop& loop, const zima::sketcher::Sketch& sketch) {
    ProfilePolygon result;
    const auto local = [&](const zima::kernel::Vec3& point) {
        return sketch.local_point(point);
    };
    const auto append = [&](const std::array<double, 2>& point) {
        if (result.empty() || std::hypot(result.back()[0] - point[0],
                                        result.back()[1] - point[1]) > 1.0e-9) {
            result.push_back(point);
        }
    };
    std::visit([&](const auto& profile) {
        using Profile = std::decay_t<decltype(profile)>;
        if constexpr (std::is_same_v<Profile,
                          zima::kernel::ExtrusionRequest::PolygonProfile>) {
            for (const auto& point : profile.vertices) append(local(point));
        } else if constexpr (std::is_same_v<Profile,
                                 zima::kernel::ExtrusionRequest::CircleProfile>) {
            const auto center = local(profile.center);
            for (int index = 0; index < 96; ++index) {
                const double angle = 2.0 * std::numbers::pi * index / 96.0;
                append({center[0] + profile.radius * std::cos(angle),
                        center[1] + profile.radius * std::sin(angle)});
            }
        } else if constexpr (std::is_same_v<Profile,
                                 zima::kernel::ExtrusionRequest::EllipseProfile>) {
            const auto center = local(profile.center);
            const auto axis_point = local({
                profile.center.x + profile.major_axis_direction.x,
                profile.center.y + profile.major_axis_direction.y,
                profile.center.z + profile.major_axis_direction.z});
            const std::array<double, 2> axis{
                axis_point[0] - center[0], axis_point[1] - center[1]};
            for (int index = 0; index < 128; ++index) {
                const double angle = 2.0 * std::numbers::pi * index / 128.0;
                append({center[0] + axis[0] * profile.major_radius * std::cos(angle) -
                            axis[1] * profile.minor_radius * std::sin(angle),
                        center[1] + axis[1] * profile.major_radius * std::cos(angle) +
                            axis[0] * profile.minor_radius * std::sin(angle)});
            }
        } else {
            for (const auto& curve_variant : profile.curves) {
                std::visit([&](const auto& curve) {
                    using Curve = std::decay_t<decltype(curve)>;
                    if constexpr (std::is_same_v<Curve,
                                      zima::kernel::ExtrusionRequest::LineCurve>) {
                        append(local(curve.start));
                        append(local(curve.end));
                    } else if constexpr (std::is_same_v<Curve,
                                             zima::kernel::ExtrusionRequest::ArcCurve>) {
                        const auto first = local(curve.start);
                        const auto middle = local(curve.middle);
                        const auto last = local(curve.end);
                        const double divisor = 2.0 * (first[0] * (middle[1] - last[1]) +
                            middle[0] * (last[1] - first[1]) +
                            last[0] * (first[1] - middle[1]));
                        if (std::abs(divisor) <= 1.0e-12) {
                            append(first); append(last); return;
                        }
                        const double first_norm = first[0] * first[0] + first[1] * first[1];
                        const double middle_norm = middle[0] * middle[0] + middle[1] * middle[1];
                        const double last_norm = last[0] * last[0] + last[1] * last[1];
                        const std::array<double, 2> center{
                            (first_norm * (middle[1] - last[1]) +
                             middle_norm * (last[1] - first[1]) +
                             last_norm * (first[1] - middle[1])) / divisor,
                            (first_norm * (last[0] - middle[0]) +
                             middle_norm * (first[0] - last[0]) +
                             last_norm * (middle[0] - first[0])) / divisor};
                        const double radius = std::hypot(
                            first[0] - center[0], first[1] - center[1]);
                        double start = std::atan2(first[1] - center[1], first[0] - center[0]);
                        double mid = std::atan2(middle[1] - center[1], middle[0] - center[0]);
                        double end = std::atan2(last[1] - center[1], last[0] - center[0]);
                        while (mid < start) mid += 2.0 * std::numbers::pi;
                        while (end < start) end += 2.0 * std::numbers::pi;
                        if (mid > end) end -= 2.0 * std::numbers::pi;
                        for (int index = 0; index <= 48; ++index) {
                            const double angle = start + (end - start) * index / 48.0;
                            append({center[0] + radius * std::cos(angle),
                                    center[1] + radius * std::sin(angle)});
                        }
                    } else if constexpr (std::is_same_v<Curve,
                                             zima::kernel::ExtrusionRequest::EllipticalArcCurve>) {
                        const auto center = local(curve.center);
                        const auto axis_point = local({
                            curve.center.x + curve.major_axis_direction.x,
                            curve.center.y + curve.major_axis_direction.y,
                            curve.center.z + curve.major_axis_direction.z});
                        const std::array<double, 2> axis{
                            axis_point[0] - center[0], axis_point[1] - center[1]};
                        for (int index = 0; index <= 64; ++index) {
                            const double parameter = curve.start_parameter +
                                (curve.end_parameter - curve.start_parameter) * index / 64.0;
                            const double signed_parameter = curve.reversed ? -parameter : parameter;
                            append({center[0] + axis[0] * curve.major_radius *
                                        std::cos(signed_parameter) - axis[1] *
                                        curve.minor_radius * std::sin(signed_parameter),
                                    center[1] + axis[1] * curve.major_radius *
                                        std::cos(signed_parameter) + axis[0] *
                                        curve.minor_radius * std::sin(signed_parameter)});
                        }
                    } else {
                        for (const auto& point : curve.control_points) append(local(point));
                    }
                }, curve_variant);
            }
        }
    }, loop);
    if (result.size() >= 2 && std::hypot(result.front()[0] - result.back()[0],
                                        result.front()[1] - result.back()[1]) <= 1.0e-9) {
        result.pop_back();
    }
    return result;
}

std::vector<ProfileRegion> request_regions(
    zima::kernel::ExtrusionRequest request) {
    ProfileRegion first;
    first.region_id = std::move(request.profile_region_id);
    first.outer_boundary_id = std::move(request.outer_boundary_id);
    first.inner_boundary_ids = std::move(request.inner_boundary_ids);
    first.outer_edge_source_ids = std::move(request.outer_edge_source_ids);
    first.inner_edge_source_ids = std::move(request.inner_edge_source_ids);
    first.outer_vertex_source_ids = std::move(request.outer_vertex_source_ids);
    first.inner_vertex_source_ids = std::move(request.inner_vertex_source_ids);
    first.outer_profile = std::move(request.outer_profile);
    first.inner_profiles = std::move(request.inner_profiles);
    std::vector<ProfileRegion> regions;
    regions.push_back(std::move(first));
    regions.insert(regions.end(),
        std::make_move_iterator(request.additional_profile_regions.begin()),
        std::make_move_iterator(request.additional_profile_regions.end()));
    return regions;
}

void assign_regions(zima::kernel::ExtrusionRequest& request,
                    std::vector<ProfileRegion> regions) {
    if (regions.empty()) throw std::runtime_error("Profile has no solid region");
    request.profile_region_id = std::move(regions.front().region_id);
    request.outer_boundary_id = std::move(regions.front().outer_boundary_id);
    request.inner_boundary_ids = std::move(regions.front().inner_boundary_ids);
    request.outer_edge_source_ids = std::move(regions.front().outer_edge_source_ids);
    request.inner_edge_source_ids = std::move(regions.front().inner_edge_source_ids);
    request.outer_vertex_source_ids =
        std::move(regions.front().outer_vertex_source_ids);
    request.inner_vertex_source_ids =
        std::move(regions.front().inner_vertex_source_ids);
    request.outer_profile = std::move(regions.front().outer_profile);
    request.inner_profiles = std::move(regions.front().inner_profiles);
    request.additional_profile_regions.assign(
        std::make_move_iterator(regions.begin() + 1),
        std::make_move_iterator(regions.end()));
}

zima::kernel::ExtrusionRequest extrusion_request(
    const zima::sketcher::Sketch& sketch, double height,
    ExtrusionDirection direction_mode) {
    require_positive(height, "extrusion height");
    validate_extrusion_direction(direction_mode);
    zima::kernel::ExtrusionRequest request;
    request.direction = sketch.plane == zima::sketcher::SketchPlane::XY
        ? zima::kernel::Vec3{0.0, 0.0, height}
        : sketch.plane == zima::sketcher::SketchPlane::XZ
            ? zima::kernel::Vec3{0.0, -height, 0.0}
            : zima::kernel::Vec3{height, 0.0, 0.0};
    if (direction_mode == ExtrusionDirection::Reverse) {
        request.first_cap_is_start = false;
        request.direction.x = -request.direction.x;
        request.direction.y = -request.direction.y;
        request.direction.z = -request.direction.z;
    }
    const auto finalize = [&](zima::kernel::ExtrusionRequest value) {
        if (direction_mode != ExtrusionDirection::Symmetric) return value;
        const zima::kernel::Vec3 offset{
            -0.5 * value.direction.x,
            -0.5 * value.direction.y,
            -0.5 * value.direction.z};
        const auto shift = [&](auto& profile_variant) {
            std::visit([&](auto& profile) {
                using Profile = std::decay_t<decltype(profile)>;
                if constexpr (std::is_same_v<Profile,
                                  zima::kernel::ExtrusionRequest::PolygonProfile>) {
                    for (auto& point : profile.vertices) {
                        point.x += offset.x;
                        point.y += offset.y;
                        point.z += offset.z;
                    }
                } else if constexpr (std::is_same_v<Profile,
                                         zima::kernel::ExtrusionRequest::CircleProfile>) {
                    profile.center.x += offset.x;
                    profile.center.y += offset.y;
                    profile.center.z += offset.z;
                } else if constexpr (std::is_same_v<Profile,
                                         zima::kernel::ExtrusionRequest::EllipseProfile>) {
                    profile.center.x += offset.x;
                    profile.center.y += offset.y;
                    profile.center.z += offset.z;
                } else {
                    for (auto& curve : profile.curves) {
                        std::visit([&](auto& exact_curve) {
                            const auto shift_point = [&](auto& point) {
                                point.x += offset.x;
                                point.y += offset.y;
                                point.z += offset.z;
                            };
                            shift_point(exact_curve.start);
                            if constexpr (std::is_same_v<
                                              std::decay_t<decltype(exact_curve)>,
                                              zima::kernel::ExtrusionRequest::ArcCurve>) {
                                shift_point(exact_curve.middle);
                            }
                            if constexpr (std::is_same_v<
                                              std::decay_t<decltype(exact_curve)>,
                                              zima::kernel::ExtrusionRequest::EllipticalArcCurve>) {
                                shift_point(exact_curve.center);
                            }
                            if constexpr (std::is_same_v<
                                              std::decay_t<decltype(exact_curve)>,
                                              zima::kernel::ExtrusionRequest::BSplineCurve>) {
                                for (auto& point : exact_curve.control_points) {
                                    shift_point(point);
                                }
                            }
                            shift_point(exact_curve.end);
                        }, curve);
                    }
                }
            }, profile_variant);
        };
        shift(value.outer_profile);
        for (auto& profile : value.inner_profiles) shift(profile);
        for (auto& region : value.additional_profile_regions) {
            shift(region.outer_profile);
            for (auto& profile : region.inner_profiles) shift(profile);
        }
        return value;
    };
    std::vector<const zima::sketcher::SketchSegment*> profile_segments;
    for (const auto& segment : sketch.segments) {
        if (!segment.construction) profile_segments.push_back(&segment);
    }
    std::vector<const zima::sketcher::SketchCircle*> profile_circles;
    for (const auto& circle : sketch.circles) {
        if (!circle.construction) profile_circles.push_back(&circle);
    }
    std::vector<const zima::sketcher::SketchArc*> profile_arcs;
    for (const auto& arc : sketch.arcs) {
        if (!arc.construction) profile_arcs.push_back(&arc);
    }
    std::vector<const zima::sketcher::SketchEllipse*> profile_ellipses;
    for (const auto& ellipse : sketch.ellipses) {
        if (!ellipse.construction) profile_ellipses.push_back(&ellipse);
    }
    std::vector<const zima::sketcher::SketchEllipticalArc*>
        profile_elliptical_arcs;
    for (const auto& arc : sketch.elliptical_arcs) {
        if (!arc.construction) profile_elliptical_arcs.push_back(&arc);
    }
    std::vector<const zima::sketcher::SketchBSpline*> profile_splines;
    for (const auto& spline : sketch.bsplines) {
        if (!spline.construction) profile_splines.push_back(&spline);
    }
    const auto exact_spline = [&](const auto& spline) {
        zima::kernel::ExtrusionRequest::BSplineCurve curve;
        curve.degree = spline.degree;
        curve.periodic = spline.closed;
        for (const auto& point_id : spline.control_point_ids) {
            const auto* point = sketch.find_point(point_id);
            curve.control_points.push_back(sketch.world_point(point->x, point->y));
        }
        curve.start = curve.control_points.front();
        curve.end = spline.closed ? curve.start : curve.control_points.back();
        return curve;
    };
    const auto exact_elliptical_arc = [&](const auto& arc) {
        const auto* center = sketch.find_point(arc.center_point_id);
        const auto* major = sketch.find_point(arc.major_point_id);
        const auto* minor = sketch.find_point(arc.minor_point_id);
        const auto* start = sketch.find_point(arc.start_point_id);
        const auto* end = sketch.find_point(arc.end_point_id);
        const auto center_world = sketch.world_point(center->x, center->y);
        const bool swap_axes = arc.major_radius < arc.minor_radius;
        const auto axis_world = swap_axes
            ? sketch.world_point(minor->x, minor->y)
            : sketch.world_point(major->x, major->y);
        const double major_radius = swap_axes ? arc.minor_radius : arc.major_radius;
        zima::kernel::ExtrusionRequest::EllipticalArcCurve curve;
        curve.start = sketch.world_point(start->x, start->y);
        curve.end = sketch.world_point(end->x, end->y);
        curve.center = center_world;
        curve.major_axis_direction = {
            (axis_world.x - center_world.x) / major_radius,
            (axis_world.y - center_world.y) / major_radius,
            (axis_world.z - center_world.z) / major_radius};
        curve.major_radius = major_radius;
        curve.minor_radius = swap_axes ? arc.major_radius : arc.minor_radius;
        curve.start_parameter = arc.start_parameter -
            (swap_axes ? 3.14159265358979323846 / 2.0 : 0.0);
        curve.end_parameter = arc.end_parameter -
            (swap_axes ? 3.14159265358979323846 / 2.0 : 0.0);
        curve.reversed = arc.reversed !=
            (direction_mode == ExtrusionDirection::Reverse);
        return curve;
    };
    const bool has_ordinary_profile = !profile_segments.empty() ||
        !profile_circles.empty() || !profile_arcs.empty() ||
        !profile_ellipses.empty() || !profile_elliptical_arcs.empty() ||
        !profile_splines.empty();
    if (!sketch.texts.empty() && has_ordinary_profile) {
        auto ordinary_sketch = sketch;
        ordinary_sketch.texts.clear();
        auto text_sketch = sketch;
        text_sketch.segments.clear();
        text_sketch.circles.clear();
        text_sketch.arcs.clear();
        text_sketch.ellipses.clear();
        text_sketch.elliptical_arcs.clear();
        text_sketch.bsplines.clear();
        auto ordinary_request = extrusion_request(
            ordinary_sketch, height, direction_mode);
        auto text_request = extrusion_request(text_sketch, height, direction_mode);
        struct Boundary {
            std::string id;
            ProfileLoop loop;
            std::vector<std::string> edge_source_ids;
            std::vector<std::string> vertex_source_ids;
            ProfilePolygon sample;
            int parent{-1};
            double area{};
        };
        std::vector<Boundary> boundaries;
        const auto append_regions = [&](std::vector<ProfileRegion> regions) {
            for (auto& region : regions) {
                boundaries.push_back({region.outer_boundary_id,
                    std::move(region.outer_profile),
                    std::move(region.outer_edge_source_ids),
                    std::move(region.outer_vertex_source_ids)});
                for (std::size_t index = 0; index < region.inner_profiles.size(); ++index) {
                    boundaries.push_back({
                        index < region.inner_boundary_ids.size()
                            ? region.inner_boundary_ids[index]
                            : region.outer_boundary_id + ":inner:" +
                                std::to_string(index),
                        std::move(region.inner_profiles[index])});
                    boundaries.back().edge_source_ids =
                        index < region.inner_edge_source_ids.size()
                            ? std::move(region.inner_edge_source_ids[index])
                            : std::vector<std::string>{};
                    boundaries.back().vertex_source_ids =
                        index < region.inner_vertex_source_ids.size()
                            ? std::move(region.inner_vertex_source_ids[index])
                            : std::vector<std::string>{};
                }
            }
        };
        append_regions(request_regions(std::move(ordinary_request)));
        append_regions(request_regions(std::move(text_request)));
        const auto cross = [](const auto& a, const auto& b, const auto& c) {
            return (b[0] - a[0]) * (c[1] - a[1]) -
                (b[1] - a[1]) * (c[0] - a[0]);
        };
        const auto on_segment = [&](const auto& point, const auto& a, const auto& b) {
            return std::abs(cross(a, b, point)) <= 1.0e-7 &&
                point[0] >= std::min(a[0], b[0]) - 1.0e-7 &&
                point[0] <= std::max(a[0], b[0]) + 1.0e-7 &&
                point[1] >= std::min(a[1], b[1]) - 1.0e-7 &&
                point[1] <= std::max(a[1], b[1]) + 1.0e-7;
        };
        const auto intersects = [&](const auto& a, const auto& b,
                                     const auto& c, const auto& d) {
            const double ab_c = cross(a, b, c), ab_d = cross(a, b, d);
            const double cd_a = cross(c, d, a), cd_b = cross(c, d, b);
            return ((ab_c > 1.0e-7 && ab_d < -1.0e-7) ||
                    (ab_c < -1.0e-7 && ab_d > 1.0e-7)) &&
                       ((cd_a > 1.0e-7 && cd_b < -1.0e-7) ||
                        (cd_a < -1.0e-7 && cd_b > 1.0e-7)) ||
                on_segment(c, a, b) || on_segment(d, a, b) ||
                on_segment(a, c, d) || on_segment(b, c, d);
        };
        const auto contains = [](const ProfilePolygon& polygon,
                                 const std::array<double, 2>& point) {
            bool inside = false;
            for (std::size_t i = 0, j = polygon.size() - 1;
                 i < polygon.size(); j = i++) {
                const auto& a = polygon[i];
                const auto& b = polygon[j];
                if ((a[1] > point[1]) != (b[1] > point[1]) &&
                    point[0] < (b[0] - a[0]) * (point[1] - a[1]) /
                            (b[1] - a[1]) + a[0]) inside = !inside;
            }
            return inside;
        };
        for (auto& boundary : boundaries) {
            boundary.sample = sampled_profile_loop(boundary.loop, sketch);
            if (boundary.sample.size() < 3) {
                throw std::runtime_error("Mixed profile boundary is degenerate");
            }
            for (std::size_t index = 0; index < boundary.sample.size(); ++index) {
                const auto& a = boundary.sample[index];
                const auto& b = boundary.sample[(index + 1) % boundary.sample.size()];
                boundary.area += a[0] * b[1] - b[0] * a[1];
            }
            boundary.area = std::abs(boundary.area) * 0.5;
        }
        for (std::size_t first = 0; first < boundaries.size(); ++first) {
            for (std::size_t second = first + 1; second < boundaries.size(); ++second) {
                for (std::size_t a = 0; a < boundaries[first].sample.size(); ++a) {
                    for (std::size_t b = 0; b < boundaries[second].sample.size(); ++b) {
                        if (intersects(boundaries[first].sample[a],
                                boundaries[first].sample[(a + 1) %
                                    boundaries[first].sample.size()],
                                boundaries[second].sample[b],
                                boundaries[second].sample[(b + 1) %
                                    boundaries[second].sample.size()])) {
                            throw std::runtime_error(
                                "Mixed profile boundaries overlap or touch");
                        }
                    }
                }
            }
        }
        for (std::size_t index = 0; index < boundaries.size(); ++index) {
            double parent_area = std::numeric_limits<double>::infinity();
            for (std::size_t candidate = 0; candidate < boundaries.size(); ++candidate) {
                if (candidate != index && boundaries[candidate].area > boundaries[index].area &&
                    contains(boundaries[candidate].sample,
                             boundaries[index].sample.front()) &&
                    boundaries[candidate].area < parent_area) {
                    boundaries[index].parent = static_cast<int>(candidate);
                    parent_area = boundaries[candidate].area;
                }
            }
        }
        const auto depth = [&](std::size_t index) {
            int result{};
            for (int parent = boundaries[index].parent; parent >= 0;
                 parent = boundaries[parent].parent) {
                if (++result > static_cast<int>(boundaries.size())) {
                    throw std::runtime_error("Mixed profile containment is cyclic");
                }
            }
            return result;
        };
        std::vector<ProfileRegion> regions;
        for (std::size_t index = 0; index < boundaries.size(); ++index) {
            if (depth(index) % 2 != 0) continue;
            ProfileRegion region;
            region.region_id = "profile-region:" + boundaries[index].id;
            region.outer_boundary_id = boundaries[index].id;
            region.outer_profile = std::move(boundaries[index].loop);
            region.outer_edge_source_ids =
                std::move(boundaries[index].edge_source_ids);
            region.outer_vertex_source_ids =
                std::move(boundaries[index].vertex_source_ids);
            for (std::size_t child = 0; child < boundaries.size(); ++child) {
                if (boundaries[child].parent == static_cast<int>(index)) {
                    region.inner_boundary_ids.push_back(boundaries[child].id);
                    region.inner_profiles.push_back(std::move(boundaries[child].loop));
                    region.inner_edge_source_ids.push_back(
                        std::move(boundaries[child].edge_source_ids));
                    region.inner_vertex_source_ids.push_back(
                        std::move(boundaries[child].vertex_source_ids));
                }
            }
            regions.push_back(std::move(region));
        }
        auto combined = extrusion_request(ordinary_sketch, height, direction_mode);
        assign_regions(combined, std::move(regions));
        return combined;
    }
    if (!sketch.texts.empty()) {
        using Contour = std::vector<std::array<double, 2>>;
        std::vector<Contour> contours;
        std::vector<std::string> contour_ids;
        std::vector<std::string> contour_source_ids;
        for (const auto& text : sketch.texts) {
            for (std::size_t index = 0; index < text.contours.size(); ++index) {
                contours.push_back(text.contours[index]);
                contour_ids.push_back(
                    "text:" + text.id + ":contour:" + std::to_string(index));
                contour_source_ids.push_back(text.id);
            }
        }
        const auto cross = [](const auto& a, const auto& b, const auto& c) {
            return (b[0] - a[0]) * (c[1] - a[1]) -
                (b[1] - a[1]) * (c[0] - a[0]);
        };
        const auto on_segment = [&](const auto& point, const auto& a, const auto& b) {
            return std::abs(cross(a, b, point)) <= 1.0e-9 &&
                point[0] >= std::min(a[0], b[0]) - 1.0e-9 &&
                point[0] <= std::max(a[0], b[0]) + 1.0e-9 &&
                point[1] >= std::min(a[1], b[1]) - 1.0e-9 &&
                point[1] <= std::max(a[1], b[1]) + 1.0e-9;
        };
        const auto segments_intersect = [&](const auto& a, const auto& b,
                                             const auto& c, const auto& d) {
            const double ab_c = cross(a, b, c);
            const double ab_d = cross(a, b, d);
            const double cd_a = cross(c, d, a);
            const double cd_b = cross(c, d, b);
            return ((ab_c > 1.0e-9 && ab_d < -1.0e-9) ||
                    (ab_c < -1.0e-9 && ab_d > 1.0e-9)) &&
                       ((cd_a > 1.0e-9 && cd_b < -1.0e-9) ||
                        (cd_a < -1.0e-9 && cd_b > 1.0e-9)) ||
                on_segment(c, a, b) || on_segment(d, a, b) ||
                on_segment(a, c, d) || on_segment(b, c, d);
        };
        for (std::size_t first = 0; first < contours.size(); ++first) {
            const auto& contour = contours[first];
            for (std::size_t a = 0; a < contour.size(); ++a) {
                for (std::size_t b = a + 1; b < contour.size(); ++b) {
                    if (b == a + 1 || (a == 0 && b + 1 == contour.size())) continue;
                    if (segments_intersect(contour[a], contour[(a + 1) % contour.size()],
                                           contour[b], contour[(b + 1) % contour.size()])) {
                        throw std::runtime_error("Text profile contour self-intersects");
                    }
                }
            }
            for (std::size_t second = first + 1; second < contours.size(); ++second) {
                for (std::size_t a = 0; a < contour.size(); ++a) {
                    for (std::size_t b = 0; b < contours[second].size(); ++b) {
                        if (segments_intersect(
                                contour[a], contour[(a + 1) % contour.size()],
                                contours[second][b],
                                contours[second][(b + 1) % contours[second].size()])) {
                            throw std::runtime_error(
                                "Text profile contours overlap or touch");
                        }
                    }
                }
            }
        }
        const auto contains = [](const Contour& contour,
                                 const std::array<double, 2>& point) {
            bool inside = false;
            for (std::size_t i = 0, j = contour.size() - 1; i < contour.size(); j = i++) {
                const auto& a = contour[i];
                const auto& b = contour[j];
                if ((a[1] > point[1]) != (b[1] > point[1]) &&
                    point[0] < (b[0] - a[0]) * (point[1] - a[1]) /
                            (b[1] - a[1]) + a[0]) inside = !inside;
            }
            return inside;
        };
        std::vector<int> parent(contours.size(), -1);
        std::vector<double> areas(contours.size());
        for (std::size_t index = 0; index < contours.size(); ++index) {
            for (std::size_t point = 0; point < contours[index].size(); ++point) {
                const auto& a = contours[index][point];
                const auto& b = contours[index][(point + 1) % contours[index].size()];
                areas[index] += a[0] * b[1] - b[0] * a[1];
            }
            areas[index] = std::abs(areas[index]) * 0.5;
        }
        for (std::size_t index = 0; index < contours.size(); ++index) {
            double parent_area = std::numeric_limits<double>::infinity();
            for (std::size_t candidate = 0; candidate < contours.size(); ++candidate) {
                if (candidate != index && contains(contours[candidate], contours[index][0]) &&
                    areas[candidate] < parent_area) {
                    parent[index] = static_cast<int>(candidate);
                    parent_area = areas[candidate];
                }
            }
        }
        const auto depth = [&](std::size_t index) {
            int value{};
            for (int current = parent[index]; current >= 0; current = parent[current]) {
                if (++value > static_cast<int>(contours.size())) {
                    throw std::runtime_error("Text profile containment is cyclic");
                }
            }
            return value;
        };
        const auto polygon_profile = [&](const Contour& contour) {
            zima::kernel::ExtrusionRequest::PolygonProfile profile;
            for (const auto& point : contour) {
                profile.vertices.push_back(sketch.world_point(point[0], point[1]));
            }
            return profile;
        };
        std::vector<zima::kernel::ExtrusionRequest::ProfileRegion> regions;
        for (std::size_t index = 0; index < contours.size(); ++index) {
            if (depth(index) % 2 != 0) continue;
            zima::kernel::ExtrusionRequest::ProfileRegion region;
            region.region_id = "profile-region:" + contour_ids[index];
            region.outer_boundary_id = contour_ids[index];
            region.outer_profile = polygon_profile(contours[index]);
            region.outer_edge_source_ids.assign(
                contours[index].size(), contour_source_ids[index]);
            for (std::size_t child = 0; child < contours.size(); ++child) {
                if (parent[child] == static_cast<int>(index)) {
                    region.inner_profiles.push_back(polygon_profile(contours[child]));
                    region.inner_boundary_ids.push_back(contour_ids[child]);
                    region.inner_edge_source_ids.push_back(
                        std::vector<std::string>(contours[child].size(),
                                                 contour_source_ids[child]));
                    region.inner_vertex_source_ids.push_back({});
                }
            }
            regions.push_back(std::move(region));
        }
        if (regions.empty()) throw std::runtime_error("Text profile has no solid region");
        request.outer_profile = std::move(regions.front().outer_profile);
        request.inner_profiles = std::move(regions.front().inner_profiles);
        request.profile_region_id = std::move(regions.front().region_id);
        request.outer_boundary_id = std::move(regions.front().outer_boundary_id);
        request.inner_boundary_ids = std::move(regions.front().inner_boundary_ids);
        request.outer_edge_source_ids =
            std::move(regions.front().outer_edge_source_ids);
        request.inner_edge_source_ids =
            std::move(regions.front().inner_edge_source_ids);
        request.outer_vertex_source_ids =
            std::move(regions.front().outer_vertex_source_ids);
        request.inner_vertex_source_ids =
            std::move(regions.front().inner_vertex_source_ids);
        request.additional_profile_regions.assign(
            std::make_move_iterator(regions.begin() + 1),
            std::make_move_iterator(regions.end()));
        return finalize(std::move(request));
    }
    const auto closed_spline = std::find_if(profile_splines.begin(), profile_splines.end(),
        [](const auto* spline) { return spline->closed; });
    if (closed_spline != profile_splines.end()) {
        if (std::count_if(profile_splines.begin(), profile_splines.end(),
                [](const auto* spline) { return spline->closed; }) != 1 ||
            profile_splines.size() != 1 || !profile_segments.empty() ||
            !profile_arcs.empty() || !profile_ellipses.empty() ||
            !profile_elliptical_arcs.empty()) {
            throw std::runtime_error(
                "Closed B-spline profile must be one standalone outer loop");
        }
        zima::kernel::ExtrusionRequest::CurvedProfile outer;
        outer.curves.push_back(exact_spline(**closed_spline));
        request.outer_profile = std::move(outer);
        request.outer_boundary_id = (**closed_spline).id;
        request.outer_edge_source_ids = {(**closed_spline).id};
        request.profile_region_id = "profile-region:" + request.outer_boundary_id;
        for (const auto* circle : profile_circles) {
            const auto* center = sketch.find_point(circle->center_point_id);
            request.inner_profiles.push_back(
                zima::kernel::ExtrusionRequest::CircleProfile{
                    sketch.world_point(center->x, center->y), circle->radius});
            request.inner_boundary_ids.push_back(circle->id);
            request.inner_edge_source_ids.push_back({circle->id});
            request.inner_vertex_source_ids.push_back({});
        }
        return finalize(std::move(request));
    }
    if (!profile_ellipses.empty()) {
        if (profile_ellipses.size() != 1 || !profile_segments.empty() ||
            !profile_circles.empty() || !profile_arcs.empty() ||
            !profile_elliptical_arcs.empty()) {
            throw std::runtime_error(
                "Ellipse profile must currently be one standalone closed loop");
        }
        const auto* ellipse = profile_ellipses.front();
        const auto* center = sketch.find_point(ellipse->center_point_id);
        const auto* major = sketch.find_point(ellipse->major_point_id);
        const auto* minor = sketch.find_point(ellipse->minor_point_id);
        auto center_world = sketch.world_point(center->x, center->y);
        auto axis_point = ellipse->major_radius >= ellipse->minor_radius
            ? sketch.world_point(major->x, major->y)
            : sketch.world_point(minor->x, minor->y);
        const double major_radius = std::max(
            ellipse->major_radius, ellipse->minor_radius);
        const double minor_radius = std::min(
            ellipse->major_radius, ellipse->minor_radius);
        request.outer_profile = zima::kernel::ExtrusionRequest::EllipseProfile{
            center_world,
            {(axis_point.x - center_world.x) / major_radius,
             (axis_point.y - center_world.y) / major_radius,
             (axis_point.z - center_world.z) / major_radius},
            major_radius, minor_radius};
        request.outer_boundary_id = ellipse->id;
        request.outer_edge_source_ids = {ellipse->id};
        request.profile_region_id = "profile-region:" + ellipse->id;
        return finalize(std::move(request));
    }
    if (!profile_arcs.empty() || !profile_elliptical_arcs.empty() ||
        !profile_splines.empty()) {
        struct CurveRecord {
            std::string id;
            std::string start_point_id;
            std::string end_point_id;
            std::array<double, 2> start;
            std::array<double, 2> end;
            std::variant<zima::kernel::ExtrusionRequest::LineCurve,
                         zima::kernel::ExtrusionRequest::ArcCurve,
                         zima::kernel::ExtrusionRequest::EllipticalArcCurve,
                         zima::kernel::ExtrusionRequest::BSplineCurve> curve;
            std::size_t start_node{};
            std::size_t end_node{};
        };
        std::vector<CurveRecord> curves;
        for (const auto* segment : profile_segments) {
            const auto* first = sketch.find_point(segment->first_point_id);
            const auto* second = sketch.find_point(segment->second_point_id);
            curves.push_back({segment->id, segment->first_point_id,
                segment->second_point_id, {first->x, first->y},
                {second->x, second->y},
                zima::kernel::ExtrusionRequest::LineCurve{
                    sketch.world_point(first->x, first->y),
                    sketch.world_point(second->x, second->y)}});
        }
        for (const auto* arc : profile_arcs) {
            const auto* center = sketch.find_point(arc->center_point_id);
            const auto* start_point = sketch.find_point(arc->start_point_id);
            const auto* end_point = sketch.find_point(arc->end_point_id);
            const double middle_angle = 0.5 * (arc->start_angle + arc->end_angle);
            const std::array<double, 2> start{
                start_point->x, start_point->y};
            const std::array<double, 2> middle{
                center->x + arc->radius * std::cos(middle_angle),
                center->y + arc->radius * std::sin(middle_angle)};
            const std::array<double, 2> end{
                end_point->x, end_point->y};
            curves.push_back({arc->id, arc->start_point_id, arc->end_point_id,
                start, end,
                zima::kernel::ExtrusionRequest::ArcCurve{
                    sketch.world_point(start[0], start[1]),
                    sketch.world_point(middle[0], middle[1]),
                    sketch.world_point(end[0], end[1])}});
        }
        for (const auto* arc : profile_elliptical_arcs) {
            const auto* start = sketch.find_point(arc->start_point_id);
            const auto* end = sketch.find_point(arc->end_point_id);
            curves.push_back({arc->id, arc->start_point_id, arc->end_point_id,
                {start->x, start->y}, {end->x, end->y},
                exact_elliptical_arc(*arc)});
        }
        for (const auto* spline : profile_splines) {
            const auto* first = sketch.find_point(spline->control_point_ids.front());
            const auto* last = sketch.find_point(spline->control_point_ids.back());
            curves.push_back({spline->id, spline->control_point_ids.front(),
                spline->control_point_ids.back(), {first->x, first->y},
                {last->x, last->y}, exact_spline(*spline)});
        }
        std::vector<std::string> nodes;
        std::vector<std::vector<std::size_t>> incident_curves;
        const auto node_for = [&](const std::string& point_id) {
            for (std::size_t index = 0; index < nodes.size(); ++index) {
                if (nodes[index] == point_id) return index;
            }
            nodes.push_back(point_id);
            incident_curves.emplace_back();
            return nodes.size() - 1;
        };
        for (std::size_t index = 0; index < curves.size(); ++index) {
            curves[index].start_node = node_for(curves[index].start_point_id);
            curves[index].end_node = node_for(curves[index].end_point_id);
            incident_curves[curves[index].start_node].push_back(index);
            incident_curves[curves[index].end_node].push_back(index);
        }
        for (const auto& incident : incident_curves) {
            if (incident.size() != 2) {
                throw std::runtime_error(
                    "Curved Extrusion profile must be one closed non-branching loop");
            }
        }
        const auto first_curve = static_cast<std::size_t>(std::distance(
            curves.begin(), std::min_element(curves.begin(), curves.end(),
                [](const auto& left, const auto& right) {
                    return left.id < right.id;
                })));
        const std::size_t first_node = curves[first_curve].start_node;
        std::size_t current_node = first_node;
        std::optional<std::size_t> previous_curve;
        std::unordered_set<std::size_t> visited;
        zima::kernel::ExtrusionRequest::CurvedProfile ordered;
        std::vector<std::string> ordered_source_ids;
        std::vector<std::string> ordered_vertex_source_ids;
        do {
            auto candidates = incident_curves[current_node];
            std::sort(candidates.begin(), candidates.end(), [&](auto left, auto right) {
                return curves[left].id < curves[right].id;
            });
            const std::size_t next = previous_curve && candidates.front() == *previous_curve
                ? candidates.back() : candidates.front();
            if (!visited.insert(next).second) {
                throw std::runtime_error(
                    "Curved Extrusion profile contains multiple loops");
            }
            auto exact_curve = curves[next].curve;
            const bool forward = curves[next].start_node == current_node;
            if (!forward) {
                std::visit([](auto& curve) {
                    std::swap(curve.start, curve.end);
                    if constexpr (std::is_same_v<std::decay_t<decltype(curve)>,
                                      zima::kernel::ExtrusionRequest::EllipticalArcCurve>) {
                        const double old_start = curve.start_parameter;
                        const double old_end = curve.end_parameter;
                        curve.reversed = !curve.reversed;
                        curve.start_parameter = -old_end;
                        curve.end_parameter = -old_start;
                    }
                    if constexpr (std::is_same_v<std::decay_t<decltype(curve)>,
                                      zima::kernel::ExtrusionRequest::BSplineCurve>) {
                        std::reverse(curve.control_points.begin(),
                                     curve.control_points.end());
                    }
                },
                           exact_curve);
            }
            ordered.curves.push_back(std::move(exact_curve));
            ordered_source_ids.push_back(curves[next].id);
            ordered_vertex_source_ids.push_back(nodes[current_node]);
            current_node = forward
                ? curves[next].end_node : curves[next].start_node;
            previous_curve = next;
        } while (current_node != first_node);
        if (visited.size() != curves.size()) {
            throw std::runtime_error(
                "Curved Extrusion profile contains disconnected loops");
        }
        request.outer_profile = std::move(ordered);
        request.outer_edge_source_ids = std::move(ordered_source_ids);
        request.outer_vertex_source_ids =
            std::move(ordered_vertex_source_ids);
        std::vector<std::string> curve_ids;
        for (const auto& curve : curves) curve_ids.push_back(curve.id);
        request.outer_boundary_id = stable_profile_id("curve-loop", curve_ids);
        request.profile_region_id = "profile-region:" + request.outer_boundary_id;
        for (const auto* circle : profile_circles) {
            const auto* center = sketch.find_point(circle->center_point_id);
            request.inner_profiles.push_back(
                zima::kernel::ExtrusionRequest::CircleProfile{
                    sketch.world_point(center->x, center->y), circle->radius});
            request.inner_boundary_ids.push_back(circle->id);
            request.inner_edge_source_ids.push_back({circle->id});
            request.inner_vertex_source_ids.push_back({});
        }
        return finalize(std::move(request));
    }
    const auto circle_profile = [&](const auto* circle) {
        const auto* center = sketch.find_point(circle->center_point_id);
        return zima::kernel::ExtrusionRequest::CircleProfile{
            sketch.world_point(center->x, center->y), circle->radius};
    };
    if (profile_segments.empty()) {
        if (profile_circles.empty()) {
            throw std::runtime_error("Extrusion profile has no closed geometry");
        }
        std::vector<int> parent(profile_circles.size(), -1);
        for (std::size_t first = 0; first < profile_circles.size(); ++first) {
            const auto* first_center =
                sketch.find_point(profile_circles[first]->center_point_id);
            double parent_radius = std::numeric_limits<double>::infinity();
            for (std::size_t second = 0; second < profile_circles.size(); ++second) {
                if (first == second) continue;
                const auto* second_center =
                    sketch.find_point(profile_circles[second]->center_point_id);
                const double distance = std::hypot(
                    first_center->x - second_center->x,
                    first_center->y - second_center->y);
                const double first_radius = profile_circles[first]->radius;
                const double second_radius = profile_circles[second]->radius;
                if (distance <= first_radius + second_radius + 1.0e-9 &&
                    distance + std::min(first_radius, second_radius) >=
                        std::max(first_radius, second_radius) - 1.0e-9) {
                    throw std::runtime_error(
                        "Circular profile boundaries overlap or touch");
                }
                if (distance + first_radius < second_radius - 1.0e-9 &&
                    second_radius < parent_radius) {
                    parent[first] = static_cast<int>(second);
                    parent_radius = second_radius;
                }
            }
        }
        const auto depth = [&](std::size_t index) {
            int result{};
            for (int current = parent[index]; current >= 0; current = parent[current]) {
                if (++result > static_cast<int>(profile_circles.size())) {
                    throw std::runtime_error("Circular profile containment is cyclic");
                }
            }
            return result;
        };
        std::vector<ProfileRegion> regions;
        for (std::size_t index = 0; index < profile_circles.size(); ++index) {
            if (depth(index) % 2 != 0) continue;
            ProfileRegion region;
            region.region_id = "profile-region:" + profile_circles[index]->id;
            region.outer_boundary_id = profile_circles[index]->id;
            region.outer_profile = circle_profile(profile_circles[index]);
            region.outer_edge_source_ids = {profile_circles[index]->id};
            for (std::size_t child = 0; child < profile_circles.size(); ++child) {
                if (parent[child] == static_cast<int>(index)) {
                    region.inner_boundary_ids.push_back(profile_circles[child]->id);
                    region.inner_profiles.push_back(circle_profile(profile_circles[child]));
                    region.inner_edge_source_ids.push_back(
                        {profile_circles[child]->id});
                    region.inner_vertex_source_ids.push_back({});
                }
            }
            regions.push_back(std::move(region));
        }
        assign_regions(request, std::move(regions));
        return finalize(std::move(request));
    }
    if (profile_circles.empty()) {
        std::unordered_map<std::string,
            std::vector<const zima::sketcher::SketchSegment*>> incident;
        for (const auto* segment : profile_segments) {
            incident[segment->first_point_id].push_back(segment);
            incident[segment->second_point_id].push_back(segment);
        }
        for (const auto& [point_id, segments] : incident) {
            if (segments.size() != 2) {
                throw std::runtime_error(
                    "Segment profile must contain closed non-branching loops");
            }
        }
        struct PolygonLoop {
            ProfilePolygon points;
            std::vector<std::string> segment_ids;
            std::vector<std::string> point_ids;
            int parent{-1};
            double area{};
        };
        std::vector<PolygonLoop> loops;
        std::unordered_set<std::string> visited;
        std::vector<const zima::sketcher::SketchSegment*> starts = profile_segments;
        std::sort(starts.begin(), starts.end(), [](const auto* left, const auto* right) {
            return left->id < right->id;
        });
        for (const auto* start_segment : starts) {
            if (visited.contains(start_segment->id)) continue;
            PolygonLoop loop;
            std::string start = start_segment->first_point_id;
            std::string current = start;
            const zima::sketcher::SketchSegment* previous{};
            do {
                const auto* point = sketch.find_point(current);
                loop.points.push_back({point->x, point->y});
                loop.point_ids.push_back(current);
                auto candidates = incident.at(current);
                std::sort(candidates.begin(), candidates.end(),
                    [](const auto* left, const auto* right) { return left->id < right->id; });
                const auto* next = candidates.front() == previous
                    ? candidates.back() : candidates.front();
                if (!visited.insert(next->id).second) {
                    throw std::runtime_error("Segment profile loop repeats an edge");
                }
                loop.segment_ids.push_back(next->id);
                current = next->first_point_id == current
                    ? next->second_point_id : next->first_point_id;
                previous = next;
            } while (current != start);
            loops.push_back(std::move(loop));
        }
        const auto cross = [](const auto& a, const auto& b, const auto& c) {
            return (b[0] - a[0]) * (c[1] - a[1]) -
                (b[1] - a[1]) * (c[0] - a[0]);
        };
        const auto on_segment = [&](const auto& point, const auto& a, const auto& b) {
            return std::abs(cross(a, b, point)) <= 1.0e-9 &&
                point[0] >= std::min(a[0], b[0]) - 1.0e-9 &&
                point[0] <= std::max(a[0], b[0]) + 1.0e-9 &&
                point[1] >= std::min(a[1], b[1]) - 1.0e-9 &&
                point[1] <= std::max(a[1], b[1]) + 1.0e-9;
        };
        const auto intersects = [&](const auto& a, const auto& b,
                                     const auto& c, const auto& d) {
            const double ab_c = cross(a, b, c), ab_d = cross(a, b, d);
            const double cd_a = cross(c, d, a), cd_b = cross(c, d, b);
            return ((ab_c > 1.0e-9 && ab_d < -1.0e-9) ||
                    (ab_c < -1.0e-9 && ab_d > 1.0e-9)) &&
                       ((cd_a > 1.0e-9 && cd_b < -1.0e-9) ||
                        (cd_a < -1.0e-9 && cd_b > 1.0e-9)) ||
                on_segment(c, a, b) || on_segment(d, a, b) ||
                on_segment(a, c, d) || on_segment(b, c, d);
        };
        const auto contains = [](const ProfilePolygon& polygon,
                                 const std::array<double, 2>& point) {
            bool inside = false;
            for (std::size_t i = 0, j = polygon.size() - 1;
                 i < polygon.size(); j = i++) {
                const auto& a = polygon[i]; const auto& b = polygon[j];
                if ((a[1] > point[1]) != (b[1] > point[1]) &&
                    point[0] < (b[0] - a[0]) * (point[1] - a[1]) /
                            (b[1] - a[1]) + a[0]) inside = !inside;
            }
            return inside;
        };
        for (std::size_t first = 0; first < loops.size(); ++first) {
            for (std::size_t edge = 0; edge < loops[first].points.size(); ++edge) {
                const auto& a = loops[first].points[edge];
                const auto& b = loops[first].points[(edge + 1) % loops[first].points.size()];
                loops[first].area += a[0] * b[1] - b[0] * a[1];
                for (std::size_t other = edge + 1; other < loops[first].points.size(); ++other) {
                    if (other == edge + 1 || (edge == 0 && other + 1 == loops[first].points.size())) continue;
                    if (intersects(a, b, loops[first].points[other],
                            loops[first].points[(other + 1) % loops[first].points.size()])) {
                        throw std::runtime_error("Segment profile loop self-intersects");
                    }
                }
            }
            loops[first].area = std::abs(loops[first].area) * 0.5;
            if (loops[first].area <= 1.0e-12) {
                throw std::runtime_error("Segment profile loop has zero area");
            }
            for (std::size_t second = first + 1; second < loops.size(); ++second) {
                for (std::size_t a = 0; a < loops[first].points.size(); ++a) {
                    for (std::size_t b = 0; b < loops[second].points.size(); ++b) {
                        if (intersects(loops[first].points[a],
                                loops[first].points[(a + 1) % loops[first].points.size()],
                                loops[second].points[b],
                                loops[second].points[(b + 1) % loops[second].points.size()])) {
                            throw std::runtime_error("Segment profile loops overlap or touch");
                        }
                    }
                }
            }
        }
        for (std::size_t index = 0; index < loops.size(); ++index) {
            double parent_area = std::numeric_limits<double>::infinity();
            for (std::size_t candidate = 0; candidate < loops.size(); ++candidate) {
                if (candidate != index && loops[candidate].area > loops[index].area &&
                    contains(loops[candidate].points, loops[index].points.front()) &&
                    loops[candidate].area < parent_area) {
                    loops[index].parent = static_cast<int>(candidate);
                    parent_area = loops[candidate].area;
                }
            }
        }
        const auto depth = [&](std::size_t index) {
            int result{};
            for (int current = loops[index].parent; current >= 0; current = loops[current].parent) {
                if (++result > static_cast<int>(loops.size())) {
                    throw std::runtime_error("Segment profile containment is cyclic");
                }
            }
            return result;
        };
        std::vector<ProfileRegion> regions;
        for (std::size_t index = 0; index < loops.size(); ++index) {
            if (depth(index) % 2 != 0) continue;
            ProfileRegion region;
            region.outer_boundary_id = stable_profile_id(
                "segment-loop", loops[index].segment_ids);
            region.region_id = "profile-region:" + region.outer_boundary_id;
            zima::kernel::ExtrusionRequest::PolygonProfile outer;
            for (const auto& point : loops[index].points) {
                outer.vertices.push_back(sketch.world_point(point[0], point[1]));
            }
            region.outer_profile = std::move(outer);
            region.outer_edge_source_ids = loops[index].segment_ids;
            region.outer_vertex_source_ids = loops[index].point_ids;
            for (std::size_t child = 0; child < loops.size(); ++child) {
                if (loops[child].parent != static_cast<int>(index)) continue;
                region.inner_boundary_ids.push_back(stable_profile_id(
                    "segment-loop", loops[child].segment_ids));
                zima::kernel::ExtrusionRequest::PolygonProfile inner;
                for (const auto& point : loops[child].points) {
                    inner.vertices.push_back(sketch.world_point(point[0], point[1]));
                }
                region.inner_profiles.push_back(std::move(inner));
                region.inner_edge_source_ids.push_back(loops[child].segment_ids);
                region.inner_vertex_source_ids.push_back(loops[child].point_ids);
            }
            regions.push_back(std::move(region));
        }
        assign_regions(request, std::move(regions));
        return finalize(std::move(request));
    }
    if (profile_segments.size() < 3) {
        throw std::runtime_error("Extrusion profile requires at least three segments");
    }
    std::unordered_map<std::string, std::vector<const zima::sketcher::SketchSegment*>>
        incident;
    for (const auto* segment : profile_segments) {
        incident[segment->first_point_id].push_back(segment);
        incident[segment->second_point_id].push_back(segment);
    }
    for (const auto& [point_id, segments] : incident) {
        if (segments.size() != 2) {
            throw std::runtime_error(
                "Extrusion profile must be one closed non-branching loop");
        }
    }
    std::string current = std::min_element(
        incident.begin(), incident.end(), [](const auto& left, const auto& right) {
            return left.first < right.first;
        })->first;
    const std::string start = current;
    const zima::sketcher::SketchSegment* previous{};
    std::unordered_set<std::string> visited_segments;
    std::vector<std::string> ordered_segment_ids;
    std::vector<std::string> ordered_point_ids;
    std::vector<std::array<double, 2>> polygon;
    do {
        const auto* point = sketch.find_point(current);
        polygon.push_back({point->x, point->y});
        ordered_point_ids.push_back(current);
        std::get<zima::kernel::ExtrusionRequest::PolygonProfile>(
            request.outer_profile)
            .vertices.push_back(sketch.world_point(point->x, point->y));
        auto candidates = incident.at(current);
        std::sort(candidates.begin(), candidates.end(), [](const auto* left, const auto* right) {
            return left->id < right->id;
        });
        const auto* next = candidates.front() == previous
            ? candidates.back() : candidates.front();
        if (!visited_segments.insert(next->id).second) {
            throw std::runtime_error("Extrusion profile contains multiple loops");
        }
        ordered_segment_ids.push_back(next->id);
        current = next->first_point_id == current
            ? next->second_point_id : next->first_point_id;
        previous = next;
    } while (current != start);
    if (visited_segments.size() != profile_segments.size()) {
        throw std::runtime_error("Extrusion profile contains disconnected loops");
    }
    const auto orientation = [](const auto& first, const auto& second,
                                const auto& third) {
        return (second[0] - first[0]) * (third[1] - first[1]) -
            (second[1] - first[1]) * (third[0] - first[0]);
    };
    const auto lies_on_segment = [](const auto& point, const auto& first,
                                    const auto& second) {
        return point[0] >= std::min(first[0], second[0]) - 1.0e-9 &&
            point[0] <= std::max(first[0], second[0]) + 1.0e-9 &&
            point[1] >= std::min(first[1], second[1]) - 1.0e-9 &&
            point[1] <= std::max(first[1], second[1]) + 1.0e-9;
    };
    for (std::size_t first = 0; first < polygon.size(); ++first) {
        const std::size_t first_next = (first + 1) % polygon.size();
        for (std::size_t second = first + 1; second < polygon.size(); ++second) {
            const std::size_t second_next = (second + 1) % polygon.size();
            if (first == second_next || first_next == second) continue;
            const double first_a = orientation(
                polygon[first], polygon[first_next], polygon[second]);
            const double first_b = orientation(
                polygon[first], polygon[first_next], polygon[second_next]);
            const double second_a = orientation(
                polygon[second], polygon[second_next], polygon[first]);
            const double second_b = orientation(
                polygon[second], polygon[second_next], polygon[first_next]);
            const bool proper_crossing =
                ((first_a > 1.0e-9 && first_b < -1.0e-9) ||
                 (first_a < -1.0e-9 && first_b > 1.0e-9)) &&
                ((second_a > 1.0e-9 && second_b < -1.0e-9) ||
                 (second_a < -1.0e-9 && second_b > 1.0e-9));
            const bool touching =
                (std::abs(first_a) <= 1.0e-9 && lies_on_segment(
                    polygon[second], polygon[first], polygon[first_next])) ||
                (std::abs(first_b) <= 1.0e-9 && lies_on_segment(
                    polygon[second_next], polygon[first], polygon[first_next])) ||
                (std::abs(second_a) <= 1.0e-9 && lies_on_segment(
                    polygon[first], polygon[second], polygon[second_next])) ||
                (std::abs(second_b) <= 1.0e-9 && lies_on_segment(
                    polygon[first_next], polygon[second], polygon[second_next]));
            if (proper_crossing || touching) {
                throw std::runtime_error(
                    "Extrusion outer profile must not self-intersect");
            }
        }
    }
    double signed_area{};
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const auto& first = polygon[index];
        const auto& second = polygon[(index + 1) % polygon.size()];
        signed_area += first[0] * second[1] - second[0] * first[1];
    }
    if (std::abs(signed_area) <= 1.0e-12) {
        throw std::runtime_error("Extrusion outer profile has zero area");
    }
    if (signed_area < 0.0) {
        std::reverse(polygon.begin(), polygon.end());
        auto& vertices =
            std::get<zima::kernel::ExtrusionRequest::PolygonProfile>(
                request.outer_profile).vertices;
        std::reverse(vertices.begin(), vertices.end());
        std::reverse(ordered_segment_ids.begin(), ordered_segment_ids.end());
        std::rotate(ordered_segment_ids.begin(),
                    ordered_segment_ids.begin() + 1,
                    ordered_segment_ids.end());
        std::reverse(ordered_point_ids.begin(), ordered_point_ids.end());
    }
    std::vector<std::string> segment_ids;
    for (const auto* segment : profile_segments) segment_ids.push_back(segment->id);
    request.outer_boundary_id = stable_profile_id("segment-loop", segment_ids);
    request.outer_edge_source_ids = std::move(ordered_segment_ids);
    request.outer_vertex_source_ids = std::move(ordered_point_ids);
    request.profile_region_id = "profile-region:" + request.outer_boundary_id;
    const auto circle_inside_polygon = [&](const auto* circle) {
        const auto* center = sketch.find_point(circle->center_point_id);
        bool inside = false;
        double minimum_edge_distance = std::numeric_limits<double>::infinity();
        for (std::size_t index = 0; index < polygon.size(); ++index) {
            const auto& first = polygon[index];
            const auto& second = polygon[(index + 1) % polygon.size()];
            if ((first[1] > center->y) != (second[1] > center->y) &&
                center->x < (second[0] - first[0]) * (center->y - first[1]) /
                        (second[1] - first[1]) + first[0]) {
                inside = !inside;
            }
            const double dx = second[0] - first[0];
            const double dy = second[1] - first[1];
            const double length_squared = dx * dx + dy * dy;
            if (length_squared <= 1.0e-18) {
                throw std::runtime_error(
                    "Extrusion profile contains a zero-length segment");
            }
            const double parameter = std::clamp(
                ((center->x - first[0]) * dx + (center->y - first[1]) * dy) /
                    length_squared,
                0.0, 1.0);
            minimum_edge_distance = std::min(minimum_edge_distance, std::hypot(
                center->x - (first[0] + parameter * dx),
                center->y - (first[1] + parameter * dy)));
        }
        return inside && minimum_edge_distance > circle->radius + 1.0e-9;
    };
    for (const auto* circle : profile_circles) {
        if (!circle_inside_polygon(circle)) {
            throw std::runtime_error(
                "Circular extrusion hole must lie strictly inside the outer loop");
        }
        request.inner_profiles.push_back(circle_profile(circle));
        request.inner_boundary_ids.push_back(circle->id);
        request.inner_edge_source_ids.push_back({circle->id});
        request.inner_vertex_source_ids.push_back({});
    }
    for (std::size_t first = 0; first < profile_circles.size(); ++first) {
        const auto* first_center =
            sketch.find_point(profile_circles[first]->center_point_id);
        for (std::size_t second = first + 1; second < profile_circles.size(); ++second) {
            const auto* second_center =
                sketch.find_point(profile_circles[second]->center_point_id);
            if (std::hypot(first_center->x - second_center->x,
                           first_center->y - second_center->y) <=
                profile_circles[first]->radius +
                    profile_circles[second]->radius + 1.0e-9) {
                throw std::runtime_error("Extrusion holes must not overlap");
            }
        }
    }
    return finalize(std::move(request));
}

zima::kernel::RevolutionRequest revolution_request(
    const zima::sketcher::Sketch& sketch,
    RevolutionAxis axis, double angle_degrees) {
    if (!std::isfinite(angle_degrees) || angle_degrees <= 0.0 ||
        angle_degrees > 360.0) {
        throw std::runtime_error("Revolution angle must be in (0, 360] degrees");
    }
    const auto source = extrusion_request(
        sketch, 1.0, ExtrusionDirection::Forward);
    zima::kernel::RevolutionRequest request;
    request.outer_profile = source.outer_profile;
    request.inner_profiles = source.inner_profiles;
    request.additional_profile_regions = source.additional_profile_regions;
    request.profile_region_id = source.profile_region_id;
    request.outer_boundary_id = source.outer_boundary_id;
    request.inner_boundary_ids = source.inner_boundary_ids;
    request.outer_edge_source_ids = source.outer_edge_source_ids;
    request.inner_edge_source_ids = source.inner_edge_source_ids;
    request.outer_vertex_source_ids = source.outer_vertex_source_ids;
    request.inner_vertex_source_ids = source.inner_vertex_source_ids;
    request.profile_normal = source.direction;
    request.axis_point = sketch.world_point(0.0, 0.0);
    if (axis == RevolutionAxis::SketchX) {
        request.axis_direction = sketch.plane == zima::sketcher::SketchPlane::YZ
            ? zima::kernel::Vec3{0.0, 1.0, 0.0}
            : zima::kernel::Vec3{1.0, 0.0, 0.0};
    } else if (axis == RevolutionAxis::SketchY) {
        request.axis_direction = sketch.plane == zima::sketcher::SketchPlane::XY
            ? zima::kernel::Vec3{0.0, 1.0, 0.0}
            : zima::kernel::Vec3{0.0, 0.0, 1.0};
    } else {
        throw std::runtime_error("Invalid Revolution axis");
    }
    request.angle_degrees = angle_degrees;
    return request;
}

// --- Universal container placement (origin + FRONT/TOP orientation) -------
//
// Ports the reference application's `_plane_reference_rotation` /
// `_rotation_with_local_offset` frame math so every container kind (not just
// Plane construction objects) can be positioned by a point/axis/plane
// reference and oriented by a FRONT/TOP reference pair, with an additional
// manual RX/RY/RZ correction composed on top. The rotation convention
// (columns = local X/Y/Z axes, R = Rz * Ry * Rx) matches
// zima::kernel primitive_transform() exactly, so the resolved degrees can be
// fed to the OCCT adapter unchanged.

zima::kernel::Vec3 placement_vec_normalized(zima::kernel::Vec3 value) {
    const double length = std::sqrt(
        value.x * value.x + value.y * value.y + value.z * value.z);
    if (length <= 1.0e-12) return {0.0, 0.0, 0.0};
    return {value.x / length, value.y / length, value.z / length};
}

zima::kernel::Vec3 placement_vec_cross(
    const zima::kernel::Vec3& a, const zima::kernel::Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x};
}

double placement_vec_dot(
    const zima::kernel::Vec3& a, const zima::kernel::Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

bool placement_vec_is_zero(const zima::kernel::Vec3& value) {
    return std::abs(value.x) <= 1.0e-12 && std::abs(value.y) <= 1.0e-12 &&
        std::abs(value.z) <= 1.0e-12;
}

zima::kernel::Vec3 placement_vec_project_perpendicular(
    const zima::kernel::Vec3& value, const zima::kernel::Vec3& fixed_axis) {
    const double projection = placement_vec_dot(value, fixed_axis);
    return placement_vec_normalized({value.x - projection * fixed_axis.x,
        value.y - projection * fixed_axis.y,
        value.z - projection * fixed_axis.z});
}

using PlacementRotationMatrix = std::array<std::array<double, 3>, 3>;

PlacementRotationMatrix placement_rotation_matrix_from_columns(
    const zima::kernel::Vec3& x_axis, const zima::kernel::Vec3& y_axis,
    const zima::kernel::Vec3& z_axis) {
    return {{{x_axis.x, y_axis.x, z_axis.x},
             {x_axis.y, y_axis.y, z_axis.y},
             {x_axis.z, y_axis.z, z_axis.z}}};
}

PlacementRotationMatrix placement_rotation_matrix_from_euler_degrees(
    const zima::kernel::Vec3& degrees) {
    constexpr double radians_per_degree = std::numbers::pi / 180.0;
    const double cx = std::cos(degrees.x * radians_per_degree);
    const double sx = std::sin(degrees.x * radians_per_degree);
    const double cy = std::cos(degrees.y * radians_per_degree);
    const double sy = std::sin(degrees.y * radians_per_degree);
    const double cz = std::cos(degrees.z * radians_per_degree);
    const double sz = std::sin(degrees.z * radians_per_degree);
    return {{{cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx},
             {sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx},
             {-sy, cy * sx, cy * cx}}};
}

PlacementRotationMatrix placement_rotation_matrix_multiply(
    const PlacementRotationMatrix& a, const PlacementRotationMatrix& b) {
    PlacementRotationMatrix result{};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            double sum = 0.0;
            for (int inner = 0; inner < 3; ++inner) {
                sum += a[row][inner] * b[inner][column];
            }
            result[row][column] = sum;
        }
    }
    return result;
}

zima::kernel::Vec3 placement_euler_degrees_from_rotation_matrix(
    const PlacementRotationMatrix& matrix) {
    constexpr double degrees_per_radian = 180.0 / std::numbers::pi;
    const double ry = std::asin(std::clamp(-matrix[2][0], -1.0, 1.0));
    double rx;
    double rz;
    if (std::abs(std::cos(ry)) > 1.0e-10) {
        rx = std::atan2(matrix[2][1], matrix[2][2]);
        rz = std::atan2(matrix[1][0], matrix[0][0]);
    } else {
        // Gimbal lock at RY = +/-90 deg: mirror the reference
        // implementation's tie-breaking so a manual RX correction still
        // resolves consistently at this frame.
        rx = std::atan2(matrix[0][1] * (ry >= 0.0 ? 1.0 : -1.0), matrix[1][1]);
        rz = 0.0;
    }
    return {rx * degrees_per_radian, ry * degrees_per_radian,
        rz * degrees_per_radian};
}

// Builds the FRONT (local Y) / TOP (local Z) orthonormal frame from one or
// two supplied directions via Gram-Schmidt, returning the base rotation in
// degrees. std::nullopt means neither direction was supplied.
std::optional<zima::kernel::Vec3> placement_frame_base_rotation_degrees(
    const std::optional<zima::kernel::Vec3>& front_direction,
    const std::optional<zima::kernel::Vec3>& top_direction) {
    if (!front_direction && !top_direction) return std::nullopt;
    zima::kernel::Vec3 x_axis{};
    zima::kernel::Vec3 y_axis{};
    zima::kernel::Vec3 z_axis{};
    if (front_direction && top_direction) {
        y_axis = placement_vec_normalized(*front_direction);
        z_axis = placement_vec_project_perpendicular(*top_direction, y_axis);
        if (placement_vec_is_zero(z_axis)) return std::nullopt;
        x_axis = placement_vec_normalized(placement_vec_cross(y_axis, z_axis));
        z_axis = placement_vec_normalized(placement_vec_cross(x_axis, y_axis));
    } else if (front_direction) {
        y_axis = placement_vec_normalized(*front_direction);
        z_axis = placement_vec_project_perpendicular({0.0, 0.0, 1.0}, y_axis);
        if (placement_vec_is_zero(z_axis)) {
            z_axis = placement_vec_project_perpendicular({1.0, 0.0, 0.0}, y_axis);
        }
        if (placement_vec_is_zero(z_axis)) return std::nullopt;
        x_axis = placement_vec_normalized(placement_vec_cross(y_axis, z_axis));
        z_axis = placement_vec_normalized(placement_vec_cross(x_axis, y_axis));
    } else {
        z_axis = placement_vec_normalized(*top_direction);
        x_axis = placement_vec_project_perpendicular({1.0, 0.0, 0.0}, z_axis);
        if (placement_vec_is_zero(x_axis)) {
            x_axis = placement_vec_project_perpendicular({0.0, 1.0, 0.0}, z_axis);
        }
        if (placement_vec_is_zero(x_axis)) return std::nullopt;
        y_axis = placement_vec_normalized(placement_vec_cross(z_axis, x_axis));
        x_axis = placement_vec_normalized(placement_vec_cross(y_axis, z_axis));
    }
    return placement_euler_degrees_from_rotation_matrix(
        placement_rotation_matrix_from_columns(x_axis, y_axis, z_axis));
}

// Composes the FRONT/TOP base frame (when present) with the manual
// rotation_offset_* correction, matching `_rotation_with_local_offset`.
zima::kernel::Vec3 placement_compose_orientation_degrees(
    const std::optional<zima::kernel::Vec3>& front_direction,
    const std::optional<zima::kernel::Vec3>& top_direction,
    const zima::kernel::Vec3& manual_offset_degrees) {
    const auto base = placement_frame_base_rotation_degrees(
        front_direction, top_direction);
    if (!base) return manual_offset_degrees;
    const auto combined = placement_rotation_matrix_multiply(
        placement_rotation_matrix_from_euler_degrees(*base),
        placement_rotation_matrix_from_euler_degrees(manual_offset_degrees));
    return placement_euler_degrees_from_rotation_matrix(combined);
}

zima::kernel::Vec3 placement_transform_point(
    const PlacementRotationMatrix& rotation,
    const zima::kernel::Vec3& translation, const zima::kernel::Vec3& point) {
    return {rotation[0][0] * point.x + rotation[0][1] * point.y +
                rotation[0][2] * point.z + translation.x,
            rotation[1][0] * point.x + rotation[1][1] * point.y +
                rotation[1][2] * point.z + translation.y,
            rotation[2][0] * point.x + rotation[2][1] * point.y +
                rotation[2][2] * point.z + translation.z};
}

zima::kernel::Vec3 placement_transform_direction(
    const PlacementRotationMatrix& rotation, const zima::kernel::Vec3& direction) {
    return {rotation[0][0] * direction.x + rotation[0][1] * direction.y +
                rotation[0][2] * direction.z,
            rotation[1][0] * direction.x + rotation[1][1] * direction.y +
                rotation[1][2] * direction.z,
            rotation[2][0] * direction.x + rotation[2][1] * direction.y +
                rotation[2][2] * direction.z};
}

// Applies a container's resolved placement (rotation about the world origin
// followed by translation) to a sketch-built profile loop, in place. This is
// how a Sketch profile -- always built in the source Sketch's own plane --
// is carried along by the owning Extrusion/Revolution container's origin
// and FRONT/TOP orientation, without the Sketch itself moving.
void placement_transform_profile_loop(
    zima::kernel::ExtrusionRequest::ProfileLoop& profile_variant,
    const PlacementRotationMatrix& rotation, const zima::kernel::Vec3& translation) {
    std::visit([&](auto& profile) {
        using Profile = std::decay_t<decltype(profile)>;
        if constexpr (std::is_same_v<Profile,
                          zima::kernel::ExtrusionRequest::PolygonProfile>) {
            for (auto& point : profile.vertices) {
                point = placement_transform_point(rotation, translation, point);
            }
        } else if constexpr (std::is_same_v<Profile,
                                 zima::kernel::ExtrusionRequest::CircleProfile>) {
            profile.center = placement_transform_point(
                rotation, translation, profile.center);
        } else if constexpr (std::is_same_v<Profile,
                                 zima::kernel::ExtrusionRequest::EllipseProfile>) {
            profile.center = placement_transform_point(
                rotation, translation, profile.center);
            profile.major_axis_direction = placement_transform_direction(
                rotation, profile.major_axis_direction);
        } else {
            for (auto& curve : profile.curves) {
                std::visit([&](auto& exact_curve) {
                    using Curve = std::decay_t<decltype(exact_curve)>;
                    exact_curve.start = placement_transform_point(
                        rotation, translation, exact_curve.start);
                    exact_curve.end = placement_transform_point(
                        rotation, translation, exact_curve.end);
                    if constexpr (std::is_same_v<Curve,
                                      zima::kernel::ExtrusionRequest::ArcCurve>) {
                        exact_curve.middle = placement_transform_point(
                            rotation, translation, exact_curve.middle);
                    }
                    if constexpr (std::is_same_v<Curve,
                                      zima::kernel::ExtrusionRequest::EllipticalArcCurve>) {
                        exact_curve.center = placement_transform_point(
                            rotation, translation, exact_curve.center);
                        exact_curve.major_axis_direction =
                            placement_transform_direction(
                                rotation, exact_curve.major_axis_direction);
                    }
                    if constexpr (std::is_same_v<Curve,
                                      zima::kernel::ExtrusionRequest::BSplineCurve>) {
                        for (auto& point : exact_curve.control_points) {
                            point = placement_transform_point(
                                rotation, translation, point);
                        }
                    }
                }, curve);
            }
        }
    }, profile_variant);
}

bool placement_is_identity(const Placement& placement) {
    return placement.x == 0.0 && placement.y == 0.0 && placement.z == 0.0 &&
        placement.rotation_x == 0.0 && placement.rotation_y == 0.0 &&
        placement.rotation_z == 0.0;
}

// Composes the owning container's resolved placement with an
// Extrusion built (by extrusion_request()) directly in its source Sketch's
// own plane. The target plane/surface fields are left untouched: they
// reference already-absolute geometry captured from prior history or an
// external datum plane, independent of this container's own placement.
void apply_container_placement(
    zima::kernel::ExtrusionRequest& request, const Placement& placement) {
    if (placement_is_identity(placement)) return;
    const auto rotation = placement_rotation_matrix_from_euler_degrees(
        {placement.rotation_x, placement.rotation_y, placement.rotation_z});
    const zima::kernel::Vec3 translation{placement.x, placement.y, placement.z};
    placement_transform_profile_loop(request.outer_profile, rotation, translation);
    for (auto& profile : request.inner_profiles) {
        placement_transform_profile_loop(profile, rotation, translation);
    }
    for (auto& region : request.additional_profile_regions) {
        placement_transform_profile_loop(region.outer_profile, rotation, translation);
        for (auto& profile : region.inner_profiles) {
            placement_transform_profile_loop(profile, rotation, translation);
        }
    }
    request.direction = placement_transform_direction(rotation, request.direction);
}

// Same composition as above, for a Revolution built directly in its source
// Sketch's own plane and about its own local axis.
void apply_container_placement(
    zima::kernel::RevolutionRequest& request, const Placement& placement) {
    if (placement_is_identity(placement)) return;
    const auto rotation = placement_rotation_matrix_from_euler_degrees(
        {placement.rotation_x, placement.rotation_y, placement.rotation_z});
    const zima::kernel::Vec3 translation{placement.x, placement.y, placement.z};
    placement_transform_profile_loop(request.outer_profile, rotation, translation);
    for (auto& profile : request.inner_profiles) {
        placement_transform_profile_loop(profile, rotation, translation);
    }
    for (auto& region : request.additional_profile_regions) {
        placement_transform_profile_loop(region.outer_profile, rotation, translation);
        for (auto& profile : region.inner_profiles) {
            placement_transform_profile_loop(profile, rotation, translation);
        }
    }
    request.profile_normal = placement_transform_direction(
        rotation, request.profile_normal);
    request.axis_point = placement_transform_point(
        rotation, translation, request.axis_point);
    request.axis_direction = placement_transform_direction(
        rotation, request.axis_direction);
}

template <typename Reference>
bool placement_reference_matches(
    const Reference& reference, const ConstructionReference& expected) {
    return reference.instance_path == expected.instance_path &&
        reference.owner_id == expected.owner_id &&
        reference.semantic_key == expected.semantic_key;
}


std::optional<zima::kernel::Vec3> placement_reference_point(
    const ConstructionReference& reference,
    const zima::kernel::ViewerReferenceGeometry& geometry) {
    const auto found = std::find_if(geometry.points.begin(), geometry.points.end(),
        [&](const auto& candidate) {
            return placement_reference_matches(candidate.reference, reference);
        });
    if (found != geometry.points.end()) return found->position;
    return std::nullopt;
}

struct PlacementReferenceAxis {
    zima::kernel::Vec3 point;
    zima::kernel::Vec3 direction;
};

std::optional<PlacementReferenceAxis> placement_reference_axis(
    const ConstructionReference& reference,
    const zima::kernel::ViewerReferenceGeometry& geometry) {
    const auto found = std::find_if(geometry.axes.begin(), geometry.axes.end(),
        [&](const auto& candidate) {
            return placement_reference_matches(candidate.reference, reference);
        });
    if (found != geometry.axes.end()) return PlacementReferenceAxis{found->point, found->direction};
    const auto edge = std::find_if(geometry.edges.begin(), geometry.edges.end(),
        [&](const auto& candidate) {
            return placement_reference_matches(candidate.reference, reference);
        });
    if (edge == geometry.edges.end() || edge->points.size() < 2) return std::nullopt;
    const auto& first = edge->points.front();
    const auto& last = edge->points.back();
    zima::kernel::Vec3 direction{last.x - first.x, last.y - first.y, last.z - first.z};
    if (placement_vec_is_zero(direction)) return std::nullopt;
    direction = placement_vec_normalized(direction);
    for (const auto& candidate : edge->points) {
        const zima::kernel::Vec3 delta{candidate.x - first.x,
            candidate.y - first.y, candidate.z - first.z};
        const auto deviation = placement_vec_cross(delta, direction);
        if (std::sqrt(deviation.x * deviation.x + deviation.y * deviation.y +
                deviation.z * deviation.z) > 1.0e-7) {
            return std::nullopt;
        }
    }
    return PlacementReferenceAxis{first, direction};
}

struct PlacementReferencePlane {
    zima::kernel::Vec3 point;
    zima::kernel::Vec3 normal;
};

std::optional<PlacementReferencePlane> placement_reference_plane(
    const ConstructionReference& reference,
    const zima::kernel::ViewerReferenceGeometry& geometry) {
    for (std::size_t index = 0; index < geometry.triangle_references.size(); ++index) {
        if (!placement_reference_matches(geometry.triangle_references[index], reference)) continue;
        const auto& a = geometry.vertices[geometry.triangles[index * 3]];
        const auto& b = geometry.vertices[geometry.triangles[index * 3 + 1]];
        const auto& c = geometry.vertices[geometry.triangles[index * 3 + 2]];
        zima::kernel::Vec3 normal{
            (b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y),
            (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z),
            (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)};
        if (placement_vec_is_zero(normal)) return std::nullopt;
        normal = placement_vec_normalized(normal);
        return PlacementReferencePlane{a, normal};
    }
    return std::nullopt;
}

// Solves the origin from an arbitrary set of point/axis/plane position
// references using the same weighted least-squares projection as
// ConstructionDefinition::PointReference, so a container can be positioned
// by more than one reference (e.g. a point plus a plane offset).
bool placement_solve_position(
    const std::vector<std::reference_wrapper<const ConstructionReference>>&
        placement_references,
    const zima::kernel::ViewerReferenceGeometry& geometry,
    zima::kernel::Vec3& origin) {
    if (placement_references.empty()) return true;
    std::vector<std::pair<zima::kernel::Vec3, double>> equations;
    const auto add_axis_equations = [&](const PlacementReferenceAxis& value) {
        const auto seed = std::abs(value.direction.x) < 0.8
            ? zima::kernel::Vec3{1.0, 0.0, 0.0}
            : zima::kernel::Vec3{0.0, 1.0, 0.0};
        const auto first = placement_vec_normalized(
            placement_vec_cross(value.direction, seed));
        const auto second = placement_vec_normalized(
            placement_vec_cross(value.direction, first));
        equations.push_back({first, placement_vec_dot(first, value.point)});
        equations.push_back({second, placement_vec_dot(second, value.point)});
    };
    for (const auto& wrapped : placement_references) {
        const auto& reference = wrapped.get();
        if (const auto resolved = placement_reference_point(reference, geometry)) {
            equations.push_back({{1.0, 0.0, 0.0}, resolved->x});
            equations.push_back({{0.0, 1.0, 0.0}, resolved->y});
            equations.push_back({{0.0, 0.0, 1.0}, resolved->z});
        } else if (const auto resolved = placement_reference_axis(reference, geometry)) {
            add_axis_equations(*resolved);
        } else if (const auto resolved = placement_reference_plane(reference, geometry)) {
            equations.push_back({resolved->normal,
                placement_vec_dot(resolved->normal, resolved->point) + reference.offset});
        } else {
            return false;
        }
    }
    if (equations.empty()) return false;
    constexpr double weight = 1.0e10;
    double matrix[3][4]{{1.0, 0.0, 0.0, origin.x},
                        {0.0, 1.0, 0.0, origin.y},
                        {0.0, 0.0, 1.0, origin.z}};
    for (const auto& [normal, rhs] : equations) {
        const double values[3]{normal.x, normal.y, normal.z};
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                matrix[row][column] += weight * values[row] * values[column];
            }
            matrix[row][3] += weight * values[row] * rhs;
        }
    }
    for (int column = 0; column < 3; ++column) {
        int pivot = column;
        for (int row = column + 1; row < 3; ++row) {
            if (std::abs(matrix[row][column]) > std::abs(matrix[pivot][column])) pivot = row;
        }
        if (std::abs(matrix[pivot][column]) <= 1.0e-12) return false;
        if (pivot != column) {
            for (int item = column; item < 4; ++item) std::swap(matrix[pivot][item], matrix[column][item]);
        }
        const double divisor = matrix[column][column];
        for (int item = column; item < 4; ++item) matrix[column][item] /= divisor;
        for (int row = 0; row < 3; ++row) {
            if (row == column) continue;
            const double factor = matrix[row][column];
            for (int item = column; item < 4; ++item) matrix[row][item] -= factor * matrix[column][item];
        }
    }
    const zima::kernel::Vec3 solved{matrix[0][3], matrix[1][3], matrix[2][3]};
    const bool consistent = std::all_of(equations.begin(), equations.end(),
        [&](const auto& equation) {
            const auto& [normal, rhs] = equation;
            return std::abs(placement_vec_dot(normal, solved) - rhs) <= 1.0e-5;
        });
    if (!consistent) return false;
    origin = solved;
    return true;
}

}  // namespace

PartDocument PartDocument::create_default() {
    PartDocument document;
    document.document_id = make_id();
    return document;
}

HistoryContainer PartDocument::create_box_container() {
    HistoryContainer container;
    container.id = make_id();
    container.feature_id = make_id();
    container.feature_parent_id = container.id;
    container.container_origin = create_container_origin(container.id);
    return container;
}

HistoryContainer PartDocument::create_cylinder_container() {
    HistoryContainer container;
    container.id = make_id();
    container.feature_id = make_id();
    container.feature_parent_id = container.id;
    container.container_origin = create_container_origin(container.id);
    container.name = "Válec";
    container.feature_kind = FeatureKind::Cylinder;
    return container;
}

HistoryContainer PartDocument::create_sphere_container() {
    HistoryContainer container;
    container.id = make_id();
    container.feature_id = make_id();
    container.feature_parent_id = container.id;
    container.container_origin = create_container_origin(container.id);
    container.name = "Koule";
    container.feature_kind = FeatureKind::Sphere;
    return container;
}

HistoryContainer PartDocument::create_cone_container() {
    HistoryContainer container;
    container.id = make_id();
    container.feature_id = make_id();
    container.feature_parent_id = container.id;
    container.container_origin = create_container_origin(container.id);
    container.name = "Kužel";
    container.feature_kind = FeatureKind::Cone;
    return container;
}

HistoryContainer PartDocument::create_pyramid_container() {
    HistoryContainer container;
    container.id = make_id();
    container.feature_id = make_id();
    container.feature_parent_id = container.id;
    container.container_origin = create_container_origin(container.id);
    container.name = "Jehlan";
    container.feature_kind = FeatureKind::Pyramid;
    return container;
}

HistoryContainer PartDocument::create_wedge_container() {
    HistoryContainer container;
    container.id = make_id();
    container.feature_id = make_id();
    container.feature_parent_id = container.id;
    container.container_origin = create_container_origin(container.id);
    container.name = "Klín";
    container.feature_kind = FeatureKind::Wedge;
    return container;
}

ConstructionObject PartDocument::create_construction(ConstructionKind kind) {
    ConstructionObject object;
    object.id = make_id();
    object.kind = kind;
    object.container_origin = create_container_origin(object.id);
    object.entity_id = kind == ConstructionKind::Point
        ? object.container_origin.id + ":point" : object.id + ":entity";
    object.entity_parent_id = kind == ConstructionKind::Point
        ? object.container_origin.id : object.id;
    object.name = kind == ConstructionKind::Point ? "Bod001"
        : kind == ConstructionKind::Axis ? "Osa001" : "Rovina001";
    return object;
}

ContainerOrigin create_container_origin(const std::string& parent_id) {
    if (parent_id.empty()) {
        throw std::invalid_argument("Container Origin parent ID is required");
    }
    ContainerOrigin origin;
    origin.id = parent_id + ":origin";
    origin.parent_id = parent_id;
    const auto child = [&](const char* suffix, const char* name,
                           OriginChildKind kind, const char* key) {
        return OriginChild{origin.id + suffix, origin.id, name, kind, key, true};
    };
    origin.children = {
        child(":point", "Point 0,0,0", OriginChildKind::Point, "point"),
        child(":axis:x", "X Axis", OriginChildKind::Axis, "axis:x"),
        child(":axis:y", "Y Axis", OriginChildKind::Axis, "axis:y"),
        child(":axis:z", "Z Axis", OriginChildKind::Axis, "axis:z"),
        child(":plane:xy", "XY Plane", OriginChildKind::Plane, "plane:xy"),
        child(":plane:yz", "YZ Plane", OriginChildKind::Plane, "plane:yz"),
        child(":plane:xz", "XZ Plane", OriginChildKind::Plane, "plane:xz"),
    };
    return origin;
}

bool resolve_construction(ConstructionObject& object,
    const zima::kernel::ViewerReferenceGeometry& geometry) {
    const auto matches = [](const auto& reference,
                            const ConstructionReference& expected) {
        return reference.instance_path == expected.instance_path &&
            reference.owner_id == expected.owner_id &&
            reference.semantic_key == expected.semantic_key;
    };
    const auto point = [&](const ConstructionReference& reference)
        -> std::optional<zima::kernel::Vec3> {
        const auto found = std::find_if(geometry.points.begin(), geometry.points.end(),
            [&](const auto& candidate) { return matches(candidate.reference, reference); });
        if (found != geometry.points.end()) return found->position;
        return std::nullopt;
    };
    const auto axis = [&](const ConstructionReference& reference)
        -> std::optional<zima::kernel::ViewerAxis> {
        const auto found = std::find_if(geometry.axes.begin(), geometry.axes.end(),
            [&](const auto& candidate) { return matches(candidate.reference, reference); });
        if (found != geometry.axes.end()) return *found;
        const auto edge = std::find_if(geometry.edges.begin(), geometry.edges.end(),
            [&](const auto& candidate) { return matches(candidate.reference, reference); });
        if (edge == geometry.edges.end() || edge->points.size() < 2) return std::nullopt;
        const auto& first = edge->points.front();
        const auto& last = edge->points.back();
        zima::kernel::Vec3 direction{last.x - first.x, last.y - first.y,
            last.z - first.z};
        const double magnitude = std::sqrt(direction.x * direction.x +
            direction.y * direction.y + direction.z * direction.z);
        if (magnitude <= 1.0e-12) return std::nullopt;
        direction = {direction.x / magnitude, direction.y / magnitude,
            direction.z / magnitude};
        for (const auto& candidate : edge->points) {
            const zima::kernel::Vec3 delta{candidate.x - first.x,
                candidate.y - first.y, candidate.z - first.z};
            const zima::kernel::Vec3 deviation{
                delta.y * direction.z - delta.z * direction.y,
                delta.z * direction.x - delta.x * direction.z,
                delta.x * direction.y - delta.y * direction.x};
            if (std::sqrt(deviation.x * deviation.x + deviation.y * deviation.y +
                    deviation.z * deviation.z) > 1.0e-7) return std::nullopt;
        }
        return zima::kernel::ViewerAxis{first, direction, object.display_size,
            {reference.owner_id, reference.semantic_key, reference.instance_path}};
    };
    const auto plane = [&](const ConstructionReference& reference)
        -> std::optional<std::pair<zima::kernel::Vec3, zima::kernel::Vec3>> {
        for (std::size_t index = 0;
             index < geometry.triangle_references.size(); ++index) {
            if (!matches(geometry.triangle_references[index], reference)) continue;
            const auto& a = geometry.vertices[geometry.triangles[index * 3]];
            const auto& b = geometry.vertices[geometry.triangles[index * 3 + 1]];
            const auto& c = geometry.vertices[geometry.triangles[index * 3 + 2]];
            zima::kernel::Vec3 normal{
                (b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y),
                (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z),
                (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)};
            const double magnitude = std::sqrt(normal.x * normal.x +
                normal.y * normal.y + normal.z * normal.z);
            if (magnitude <= 1.0e-12) return std::nullopt;
            normal = {normal.x / magnitude, normal.y / magnitude,
                      normal.z / magnitude};
            return std::pair{a, normal};
        }
        return std::nullopt;
    };
    object.reference_valid = false;
    // Plane orientation references are persisted alongside placement
    // references, but they do not participate in the definition's required
    // placement count.  Keep the two contracts separate when resolving.
    const auto placement_references = [&] {
        std::vector<std::reference_wrapper<const ConstructionReference>> result;
        for (const auto& reference : object.references) {
            if (!reference.orientation_drives_rotation) result.push_back(reference);
        }
        return result;
    }();
    if (object.definition == ConstructionDefinition::Absolute) {
        object.reference_valid = true;
    } else if (object.definition == ConstructionDefinition::PointReference &&
        !placement_references.empty()) {
        // Point placement is a set of geometric constraints, not merely an
        // alias of another vertex.  Preserve the entered origin as the
        // under-constrained fallback and project it onto all selected stable
        // points, straight edges/axes and planar faces/planes.
        std::vector<std::pair<zima::kernel::Vec3, double>> equations;
        const auto add_axis_equations = [&](const zima::kernel::ViewerAxis& value) {
            const auto& d = value.direction;
            const zima::kernel::Vec3 seed = std::abs(d.x) < 0.8
                ? zima::kernel::Vec3{1.0, 0.0, 0.0}
                : zima::kernel::Vec3{0.0, 1.0, 0.0};
            zima::kernel::Vec3 first{
                d.y * seed.z - d.z * seed.y,
                d.z * seed.x - d.x * seed.z,
                d.x * seed.y - d.y * seed.x};
            const double first_length = std::sqrt(first.x * first.x +
                first.y * first.y + first.z * first.z);
            first = {first.x / first_length, first.y / first_length,
                first.z / first_length};
            const zima::kernel::Vec3 second{
                d.y * first.z - d.z * first.y,
                d.z * first.x - d.x * first.z,
                d.x * first.y - d.y * first.x};
            equations.push_back({first, first.x * value.point.x +
                first.y * value.point.y + first.z * value.point.z});
            equations.push_back({second, second.x * value.point.x +
                second.y * value.point.y + second.z * value.point.z});
        };
        bool supported = true;
        for (const auto& wrapped_reference : placement_references) {
            const auto& reference = wrapped_reference.get();
            if (const auto resolved = point(reference)) {
                equations.push_back({{1.0, 0.0, 0.0}, resolved->x});
                equations.push_back({{0.0, 1.0, 0.0}, resolved->y});
                equations.push_back({{0.0, 0.0, 1.0}, resolved->z});
            } else if (const auto resolved = axis(reference)) {
                add_axis_equations(*resolved);
            } else if (const auto resolved = plane(reference)) {
                const auto& [plane_point, normal] = *resolved;
                equations.push_back({normal, normal.x * plane_point.x +
                    normal.y * plane_point.y + normal.z * plane_point.z +
                    reference.offset});
            } else {
                supported = false;
                break;
            }
        }
        if (supported && !equations.empty()) {
            constexpr double weight = 1.0e10;
            double matrix[3][4]{{1.0, 0.0, 0.0, object.origin.x},
                                {0.0, 1.0, 0.0, object.origin.y},
                                {0.0, 0.0, 1.0, object.origin.z}};
            for (const auto& [normal, rhs] : equations) {
                const double values[3]{normal.x, normal.y, normal.z};
                for (int row = 0; row < 3; ++row) {
                    for (int column = 0; column < 3; ++column) {
                        matrix[row][column] +=
                            weight * values[row] * values[column];
                    }
                    matrix[row][3] += weight * values[row] * rhs;
                }
            }
            bool solvable = true;
            for (int column = 0; column < 3; ++column) {
                int pivot = column;
                for (int row = column + 1; row < 3; ++row) {
                    if (std::abs(matrix[row][column]) >
                        std::abs(matrix[pivot][column])) pivot = row;
                }
                if (std::abs(matrix[pivot][column]) <= 1.0e-12) {
                    solvable = false;
                    break;
                }
                if (pivot != column) {
                    for (int item = column; item < 4; ++item) {
                        std::swap(matrix[pivot][item], matrix[column][item]);
                    }
                }
                const double divisor = matrix[column][column];
                for (int item = column; item < 4; ++item) matrix[column][item] /= divisor;
                for (int row = 0; row < 3; ++row) {
                    if (row == column) continue;
                    const double factor = matrix[row][column];
                    for (int item = column; item < 4; ++item) {
                        matrix[row][item] -= factor * matrix[column][item];
                    }
                }
            }
            if (solvable) {
                const zima::kernel::Vec3 solved{
                    matrix[0][3], matrix[1][3], matrix[2][3]};
                const bool consistent = std::all_of(equations.begin(), equations.end(),
                    [&](const auto& equation) {
                        const auto& [normal, rhs] = equation;
                        return std::abs(normal.x * solved.x + normal.y * solved.y +
                            normal.z * solved.z - rhs) <= 1.0e-5;
                    });
                if (consistent) {
                    object.origin = solved;
                    object.reference_valid = true;
                }
            }
        }
    } else if (object.definition == ConstructionDefinition::TwoPointAxis &&
               placement_references.size() == 2) {
        const auto first = point(placement_references[0].get());
        const auto second = point(placement_references[1].get());
        if (first && second) {
            const zima::kernel::Vec3 direction{second->x - first->x,
                second->y - first->y, second->z - first->z};
            const double magnitude = std::sqrt(direction.x * direction.x +
                direction.y * direction.y + direction.z * direction.z);
            if (magnitude > 1.0e-12) {
                object.origin = *first;
                object.direction = {direction.x / magnitude,
                    direction.y / magnitude, direction.z / magnitude};
                object.reference_valid = true;
            }
        }
    } else if (object.definition == ConstructionDefinition::AxisReference &&
               placement_references.size() == 1) {
        if (const auto resolved = axis(placement_references[0].get())) {
            object.origin = resolved->point;
            object.direction = resolved->direction;
            object.reference_valid = true;
        }
    } else if (object.definition == ConstructionDefinition::PlaneReference &&
               placement_references.size() == 1) {
        if (const auto resolved = plane(placement_references[0].get())) {
            object.direction = resolved->second;
            const double offset = placement_references[0].get().offset;
            object.origin = {resolved->first.x + offset * object.direction.x,
                resolved->first.y + offset * object.direction.y,
                resolved->first.z + offset * object.direction.z};
            object.reference_valid = true;
        }
    } else if (object.definition == ConstructionDefinition::ThreePointPlane &&
               placement_references.size() == 3) {
        const auto a = point(placement_references[0].get());
        const auto b = point(placement_references[1].get());
        const auto c = point(placement_references[2].get());
        if (a && b && c) {
            const zima::kernel::Vec3 ab{b->x - a->x, b->y - a->y, b->z - a->z};
            const zima::kernel::Vec3 ac{c->x - a->x, c->y - a->y, c->z - a->z};
            zima::kernel::Vec3 normal{ab.y * ac.z - ab.z * ac.y,
                ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x};
            const double magnitude = std::sqrt(normal.x * normal.x +
                normal.y * normal.y + normal.z * normal.z);
            if (magnitude > 1.0e-12) {
                object.origin = *a;
                object.direction = {normal.x / magnitude, normal.y / magnitude,
                    normal.z / magnitude};
                object.reference_valid = true;
            }
        }
    }
    if (object.reference_valid && object.kind == ConstructionKind::Plane) {
        std::optional<zima::kernel::Vec3> front;
        std::optional<zima::kernel::Vec3> top;
        for (const auto& reference : object.references) {
            if (!reference.orientation_drives_rotation) continue;
            std::optional<zima::kernel::Vec3> direction;
            if (const auto resolved = axis(reference)) direction = resolved->direction;
            else if (const auto resolved = plane(reference)) direction = resolved->second;
            if (!direction) continue;
            const double magnitude = std::hypot(
                std::hypot(direction->x, direction->y), direction->z);
            if (magnitude <= 1.0e-12) continue;
            *direction = {direction->x / magnitude, direction->y / magnitude,
                direction->z / magnitude};
            if (reference.orientation_role == "top") top = *direction;
            else front = *direction;
        }
        const auto perpendicular = [](const zima::kernel::Vec3& value) {
            const auto seed = std::abs(value.z) < 0.8
                ? zima::kernel::Vec3{0.0, 0.0, 1.0}
                : zima::kernel::Vec3{0.0, 1.0, 0.0};
            zima::kernel::Vec3 result{seed.y * value.z - seed.z * value.y,
                seed.z * value.x - seed.x * value.z,
                seed.x * value.y - seed.y * value.x};
            const double magnitude = std::hypot(
                std::hypot(result.x, result.y), result.z);
            return zima::kernel::Vec3{result.x / magnitude,
                result.y / magnitude, result.z / magnitude};
        };
        if (front || top) {
            if (!front) front = perpendicular(*top);
            if (!top) top = perpendicular(*front);
            zima::kernel::Vec3 normal{front->y * top->z - front->z * top->y,
                front->z * top->x - front->x * top->z,
                front->x * top->y - front->y * top->x};
            const double magnitude = std::hypot(
                std::hypot(normal.x, normal.y), normal.z);
            if (magnitude > 1.0e-9) {
                object.direction = {normal.x / magnitude, normal.y / magnitude,
                    normal.z / magnitude};
            }
        }
    }
    return object.reference_valid;
}

bool resolve_placement(
    Placement& placement, const zima::kernel::ViewerReferenceGeometry& geometry) {
    std::vector<std::reference_wrapper<const ConstructionReference>> position_references;
    std::optional<zima::kernel::Vec3> front_direction;
    std::optional<zima::kernel::Vec3> top_direction;
    bool orientation_resolved = true;
    for (const auto& reference : placement.references) {
        if (!reference.orientation_drives_rotation) {
            position_references.push_back(reference);
            continue;
        }
        std::optional<zima::kernel::Vec3> direction;
        if (const auto resolved = placement_reference_axis(reference, geometry)) {
            direction = resolved->direction;
        } else if (const auto resolved = placement_reference_plane(reference, geometry)) {
            direction = resolved->normal;
        }
        if (!direction) {
            orientation_resolved = false;
            continue;
        }
        if (reference.orientation_role == "top") top_direction = direction;
        else front_direction = direction;
    }
    zima::kernel::Vec3 origin{placement.x, placement.y, placement.z};
    const bool position_resolved = placement_solve_position(
        position_references, geometry, origin);
    if (position_resolved) {
        placement.x = origin.x;
        placement.y = origin.y;
        placement.z = origin.z;
    }
    const zima::kernel::Vec3 manual_offset{placement.rotation_offset_x,
        placement.rotation_offset_y, placement.rotation_offset_z};
    const auto composed = placement_compose_orientation_degrees(
        front_direction, top_direction, manual_offset);
    placement.rotation_x = composed.x;
    placement.rotation_y = composed.y;
    placement.rotation_z = composed.z;
    placement.reference_valid = position_resolved && orientation_resolved;
    return placement.reference_valid;
}

PointConstraintState point_constraint_state(
    const std::vector<ConstructionReference>& references,
    const zima::kernel::ViewerReferenceGeometry& geometry) {
    const auto matches = [](const auto& actual, const auto& expected) {
        return actual.instance_path == expected.instance_path &&
            actual.owner_id == expected.owner_id &&
            actual.semantic_key == expected.semantic_key;
    };
    std::vector<zima::kernel::Vec3> rows;
    const auto append_axis_rows = [&](zima::kernel::Vec3 direction) {
        const double magnitude = std::hypot(
            std::hypot(direction.x, direction.y), direction.z);
        if (magnitude <= 1.0e-12) return;
        direction = {direction.x / magnitude, direction.y / magnitude,
            direction.z / magnitude};
        const auto seed = std::abs(direction.x) < 0.8
            ? zima::kernel::Vec3{1.0, 0.0, 0.0}
            : zima::kernel::Vec3{0.0, 1.0, 0.0};
        zima::kernel::Vec3 first{
            direction.y * seed.z - direction.z * seed.y,
            direction.z * seed.x - direction.x * seed.z,
            direction.x * seed.y - direction.y * seed.x};
        const double length = std::hypot(std::hypot(first.x, first.y), first.z);
        first = {first.x / length, first.y / length, first.z / length};
        rows.push_back(first);
        rows.push_back({direction.y * first.z - direction.z * first.y,
            direction.z * first.x - direction.x * first.z,
            direction.x * first.y - direction.y * first.x});
    };
    for (const auto& reference : references) {
        if (reference.orientation_drives_rotation) continue;
        if (std::any_of(geometry.points.begin(), geometry.points.end(),
                [&](const auto& item) { return matches(item.reference, reference); })) {
            rows.insert(rows.end(), {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}});
            continue;
        }
        const auto axis = std::find_if(geometry.axes.begin(), geometry.axes.end(),
            [&](const auto& item) { return matches(item.reference, reference); });
        if (axis != geometry.axes.end()) {
            append_axis_rows(axis->direction);
            continue;
        }
        const auto edge = std::find_if(geometry.edges.begin(), geometry.edges.end(),
            [&](const auto& item) { return matches(item.reference, reference); });
        if (edge != geometry.edges.end() && edge->points.size() >= 2) {
            const auto& first = edge->points.front();
            const auto& last = edge->points.back();
            zima::kernel::Vec3 direction{
                last.x - first.x, last.y - first.y, last.z - first.z};
            const double length = std::hypot(
                std::hypot(direction.x, direction.y), direction.z);
            if (length <= 1.0e-12) continue;
            direction = {direction.x / length, direction.y / length,
                direction.z / length};
            const bool straight = std::all_of(edge->points.begin(), edge->points.end(),
                [&](const auto& point) {
                    const zima::kernel::Vec3 delta{point.x - first.x,
                        point.y - first.y, point.z - first.z};
                    const zima::kernel::Vec3 deviation{
                        delta.y * direction.z - delta.z * direction.y,
                        delta.z * direction.x - delta.x * direction.z,
                        delta.x * direction.y - delta.y * direction.x};
                    return std::hypot(std::hypot(deviation.x, deviation.y),
                        deviation.z) <= 1.0e-7;
                });
            if (!straight) continue;
            append_axis_rows(direction);
            continue;
        }
        for (std::size_t index = 0;
             index < geometry.triangle_references.size(); ++index) {
            if (!matches(geometry.triangle_references[index], reference) ||
                index * 3 + 2 >= geometry.triangles.size()) continue;
            const auto& a = geometry.vertices[geometry.triangles[index * 3]];
            const auto& b = geometry.vertices[geometry.triangles[index * 3 + 1]];
            const auto& c = geometry.vertices[geometry.triangles[index * 3 + 2]];
            rows.push_back({(b.y-a.y)*(c.z-a.z)-(b.z-a.z)*(c.y-a.y),
                (b.z-a.z)*(c.x-a.x)-(b.x-a.x)*(c.z-a.z),
                (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x)});
            break;
        }
    }
    int rank = 0;
    std::array<bool, 3> pivot_columns{};
    for (int column = 0; column < 3 && rank < static_cast<int>(rows.size()); ++column) {
        int pivot = rank;
        for (int row = rank + 1; row < static_cast<int>(rows.size()); ++row) {
            const double value = column == 0 ? rows[row].x
                : column == 1 ? rows[row].y : rows[row].z;
            const double pivot_value = column == 0 ? rows[pivot].x
                : column == 1 ? rows[pivot].y : rows[pivot].z;
            if (std::abs(value) > std::abs(pivot_value)) pivot = row;
        }
        double divisor = column == 0 ? rows[pivot].x
            : column == 1 ? rows[pivot].y : rows[pivot].z;
        if (std::abs(divisor) <= 1.0e-9) continue;
        std::swap(rows[rank], rows[pivot]);
        divisor = column == 0 ? rows[rank].x
            : column == 1 ? rows[rank].y : rows[rank].z;
        rows[rank] = {rows[rank].x/divisor, rows[rank].y/divisor,
            rows[rank].z/divisor};
        for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
            if (row == rank) continue;
            const double factor = column == 0 ? rows[row].x
                : column == 1 ? rows[row].y : rows[row].z;
            rows[row] = {rows[row].x-factor*rows[rank].x,
                rows[row].y-factor*rows[rank].y,
                rows[row].z-factor*rows[rank].z};
        }
        pivot_columns[static_cast<std::size_t>(column)] = true;
        ++rank;
    }
    PointConstraintState state;
    state.remaining_dof = 3 - std::min(rank, 3);
    // Python exposes the pivot columns of its reduced equation matrix as the
    // constrained X/Y/Z controls. Keep the UI contract identical, including
    // oblique planes where one pivot coordinate is solved from free fallbacks.
    state.constrained_axes = pivot_columns;
    return state;
}

int point_constraint_remaining_dof(
    const std::vector<ConstructionReference>& references,
    const zima::kernel::ViewerReferenceGeometry& geometry) {
    return point_constraint_state(references, geometry).remaining_dof;
}

int orientation_constraint_remaining_dof(
    const std::vector<ConstructionReference>& references,
    const zima::kernel::ViewerReferenceGeometry& geometry,
    bool marked_only) {
    const auto matches = [](const auto& actual, const auto& expected) {
        return actual.instance_path == expected.instance_path &&
            actual.owner_id == expected.owner_id &&
            actual.semantic_key == expected.semantic_key;
    };
    std::vector<zima::kernel::Vec3> directions;
    for (const auto& reference : references) {
        if (marked_only && !reference.orientation_drives_rotation) continue;
        std::optional<zima::kernel::Vec3> direction;
        const auto axis = std::find_if(geometry.axes.begin(), geometry.axes.end(),
            [&](const auto& item) { return matches(item.reference, reference); });
        if (axis != geometry.axes.end()) direction = axis->direction;
        if (!direction) {
            const auto edge = std::find_if(geometry.edges.begin(), geometry.edges.end(),
                [&](const auto& item) { return matches(item.reference, reference); });
            if (edge != geometry.edges.end() && edge->points.size() >= 2) {
                const auto& first = edge->points.front();
                const auto& last = edge->points.back();
                direction = zima::kernel::Vec3{last.x - first.x,
                    last.y - first.y, last.z - first.z};
            }
        }
        if (!direction) {
            for (std::size_t index = 0;
                 index < geometry.triangle_references.size(); ++index) {
                if (!matches(geometry.triangle_references[index], reference) ||
                    index * 3 + 2 >= geometry.triangles.size()) continue;
                const auto& a = geometry.vertices[geometry.triangles[index * 3]];
                const auto& b = geometry.vertices[geometry.triangles[index * 3 + 1]];
                const auto& c = geometry.vertices[geometry.triangles[index * 3 + 2]];
                direction = zima::kernel::Vec3{
                    (b.y-a.y)*(c.z-a.z)-(b.z-a.z)*(c.y-a.y),
                    (b.z-a.z)*(c.x-a.x)-(b.x-a.x)*(c.z-a.z),
                    (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x)};
                break;
            }
        }
        if (!direction) continue;
        const double magnitude = std::hypot(
            std::hypot(direction->x, direction->y), direction->z);
        if (magnitude <= 1.0e-9) continue;
        directions.push_back({direction->x / magnitude,
            direction->y / magnitude, direction->z / magnitude});
    }
    if (directions.empty()) return 3;
    const auto& first = directions.front();
    const bool independent = std::any_of(directions.begin() + 1,
        directions.end(), [&](const auto& direction) {
            const zima::kernel::Vec3 cross{
                first.y * direction.z - first.z * direction.y,
                first.z * direction.x - first.x * direction.z,
                first.x * direction.y - first.y * direction.x};
            return std::hypot(std::hypot(cross.x, cross.y), cross.z) > 1.0e-6;
        });
    return independent ? 0 : 1;
}

ConstructionObject* PartDocument::find_construction(const std::string& id) {
    const auto found = std::find_if(constructions.begin(), constructions.end(),
        [&](const auto& object) { return object.id == id; });
    return found == constructions.end() ? nullptr : &*found;
}

const ConstructionObject* PartDocument::find_construction(
    const std::string& id) const {
    const auto found = std::find_if(constructions.begin(), constructions.end(),
        [&](const auto& object) { return object.id == id; });
    return found == constructions.end() ? nullptr : &*found;
}

zima::kernel::ViewerMesh PartDocument::origin_viewer_mesh() const {
    zima::kernel::ViewerMesh mesh;
    const auto normalized = [](zima::kernel::Vec3 value) {
        const double length = std::sqrt(value.x * value.x + value.y * value.y +
                                        value.z * value.z);
        return zima::kernel::Vec3{value.x / length, value.y / length,
                                  value.z / length};
    };
    const auto cross = [](const auto& a, const auto& b) {
        return zima::kernel::Vec3{a.y * b.z - a.z * b.y,
                                  a.z * b.x - a.x * b.z,
                                  a.x * b.y - a.y * b.x};
    };
    const std::string origin_id = document_id + ":origin";
    const zima::kernel::Vec3 zero{};
    mesh.points.push_back({zero, {origin_id, "origin:point", {}},
                           name + " · Origin"});
    mesh.original_references.points.push_back(
        {zero, {origin_id, "origin:point", {}}});
    constexpr double origin_axis_length = 4.0;
    for (const auto& [key, direction] : std::array{
             std::pair{"x", zima::kernel::Vec3{1.0, 0.0, 0.0}},
             std::pair{"y", zima::kernel::Vec3{0.0, 1.0, 0.0}},
             std::pair{"z", zima::kernel::Vec3{0.0, 0.0, 1.0}}}) {
        zima::kernel::ViewerAxis axis{
            zero, direction, origin_axis_length,
            {origin_id, std::string("origin:axis:") + key, {}}};
        axis.label = key;
        mesh.axes.push_back(axis);
        mesh.original_references.axes.push_back(std::move(axis));
    }
    const auto append_origin_plane = [&](const char* key,
            zima::kernel::Vec3 first, zima::kernel::Vec3 second) {
        constexpr double half = 4.0;
        const std::array corners{
            zima::kernel::Vec3{-half * first.x - half * second.x,
                               -half * first.y - half * second.y,
                               -half * first.z - half * second.z},
            zima::kernel::Vec3{ half * first.x - half * second.x,
                                half * first.y - half * second.y,
                                half * first.z - half * second.z},
            zima::kernel::Vec3{ half * first.x + half * second.x,
                                half * first.y + half * second.y,
                                half * first.z + half * second.z},
            zima::kernel::Vec3{-half * first.x + half * second.x,
                               -half * first.y + half * second.y,
                               -half * first.z + half * second.z}};
        const std::string semantic = std::string("origin:plane:") + key;
        mesh.edges.push_back({{corners[0], corners[1], corners[2], corners[3],
                               corners[0]}, {origin_id, semantic, {}}, false, true});
        auto& references = mesh.original_references;
        const auto offset = static_cast<std::uint32_t>(references.vertices.size());
        references.vertices.insert(
            references.vertices.end(), corners.begin(), corners.end());
        references.triangles.insert(references.triangles.end(),
            {offset, offset + 1, offset + 2, offset, offset + 2, offset + 3});
        references.triangle_references.insert(
            references.triangle_references.end(), 2,
            {origin_id, semantic, {}});
    };
    append_origin_plane("xy", {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
    append_origin_plane("yz", {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0});
    append_origin_plane("xz", {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
    return mesh;
}

zima::kernel::ViewerMesh PartDocument::construction_viewer_mesh(
    const std::string& editing_object_id) const {
    zima::kernel::ViewerMesh mesh;
    const auto normalized = [](zima::kernel::Vec3 value) {
        const double length = std::sqrt(value.x * value.x + value.y * value.y +
                                        value.z * value.z);
        return zima::kernel::Vec3{value.x / length, value.y / length,
                                  value.z / length};
    };
    const auto cross = [](const auto& a, const auto& b) {
        return zima::kernel::Vec3{a.y * b.z - a.z * b.y,
                                  a.z * b.x - a.x * b.z,
                                  a.x * b.y - a.y * b.x};
    };
    const auto rotated = [](zima::kernel::Vec3 value,
                            const zima::kernel::Vec3& rotation) {
        constexpr double radians = std::numbers::pi / 180.0;
        const double cx = std::cos(rotation.x * radians);
        const double sx = std::sin(rotation.x * radians);
        const double cy = std::cos(rotation.y * radians);
        const double sy = std::sin(rotation.y * radians);
        const double cz = std::cos(rotation.z * radians);
        const double sz = std::sin(rotation.z * radians);
        value = {value.x, cx * value.y - sx * value.z,
            sx * value.y + cx * value.z};
        value = {cy * value.x + sy * value.z, value.y,
            -sy * value.x + cy * value.z};
        return zima::kernel::Vec3{cz * value.x - sz * value.y,
            sz * value.x + cz * value.y, value.z};
    };
    for (const auto& object : constructions) {
        if (!object.reference_valid || object.suppressed) continue;
        if (object.kind == ConstructionKind::Point) {
            const std::string& origin_id = object.container_origin.id;
            const bool editing = editing_object_id == object.id;
            // Point is the intentional container/geometry exception: its one
            // visible marker always represents the Point container itself.
            // Editing may expose the container's auxiliary Origin axes and
            // planes, but must not rename the marker to a nested origin point.
            constexpr std::string_view point_semantic{"point"};
            mesh.points.push_back(
                {object.origin, {origin_id, std::string(point_semantic), {}}, object.name});
            mesh.original_references.points.push_back(
                {object.origin, {origin_id, std::string(point_semantic), {}}});
            if (!editing) continue;
            // Every container Origin is half the linear size of the document
            // Origin. Arrowheads remain screen-space renderer geometry.
            constexpr double extent = 2.0;
            for (const auto& [key, local_direction] : std::array{
                     std::pair{"x", zima::kernel::Vec3{1.0, 0.0, 0.0}},
                     std::pair{"y", zima::kernel::Vec3{0.0, 1.0, 0.0}},
                     std::pair{"z", zima::kernel::Vec3{0.0, 0.0, 1.0}}}) {
                zima::kernel::ViewerAxis axis{object.origin,
                    rotated(local_direction, object.rotation), extent,
                    {origin_id, std::string("origin:axis:") + key, {}}};
                axis.label = key;
                mesh.axes.push_back(axis);
                mesh.original_references.axes.push_back(std::move(axis));
            }
            const auto append_plane = [&](const char* key,
                    zima::kernel::Vec3 local_first,
                    zima::kernel::Vec3 local_second) {
                const auto first = rotated(local_first, object.rotation);
                const auto second = rotated(local_second, object.rotation);
                const std::array corners{
                    zima::kernel::Vec3{object.origin.x - extent * first.x - extent * second.x,
                        object.origin.y - extent * first.y - extent * second.y,
                        object.origin.z - extent * first.z - extent * second.z},
                    zima::kernel::Vec3{object.origin.x + extent * first.x - extent * second.x,
                        object.origin.y + extent * first.y - extent * second.y,
                        object.origin.z + extent * first.z - extent * second.z},
                    zima::kernel::Vec3{object.origin.x + extent * first.x + extent * second.x,
                        object.origin.y + extent * first.y + extent * second.y,
                        object.origin.z + extent * first.z + extent * second.z},
                    zima::kernel::Vec3{object.origin.x - extent * first.x + extent * second.x,
                        object.origin.y - extent * first.y + extent * second.y,
                        object.origin.z - extent * first.z + extent * second.z}};
                const std::string semantic = std::string("origin:plane:") + key;
                mesh.edges.push_back({{corners[0], corners[1], corners[2], corners[3],
                    corners[0]}, {origin_id, semantic, {}}, false, true});
                auto& references = mesh.original_references;
                const auto offset = static_cast<std::uint32_t>(references.vertices.size());
                references.vertices.insert(
                    references.vertices.end(), corners.begin(), corners.end());
                references.triangles.insert(references.triangles.end(),
                    {offset, offset + 1, offset + 2,
                     offset, offset + 2, offset + 3});
                references.triangle_references.insert(
                    references.triangle_references.end(), 2,
                    {origin_id, semantic, {}});
            };
            append_plane("xy", {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
            append_plane("yz", {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0});
            append_plane("xz", {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
            continue;
        }
        const auto normal = normalized(object.direction);
        if (object.kind == ConstructionKind::Axis) {
            mesh.axes.push_back({object.origin, normal, object.display_size,
                                 {object.entity_id, "axis", {}}});
            mesh.original_references.axes.push_back(
                {object.origin, normal, object.display_size,
                 {object.entity_id, "axis", {}}});
            continue;
        }
        const auto helper = std::abs(normal.z) < 0.9
            ? zima::kernel::Vec3{0.0, 0.0, 1.0}
            : zima::kernel::Vec3{0.0, 1.0, 0.0};
        const auto first = normalized(cross(normal, helper));
        const auto second = normalized(cross(normal, first));
        const double half = object.display_size * 0.5;
        std::array<zima::kernel::Vec3, 4> corners;
        for (std::size_t index = 0; index < corners.size(); ++index) {
            const double a = index == 0 || index == 3 ? -half : half;
            const double b = index < 2 ? -half : half;
            corners[index] = {object.origin.x + a * first.x + b * second.x,
                              object.origin.y + a * first.y + b * second.y,
                              object.origin.z + a * first.z + b * second.z};
        }
        mesh.edges.push_back({{corners[0], corners[1], corners[2], corners[3],
                               corners[0]}, {object.entity_id, "border", {}}, false, true});
        auto& references = mesh.original_references;
        const auto offset = static_cast<std::uint32_t>(references.vertices.size());
        references.vertices.insert(references.vertices.end(), corners.begin(), corners.end());
        references.triangles.insert(references.triangles.end(),
            {offset, offset + 1, offset + 2, offset, offset + 2, offset + 3});
        references.triangle_references.insert(references.triangle_references.end(),
            2, {object.entity_id, "plane", {}});
    }
    return mesh;
}

void PartDocument::resolve_constructions(
    zima::kernel::ViewerReferenceGeometry source_geometry) {
    const auto append = [](auto& target, const auto& source) {
        const auto vertex_offset = static_cast<std::uint32_t>(target.vertices.size());
        target.vertices.insert(target.vertices.end(),
            source.vertices.begin(), source.vertices.end());
        for (const auto index : source.triangles) {
            target.triangles.push_back(vertex_offset + index);
        }
        target.triangle_references.insert(target.triangle_references.end(),
            source.triangle_references.begin(), source.triangle_references.end());
        target.edges.insert(target.edges.end(), source.edges.begin(), source.edges.end());
        target.points.insert(target.points.end(), source.points.begin(), source.points.end());
        target.axes.insert(target.axes.end(), source.axes.begin(), source.axes.end());
    };
    append(source_geometry, origin_viewer_mesh().original_references);
    for (auto& object : constructions) {
        static_cast<void>(resolve_construction(object, source_geometry));
        if (!object.reference_valid) continue;
        PartDocument carrier;
        carrier.constructions.push_back(object);
        append(source_geometry,
            carrier.construction_viewer_mesh().original_references);
    }
    // Every history container shares the same universal placement: an
    // origin resolved from a position reference plus an optional FRONT/TOP
    // reference-driven orientation frame, composed with a manual RX/RY/RZ
    // correction. This mirrors the reference implementation's container
    // placement, which is independent of the feature/construction kind.
    // Containers with no placement references keep using their raw
    // x/y/z/rotation_x/y/z fields untouched (legacy direct XYZ placement),
    // since resolve_placement() would otherwise overwrite rotation_x/y/z
    // with the (possibly zero) manual rotation_offset_x/y/z correction.
    for (auto& container : history) {
        if (container.placement.references.empty()) continue;
        static_cast<void>(resolve_placement(container.placement, source_geometry));
    }
}

std::vector<zima::kernel::ViewerEdge> PartDocument::extrusion_preview_edges(
    const HistoryContainer& container, double through_all_span) const {
    if (container.feature_kind != FeatureKind::Extrusion) return {};
    const auto sketch = std::find_if(sketches.begin(), sketches.end(),
        [&](const auto& value) { return value.id == container.extrusion.sketch_id; });
    if (sketch == sketches.end()) return {};
    const auto& parameters = container.extrusion;
    const bool legacy_definition = parameters.extent != ExtrusionExtent::Blind ||
        (parameters.height != 10.0 && parameters.length_forward == 10.0 &&
         parameters.extent_mode == ProfileExtentMode::OneSide &&
         parameters.end_condition_forward == EndCondition::Length &&
         parameters.end_targets_forward.empty());
    const double forward = legacy_definition ? parameters.height
                                              : parameters.length_forward;
    const double reverse = legacy_definition ? 0.0
        : parameters.extent_mode == ProfileExtentMode::OneSide
        ? 0.0
        : parameters.extent_mode == ProfileExtentMode::Symmetric
            ? forward : parameters.length_reverse;
    auto request = extrusion_request(*sketch,
        forward + reverse, parameters.direction);
    const double length = std::sqrt(request.direction.x * request.direction.x +
                                    request.direction.y * request.direction.y +
                                    request.direction.z * request.direction.z);
    const zima::kernel::Vec3 unit{request.direction.x / length,
                                  request.direction.y / length,
                                  request.direction.z / length};
    const auto surface_distance = [&](const zima::kernel::Vec3& point) {
        const auto& triangles = legacy_definition
            ? parameters.target_surface_triangles
            : parameters.end_targets_forward.front().fallback_triangles;
        double nearest = std::numeric_limits<double>::infinity();
        for (std::size_t index = 0; index < triangles.size(); index += 3) {
            const auto& v0 = triangles[index];
            const auto& v1 = triangles[index + 1];
            const auto& v2 = triangles[index + 2];
            const zima::kernel::Vec3 edge1{v1.x - v0.x, v1.y - v0.y, v1.z - v0.z};
            const zima::kernel::Vec3 edge2{v2.x - v0.x, v2.y - v0.y, v2.z - v0.z};
            const zima::kernel::Vec3 h{unit.y * edge2.z - unit.z * edge2.y,
                                       unit.z * edge2.x - unit.x * edge2.z,
                                       unit.x * edge2.y - unit.y * edge2.x};
            const double determinant = edge1.x * h.x + edge1.y * h.y + edge1.z * h.z;
            if (std::abs(determinant) <= 1e-12) continue;
            const double inverse = 1.0 / determinant;
            const zima::kernel::Vec3 s{point.x - v0.x, point.y - v0.y, point.z - v0.z};
            const double u = inverse * (s.x * h.x + s.y * h.y + s.z * h.z);
            if (u < -1e-9 || u > 1.0 + 1e-9) continue;
            const zima::kernel::Vec3 q{s.y * edge1.z - s.z * edge1.y,
                                       s.z * edge1.x - s.x * edge1.z,
                                       s.x * edge1.y - s.y * edge1.x};
            const double v = inverse * (unit.x * q.x + unit.y * q.y + unit.z * q.z);
            if (v < -1e-9 || u + v > 1.0 + 1e-9) continue;
            const double distance = inverse *
                (edge2.x * q.x + edge2.y * q.y + edge2.z * q.z);
            if (distance <= 1e-9) continue;
            if (distance < nearest - 1e-7) {
                nearest = distance;
            }
        }
        if (!std::isfinite(nearest)) {
            throw std::runtime_error("Extrusion profile misses target surface");
        }
        return nearest;
    };
    const auto endpoint = [&](const zima::kernel::Vec3& point) {
        const auto condition = legacy_definition
            ? parameters.extent == ExtrusionExtent::ThroughAll
                ? EndCondition::ThroughAll
                : parameters.extent == ExtrusionExtent::Blind
                    ? EndCondition::Length : EndCondition::UpTo
            : parameters.end_condition_forward;
        const auto* target = parameters.end_targets_forward.empty()
            ? nullptr : &parameters.end_targets_forward.front();
        if (condition == EndCondition::UpTo &&
            ((target && target->kind == EndTargetKind::Plane) ||
             (legacy_definition && parameters.extent == ExtrusionExtent::UpToPlane))) {
            const auto& normal = legacy_definition ? parameters.target_plane_normal
                                                   : target->fallback_normal;
            const auto& origin = legacy_definition ? parameters.target_plane_origin
                                                   : target->fallback_origin;
            const double denominator = unit.x * normal.x + unit.y * normal.y +
                                       unit.z * normal.z;
            if (std::abs(denominator) <= 1e-12) {
                throw std::runtime_error("Extrusion direction is parallel to target plane");
            }
            const double distance =
                ((origin.x - point.x) * normal.x +
                 (origin.y - point.y) * normal.y +
                 (origin.z - point.z) * normal.z) /
                denominator;
            if (!std::isfinite(distance) || distance <= 1e-9) {
                throw std::runtime_error("Extrusion profile crosses target plane");
            }
            return zima::kernel::Vec3{point.x + unit.x * distance,
                                      point.y + unit.y * distance,
                                      point.z + unit.z * distance};
        }
        if (condition == EndCondition::UpTo &&
            (target || (legacy_definition &&
                parameters.extent == ExtrusionExtent::UpToSurface))) {
            const double distance = surface_distance(point);
            return zima::kernel::Vec3{point.x + unit.x * distance,
                                      point.y + unit.y * distance,
                                      point.z + unit.z * distance};
        }
        return zima::kernel::Vec3{point.x + request.direction.x,
                                  point.y + request.direction.y,
                                  point.z + request.direction.z};
    };
    std::vector<zima::kernel::ViewerEdge> result;
    for (const auto& source : sketch->viewer_mesh().edges) {
        if (source.points.size() < 2) continue;
        zima::kernel::ViewerEdge start = source;
        start.reference = {container.id, "preview:start", {}};
        for (auto& point : start.points) {
            point.x -= unit.x * reverse;
            point.y -= unit.y * reverse;
            point.z -= unit.z * reverse;
        }
        zima::kernel::ViewerEdge end;
        end.reference = {container.id, "preview:end", {}};
        end.points.reserve(source.points.size());
        if ((legacy_definition && parameters.extent == ExtrusionExtent::ThroughAll) ||
            (!legacy_definition &&
             parameters.end_condition_forward == EndCondition::ThroughAll)) {
            start.points = source.points;
            for (auto& point : start.points) {
                point.x -= unit.x * through_all_span;
                point.y -= unit.y * through_all_span;
                point.z -= unit.z * through_all_span;
                end.points.push_back({point.x + unit.x * 2.0 * through_all_span,
                                      point.y + unit.y * 2.0 * through_all_span,
                                      point.z + unit.z * 2.0 * through_all_span});
            }
        } else {
            for (const auto& point : start.points) end.points.push_back(endpoint(point));
        }
        result.push_back(start);
        result.push_back(end);
        const std::size_t samples[] = {0, source.points.size() - 1};
        for (const auto index : samples) {
            result.push_back({{start.points[index], end.points[index]},
                              {container.id, "preview:side", {}}});
        }
    }
    return result;
}

std::vector<zima::kernel::ViewerEdge> PartDocument::revolution_preview_edges(
    const HistoryContainer& container) const {
    if (container.feature_kind != FeatureKind::Revolution) return {};
    const auto sketch = std::find_if(sketches.begin(), sketches.end(),
        [&](const auto& value) { return value.id == container.revolution.sketch_id; });
    if (sketch == sketches.end()) return {};
    const auto& parameters = container.revolution;
    const double reverse = parameters.extent_mode == ProfileExtentMode::OneSide
        ? 0.0 : parameters.extent_mode == ProfileExtentMode::Symmetric
            ? parameters.angle_degrees : parameters.angle_reverse;
    auto request = revolution_request(
        *sketch, parameters.axis, parameters.angle_degrees + reverse);
    if (parameters.direction == ExtrusionDirection::Reverse) {
        request.axis_direction.x = -request.axis_direction.x;
        request.axis_direction.y = -request.axis_direction.y;
        request.axis_direction.z = -request.axis_direction.z;
    }
    const double length = std::sqrt(
        request.axis_direction.x * request.axis_direction.x +
        request.axis_direction.y * request.axis_direction.y +
        request.axis_direction.z * request.axis_direction.z);
    const zima::kernel::Vec3 axis{request.axis_direction.x / length,
                                  request.axis_direction.y / length,
                                  request.axis_direction.z / length};
    const auto rotate = [&](const zima::kernel::Vec3& point, double degrees) {
        const double angle = degrees * std::numbers::pi / 180.0;
        const zima::kernel::Vec3 relative{point.x - request.axis_point.x,
                                          point.y - request.axis_point.y,
                                          point.z - request.axis_point.z};
        const double dot = relative.x * axis.x + relative.y * axis.y +
                           relative.z * axis.z;
        const zima::kernel::Vec3 cross{
            axis.y * relative.z - axis.z * relative.y,
            axis.z * relative.x - axis.x * relative.z,
            axis.x * relative.y - axis.y * relative.x};
        const double cosine = std::cos(angle);
        const double sine = std::sin(angle);
        return zima::kernel::Vec3{
            request.axis_point.x + relative.x * cosine + cross.x * sine +
                axis.x * dot * (1.0 - cosine),
            request.axis_point.y + relative.y * cosine + cross.y * sine +
                axis.y * dot * (1.0 - cosine),
            request.axis_point.z + relative.z * cosine + cross.z * sine +
                axis.z * dot * (1.0 - cosine)};
    };
    const double start_angle = -reverse;
    const double end_angle = parameters.angle_degrees;
    std::vector<zima::kernel::ViewerEdge> result;
    for (const auto& source : sketch->viewer_mesh().edges) {
        if (source.points.size() < 2) continue;
        zima::kernel::ViewerEdge start;
        zima::kernel::ViewerEdge end;
        start.reference = {container.id, "preview:start", {}};
        end.reference = {container.id, "preview:end", {}};
        for (const auto& point : source.points) {
            start.points.push_back(rotate(point, start_angle));
            end.points.push_back(rotate(point, end_angle));
        }
        result.push_back(std::move(start));
        result.push_back(std::move(end));
        for (const auto endpoint : {source.points.front(), source.points.back()}) {
            zima::kernel::ViewerEdge sweep;
            sweep.reference = {container.id, "preview:sweep", {}};
            constexpr int samples = 32;
            for (int sample = 0; sample <= samples; ++sample) {
                const double fraction = static_cast<double>(sample) / samples;
                sweep.points.push_back(rotate(endpoint,
                    start_angle + (end_angle - start_angle) * fraction));
            }
            result.push_back(std::move(sweep));
        }
    }
    return result;
}

HistoryContainer PartDocument::create_extrusion_container(std::string sketch_id) {
    if (sketch_id.empty()) throw std::invalid_argument("Extrusion Sketch ID is required");
    HistoryContainer container;
    container.id = make_id();
    container.feature_id = make_id();
    container.feature_parent_id = container.id;
    container.container_origin = create_container_origin(container.id);
    container.name = "Vytažení";
    container.feature_kind = FeatureKind::Extrusion;
    container.extrusion.sketch_id = std::move(sketch_id);
    return container;
}

HistoryContainer PartDocument::create_revolution_container(std::string sketch_id) {
    if (sketch_id.empty()) throw std::invalid_argument("Revolution Sketch ID is required");
    HistoryContainer container;
    container.id = make_id();
    container.feature_id = make_id();
    container.feature_parent_id = container.id;
    container.container_origin = create_container_origin(container.id);
    container.name = "Rotace";
    container.feature_kind = FeatureKind::Revolution;
    container.revolution.sketch_id = std::move(sketch_id);
    return container;
}

HistoryContainer PartDocument::create_fillet_container(
    std::vector<zima::kernel::EdgeReference> edges) {
    if (edges.empty() || std::any_of(edges.begin(), edges.end(), [](const auto& edge) {
            return !edge.valid() || !edge.instance_path.empty();
        })) {
        throw std::invalid_argument("Fillet requires a local original edge reference");
    }
    HistoryContainer container;
    container.id = make_id();
    container.feature_id = make_id();
    container.feature_parent_id = container.id;
    container.container_origin = create_container_origin(container.id);
    container.name = "Zaoblení";
    container.feature_kind = FeatureKind::Fillet;
    container.edge_treatment.edges = std::move(edges);
    return container;
}

HistoryContainer PartDocument::create_chamfer_container(
    std::vector<zima::kernel::EdgeReference> edges) {
    if (edges.empty() || std::any_of(edges.begin(), edges.end(), [](const auto& edge) {
            return !edge.valid() || !edge.instance_path.empty();
        })) {
        throw std::invalid_argument("Chamfer requires a local original edge reference");
    }
    HistoryContainer container;
    container.id = make_id();
    container.feature_id = make_id();
    container.feature_parent_id = container.id;
    container.container_origin = create_container_origin(container.id);
    container.name = "Sražení";
    container.feature_kind = FeatureKind::Chamfer;
    container.edge_treatment.edges = std::move(edges);
    return container;
}

HistoryContainer PartDocument::create_imported_step_container(
    std::filesystem::path source_path, std::string component_path,
    std::string component_name) {
    if (source_path.empty()) throw std::invalid_argument("STEP source path is empty");
    HistoryContainer container;
    container.id = make_id();
    container.feature_id = make_id();
    container.feature_parent_id = container.id;
    container.container_origin = create_container_origin(container.id);
    container.name = component_name.empty() ? source_path.stem().string()
                                            : std::move(component_name);
    container.feature_kind = FeatureKind::ImportedStep;
    container.imported_step.source_path = source_path.generic_string();
    container.imported_step.component_path = std::move(component_path);
    return container;
}

HistoryContainer* PartDocument::find_container(const std::string& id) {
    const auto found = std::find_if(history.begin(), history.end(),
        [&](const HistoryContainer& container) { return container.id == id; });
    return found == history.end() ? nullptr : &*found;
}

std::size_t PartDocument::effective_history_cursor() const {
    return std::min(history_cursor, history_order.size());
}

void PartDocument::set_history_cursor(std::size_t cursor) {
    history_cursor = std::min(cursor, history_order.size());
}

void PartDocument::insert_history_entry(PartHistoryKind kind, std::string id) {
    const auto cursor = effective_history_cursor();
    history_order.insert(history_order.begin() + static_cast<std::ptrdiff_t>(cursor),
        PartHistoryEntry{kind, std::move(id)});
    history_cursor = cursor + 1;
}

const HistoryContainer* PartDocument::find_container(const std::string& id) const {
    const auto found = std::find_if(history.begin(), history.end(),
        [&](const HistoryContainer& container) { return container.id == id; });
    return found == history.end() ? nullptr : &*found;
}

std::optional<std::size_t> PartDocument::history_index(
    const std::string& id) const {
    const auto found = std::find_if(history.begin(), history.end(),
        [&](const HistoryContainer& container) { return container.id == id; });
    if (found == history.end()) return std::nullopt;
    return static_cast<std::size_t>(std::distance(history.begin(), found));
}

std::vector<zima::kernel::HistoryOperation> PartDocument::kernel_operations(
    bool allow_persisted_external_target) const {
    std::vector<zima::kernel::HistoryOperation> operations;
    operations.reserve(history.size());
    for (const auto& container : history) {
        zima::kernel::Vec3 translation{
            container.placement.x, container.placement.y, container.placement.z};
        zima::kernel::Vec3 rotation{
            container.placement.rotation_x, container.placement.rotation_y,
            container.placement.rotation_z};
        zima::kernel::PrimitiveRequest primitive;
        if (container.feature_kind == FeatureKind::Box) {
            zima::kernel::BoxRequest box{
                container.box.length, container.box.width, container.box.height};
            box.translation = translation;
            box.rotation_degrees = rotation;
            primitive = box;
        } else if (container.feature_kind == FeatureKind::Cylinder) {
            zima::kernel::CylinderRequest cylinder;
            cylinder.radius = container.cylinder.radius;
            cylinder.height = container.cylinder.height;
            cylinder.translation = translation;
            cylinder.rotation_degrees = rotation;
            primitive = cylinder;
        } else if (container.feature_kind == FeatureKind::Sphere) {
            zima::kernel::SphereRequest sphere;
            sphere.radius = container.sphere.radius;
            sphere.translation = translation;
            sphere.rotation_degrees = rotation;
            primitive = sphere;
        } else if (container.feature_kind == FeatureKind::Cone) {
            zima::kernel::ConeRequest cone;
            cone.bottom_radius = container.cone.bottom_radius;
            cone.top_radius = container.cone.top_radius;
            cone.height = container.cone.height;
            cone.translation = translation;
            cone.rotation_degrees = rotation;
            primitive = cone;
        } else if (container.feature_kind == FeatureKind::Pyramid) {
            zima::kernel::PyramidRequest pyramid;
            pyramid.length = container.pyramid.length;
            pyramid.width = container.pyramid.width;
            pyramid.height = container.pyramid.height;
            pyramid.translation = translation;
            pyramid.rotation_degrees = rotation;
            primitive = pyramid;
        } else if (container.feature_kind == FeatureKind::Wedge) {
            zima::kernel::WedgeRequest wedge;
            wedge.length = container.wedge.length;
            wedge.width = container.wedge.width;
            wedge.height = container.wedge.height;
            wedge.top_offset = container.wedge.top_offset;
            wedge.translation = translation;
            wedge.rotation_degrees = rotation;
            primitive = wedge;
        } else if (container.feature_kind == FeatureKind::Extrusion) {
            const auto sketch = std::find_if(sketches.begin(), sketches.end(),
                [&](const auto& value) {
                    return value.id == container.extrusion.sketch_id;
                });
            if (sketch == sketches.end()) {
                throw std::runtime_error("Extrusion references a missing Sketch");
            }
            const auto& parameters = container.extrusion;
            const bool legacy_definition = parameters.extent != ExtrusionExtent::Blind ||
                (parameters.height != 10.0 && parameters.length_forward == 10.0 &&
                 parameters.extent_mode == ProfileExtentMode::OneSide &&
                 parameters.end_condition_forward == EndCondition::Length &&
                 parameters.end_targets_forward.empty());
            const double forward = legacy_definition ? parameters.height
                                                      : parameters.length_forward;
            const double reverse = legacy_definition ? 0.0
                : parameters.extent_mode == ProfileExtentMode::OneSide
                ? 0.0
                : parameters.extent_mode == ProfileExtentMode::Symmetric
                    ? forward
                    : parameters.length_reverse;
            auto extrusion = extrusion_request(
                *sketch, forward + reverse, parameters.direction);
            extrusion.start_offset = -reverse;
            const auto condition = legacy_definition
                ? parameters.extent == ExtrusionExtent::ThroughAll
                    ? EndCondition::ThroughAll
                    : parameters.extent == ExtrusionExtent::Blind
                        ? EndCondition::Length : EndCondition::UpTo
                : parameters.end_condition_forward;
            const bool legacy_plane = legacy_definition &&
                parameters.extent == ExtrusionExtent::UpToPlane;
            extrusion.extent = condition == EndCondition::UpTo &&
                    (legacy_plane || (!parameters.end_targets_forward.empty() &&
                     parameters.end_targets_forward.front().kind == EndTargetKind::Plane))
                ? zima::kernel::ExtrusionRequest::Extent::UpToPlane
                : condition == EndCondition::UpTo
                    ? zima::kernel::ExtrusionRequest::Extent::UpToSurface
                : condition == EndCondition::ThroughAll
                    ? zima::kernel::ExtrusionRequest::Extent::ThroughAll
                    : zima::kernel::ExtrusionRequest::Extent::Blind;
            const ExtrusionParameters::EndTarget* target =
                parameters.end_targets_forward.empty()
                    ? nullptr : &parameters.end_targets_forward.front();
            extrusion.target_face = legacy_definition ? parameters.target_face
                : target ? target->reference : zima::kernel::FaceReference{};
            if (allow_persisted_external_target) {
                extrusion.target_face.instance_path.clear();
            }
            extrusion.target_is_datum = legacy_plane ||
                (target && target->kind == EndTargetKind::Plane);
            if (condition == EndCondition::UpTo && (target || legacy_definition) &&
                !extrusion.target_is_datum &&
                !allow_persisted_external_target &&
                std::none_of(operations.begin(), operations.end(), [&](const auto& prior) {
                    return prior.owner_id == extrusion.target_face.owner_id;
                })) {
                throw std::runtime_error(
                    "Extrusion target must belong to prior history or a datum plane");
            }
            if (legacy_definition) {
                extrusion.target_plane_origin = parameters.target_plane_origin;
                extrusion.target_plane_normal = parameters.target_plane_normal;
                extrusion.target_surface_triangles = parameters.target_surface_triangles;
            } else if (target) {
                extrusion.target_plane_origin = target->fallback_origin;
                extrusion.target_plane_normal = target->fallback_normal;
                extrusion.target_surface_triangles = target->fallback_triangles;
            }
            apply_container_placement(extrusion, container.placement);
            primitive = std::move(extrusion);
        } else if (container.feature_kind == FeatureKind::Revolution) {
            const auto sketch = std::find_if(sketches.begin(), sketches.end(),
                [&](const auto& value) {
                    return value.id == container.revolution.sketch_id;
                });
            if (sketch == sketches.end()) {
                throw std::runtime_error("Revolution references a missing Sketch");
            }
            const auto& parameters = container.revolution;
            const double reverse = parameters.extent_mode ==
                    ProfileExtentMode::OneSide
                ? 0.0
                : parameters.extent_mode == ProfileExtentMode::Symmetric
                    ? parameters.angle_degrees : parameters.angle_reverse;
            auto revolution = revolution_request(
                *sketch, parameters.axis, parameters.angle_degrees + reverse);
            revolution.start_angle_degrees = -reverse;
            if (parameters.direction == ExtrusionDirection::Reverse) {
                revolution.axis_direction.x = -revolution.axis_direction.x;
                revolution.axis_direction.y = -revolution.axis_direction.y;
                revolution.axis_direction.z = -revolution.axis_direction.z;
            }
            apply_container_placement(revolution, container.placement);
            primitive = std::move(revolution);
        } else if (container.feature_kind == FeatureKind::ImportedStep) {
            zima::kernel::StepRequest step{
                container.imported_step.source_path,
                container.imported_step.component_path};
            primitive = std::move(step);
        } else if (container.feature_kind == FeatureKind::Fillet) {
            require_default_sketch_feature_placement(container.placement);
            primitive = zima::kernel::FilletRequest{
                container.edge_treatment.edges,
                container.edge_treatment.origin,
                container.edge_treatment.size};
        } else {
            require_default_sketch_feature_placement(container.placement);
            primitive = zima::kernel::ChamferRequest{
                container.edge_treatment.edges,
                container.edge_treatment.origin,
                container.edge_treatment.size};
        }
        operations.push_back({
            container.id,
            std::move(primitive),
            container.combine_mode == CombineMode::Subtract
                ? zima::kernel::BooleanOperation::Subtract
                : zima::kernel::BooleanOperation::Add,
            container.suppressed,
        });
    }
    return operations;
}

PartDocument PartDocument::load(
    const std::filesystem::path& path,
    std::vector<zima::kernel::BodyResult>* calculated_boundaries) {
    const nlohmann::json root = read_part_ini(path);
    PartDocument document;
    document.document_id = root.at("document_id").get<std::string>();
    document.name = root.at("name").get<std::string>();
    document.user_parameters =
        root.at("user_parameters").get<std::map<std::string, std::string>>();
    document.user_parameter_order =
        root.at("user_parameter_order").get<std::vector<std::string>>();
    document.user_parameter_labels = root.at("user_parameter_labels").get<
        decltype(document.user_parameter_labels)>();
    document.user_parameter_values = root.at("user_parameter_values").get<
        decltype(document.user_parameter_values)>();
    for (const auto& relation : root.at("relations")) {
        document.relations.push_back({relation.at("target").get<std::string>(),
            relation.at("expression").get<std::string>()});
    }
    document.document_units = root.at("document_units").get<decltype(document.document_units)>();
    document.document_precision = root.at("document_precision").get<decltype(document.document_precision)>();
    document.physical_parameters = root.at("physical_parameters").get<decltype(document.physical_parameters)>();
    document.physical_parameter_units = root.at("physical_parameter_units").get<decltype(document.physical_parameter_units)>();
    document.material_parameter_descriptions = root.at("material_parameter_descriptions").get<decltype(document.material_parameter_descriptions)>();
    document.family_table = root.at("family_table").get<std::string>();
    const auto& source_history = root.at("history");
    if (!source_history.is_array()) {
        throw std::runtime_error("Document history must be an array");
    }
    std::unordered_set<std::string> container_ids;
    for (const auto& source : source_history) {
        const std::string type = source.at("type").get<std::string>();
        if (type != "box" && type != "cylinder" && type != "sphere" &&
            type != "cone" && type != "pyramid" && type != "wedge" &&
            type != "extrusion" &&
            type != "revolution" && type != "imported_step" &&
            type != "fillet" && type != "chamfer") {
            throw std::runtime_error("Unsupported history feature type");
        }
        HistoryContainer container;
        container.feature_kind = type == "cylinder" ? FeatureKind::Cylinder
            : type == "sphere" ? FeatureKind::Sphere
            : type == "cone" ? FeatureKind::Cone
            : type == "pyramid" ? FeatureKind::Pyramid
            : type == "wedge" ? FeatureKind::Wedge
            : type == "extrusion" ? FeatureKind::Extrusion
            : type == "revolution" ? FeatureKind::Revolution
            : type == "imported_step" ? FeatureKind::ImportedStep
            : type == "fillet" ? FeatureKind::Fillet
            : type == "chamfer" ? FeatureKind::Chamfer : FeatureKind::Box;
        container.id = source.at("id").get<std::string>();
        container.feature_id = source.at("feature_id").get<std::string>();
        container.feature_parent_id =
            source.at("feature_parent_id").get<std::string>();
        container.name = source.at("name").get<std::string>();
        if (container.id.empty() || !container_ids.insert(container.id).second) {
            throw std::runtime_error("History container IDs must be non-empty and unique");
        }
        if (container.name.empty()) {
            throw std::runtime_error("History container name must not be empty");
        }
        if (container.feature_id.empty() || container.feature_id == container.id ||
            container.feature_parent_id != container.id) {
            throw std::runtime_error("History feature ID must be distinct and non-empty");
        }
        const auto& serialized_origin = source.at("container_origin");
        container.container_origin.id =
            serialized_origin.at("id").get<std::string>();
        container.container_origin.parent_id =
            serialized_origin.at("parent_id").get<std::string>();
        container.container_origin.name =
            serialized_origin.at("name").get<std::string>();
        container.container_origin.locked =
            serialized_origin.at("locked").get<bool>();
        for (const auto& child : serialized_origin.at("children")) {
            const std::string kind = child.at("kind").get<std::string>();
            container.container_origin.children.push_back({
                child.at("id").get<std::string>(),
                child.at("parent_id").get<std::string>(),
                child.at("name").get<std::string>(),
                kind == "point" ? OriginChildKind::Point
                    : kind == "axis" ? OriginChildKind::Axis
                    : kind == "plane" ? OriginChildKind::Plane
                    : throw std::runtime_error("Invalid history Container Origin child kind"),
                child.at("key").get<std::string>(),
                child.at("locked").get<bool>()});
        }
        if (container.container_origin != create_container_origin(container.id)) {
            throw std::runtime_error("Invalid history Container Origin");
        }
        const std::string combine = source.value("combine", "add");
        if (combine != "add" && combine != "subtract") {
            throw std::runtime_error("Invalid history combination mode");
        }
        container.combine_mode = combine == "subtract"
            ? CombineMode::Subtract : CombineMode::Add;
        container.suppressed = source.at("suppressed").get<bool>();
        if (container.feature_kind == FeatureKind::Box) {
            container.box.length = source.at("length").get<double>();
            container.box.width = source.at("width").get<double>();
            container.box.height = source.at("height").get<double>();
            require_positive(container.box.length, "length");
            require_positive(container.box.width, "width");
            require_positive(container.box.height, "height");
        } else if (container.feature_kind == FeatureKind::Cylinder) {
            container.cylinder.radius = source.at("radius").get<double>();
            container.cylinder.height = source.at("height").get<double>();
            require_positive(container.cylinder.radius, "radius");
            require_positive(container.cylinder.height, "height");
        } else if (container.feature_kind == FeatureKind::Sphere) {
            container.sphere.radius = source.at("radius").get<double>();
            require_positive(container.sphere.radius, "radius");
        } else if (container.feature_kind == FeatureKind::Cone) {
            container.cone.bottom_radius = source.at("bottom_radius").get<double>();
            container.cone.top_radius = source.at("top_radius").get<double>();
            container.cone.height = source.at("height").get<double>();
            if (!std::isfinite(container.cone.bottom_radius) ||
                !std::isfinite(container.cone.top_radius) ||
                container.cone.bottom_radius < 0.0 || container.cone.top_radius < 0.0 ||
                (container.cone.bottom_radius <= 0.0 && container.cone.top_radius <= 0.0)) {
                throw std::runtime_error("Invalid Cone radii");
            }
            require_positive(container.cone.height, "height");
        } else if (container.feature_kind == FeatureKind::Pyramid) {
            container.pyramid = {source.at("length").get<double>(),
                source.at("width").get<double>(), source.at("height").get<double>()};
            require_positive(container.pyramid.length, "length");
            require_positive(container.pyramid.width, "width");
            require_positive(container.pyramid.height, "height");
        } else if (container.feature_kind == FeatureKind::Wedge) {
            container.wedge = {source.at("length").get<double>(),
                source.at("width").get<double>(), source.at("height").get<double>(),
                source.at("top_offset").get<double>()};
            require_positive(container.wedge.length, "length");
            require_positive(container.wedge.width, "width");
            require_positive(container.wedge.height, "height");
            if (!std::isfinite(container.wedge.top_offset) ||
                container.wedge.top_offset < 0.0 ||
                container.wedge.top_offset > container.wedge.length) {
                throw std::runtime_error("Invalid Wedge top offset");
            }
        } else if (container.feature_kind == FeatureKind::Extrusion) {
            container.extrusion.sketch_id = source.at("sketch_id").get<std::string>();
            container.extrusion.profile_source = source.at("profile_source") == "internal"
                ? ProfileSource::Internal : source.at("profile_source") == "external"
                    ? ProfileSource::External
                    : throw std::runtime_error("Invalid profile source");
            container.extrusion.result_type = source.at("result_type") == "solid"
                ? ProfileResultType::Solid : source.at("result_type") == "thin"
                    ? ProfileResultType::Thin
                    : throw std::runtime_error("Invalid profile result type");
            container.extrusion.thin_thickness = source.at("thin_thickness");
            container.extrusion.thin_mode = source.at("thin_mode") == "one_side"
                ? ThinMode::OneSide : source.at("thin_mode") == "other_side"
                    ? ThinMode::OtherSide : source.at("thin_mode") == "symmetric"
                        ? ThinMode::Symmetric
                        : throw std::runtime_error("Invalid thin mode");
            container.extrusion.extent_mode = source.at("extent_mode") == "one_side"
                ? ProfileExtentMode::OneSide : source.at("extent_mode") == "two_sides"
                    ? ProfileExtentMode::TwoSides : source.at("extent_mode") == "symmetric"
                        ? ProfileExtentMode::Symmetric
                        : throw std::runtime_error("Invalid profile extent mode");
            container.extrusion.length_forward = source.at("length_forward");
            container.extrusion.length_reverse = source.at("length_reverse");
            const auto parse_condition = [](const nlohmann::json& value) {
                return value == "length" ? EndCondition::Length
                    : value == "up_to" ? EndCondition::UpTo
                    : value == "through_all" ? EndCondition::ThroughAll
                    : throw std::runtime_error("Invalid profile end condition");
            };
            container.extrusion.end_condition_forward =
                parse_condition(source.at("end_condition_forward"));
            container.extrusion.end_condition_reverse =
                parse_condition(source.at("end_condition_reverse"));
            const auto parse_targets = [](const nlohmann::json& values) {
                std::vector<ExtrusionParameters::EndTarget> result;
                for (const auto& value : values) {
                    ExtrusionParameters::EndTarget target;
                    target.kind = value.at("kind") == "point" ? EndTargetKind::Point
                        : value.at("kind") == "plane" ? EndTargetKind::Plane
                        : value.at("kind") == "face" ? EndTargetKind::Face
                        : throw std::runtime_error("Invalid extrusion end target kind");
                    target.reference = {value.at("owner"), value.at("key"),
                                        value.at("instance_path")};
                    target.label = value.at("label");
                    target.fallback_origin = {value.at("origin").at(0),
                        value.at("origin").at(1), value.at("origin").at(2)};
                    target.fallback_normal = {value.at("normal").at(0),
                        value.at("normal").at(1), value.at("normal").at(2)};
                    for (const auto& point : value.at("triangles")) {
                        target.fallback_triangles.push_back(
                            {point.at(0), point.at(1), point.at(2)});
                    }
                    if (!target.reference.valid()) {
                        throw std::runtime_error("Invalid extrusion end target reference");
                    }
                    result.push_back(std::move(target));
                }
                return result;
            };
            container.extrusion.end_targets_forward =
                parse_targets(source.at("end_targets_forward"));
            container.extrusion.end_targets_reverse =
                parse_targets(source.at("end_targets_reverse"));
            require_positive(container.extrusion.thin_thickness, "thin thickness");
            require_positive(container.extrusion.length_forward, "forward length");
            require_positive(container.extrusion.length_reverse, "reverse length");
            container.extrusion.height = source.at("height").get<double>();
            const std::string direction =
                source.at("direction").get<std::string>();
            if (direction == "forward") {
                container.extrusion.direction = ExtrusionDirection::Forward;
            } else if (direction == "reverse") {
                container.extrusion.direction = ExtrusionDirection::Reverse;
            } else if (direction == "symmetric") {
                container.extrusion.direction = ExtrusionDirection::Symmetric;
            } else {
                throw std::runtime_error("Invalid Extrusion direction");
            }
            if (container.extrusion.sketch_id.empty()) {
                throw std::runtime_error("Extrusion Sketch ID is required");
            }
            require_positive(container.extrusion.height, "extrusion height");
            validate_extrusion_direction(container.extrusion.direction);
            const std::string extent = source.at("extent").get<std::string>();
            container.extrusion.extent = extent == "up_to_plane"
                ? ExtrusionExtent::UpToPlane
                : extent == "up_to_surface" ? ExtrusionExtent::UpToSurface
                : extent == "through_all" ? ExtrusionExtent::ThroughAll
                : extent == "blind" ? ExtrusionExtent::Blind
                : throw std::runtime_error("Invalid Extrusion extent");
            if (container.extrusion.extent == ExtrusionExtent::UpToPlane) {
                if (container.extrusion.direction == ExtrusionDirection::Symmetric) {
                    throw std::runtime_error(
                        "Up-to-plane Extrusion requires forward or reverse direction");
                }
                container.extrusion.target_face = {
                    source.at("target_owner").get<std::string>(),
                    source.at("target_key").get<std::string>(),
                    source.at("target_path").get<std::string>()};
                container.extrusion.target_plane_origin = {
                    source.at("target_origin_x").get<double>(),
                    source.at("target_origin_y").get<double>(),
                    source.at("target_origin_z").get<double>()};
                container.extrusion.target_plane_normal = {
                    source.at("target_normal_x").get<double>(),
                    source.at("target_normal_y").get<double>(),
                    source.at("target_normal_z").get<double>()};
                const auto& normal = container.extrusion.target_plane_normal;
                const double normal_length = std::sqrt(
                    normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
                if (!container.extrusion.target_face.valid() || normal_length <= 1e-12) {
                    throw std::runtime_error("Invalid Extrusion target plane");
                }
            }
            if (container.extrusion.extent == ExtrusionExtent::UpToSurface) {
                container.extrusion.target_face = {
                    source.at("target_owner").get<std::string>(),
                    source.at("target_key").get<std::string>(),
                    source.at("target_path").get<std::string>()};
                for (const auto& point : source.at("target_triangles")) {
                    container.extrusion.target_surface_triangles.push_back({
                        point.at(0).get<double>(), point.at(1).get<double>(),
                        point.at(2).get<double>()});
                }
                if (!container.extrusion.target_face.valid() ||
                    container.extrusion.target_surface_triangles.empty() ||
                    container.extrusion.target_surface_triangles.size() % 3 != 0) {
                    throw std::runtime_error("Invalid Extrusion target surface");
                }
            }
            if (container.extrusion.extent == ExtrusionExtent::ThroughAll &&
                container.combine_mode != CombineMode::Subtract) {
                throw std::runtime_error("Through-all Extrusion must subtract");
            }
        } else if (container.feature_kind == FeatureKind::Revolution) {
            container.revolution.sketch_id =
                source.at("sketch_id").get<std::string>();
            container.revolution.profile_source = source.at("profile_source") == "internal"
                ? ProfileSource::Internal : source.at("profile_source") == "external"
                    ? ProfileSource::External
                    : throw std::runtime_error("Invalid Revolution profile source");
            container.revolution.result_type = source.at("result_type") == "solid"
                ? ProfileResultType::Solid : source.at("result_type") == "thin"
                    ? ProfileResultType::Thin
                    : throw std::runtime_error("Invalid Revolution result type");
            container.revolution.thin_thickness = source.at("thin_thickness");
            const std::string thin_mode = source.at("thin_mode");
            container.revolution.thin_mode = thin_mode == "one_side"
                ? ThinMode::OneSide : thin_mode == "other_side"
                    ? ThinMode::OtherSide : thin_mode == "symmetric"
                        ? ThinMode::Symmetric
                        : throw std::runtime_error("Invalid Revolution thin mode");
            const std::string extent_mode = source.at("extent_mode");
            container.revolution.extent_mode = extent_mode == "one_side"
                ? ProfileExtentMode::OneSide : extent_mode == "two_sides"
                    ? ProfileExtentMode::TwoSides : extent_mode == "symmetric"
                        ? ProfileExtentMode::Symmetric
                        : throw std::runtime_error("Invalid Revolution extent mode");
            const std::string direction = source.at("direction");
            container.revolution.direction = direction == "forward"
                ? ExtrusionDirection::Forward : direction == "reverse"
                    ? ExtrusionDirection::Reverse
                    : throw std::runtime_error("Invalid Revolution direction");
            container.revolution.angle_reverse = source.at("angle_reverse");
            require_positive(container.revolution.thin_thickness, "Revolution thin thickness");
            require_positive(container.revolution.angle_reverse, "reverse angle");
            container.revolution.angle_degrees =
                source.at("angle_degrees").get<double>();
            const std::string axis = source.at("axis").get<std::string>();
            if (axis == "sketch_x") {
                container.revolution.axis = RevolutionAxis::SketchX;
            } else if (axis == "sketch_y") {
                container.revolution.axis = RevolutionAxis::SketchY;
            } else {
                throw std::runtime_error("Invalid Revolution axis");
            }
            if (container.revolution.sketch_id.empty() ||
                !std::isfinite(container.revolution.angle_degrees) ||
                container.revolution.angle_degrees <= 0.0 ||
                container.revolution.angle_degrees > 360.0 ||
                container.revolution.angle_degrees +
                    (container.revolution.extent_mode == ProfileExtentMode::OneSide
                        ? 0.0
                        : container.revolution.extent_mode == ProfileExtentMode::Symmetric
                            ? container.revolution.angle_degrees
                            : container.revolution.angle_reverse) > 360.0) {
                throw std::runtime_error("Invalid Revolution parameters");
            }
        } else if (container.feature_kind == FeatureKind::ImportedStep) {
            container.imported_step.source_path = source.at("source_path").get<std::string>();
            container.imported_step.component_path =
                source.at("component_path").get<std::string>();
            if (container.imported_step.source_path.empty() ||
                container.combine_mode != CombineMode::Add) {
                throw std::runtime_error("Invalid imported STEP parameters");
            }
        } else {
            for (const auto& edge : source.at("edges")) {
                container.edge_treatment.edges.push_back({
                    edge.at("owner").get<std::string>(),
                    edge.at("key").get<std::string>(), {}});
            }
            const std::string origin = source.at("edge_origin").get<std::string>();
            if (origin == "original_entity") {
                container.edge_treatment.origin =
                    zima::kernel::EdgeSelectionOrigin::OriginalEntity;
            } else if (origin == "operational_body") {
                container.edge_treatment.origin =
                    zima::kernel::EdgeSelectionOrigin::OperationalBody;
            } else {
                throw std::runtime_error("Invalid edge treatment origin");
            }
            container.edge_treatment.size = source.at("size").get<double>();
            if (container.edge_treatment.edges.empty() ||
                std::any_of(container.edge_treatment.edges.begin(),
                    container.edge_treatment.edges.end(), [](const auto& edge) {
                        return !edge.valid() || !edge.instance_path.empty();
                    }) ||
                !std::isfinite(container.edge_treatment.size) ||
                container.edge_treatment.size <= 0.0 ||
                container.combine_mode != CombineMode::Add) {
                throw std::runtime_error("Invalid Fillet/Chamfer parameters");
            }
        }
        if (source.contains("placement")) {
            const auto& placement = source.at("placement");
            container.placement.x = placement.value("x", 0.0);
            container.placement.y = placement.value("y", 0.0);
            container.placement.z = placement.value("z", 0.0);
            container.placement.rotation_x = placement.value("rotation_x", 0.0);
            container.placement.rotation_y = placement.value("rotation_y", 0.0);
            container.placement.rotation_z = placement.value("rotation_z", 0.0);
            container.placement.rotation_offset_x =
                placement.value("rotation_offset_x", 0.0);
            container.placement.rotation_offset_y =
                placement.value("rotation_offset_y", 0.0);
            container.placement.rotation_offset_z =
                placement.value("rotation_offset_z", 0.0);
            container.placement.reference_valid =
                placement.value("reference_valid", false);
            if (placement.contains("references")) {
                for (const auto& serialized : placement.at("references")) {
                    container.placement.references.push_back({
                        serialized.at("instance_path").get<std::string>(),
                        serialized.at("owner_id").get<std::string>(),
                        serialized.at("semantic_key").get<std::string>(),
                        serialized.at("offset").get<double>(),
                        serialized.at("supports_offset").get<bool>(),
                        serialized.at("orientation_role").get<std::string>(),
                        serialized.at("orientation_drives_rotation").get<bool>()});
                }
            }
        }
        validate_placement(container.placement);
        if (container.feature_kind == FeatureKind::Fillet ||
            container.feature_kind == FeatureKind::Chamfer) {
            require_default_sketch_feature_placement(container.placement);
        }
        document.history.push_back(std::move(container));
    }
    std::unordered_set<std::string> sketch_ids;
    for (const auto& source : root.at("sketches")) {
        auto sketch = zima::sketcher::Sketch::from_serialized(source.dump());
        if (!sketch_ids.insert(sketch.id).second) {
            throw std::runtime_error("Sketch IDs must be unique in a Part");
        }
        document.sketches.push_back(std::move(sketch));
    }
    std::unordered_set<std::string> construction_ids;
    for (const auto& source : root.at("constructions")) {
        ConstructionObject object;
        object.id = source.at("id").get<std::string>();
        object.entity_id = source.at("entity_id").get<std::string>();
        object.entity_parent_id =
            source.at("entity_parent_id").get<std::string>();
        object.name = source.at("name").get<std::string>();
        const auto type = source.at("type").get<std::string>();
        object.kind = type == "point" ? ConstructionKind::Point
            : type == "axis" ? ConstructionKind::Axis
            : type == "plane" ? ConstructionKind::Plane
                               : throw std::runtime_error("Invalid construction type");
        const auto& serialized_origin = source.at("container_origin");
        object.container_origin.id = serialized_origin.at("id").get<std::string>();
        object.container_origin.parent_id =
            serialized_origin.at("parent_id").get<std::string>();
        object.container_origin.name = serialized_origin.at("name").get<std::string>();
        object.container_origin.locked = serialized_origin.at("locked").get<bool>();
        for (const auto& serialized_child : serialized_origin.at("children")) {
            const auto kind = serialized_child.at("kind").get<std::string>();
            object.container_origin.children.push_back({
                serialized_child.at("id").get<std::string>(),
                serialized_child.at("parent_id").get<std::string>(),
                serialized_child.at("name").get<std::string>(),
                kind == "point" ? OriginChildKind::Point
                    : kind == "axis" ? OriginChildKind::Axis
                    : kind == "plane" ? OriginChildKind::Plane
                    : throw std::runtime_error("Invalid Container Origin child kind"),
                serialized_child.at("key").get<std::string>(),
                serialized_child.at("locked").get<bool>()});
        }
        if (object.container_origin != create_container_origin(object.id) ||
            object.entity_id.empty() || object.entity_id == object.id ||
            (object.kind == ConstructionKind::Point &&
             object.entity_id != object.container_origin.id + ":point") ||
            (object.kind != ConstructionKind::Point &&
             object.entity_id != object.id + ":entity") ||
            object.entity_parent_id != (object.kind == ConstructionKind::Point
                ? object.container_origin.id : object.id)) {
            throw std::runtime_error("Invalid construction Container Origin");
        }
        object.origin = {source.at("x").get<double>(),
                         source.at("y").get<double>(),
                         source.at("z").get<double>()};
        object.rotation = {source.at("rotation_x").get<double>(),
                           source.at("rotation_y").get<double>(),
                           source.at("rotation_z").get<double>()};
        object.direction = {source.at("direction_x").get<double>(),
                            source.at("direction_y").get<double>(),
                            source.at("direction_z").get<double>()};
        object.display_size = source.at("display_size").get<double>();
        const auto definition = source.at("definition").get<std::string>();
        object.definition = definition == "absolute" ? ConstructionDefinition::Absolute
            : definition == "point_reference" ? ConstructionDefinition::PointReference
            : definition == "two_point_axis" ? ConstructionDefinition::TwoPointAxis
            : definition == "axis_reference" ? ConstructionDefinition::AxisReference
            : definition == "three_point_plane" ? ConstructionDefinition::ThreePointPlane
            : definition == "plane_reference" ? ConstructionDefinition::PlaneReference
            : throw std::runtime_error("Invalid construction definition");
        object.offset = source.at("offset").get<double>();
        object.reference_valid = source.at("reference_valid").get<bool>();
        object.suppressed = source.at("suppressed").get<bool>();
        for (const auto& serialized : source.at("references")) {
            object.references.push_back({
                serialized.at("instance_path").get<std::string>(),
                serialized.at("owner_id").get<std::string>(),
                serialized.at("semantic_key").get<std::string>(),
                serialized.at("offset").get<double>(),
                serialized.at("supports_offset").get<bool>(),
                serialized.at("orientation_role").get<std::string>(),
                serialized.at("orientation_drives_rotation").get<bool>()});
        }
        const double direction_length = std::sqrt(
            object.direction.x * object.direction.x +
            object.direction.y * object.direction.y +
            object.direction.z * object.direction.z);
        if (object.id.empty() || object.entity_id.empty() ||
            object.entity_id == object.id || object.name.empty() ||
            !construction_ids.insert(object.id).second ||
            !std::isfinite(object.origin.x) || !std::isfinite(object.origin.y) ||
            !std::isfinite(object.origin.z) ||
            !std::isfinite(object.rotation.x) ||
            !std::isfinite(object.rotation.y) ||
            !std::isfinite(object.rotation.z) ||
            !std::isfinite(direction_length) ||
            (object.kind != ConstructionKind::Point && direction_length <= 0.0) ||
            !std::isfinite(object.display_size) || object.display_size <= 0.0) {
            throw std::runtime_error("Invalid construction object");
        }
        document.constructions.push_back(std::move(object));
    }
    std::unordered_set<std::string> ordered_ids;
    for (const auto& serialized : root.at("history_order")) {
        const std::string kind = serialized.at("kind").get<std::string>();
        PartHistoryEntry entry{
            kind == "feature" ? PartHistoryKind::Feature
                : kind == "sketch" ? PartHistoryKind::Sketch
                : kind == "construction" ? PartHistoryKind::Construction
                : throw std::runtime_error("Invalid Part history entry kind"),
            serialized.at("id").get<std::string>()};
        const bool exists = entry.kind == PartHistoryKind::Feature
            ? std::any_of(document.history.begin(), document.history.end(),
                [&](const auto& value) { return value.id == entry.id; })
            : entry.kind == PartHistoryKind::Sketch
                ? std::any_of(document.sketches.begin(), document.sketches.end(),
                    [&](const auto& value) { return value.id == entry.id; })
                : std::any_of(document.constructions.begin(),
                    document.constructions.end(),
                    [&](const auto& value) { return value.id == entry.id; });
        if (!exists || !ordered_ids.insert(entry.id).second) {
            throw std::runtime_error("Invalid or duplicate Part history entry");
        }
        document.history_order.push_back(std::move(entry));
    }
    if (ordered_ids.size() != document.history.size() + document.sketches.size() +
            document.constructions.size()) {
        throw std::runtime_error("Part history order does not cover every container");
    }
    document.history_cursor = root.at("history_cursor").get<std::size_t>();
    if (document.history_cursor > document.history_order.size()) {
        throw std::runtime_error("Part history cursor is outside history");
    }
    for (const auto& container : document.history) {
        if (container.feature_kind == FeatureKind::Extrusion &&
            std::none_of(document.sketches.begin(), document.sketches.end(),
                [&](const auto& sketch) {
                    return sketch.id == container.extrusion.sketch_id;
                })) {
            throw std::runtime_error("Extrusion references a missing Sketch");
        }
        if (container.feature_kind == FeatureKind::Revolution &&
            std::none_of(document.sketches.begin(), document.sketches.end(),
                [&](const auto& sketch) {
                    return sketch.id == container.revolution.sketch_id;
                })) {
            throw std::runtime_error("Revolution references a missing Sketch");
        }
    }
    const auto first_active = std::find_if(document.history.begin(),
        document.history.end(), [](const auto& container) {
            return !container.suppressed;
        });
    if (first_active != document.history.end() &&
        first_active->combine_mode == CombineMode::Subtract) {
        throw std::runtime_error("The first history container cannot subtract");
    }
    std::vector<zima::kernel::BodyResult> loaded_boundaries;
    for (const auto& boundary : root.at("calculated_boundaries")) {
        loaded_boundaries.push_back(load_body_result(boundary));
    }
    if (!loaded_boundaries.empty() &&
        loaded_boundaries.size() != document.history.size()) {
        throw std::runtime_error(
            "Calculated history boundaries do not match document history");
    }
    std::unordered_set<std::string> available_owners;
    const auto expected_operations = document.kernel_operations();
    for (std::size_t boundary_index = 0;
         boundary_index < loaded_boundaries.size(); ++boundary_index) {
        available_owners.insert(document.history[boundary_index].id);
        if (loaded_boundaries[boundary_index].source_fingerprint !=
            zima::kernel::history_fingerprint(
                expected_operations, boundary_index + 1)) {
            throw std::runtime_error(
                "Calculated history boundary does not match its parameters");
        }
        const auto validate_reference = [&](const auto& reference, bool may_be_invalid) {
            const bool owner_empty = reference.owner_id.empty();
            const bool key_empty = reference.semantic_key.empty();
            if (owner_empty != key_empty || (!may_be_invalid && owner_empty) ||
                (!owner_empty && !available_owners.contains(reference.owner_id)) ||
                !reference.instance_path.empty()) {
                throw std::runtime_error(
                    "Calculated topology reference has invalid owner/key: owner='" +
                    reference.owner_id + "', key='" + reference.semantic_key + "'");
            }
        };
        const auto& mesh = loaded_boundaries[boundary_index].mesh;
        for (const auto& reference : mesh.triangle_references) {
            validate_reference(reference, true);
        }
        for (const auto& edge : mesh.edges) {
            validate_reference(edge.reference, true);
        }
        for (const auto& point : mesh.points) {
            validate_reference(point.reference, false);
        }
        for (const auto& axis : mesh.axes) {
            validate_reference(axis.reference, false);
        }
        for (const auto& dimension : mesh.dimensions) {
            validate_reference(dimension.reference, false);
        }
        const auto& references = mesh.original_references;
        if (references.triangles.size() % 3 != 0 ||
            references.triangle_references.size() != references.triangles.size() / 3) {
            throw std::runtime_error(
                "Calculated original-reference triangles are not aligned");
        }
        for (const auto index : references.triangles) {
            if (index >= references.vertices.size()) {
                throw std::runtime_error(
                    "Calculated original-reference triangle is invalid");
            }
        }
        for (const auto& reference : references.triangle_references) {
            validate_reference(reference, false);
        }
        for (const auto& edge : references.edges) {
            validate_reference(edge.reference, false);
        }
        for (const auto& point : references.points) {
            validate_reference(point.reference, false);
        }
        for (const auto& axis : references.axes) {
            validate_reference(axis.reference, false);
        }
    }
    if (calculated_boundaries != nullptr) {
        *calculated_boundaries = std::move(loaded_boundaries);
    }
    return document;
}

void PartDocument::save(
    const std::filesystem::path& path,
    const std::vector<zima::kernel::BodyResult>& calculated_boundaries) const {
    nlohmann::json serialized_history = nlohmann::json::array();
    std::unordered_set<std::string> container_ids;
    for (const auto& container : history) {
        if (container.id.empty() || !container_ids.insert(container.id).second) {
            throw std::runtime_error("History container IDs must be non-empty and unique");
        }
        if (container.name.empty()) {
            throw std::runtime_error("History container name must not be empty");
        }
        if (container.feature_id.empty() || container.feature_id == container.id ||
            container.feature_parent_id != container.id ||
            container.container_origin != create_container_origin(container.id)) {
            throw std::runtime_error("History container hierarchy is invalid");
        }
        if (container.feature_kind == FeatureKind::Box) {
            require_positive(container.box.length, "length");
            require_positive(container.box.width, "width");
            require_positive(container.box.height, "height");
        } else if (container.feature_kind == FeatureKind::Cylinder) {
            require_positive(container.cylinder.radius, "radius");
            require_positive(container.cylinder.height, "height");
        } else if (container.feature_kind == FeatureKind::Sphere) {
            require_positive(container.sphere.radius, "radius");
        } else if (container.feature_kind == FeatureKind::Cone) {
            if (!std::isfinite(container.cone.bottom_radius) ||
                !std::isfinite(container.cone.top_radius) ||
                container.cone.bottom_radius < 0.0 || container.cone.top_radius < 0.0 ||
                (container.cone.bottom_radius <= 0.0 && container.cone.top_radius <= 0.0)) {
                throw std::runtime_error("Invalid Cone radii");
            }
            require_positive(container.cone.height, "height");
        } else if (container.feature_kind == FeatureKind::Pyramid) {
            require_positive(container.pyramid.length, "length");
            require_positive(container.pyramid.width, "width");
            require_positive(container.pyramid.height, "height");
        } else if (container.feature_kind == FeatureKind::Wedge) {
            require_positive(container.wedge.length, "length");
            require_positive(container.wedge.width, "width");
            require_positive(container.wedge.height, "height");
            if (!std::isfinite(container.wedge.top_offset) ||
                container.wedge.top_offset < 0.0 ||
                container.wedge.top_offset > container.wedge.length) {
                throw std::runtime_error("Invalid Wedge top offset");
            }
        } else if (container.feature_kind == FeatureKind::Extrusion) {
            if (container.extrusion.sketch_id.empty() ||
                std::none_of(sketches.begin(), sketches.end(), [&](const auto& sketch) {
                    return sketch.id == container.extrusion.sketch_id;
                })) {
                throw std::runtime_error("Extrusion references a missing Sketch");
            }
            require_positive(container.extrusion.height, "extrusion height");
            validate_extrusion_direction(container.extrusion.direction);
            if (container.extrusion.extent == ExtrusionExtent::UpToPlane) {
                const auto& normal = container.extrusion.target_plane_normal;
                if (container.extrusion.direction == ExtrusionDirection::Symmetric ||
                    !container.extrusion.target_face.valid() ||
                    std::sqrt(normal.x * normal.x + normal.y * normal.y +
                              normal.z * normal.z) <= 1e-12) {
                    throw std::runtime_error("Invalid Extrusion target plane");
                }
            } else if (container.extrusion.extent == ExtrusionExtent::UpToSurface) {
                if (container.extrusion.direction == ExtrusionDirection::Symmetric ||
                    !container.extrusion.target_face.valid() ||
                    container.extrusion.target_surface_triangles.empty() ||
                    container.extrusion.target_surface_triangles.size() % 3 != 0) {
                    throw std::runtime_error("Invalid Extrusion target surface");
                }
            } else if (container.extrusion.extent == ExtrusionExtent::ThroughAll &&
                       container.combine_mode != CombineMode::Subtract) {
                throw std::runtime_error("Through-all Extrusion must subtract");
            }
        } else if (container.feature_kind == FeatureKind::Revolution) {
            if (container.revolution.sketch_id.empty() ||
                std::none_of(sketches.begin(), sketches.end(), [&](const auto& sketch) {
                    return sketch.id == container.revolution.sketch_id;
                }) || (container.revolution.axis != RevolutionAxis::SketchX &&
                       container.revolution.axis != RevolutionAxis::SketchY) ||
                !std::isfinite(container.revolution.angle_degrees) ||
                container.revolution.angle_degrees <= 0.0 ||
                container.revolution.angle_degrees > 360.0 ||
                !std::isfinite(container.revolution.angle_reverse) ||
                container.revolution.angle_reverse <= 0.0 ||
                container.revolution.angle_degrees +
                    (container.revolution.extent_mode == ProfileExtentMode::OneSide
                        ? 0.0
                        : container.revolution.extent_mode == ProfileExtentMode::Symmetric
                            ? container.revolution.angle_degrees
                            : container.revolution.angle_reverse) > 360.0) {
                throw std::runtime_error("Invalid Revolution parameters");
            }
        } else if (container.feature_kind == FeatureKind::ImportedStep) {
            if (container.imported_step.source_path.empty() ||
                container.combine_mode != CombineMode::Add) {
                throw std::runtime_error("Invalid imported STEP parameters");
            }
        } else if (container.edge_treatment.edges.empty() ||
                   std::any_of(container.edge_treatment.edges.begin(),
                       container.edge_treatment.edges.end(), [](const auto& edge) {
                           return !edge.valid() || !edge.instance_path.empty();
                       }) ||
                   !std::isfinite(container.edge_treatment.size) ||
                   container.edge_treatment.size <= 0.0 ||
                   container.combine_mode != CombineMode::Add) {
            throw std::runtime_error("Invalid Fillet/Chamfer parameters");
        }
        validate_placement(container.placement);
        if (container.feature_kind == FeatureKind::Fillet ||
            container.feature_kind == FeatureKind::Chamfer) {
            require_default_sketch_feature_placement(container.placement);
        }
        nlohmann::json serialized = {
            {"id", container.id},
            {"feature_id", container.feature_id},
            {"feature_parent_id", container.feature_parent_id},
            {"type", container.feature_kind == FeatureKind::Box ? "box"
                : container.feature_kind == FeatureKind::Cylinder
                    ? "cylinder"
                : container.feature_kind == FeatureKind::Sphere
                    ? "sphere"
                : container.feature_kind == FeatureKind::Cone
                    ? "cone"
                : container.feature_kind == FeatureKind::Pyramid
                    ? "pyramid"
                : container.feature_kind == FeatureKind::Wedge
                    ? "wedge"
                : container.feature_kind == FeatureKind::Extrusion
                    ? "extrusion"
                : container.feature_kind == FeatureKind::Revolution
                    ? "revolution"
                : container.feature_kind == FeatureKind::ImportedStep
                    ? "imported_step"
                : container.feature_kind == FeatureKind::Fillet
                    ? "fillet" : "chamfer"},
            {"name", container.name},
            {"combine", container.combine_mode == CombineMode::Subtract
                ? "subtract" : "add"}, {"suppressed", container.suppressed},
            {"container_origin", {
                {"id", container.container_origin.id},
                {"parent_id", container.container_origin.parent_id},
                {"name", container.container_origin.name},
                {"locked", container.container_origin.locked},
                {"children", nlohmann::json::array()},
            }},
        };
        for (const auto& child : container.container_origin.children) {
            serialized["container_origin"]["children"].push_back({
                {"id", child.id}, {"parent_id", child.parent_id},
                {"name", child.name},
                {"kind", child.kind == OriginChildKind::Point ? "point"
                    : child.kind == OriginChildKind::Axis ? "axis" : "plane"},
                {"key", child.key}, {"locked", child.locked},
            });
        }
        if (container.feature_kind != FeatureKind::Fillet &&
            container.feature_kind != FeatureKind::Chamfer) {
            nlohmann::json placement_references = nlohmann::json::array();
            for (const auto& reference : container.placement.references) {
                if (reference.owner_id.empty() || reference.semantic_key.empty()) {
                    throw std::runtime_error("Invalid placement reference");
                }
                placement_references.push_back(
                    {{"instance_path", reference.instance_path},
                        {"owner_id", reference.owner_id},
                        {"semantic_key", reference.semantic_key},
                        {"offset", reference.offset},
                        {"supports_offset", reference.supports_offset},
                        {"orientation_role", reference.orientation_role},
                        {"orientation_drives_rotation",
                            reference.orientation_drives_rotation}});
            }
            serialized["placement"] = {
                {"x", container.placement.x},
                {"y", container.placement.y},
                {"z", container.placement.z},
                {"rotation_x", container.placement.rotation_x},
                {"rotation_y", container.placement.rotation_y},
                {"rotation_z", container.placement.rotation_z},
                {"rotation_offset_x", container.placement.rotation_offset_x},
                {"rotation_offset_y", container.placement.rotation_offset_y},
                {"rotation_offset_z", container.placement.rotation_offset_z},
                {"reference_valid", container.placement.reference_valid},
                {"references", std::move(placement_references)},
            };
        }
        if (container.feature_kind == FeatureKind::Box) {
            serialized["length"] = container.box.length;
            serialized["width"] = container.box.width;
            serialized["height"] = container.box.height;
        } else if (container.feature_kind == FeatureKind::Cylinder) {
            serialized["radius"] = container.cylinder.radius;
            serialized["height"] = container.cylinder.height;
        } else if (container.feature_kind == FeatureKind::Sphere) {
            serialized["radius"] = container.sphere.radius;
        } else if (container.feature_kind == FeatureKind::Cone) {
            serialized["bottom_radius"] = container.cone.bottom_radius;
            serialized["top_radius"] = container.cone.top_radius;
            serialized["height"] = container.cone.height;
        } else if (container.feature_kind == FeatureKind::Pyramid) {
            serialized["length"] = container.pyramid.length;
            serialized["width"] = container.pyramid.width;
            serialized["height"] = container.pyramid.height;
        } else if (container.feature_kind == FeatureKind::Wedge) {
            serialized["length"] = container.wedge.length;
            serialized["width"] = container.wedge.width;
            serialized["height"] = container.wedge.height;
            serialized["top_offset"] = container.wedge.top_offset;
        } else if (container.feature_kind == FeatureKind::Extrusion) {
            serialized["sketch_id"] = container.extrusion.sketch_id;
            serialized["profile_source"] = container.extrusion.profile_source ==
                    ProfileSource::Internal ? "internal" : "external";
            serialized["result_type"] = container.extrusion.result_type ==
                    ProfileResultType::Solid ? "solid" : "thin";
            serialized["thin_thickness"] = container.extrusion.thin_thickness;
            serialized["thin_mode"] = container.extrusion.thin_mode == ThinMode::OneSide
                ? "one_side" : container.extrusion.thin_mode == ThinMode::OtherSide
                    ? "other_side" : "symmetric";
            serialized["extent_mode"] = container.extrusion.extent_mode ==
                    ProfileExtentMode::OneSide ? "one_side"
                : container.extrusion.extent_mode == ProfileExtentMode::TwoSides
                    ? "two_sides" : "symmetric";
            serialized["length_forward"] = container.extrusion.length_forward;
            serialized["length_reverse"] = container.extrusion.length_reverse;
            const auto condition_name = [](EndCondition condition) {
                return condition == EndCondition::Length ? "length"
                    : condition == EndCondition::UpTo ? "up_to" : "through_all";
            };
            serialized["end_condition_forward"] =
                condition_name(container.extrusion.end_condition_forward);
            serialized["end_condition_reverse"] =
                condition_name(container.extrusion.end_condition_reverse);
            const auto target_json = [](const auto& targets) {
                nlohmann::json result = nlohmann::json::array();
                for (const auto& target : targets) {
                    nlohmann::json triangles = nlohmann::json::array();
                    for (const auto& point : target.fallback_triangles) {
                        triangles.push_back({point.x, point.y, point.z});
                    }
                    result.push_back({{"kind", target.kind == EndTargetKind::Point
                            ? "point" : target.kind == EndTargetKind::Plane
                                ? "plane" : "face"},
                        {"owner", target.reference.owner_id},
                        {"key", target.reference.semantic_key},
                        {"instance_path", target.reference.instance_path},
                        {"label", target.label},
                        {"origin", {target.fallback_origin.x,
                            target.fallback_origin.y, target.fallback_origin.z}},
                        {"normal", {target.fallback_normal.x,
                            target.fallback_normal.y, target.fallback_normal.z}},
                        {"triangles", std::move(triangles)}});
                }
                return result;
            };
            serialized["end_targets_forward"] =
                target_json(container.extrusion.end_targets_forward);
            serialized["end_targets_reverse"] =
                target_json(container.extrusion.end_targets_reverse);
            serialized["height"] = container.extrusion.height;
            serialized["direction"] =
                container.extrusion.direction == ExtrusionDirection::Forward
                    ? "forward"
                : container.extrusion.direction == ExtrusionDirection::Reverse
                    ? "reverse" : "symmetric";
            serialized["extent"] = container.extrusion.extent == ExtrusionExtent::UpToPlane
                ? "up_to_plane"
                : container.extrusion.extent == ExtrusionExtent::UpToSurface
                    ? "up_to_surface"
                : container.extrusion.extent == ExtrusionExtent::ThroughAll
                    ? "through_all" : "blind";
            serialized["target_owner"] = container.extrusion.target_face.owner_id;
            serialized["target_key"] = container.extrusion.target_face.semantic_key;
            serialized["target_path"] = container.extrusion.target_face.instance_path;
            serialized["target_origin_x"] = container.extrusion.target_plane_origin.x;
            serialized["target_origin_y"] = container.extrusion.target_plane_origin.y;
            serialized["target_origin_z"] = container.extrusion.target_plane_origin.z;
            serialized["target_normal_x"] = container.extrusion.target_plane_normal.x;
            serialized["target_normal_y"] = container.extrusion.target_plane_normal.y;
            serialized["target_normal_z"] = container.extrusion.target_plane_normal.z;
            serialized["target_triangles"] = nlohmann::json::array();
            for (const auto& point : container.extrusion.target_surface_triangles) {
                serialized["target_triangles"].push_back({point.x, point.y, point.z});
            }
        } else if (container.feature_kind == FeatureKind::Revolution) {
            serialized["sketch_id"] = container.revolution.sketch_id;
            serialized["profile_source"] = container.revolution.profile_source ==
                    ProfileSource::Internal ? "internal" : "external";
            serialized["result_type"] = container.revolution.result_type ==
                    ProfileResultType::Solid ? "solid" : "thin";
            serialized["thin_thickness"] = container.revolution.thin_thickness;
            serialized["thin_mode"] = container.revolution.thin_mode == ThinMode::OneSide
                ? "one_side" : container.revolution.thin_mode == ThinMode::OtherSide
                    ? "other_side" : "symmetric";
            serialized["extent_mode"] = container.revolution.extent_mode ==
                    ProfileExtentMode::OneSide ? "one_side"
                : container.revolution.extent_mode == ProfileExtentMode::TwoSides
                    ? "two_sides" : "symmetric";
            serialized["direction"] = container.revolution.direction ==
                    ExtrusionDirection::Forward ? "forward" : "reverse";
            serialized["angle_reverse"] = container.revolution.angle_reverse;
            serialized["axis"] = container.revolution.axis == RevolutionAxis::SketchX
                ? "sketch_x" : "sketch_y";
            serialized["angle_degrees"] = container.revolution.angle_degrees;
        } else if (container.feature_kind == FeatureKind::ImportedStep) {
            serialized["source_path"] = container.imported_step.source_path;
            serialized["component_path"] = container.imported_step.component_path;
        } else {
            serialized["edges"] = nlohmann::json::array();
            for (const auto& edge : container.edge_treatment.edges) {
                serialized["edges"].push_back({
                    {"owner", edge.owner_id}, {"key", edge.semantic_key}});
            }
            serialized["edge_origin"] = container.edge_treatment.origin ==
                    zima::kernel::EdgeSelectionOrigin::OriginalEntity
                ? "original_entity" : "operational_body";
            serialized["size"] = container.edge_treatment.size;
        }
        serialized_history.push_back(std::move(serialized));
    }
    const auto first_active = std::find_if(history.begin(), history.end(),
        [](const auto& container) { return !container.suppressed; });
    if (first_active != history.end() &&
        first_active->combine_mode == CombineMode::Subtract) {
        throw std::runtime_error("The first history container cannot subtract");
    }
    if (!calculated_boundaries.empty() &&
        calculated_boundaries.size() != history.size()) {
        throw std::runtime_error(
            "Calculated history boundaries do not match document history");
    }
    const auto expected_operations = kernel_operations();
    for (std::size_t index = 0; index < calculated_boundaries.size(); ++index) {
        if (calculated_boundaries[index].source_fingerprint !=
            zima::kernel::history_fingerprint(expected_operations, index + 1)) {
            throw std::runtime_error(
                "Calculated history boundary does not match its parameters");
        }
    }
    nlohmann::json serialized_boundaries = nlohmann::json::array();
    for (const auto& boundary : calculated_boundaries) {
        serialized_boundaries.push_back(serialize_body_result(boundary));
    }
    nlohmann::json serialized_sketches = nlohmann::json::array();
    std::unordered_set<std::string> sketch_ids;
    for (const auto& sketch : sketches) {
        if (sketch.id.empty() || !sketch_ids.insert(sketch.id).second) {
            throw std::runtime_error("Sketch IDs must be non-empty and unique in a Part");
        }
        serialized_sketches.push_back(nlohmann::json::parse(sketch.serialized()));
    }
    nlohmann::json serialized_constructions = nlohmann::json::array();
    std::unordered_set<std::string> construction_ids;
    for (const auto& object : constructions) {
        const double direction_length = std::sqrt(
            object.direction.x * object.direction.x +
            object.direction.y * object.direction.y +
            object.direction.z * object.direction.z);
        if (object.id.empty() || object.entity_id.empty() || object.name.empty() ||
            !construction_ids.insert(object.id).second ||
            !std::isfinite(object.origin.x) || !std::isfinite(object.origin.y) ||
            !std::isfinite(object.origin.z) ||
            !std::isfinite(object.rotation.x) ||
            !std::isfinite(object.rotation.y) ||
            !std::isfinite(object.rotation.z) ||
            !std::isfinite(direction_length) ||
            (object.kind != ConstructionKind::Point && direction_length <= 0.0) ||
            !std::isfinite(object.display_size) || object.display_size <= 0.0) {
            throw std::runtime_error("Invalid construction object");
        }
        if (object.container_origin != create_container_origin(object.id) ||
            (object.kind == ConstructionKind::Point &&
             object.entity_id != object.container_origin.id + ":point") ||
            (object.kind != ConstructionKind::Point &&
             object.entity_id != object.id + ":entity") ||
            object.entity_parent_id != (object.kind == ConstructionKind::Point
                ? object.container_origin.id : object.id)) {
            throw std::runtime_error("Invalid construction Container Origin");
        }
        nlohmann::json origin_children = nlohmann::json::array();
        for (const auto& child : object.container_origin.children) {
            origin_children.push_back({
                {"id", child.id}, {"parent_id", child.parent_id},
                {"name", child.name},
                {"kind", child.kind == OriginChildKind::Point ? "point"
                    : child.kind == OriginChildKind::Axis ? "axis" : "plane"},
                {"key", child.key}, {"locked", child.locked}});
        }
        nlohmann::json references = nlohmann::json::array();
        for (const auto& reference : object.references) {
            if (reference.owner_id.empty() || reference.semantic_key.empty()) {
                throw std::runtime_error("Invalid construction reference");
            }
            references.push_back({{"instance_path", reference.instance_path},
                {"owner_id", reference.owner_id},
                {"semantic_key", reference.semantic_key},
                {"offset", reference.offset},
                {"supports_offset", reference.supports_offset},
                {"orientation_role", reference.orientation_role},
                {"orientation_drives_rotation",
                    reference.orientation_drives_rotation}});
        }
        const auto definition = object.definition == ConstructionDefinition::Absolute
            ? "absolute"
            : object.definition == ConstructionDefinition::PointReference
                ? "point_reference"
            : object.definition == ConstructionDefinition::TwoPointAxis
                ? "two_point_axis"
            : object.definition == ConstructionDefinition::AxisReference
                ? "axis_reference"
            : object.definition == ConstructionDefinition::ThreePointPlane
                ? "three_point_plane" : "plane_reference";
        serialized_constructions.push_back({
            {"id", object.id}, {"entity_id", object.entity_id},
            {"entity_parent_id", object.entity_parent_id},
            {"name", object.name},
            {"type", object.kind == ConstructionKind::Point ? "point"
                : object.kind == ConstructionKind::Axis ? "axis" : "plane"},
            {"container_origin", {
                {"id", object.container_origin.id},
                {"parent_id", object.container_origin.parent_id},
                {"name", object.container_origin.name},
                {"locked", object.container_origin.locked},
                {"children", std::move(origin_children)}}},
            {"x", object.origin.x}, {"y", object.origin.y}, {"z", object.origin.z},
            {"rotation_x", object.rotation.x},
            {"rotation_y", object.rotation.y},
            {"rotation_z", object.rotation.z},
            {"direction_x", object.direction.x},
            {"direction_y", object.direction.y},
            {"direction_z", object.direction.z},
            {"display_size", object.display_size}, {"definition", definition},
            {"references", std::move(references)}, {"offset", object.offset},
            {"reference_valid", object.reference_valid},
            {"suppressed", object.suppressed}});
    }
    nlohmann::json serialized_relations = nlohmann::json::array();
    for (const auto& relation : relations) serialized_relations.push_back(
        {{"target", relation.target}, {"expression", relation.expression}});
    std::vector<PartHistoryEntry> effective_order = history_order;
    if (effective_order.empty()) {
        for (const auto& value : history) effective_order.push_back(
            {PartHistoryKind::Feature, value.id});
        for (const auto& value : sketches) effective_order.push_back(
            {PartHistoryKind::Sketch, value.id});
        for (const auto& value : constructions) effective_order.push_back(
            {PartHistoryKind::Construction, value.id});
    }
    nlohmann::json serialized_order = nlohmann::json::array();
    std::unordered_set<std::string> ordered_ids;
    for (const auto& entry : effective_order) {
        if (entry.id.empty() || !ordered_ids.insert(entry.id).second) {
            throw std::runtime_error("Part history order contains an invalid entry");
        }
        serialized_order.push_back({
            {"kind", entry.kind == PartHistoryKind::Feature ? "feature"
                : entry.kind == PartHistoryKind::Sketch ? "sketch"
                : "construction"}, {"id", entry.id}});
    }
    if (ordered_ids.size() != history.size() + sketches.size() +
            constructions.size()) {
        throw std::runtime_error("Part history order does not cover every container");
    }
    const nlohmann::json root = {
        {"format", "zima-cad-cpp"},
        {"format_version", 36},
        {"document_id", document_id},
        {"type", "part"},
        {"name", name},
        {"user_parameters", user_parameters},
        {"user_parameter_order", user_parameter_order},
        {"user_parameter_labels", user_parameter_labels},
        {"user_parameter_values", user_parameter_values},
        {"relations", std::move(serialized_relations)},
        {"document_units", document_units},
        {"document_precision", document_precision},
        {"physical_parameters", physical_parameters},
        {"physical_parameter_units", physical_parameter_units},
        {"material_parameter_descriptions", material_parameter_descriptions},
        {"family_table", family_table},
        {"history", std::move(serialized_history)},
        {"sketches", std::move(serialized_sketches)},
        {"constructions", std::move(serialized_constructions)},
        {"history_order", std::move(serialized_order)},
        {"history_cursor", std::min(history_cursor, effective_order.size())},
        {"calculated_boundaries", std::move(serialized_boundaries)},
    };
    write_part_ini(root, path);
}

}  // namespace zima::document
