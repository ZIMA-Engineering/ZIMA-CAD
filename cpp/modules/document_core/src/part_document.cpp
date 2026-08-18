#include <zima/document/part_document.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <random>
#include <stdexcept>
#include <unordered_set>

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

nlohmann::json serialize_vec3(const zima::kernel::Vec3& point) {
    return nlohmann::json::array({point.x, point.y, point.z});
}

zima::kernel::Vec3 load_vec3(const nlohmann::json& source) {
    if (!source.is_array() || source.size() != 3) {
        throw std::runtime_error("Viewer point must contain three coordinates");
    }
    zima::kernel::Vec3 point{
        source.at(0).get<double>(), source.at(1).get<double>(),
        source.at(2).get<double>()};
    require_finite(point.x, "viewer x");
    require_finite(point.y, "viewer y");
    require_finite(point.z, "viewer z");
    return point;
}

nlohmann::json serialize_body_result(const zima::kernel::BodyResult& result) {
    nlohmann::json vertices = nlohmann::json::array();
    for (const auto& point : result.mesh.vertices) vertices.push_back(serialize_vec3(point));
    nlohmann::json faces = nlohmann::json::array();
    for (const auto& reference : result.mesh.triangle_references) {
        faces.push_back({{"owner", reference.owner_id}, {"key", reference.semantic_key}});
    }
    nlohmann::json edges = nlohmann::json::array();
    for (const auto& edge : result.mesh.edges) {
        nlohmann::json points = nlohmann::json::array();
        for (const auto& point : edge.points) points.push_back(serialize_vec3(point));
        edges.push_back({
            {"owner", edge.reference.owner_id}, {"key", edge.reference.semantic_key},
            {"points", std::move(points)},
        });
    }
    nlohmann::json points = nlohmann::json::array();
    for (const auto& point : result.mesh.points) {
        points.push_back({
            {"owner", point.reference.owner_id}, {"key", point.reference.semantic_key},
            {"position", serialize_vec3(point.position)},
        });
    }
    return {
        {"volume", result.volume}, {"surface_area", result.surface_area},
        {"source_fingerprint", result.source_fingerprint},
        {"vertices", std::move(vertices)}, {"triangles", result.mesh.triangles},
        {"triangle_references", std::move(faces)},
        {"edges", std::move(edges)}, {"points", std::move(points)},
    };
}

zima::kernel::BodyResult load_body_result(const nlohmann::json& source) {
    zima::kernel::BodyResult result;
    result.volume = source.at("volume").get<double>();
    result.surface_area = source.at("surface_area").get<double>();
    result.source_fingerprint = source.at("source_fingerprint").get<std::string>();
    require_finite(result.volume, "calculated volume");
    require_finite(result.surface_area, "calculated surface area");
    for (const auto& point : source.at("vertices")) {
        result.mesh.vertices.push_back(load_vec3(point));
    }
    result.mesh.triangles =
        source.at("triangles").get<std::vector<std::uint32_t>>();
    for (const auto index : result.mesh.triangles) {
        if (index >= result.mesh.vertices.size()) {
            throw std::runtime_error("Viewer triangle index is out of range");
        }
    }
    for (const auto& reference : source.at("triangle_references")) {
        result.mesh.triangle_references.push_back({
            reference.at("owner").get<std::string>(),
            reference.at("key").get<std::string>(),
        });
    }
    if (result.mesh.triangles.size() % 3 != 0 ||
        result.mesh.triangle_references.size() != result.mesh.triangles.size() / 3) {
        throw std::runtime_error("Viewer triangle references are not aligned");
    }
    for (const auto& edge : source.at("edges")) {
        zima::kernel::ViewerEdge loaded;
        loaded.reference = {
            edge.at("owner").get<std::string>(), edge.at("key").get<std::string>()};
        for (const auto& point : edge.at("points")) {
            loaded.points.push_back(load_vec3(point));
        }
        if (!loaded.reference.valid() || loaded.points.size() < 2) {
            throw std::runtime_error("Persisted viewer edge is invalid");
        }
        result.mesh.edges.push_back(std::move(loaded));
    }
    for (const auto& point : source.at("points")) {
        zima::kernel::ViewerPoint loaded;
        loaded.reference = {
            point.at("owner").get<std::string>(), point.at("key").get<std::string>()};
        loaded.position = load_vec3(point.at("position"));
        if (!loaded.reference.valid()) {
            throw std::runtime_error("Persisted viewer point is invalid");
        }
        result.mesh.points.push_back(std::move(loaded));
    }
    return result;
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
    return container;
}

HistoryContainer PartDocument::create_cylinder_container() {
    HistoryContainer container;
    container.id = make_id();
    container.name = "Válec";
    container.feature_kind = FeatureKind::Cylinder;
    return container;
}

HistoryContainer* PartDocument::find_container(const std::string& id) {
    const auto found = std::find_if(history.begin(), history.end(),
        [&](const HistoryContainer& container) { return container.id == id; });
    return found == history.end() ? nullptr : &*found;
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

std::vector<zima::kernel::HistoryOperation> PartDocument::kernel_operations() const {
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
        } else {
            zima::kernel::CylinderRequest cylinder;
            cylinder.radius = container.cylinder.radius;
            cylinder.height = container.cylinder.height;
            cylinder.translation = translation;
            cylinder.rotation_degrees = rotation;
            primitive = cylinder;
        }
        operations.push_back({
            container.id,
            std::move(primitive),
            container.combine_mode == CombineMode::Subtract
                ? zima::kernel::BooleanOperation::Subtract
                : zima::kernel::BooleanOperation::Add,
        });
    }
    return operations;
}

