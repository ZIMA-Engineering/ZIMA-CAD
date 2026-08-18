#include <zima/assembly/assembly_document.hpp>
#include <zima/document/viewer_packet_json.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace zima::assembly {
namespace {

std::string make_id() {
    std::mt19937_64 generator(static_cast<std::mt19937_64::result_type>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<unsigned long long> distribution;
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16)
           << distribution(generator) << std::setw(16) << distribution(generator);
    return stream.str();
}

zima::kernel::Vec3 transform_point(
    const zima::kernel::Vec3& source, const ComponentPlacement& placement) {
    constexpr double radians = 3.14159265358979323846 / 180.0;
    const double cx = std::cos(placement.rotation_x * radians);
    const double sx = std::sin(placement.rotation_x * radians);
    const double cy = std::cos(placement.rotation_y * radians);
    const double sy = std::sin(placement.rotation_y * radians);
    const double cz = std::cos(placement.rotation_z * radians);
    const double sz = std::sin(placement.rotation_z * radians);
    const zima::kernel::Vec3 rotated_x{
        source.x, cx * source.y - sx * source.z,
        sx * source.y + cx * source.z};
    const zima::kernel::Vec3 rotated_y{
        cy * rotated_x.x + sy * rotated_x.z, rotated_x.y,
        -sy * rotated_x.x + cy * rotated_x.z};
    return {
        cz * rotated_y.x - sz * rotated_y.y + placement.x,
        sz * rotated_y.x + cz * rotated_y.y + placement.y,
        rotated_y.z + placement.z,
    };
}

template <typename Reference>
void assign_instance(Reference& reference, const std::string& instance_path) {
    if (reference.valid()) reference.instance_path = instance_path;
}

}  // namespace

InstancePath InstancePath::child(const std::string& occurrence_id) const {
    if (occurrence_id.empty()) {
        throw std::invalid_argument("Occurrence ID must not be empty");
    }
    auto result = *this;
    result.occurrence_ids.push_back(occurrence_id);
    return result;
}

std::string InstancePath::encoded() const {
    std::string result;
    for (const auto& id : occurrence_ids) {
        result += std::to_string(id.size()) + ":" + id;
    }
    return result;
}

AssemblyDocument AssemblyDocument::create_default() {
    AssemblyDocument document;
    document.document_id = make_id();
    return document;
}

PartOccurrence AssemblyDocument::create_part_occurrence(
    std::string name,
    std::string source_document_id,
    std::filesystem::path source_path,
    zima::kernel::BodyResult calculated_source) {
    if (name.empty() || source_document_id.empty()) {
        throw std::invalid_argument("Part occurrence name and source ID are required");
    }
    return {
        make_id(), std::move(name), std::move(source_document_id),
        std::move(source_path), {}, std::move(calculated_source),
    };
}

const PartOccurrence* AssemblyDocument::find_occurrence(
    const std::string& occurrence_id) const {
    const auto found = std::find_if(components.begin(), components.end(),
        [&](const PartOccurrence& occurrence) {
            return occurrence.occurrence_id == occurrence_id;
        });
    return found == components.end() ? nullptr : &*found;
}

zima::kernel::ViewerMesh AssemblyDocument::build_scene() const {
    zima::kernel::ViewerMesh scene;
    std::unordered_set<std::string> occurrence_ids;
    for (const auto& component : components) {
        if (component.occurrence_id.empty() ||
            !occurrence_ids.insert(component.occurrence_id).second) {
            throw std::runtime_error("Assembly occurrence IDs must be non-empty and unique");
        }
        const std::string path = InstancePath{}.child(component.occurrence_id).encoded();
        const std::uint32_t vertex_offset =
            static_cast<std::uint32_t>(scene.vertices.size());
        const auto& source_mesh = component.calculated_source.mesh;
        for (const auto& vertex : source_mesh.vertices) {
            scene.vertices.push_back(transform_point(vertex, component.placement));
        }
        for (const auto index : source_mesh.triangles) {
            if (index >= source_mesh.vertices.size()) {
                throw std::runtime_error("Component viewer triangle index is invalid");
            }
            scene.triangles.push_back(vertex_offset + index);
        }
        for (auto reference : source_mesh.triangle_references) {
            assign_instance(reference, path);
            scene.triangle_references.push_back(std::move(reference));
        }
        for (auto edge : source_mesh.edges) {
            assign_instance(edge.reference, path);
            for (auto& point : edge.points) {
                point = transform_point(point, component.placement);
            }
            scene.edges.push_back(std::move(edge));
        }
        for (auto point : source_mesh.points) {
            assign_instance(point.reference, path);
            point.position = transform_point(point.position, component.placement);
            scene.points.push_back(std::move(point));
        }
    }
    if (scene.triangle_references.size() != scene.triangles.size() / 3) {
        throw std::runtime_error("Assembly triangle references are not aligned");
    }
    return scene;
}