PartDocument PartDocument::load(
    const std::filesystem::path& path,
    std::vector<zima::kernel::BodyResult>* calculated_boundaries) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Cannot open document: " + path.string());
    }
    nlohmann::json root;
    input >> root;
    if (root.at("format").get<std::string>() != "zima-cad-cpp" ||
        root.at("format_version").get<int>() != 1) {
        throw std::runtime_error("Unsupported C++ prototype document format");
    }
    PartDocument document;
    document.document_id = root.at("document_id").get<std::string>();
    document.name = root.at("name").get<std::string>();
    const auto& source_history = root.at("history");
    if (!source_history.is_array()) {
        throw std::runtime_error("Document history must be an array");
    }
    std::unordered_set<std::string> container_ids;
    for (const auto& source : source_history) {
        const std::string type = source.at("type").get<std::string>();
        if (type != "box" && type != "cylinder") {
            throw std::runtime_error("Unsupported history feature type");
        }
        HistoryContainer container;
        container.feature_kind = type == "cylinder"
            ? FeatureKind::Cylinder : FeatureKind::Box;
        container.id = source.at("id").get<std::string>();
        container.name = source.at("name").get<std::string>();
        if (container.id.empty() || !container_ids.insert(container.id).second) {
            throw std::runtime_error("History container IDs must be non-empty and unique");
        }
        if (container.name.empty()) {
            throw std::runtime_error("History container name must not be empty");
        }
        const std::string combine = source.value("combine", "add");
        if (combine != "add" && combine != "subtract") {
            throw std::runtime_error("Invalid history combination mode");
        }
        container.combine_mode = combine == "subtract"
            ? CombineMode::Subtract : CombineMode::Add;
        if (container.feature_kind == FeatureKind::Box) {
            container.box.length = source.at("length").get<double>();
            container.box.width = source.at("width").get<double>();
            container.box.height = source.at("height").get<double>();
            require_positive(container.box.length, "length");
            require_positive(container.box.width, "width");
            require_positive(container.box.height, "height");
        } else {
            container.cylinder.radius = source.at("radius").get<double>();
            container.cylinder.height = source.at("height").get<double>();
            require_positive(container.cylinder.radius, "radius");
            require_positive(container.cylinder.height, "height");
        }
        if (source.contains("placement")) {
            const auto& placement = source.at("placement");
            container.placement.x = placement.value("x", 0.0);
            container.placement.y = placement.value("y", 0.0);
            container.placement.z = placement.value("z", 0.0);
            container.placement.rotation_x = placement.value("rotation_x", 0.0);
            container.placement.rotation_y = placement.value("rotation_y", 0.0);
            container.placement.rotation_z = placement.value("rotation_z", 0.0);
        }
        validate_placement(container.placement);
        document.history.push_back(std::move(container));
    }
    if (!document.history.empty() &&
        document.history.front().combine_mode == CombineMode::Subtract) {
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
                (!owner_empty && !available_owners.contains(reference.owner_id))) {
                throw std::runtime_error(
                    "Calculated topology reference has an invalid history owner");
            }
        };
        const auto& mesh = loaded_boundaries[boundary_index].mesh;
        for (const auto& reference : mesh.triangle_references) {
            validate_reference(reference, true);
        }
        for (const auto& edge : mesh.edges) {
            validate_reference(edge.reference, false);
        }
        for (const auto& point : mesh.points) {
            validate_reference(point.reference, false);
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
        if (container.feature_kind == FeatureKind::Box) {
            require_positive(container.box.length, "length");
            require_positive(container.box.width, "width");
            require_positive(container.box.height, "height");
        } else {
            require_positive(container.cylinder.radius, "radius");
            require_positive(container.cylinder.height, "height");
        }
        validate_placement(container.placement);
        nlohmann::json serialized = {
            {"id", container.id},
            {"type", container.feature_kind == FeatureKind::Box ? "box" : "cylinder"},
            {"name", container.name},
            {"combine", container.combine_mode == CombineMode::Subtract
                ? "subtract" : "add"},
            {"placement", {
                {"x", container.placement.x},
                {"y", container.placement.y},
                {"z", container.placement.z},
                {"rotation_x", container.placement.rotation_x},
                {"rotation_y", container.placement.rotation_y},
                {"rotation_z", container.placement.rotation_z},
            }},
        };
        if (container.feature_kind == FeatureKind::Box) {
            serialized["length"] = container.box.length;
            serialized["width"] = container.box.width;
            serialized["height"] = container.box.height;
        } else {
            serialized["radius"] = container.cylinder.radius;
            serialized["height"] = container.cylinder.height;
        }
        serialized_history.push_back(std::move(serialized));
    }
    if (!history.empty() && history.front().combine_mode == CombineMode::Subtract) {
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
    const nlohmann::json root = {
        {"format", "zima-cad-cpp"},
        {"format_version", 1},
        {"document_id", document_id},
        {"type", "part"},
        {"name", name},
        {"history", std::move(serialized_history)},
        {"calculated_boundaries", std::move(serialized_boundaries)},
    };
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) {
            throw std::runtime_error("Cannot write document: " + path.string());
        }
        output << std::setw(2) << root << '\n';
        if (!output) {
            throw std::runtime_error("Document write failed: " + path.string());
        }
    }
    std::filesystem::rename(temporary, path);
}

}  // namespace zima::document