AssemblyDocument AssemblyDocument::load(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open assembly: " + path.string());
    nlohmann::json root;
    input >> root;
    if (root.at("format").get<std::string>() != "zima-cad-cpp" ||
        root.at("format_version").get<int>() != 1 ||
        root.at("type").get<std::string>() != "assembly") {
        throw std::runtime_error("Unsupported C++ Assembly document format");
    }
    AssemblyDocument document;
    document.document_id = root.at("document_id").get<std::string>();
    document.name = root.at("name").get<std::string>();
    std::unordered_set<std::string> occurrence_ids;
    for (const auto& source : root.at("components")) {
        PartOccurrence component;
        component.occurrence_id = source.at("occurrence_id").get<std::string>();
        component.name = source.at("name").get<std::string>();
        component.source_document_id = source.at("source_document_id").get<std::string>();
        component.source_path = source.at("source_path").get<std::string>();
        if (component.occurrence_id.empty() || component.name.empty() ||
            component.source_document_id.empty() ||
            !occurrence_ids.insert(component.occurrence_id).second) {
            throw std::runtime_error("Assembly component identity is invalid");
        }
        const auto& placement = source.at("placement");
        component.placement = {
            placement.at("x").get<double>(), placement.at("y").get<double>(),
            placement.at("z").get<double>(),
            placement.at("rotation_x").get<double>(),
            placement.at("rotation_y").get<double>(),
            placement.at("rotation_z").get<double>(),
        };
        for (const double value : {
                component.placement.x, component.placement.y, component.placement.z,
                component.placement.rotation_x, component.placement.rotation_y,
                component.placement.rotation_z}) {
            if (!std::isfinite(value)) {
                throw std::runtime_error("Assembly component placement must be finite");
            }
        }
        component.calculated_source =
            zima::document::load_body_result(source.at("calculated_source"));
        for (const auto& reference :
             component.calculated_source.mesh.triangle_references) {
            if (!reference.instance_path.empty()) {
                throw std::runtime_error("Source Part packet contains an occurrence path");
            }
        }
        document.components.push_back(std::move(component));
    }
    static_cast<void>(document.build_scene());
    return document;
}

void AssemblyDocument::save(const std::filesystem::path& path) const {
    static_cast<void>(build_scene());
    nlohmann::json components_json = nlohmann::json::array();
    for (const auto& component : components) {
        components_json.push_back({
            {"occurrence_id", component.occurrence_id},
            {"name", component.name},
            {"source_document_id", component.source_document_id},
            {"source_path", component.source_path.generic_string()},
            {"placement", {
                {"x", component.placement.x}, {"y", component.placement.y},
                {"z", component.placement.z},
                {"rotation_x", component.placement.rotation_x},
                {"rotation_y", component.placement.rotation_y},
                {"rotation_z", component.placement.rotation_z},
            }},
            {"calculated_source",
             zima::document::serialize_body_result(component.calculated_source)},
        });
    }
    const nlohmann::json root = {
        {"format", "zima-cad-cpp"}, {"format_version", 1},
        {"type", "assembly"}, {"document_id", document_id}, {"name", name},
        {"components", std::move(components_json)},
    };
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot write assembly: " + path.string());
        output << std::setw(2) << root << '\n';
        if (!output) throw std::runtime_error("Assembly write failed: " + path.string());
    }
    std::filesystem::rename(temporary, path);
}

bool DependencyGraph::reaches(
    const std::string& start,
    const std::string& target,
    std::unordered_set<std::string>& visited) const {
    if (start == target) return true;
    if (!visited.insert(start).second) return false;
    const auto found = edges_.find(start);
    if (found == edges_.end()) return false;
    return std::any_of(found->second.begin(), found->second.end(),
        [&](const std::string& dependency) {
            return reaches(dependency, target, visited);
        });
}

bool DependencyGraph::would_create_cycle(
    const std::string& owner_document_id,
    const std::string& dependency_document_id) const {
    if (owner_document_id.empty() || dependency_document_id.empty() ||
        owner_document_id == dependency_document_id) return true;
    std::unordered_set<std::string> visited;
    return reaches(dependency_document_id, owner_document_id, visited);
}

void DependencyGraph::add_dependency(
    const std::string& owner_document_id,
    const std::string& dependency_document_id) {
    if (would_create_cycle(owner_document_id, dependency_document_id)) {
        throw std::invalid_argument("Assembly dependency would create a cycle");
    }
    edges_[owner_document_id].insert(dependency_document_id);
}

}  // namespace zima::assembly
