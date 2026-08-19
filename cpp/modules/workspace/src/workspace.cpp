#include <zima/workspace/workspace.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_set>

namespace zima::workspace {
namespace {

void append_mesh(zima::kernel::ViewerMesh& target,
                 const zima::kernel::ViewerMesh& source) {
    target.points.insert(target.points.end(), source.points.begin(), source.points.end());
    target.axes.insert(target.axes.end(), source.axes.begin(), source.axes.end());
    target.edges.insert(target.edges.end(), source.edges.begin(), source.edges.end());
    auto& references = target.original_references;
    const auto offset = static_cast<std::uint32_t>(references.vertices.size());
    references.vertices.insert(references.vertices.end(),
        source.original_references.vertices.begin(),
        source.original_references.vertices.end());
    for (const auto index : source.original_references.triangles) {
        references.triangles.push_back(offset + index);
    }
    references.triangle_references.insert(references.triangle_references.end(),
        source.original_references.triangle_references.begin(),
        source.original_references.triangle_references.end());
}

zima::kernel::BodyResult part_result(const PartState& part) {
    auto result = part.session.calculated_boundaries().back();
    append_mesh(result.mesh, part.session.document().construction_viewer_mesh());
    return result;
}

zima::kernel::Vec3 apply_placement(
    zima::kernel::Vec3 point,
    const zima::assembly::ComponentPlacement& placement,
    bool translate) {
    constexpr double radians = 3.14159265358979323846 / 180.0;
    const double cx = std::cos(placement.rotation_x * radians);
    const double sx = std::sin(placement.rotation_x * radians);
    const double cy = std::cos(placement.rotation_y * radians);
    const double sy = std::sin(placement.rotation_y * radians);
    const double cz = std::cos(placement.rotation_z * radians);
    const double sz = std::sin(placement.rotation_z * radians);
    point = {point.x, cx * point.y - sx * point.z,
             sx * point.y + cx * point.z};
    point = {cy * point.x + sy * point.z, point.y,
             -sy * point.x + cy * point.z};
    point = {cz * point.x - sz * point.y, sz * point.x + cz * point.y,
             point.z};
    if (translate) {
        point.x += placement.x;
        point.y += placement.y;
        point.z += placement.z;
    }
    return point;
}

zima::kernel::Vec3 remove_placement(
    zima::kernel::Vec3 point,
    const zima::assembly::ComponentPlacement& placement,
    bool translate) {
    constexpr double radians = 3.14159265358979323846 / 180.0;
    if (translate) {
        point.x -= placement.x;
        point.y -= placement.y;
        point.z -= placement.z;
    }
    const double cz = std::cos(placement.rotation_z * radians);
    const double sz = std::sin(placement.rotation_z * radians);
    const double cy = std::cos(placement.rotation_y * radians);
    const double sy = std::sin(placement.rotation_y * radians);
    const double cx = std::cos(placement.rotation_x * radians);
    const double sx = std::sin(placement.rotation_x * radians);
    point = {cz * point.x + sz * point.y, -sz * point.x + cz * point.y,
             point.z};
    point = {cy * point.x - sy * point.z, point.y,
             sy * point.x + cy * point.z};
    return {point.x, cx * point.y + sx * point.z,
            -sx * point.y + cx * point.z};
}

struct PersistedOccurrence {
    std::string occurrence_id;
    std::string source_document_id;
    zima::assembly::ComponentSourceKind source_kind;
    zima::assembly::ComponentPlacement placement;
};

std::optional<std::vector<PersistedOccurrence>> persisted_occurrence_chain(
    const zima::assembly::AssemblyDocument& top_assembly,
    const zima::assembly::InstancePath& instance_path) {
    if (instance_path.occurrence_ids.empty()) return std::nullopt;
    std::vector<PersistedOccurrence> result;
    const std::vector<zima::assembly::OccurrenceSnapshot>* snapshots = nullptr;
    for (std::size_t depth = 0; depth < instance_path.occurrence_ids.size(); ++depth) {
        if (depth == 0) {
            const auto* occurrence = top_assembly.find_occurrence(
                instance_path.occurrence_ids[depth]);
            if (occurrence == nullptr) return std::nullopt;
            result.push_back({occurrence->occurrence_id,
                occurrence->source_document_id, occurrence->source_kind,
                occurrence->placement});
            snapshots = &occurrence->nested_snapshot;
        } else {
            const auto found = std::find_if(snapshots->begin(), snapshots->end(),
                [&](const auto& snapshot) {
                    return snapshot.occurrence_id == instance_path.occurrence_ids[depth];
                });
            if (found == snapshots->end()) return std::nullopt;
            result.push_back({found->occurrence_id, found->source_document_id,
                              found->source_kind, found->placement});
            snapshots = &found->children;
        }
        if (depth + 1 < instance_path.occurrence_ids.size() &&
            result.back().source_kind !=
                zima::assembly::ComponentSourceKind::Assembly) return std::nullopt;
    }
    return result;
}

}  // namespace

const std::string& Workspace::id_of(const DocumentState& state) {
    return std::visit([](const auto& document) -> const std::string& {
        using State = std::decay_t<decltype(document)>;
        if constexpr (std::is_same_v<State, PartState>) {
            return document.session.document().document_id;
        } else if constexpr (std::is_same_v<State, AssemblyState>) {
            return document.session.document().document_id;
        } else {
            return document.document.document_id;
        }
    }, state);
}

void Workspace::add_part(
    zima::document::PartDocument document,
    std::vector<zima::kernel::BodyResult> calculated_boundaries,
    std::filesystem::path path) {
    if (document.document_id.empty() || find(document.document_id) != nullptr) {
        throw std::invalid_argument("Workspace document ID must be non-empty and unique");
    }
    const std::string id = document.document_id;
    documents_.push_back(PartState{
        zima::document::DocumentSession(
            std::move(document), std::move(calculated_boundaries)),
        std::move(path)});
    if (active_document_id_.empty()) active_document_id_ = id;
    if (displayed_document_id_.empty()) displayed_document_id_ = id;
}

void Workspace::add_assembly(
    zima::assembly::AssemblyDocument document,
    std::filesystem::path path) {
    if (document.document_id.empty() || find(document.document_id) != nullptr) {
        throw std::invalid_argument("Workspace document ID must be non-empty and unique");
    }
    const std::string id = document.document_id;
    documents_.push_back(AssemblyState{
        zima::assembly::AssemblySession(std::move(document)), std::move(path)});
    if (active_document_id_.empty()) active_document_id_ = id;
    if (displayed_document_id_.empty()) displayed_document_id_ = id;
}

void Workspace::add_drawing(
    zima::drawing::DrawingDocument document, std::filesystem::path path) {
    if (document.document_id.empty() || find(document.document_id) != nullptr) {
        throw std::invalid_argument("Workspace document ID must be non-empty and unique");
    }
    const std::string id = document.document_id;
    documents_.push_back(DrawingState{std::move(document), std::move(path)});
    if (active_document_id_.empty()) active_document_id_ = id;
    if (displayed_document_id_.empty()) displayed_document_id_ = id;
}

bool Workspace::remove(const std::string& document_id) {
    const auto found = std::find_if(documents_.begin(), documents_.end(),
        [&](const DocumentState& state) { return id_of(state) == document_id; });
    if (found == documents_.end()) return false;
    const auto index = static_cast<std::size_t>(std::distance(documents_.begin(), found));
    const bool removed_active = active_document_id_ == document_id;
    const bool removed_displayed = displayed_document_id_ == document_id;
    documents_.erase(found);
    if (documents_.empty()) {
        active_document_id_.clear();
        displayed_document_id_.clear();
        return true;
    }
    const std::string replacement = id_of(documents_[
        std::min(index, documents_.size() - 1)]);
    if (removed_displayed) displayed_document_id_ = replacement;
    if (removed_active) {
        active_document_id_ = removed_displayed
            ? replacement
            : find(displayed_document_id_) != nullptr
                ? displayed_document_id_ : replacement;
    }
    return true;
}

std::size_t Workspace::size() const { return documents_.size(); }

DocumentState* Workspace::find(const std::string& document_id) {
    const auto found = std::find_if(documents_.begin(), documents_.end(),
        [&](const DocumentState& state) { return id_of(state) == document_id; });
    return found == documents_.end() ? nullptr : &*found;
}

const DocumentState* Workspace::find(const std::string& document_id) const {
    const auto found = std::find_if(documents_.begin(), documents_.end(),
        [&](const DocumentState& state) { return id_of(state) == document_id; });
    return found == documents_.end() ? nullptr : &*found;
}

const std::string& Workspace::active_document_id() const {
    return active_document_id_;
}

const std::string& Workspace::displayed_document_id() const {
    return displayed_document_id_;
}

void Workspace::activate(const std::string& document_id) {
    if (find(document_id) == nullptr) {
        throw std::invalid_argument("Cannot activate a document outside the Workspace");
    }
    active_document_id_ = document_id;
}

void Workspace::display_top_level(const std::string& document_id) {
    if (find(document_id) == nullptr) {
        throw std::invalid_argument("Cannot display a document outside the Workspace");
    }
    displayed_document_id_ = document_id;
}

PartState* Workspace::open_part(const std::string& document_id) {
    auto* state = find(document_id);
    return state == nullptr ? nullptr : std::get_if<PartState>(state);
}

const PartState* Workspace::open_part(const std::string& document_id) const {
    const auto* state = find(document_id);
    return state == nullptr ? nullptr : std::get_if<PartState>(state);
}

AssemblyState* Workspace::open_assembly(const std::string& document_id) {
    auto* state = find(document_id);
    return state == nullptr ? nullptr : std::get_if<AssemblyState>(state);
}

const AssemblyState* Workspace::open_assembly(const std::string& document_id) const {
    const auto* state = find(document_id);
    return state == nullptr ? nullptr : std::get_if<AssemblyState>(state);
}

DrawingState* Workspace::open_drawing(const std::string& document_id) {
    auto* state = find(document_id);
    return state == nullptr ? nullptr : std::get_if<DrawingState>(state);
}

const DrawingState* Workspace::open_drawing(const std::string& document_id) const {
    const auto* state = find(document_id);
    return state == nullptr ? nullptr : std::get_if<DrawingState>(state);
}

std::optional<std::string> Workspace::document_id_for_path(
    const std::filesystem::path& path) const {
    if (path.empty()) return std::nullopt;
    const auto normalized = std::filesystem::absolute(path).lexically_normal();
    for (const auto& state : documents_) {
        const auto* candidate = std::visit([](const auto& document)
            -> const std::filesystem::path* { return &document.path; }, state);
        if (!candidate->empty() &&
            std::filesystem::absolute(*candidate).lexically_normal() == normalized)
            return id_of(state);
    }
    return std::nullopt;
}

zima::kernel::ViewerMesh Workspace::authoritative_viewer_mesh(
    const std::string& document_id) const {
    if (const auto* part = open_part(document_id)) {
        if (part->session.calculated_boundaries().empty())
            throw std::runtime_error("Open Part has no calculated body");
        return part_result(*part).mesh;
    }
    if (const auto* assembly = open_assembly(document_id))
        return assembly->session.document().build_scene();
    throw std::invalid_argument("Drawing source must be an open Part or Assembly");
}

const std::vector<DocumentState>& Workspace::documents() const { return documents_; }

std::optional<OccurrenceAddress> Workspace::resolve_occurrence(
    const std::string& top_assembly_document_id,
    const zima::assembly::InstancePath& instance_path) const {
    const auto* top = open_assembly(top_assembly_document_id);
    if (top == nullptr) return std::nullopt;
    const auto chain = persisted_occurrence_chain(
        top->session.document(), instance_path);
    if (!chain) return std::nullopt;
    const auto& occurrence = chain->back();
    const std::string owner_id = chain->size() == 1
        ? top_assembly_document_id
        : (*chain)[chain->size() - 2].source_document_id;
    return OccurrenceAddress{owner_id, occurrence.occurrence_id,
        occurrence.source_document_id, occurrence.source_kind, instance_path};
}

std::optional<OccurrenceAddress> Workspace::activate_occurrence(
    const std::string& top_assembly_document_id,
    const zima::assembly::InstancePath& instance_path) {
    if (open_assembly(top_assembly_document_id) == nullptr) {
        throw std::invalid_argument(
            "Occurrence activation requires an open top-level Assembly");
    }
    auto address = resolve_occurrence(top_assembly_document_id, instance_path);
    if (!address || find(address->source_document_id) == nullptr) return std::nullopt;
    display_top_level(top_assembly_document_id);
    activate(address->source_document_id);
    return address;
}

zima::kernel::Vec3 Workspace::occurrence_point_to_scene(
    const std::string& top_assembly_document_id,
    const zima::assembly::InstancePath& instance_path,
    const zima::kernel::Vec3& local_point) const {
    const auto* top = open_assembly(top_assembly_document_id);
    const auto chain = top == nullptr ? std::nullopt
        : persisted_occurrence_chain(top->session.document(), instance_path);
    if (!chain) {
        throw std::invalid_argument("Occurrence point transform requires an exact path");
    }
    auto result = local_point;
    for (auto occurrence = chain->rbegin(); occurrence != chain->rend(); ++occurrence) {
        result = apply_placement(result, occurrence->placement, true);
    }
    return result;
}

zima::kernel::Vec3 Workspace::occurrence_point_from_scene(
    const std::string& top_assembly_document_id,
    const zima::assembly::InstancePath& instance_path,
    const zima::kernel::Vec3& scene_point) const {
    const auto* top = open_assembly(top_assembly_document_id);
    const auto chain = top == nullptr ? std::nullopt
        : persisted_occurrence_chain(top->session.document(), instance_path);
    if (!chain) {
        throw std::invalid_argument("Occurrence point transform requires an exact path");
    }
    auto result = scene_point;
    for (const auto& occurrence : *chain) {
        result = remove_placement(result, occurrence.placement, true);
    }
    return result;
}

zima::kernel::Vec3 Workspace::occurrence_direction_to_scene(
    const std::string& top_assembly_document_id,
    const zima::assembly::InstancePath& instance_path,
    const zima::kernel::Vec3& local_direction) const {
    const auto scene_origin = occurrence_point_to_scene(
        top_assembly_document_id, instance_path, {});
    const auto scene_endpoint = occurrence_point_to_scene(
        top_assembly_document_id, instance_path, local_direction);
    return {scene_endpoint.x - scene_origin.x, scene_endpoint.y - scene_origin.y,
            scene_endpoint.z - scene_origin.z};
}

zima::kernel::Vec3 Workspace::occurrence_direction_from_scene(
    const std::string& top_assembly_document_id,
    const zima::assembly::InstancePath& instance_path,
    const zima::kernel::Vec3& scene_direction) const {
    const auto local_origin = occurrence_point_from_scene(
        top_assembly_document_id, instance_path, {});
    const auto local_endpoint = occurrence_point_from_scene(
        top_assembly_document_id, instance_path, scene_direction);
    return {local_endpoint.x - local_origin.x, local_endpoint.y - local_origin.y,
            local_endpoint.z - local_origin.z};
}

zima::kernel::ViewerReferenceGeometry
Workspace::authoritative_external_reference_geometry(
    const std::string& top_assembly_document_id,
    const zima::assembly::InstancePath& dependent_instance_path,
    const std::string& source_document_id) const {
    if (source_document_id.empty()) {
        throw std::invalid_argument("External reference source document ID is required");
    }
    std::vector<std::string> recursion_stack;
    const auto refreshed = refreshed_assembly(
        top_assembly_document_id, recursion_stack);
    const auto dependent_chain = persisted_occurrence_chain(
        refreshed, dependent_instance_path);
    if (!dependent_chain || dependent_chain->back().source_kind !=
            zima::assembly::ComponentSourceKind::Part) {
        throw std::invalid_argument(
            "External reference requires an exact dependent Part occurrence");
    }
    const auto local_point = [&](zima::kernel::Vec3 point) {
        for (const auto& occurrence : *dependent_chain) {
            point = remove_placement(point, occurrence.placement, true);
        }
        return point;
    };
    const auto local_direction = [&](zima::kernel::Vec3 direction) {
        for (const auto& occurrence : *dependent_chain) {
            direction = remove_placement(direction, occurrence.placement, false);
        }
        return direction;
    };
    const auto belongs_to_source = [&](const std::string& encoded_path) {
        if (encoded_path.empty()) return false;
        try {
            const auto chain = persisted_occurrence_chain(
                refreshed, zima::assembly::InstancePath::decode(encoded_path));
            return chain && chain->back().source_kind ==
                    zima::assembly::ComponentSourceKind::Part &&
                chain->back().source_document_id == source_document_id;
        } catch (const std::invalid_argument&) {
            return false;
        }
    };
    const auto scene = refreshed.build_scene();
    const auto& source = scene.original_references;
    zima::kernel::ViewerReferenceGeometry result;
    result.vertices.reserve(source.vertices.size());
    for (const auto& vertex : source.vertices) {
        result.vertices.push_back(local_point(vertex));
    }
    for (std::size_t triangle = 0;
         triangle < source.triangle_references.size(); ++triangle) {
        const auto& reference = source.triangle_references[triangle];
        if (!belongs_to_source(reference.instance_path)) continue;
        result.triangle_references.push_back(reference);
        result.triangles.insert(result.triangles.end(), {
            source.triangles[triangle * 3], source.triangles[triangle * 3 + 1],
            source.triangles[triangle * 3 + 2]});
    }
    for (auto edge : source.edges) {
        if (!belongs_to_source(edge.reference.instance_path)) continue;
        for (auto& point : edge.points) point = local_point(point);
        result.edges.push_back(std::move(edge));
    }
    for (auto point : source.points) {
        if (!belongs_to_source(point.reference.instance_path)) continue;
        point.position = local_point(point.position);
        result.points.push_back(std::move(point));
    }
    for (auto axis : source.axes) {
        if (!belongs_to_source(axis.reference.instance_path)) continue;
        axis.point = local_point(axis.point);
        axis.direction = local_direction(axis.direction);
        result.axes.push_back(std::move(axis));
    }
    return result;
}

bool Workspace::refresh_context_external_references(
    zima::document::PartDocument& document) const {
    using Context = std::tuple<std::string, std::string, std::string>;
    std::set<Context> contexts;
    for (const auto& sketch : document.sketches) {
        for (const auto& reference : sketch.external_references) {
            if (reference.context_assembly_document_id.empty()) continue;
            contexts.emplace(reference.context_assembly_document_id,
                reference.context_instance_path,
                reference.source_document_id);
        }
    }
    bool changed = false;
    for (const auto& [assembly_id, dependent_path, source_document_id] : contexts) {
        zima::kernel::ViewerReferenceGeometry geometry;
        if (open_assembly(assembly_id) != nullptr) {
            geometry = authoritative_external_reference_geometry(
                assembly_id, zima::assembly::InstancePath::decode(dependent_path),
                source_document_id);
        }
        for (auto& sketch : document.sketches) {
            const bool owns_context = std::any_of(
                sketch.external_references.begin(),
                sketch.external_references.end(), [&](const auto& reference) {
                    return reference.context_assembly_document_id == assembly_id &&
                        reference.context_instance_path == dependent_path &&
                        reference.source_document_id == source_document_id;
                });
            if (owns_context && sketch.refresh_external_references(
                    source_document_id, geometry)) {
                changed = true;
            }
        }
    }
    return changed;
}

void Workspace::add_external_sketch_dependency(
    const std::string& top_assembly_document_id,
    const zima::assembly::InstancePath& dependent_instance_path,
    const zima::assembly::InstancePath& prerequisite_instance_path) {
    const auto dependent = resolve_occurrence(
        top_assembly_document_id, dependent_instance_path);
    const auto prerequisite = resolve_occurrence(
        top_assembly_document_id, prerequisite_instance_path);
    if (!dependent || !prerequisite ||
        dependent->source_kind != zima::assembly::ComponentSourceKind::Part ||
        prerequisite->source_kind != zima::assembly::ComponentSourceKind::Part ||
        dependent->source_document_id == prerequisite->source_document_id ||
        dependent_instance_path == prerequisite_instance_path) {
        throw std::invalid_argument(
            "External Sketch dependency requires two exact Part occurrences");
    }
    zima::assembly::DependencyGraph document_dependencies;
    for (const auto& state : documents_) {
        const auto* part = std::get_if<PartState>(&state);
        if (part == nullptr) continue;
        for (const auto& sketch : part->session.document().sketches) {
            for (const auto& reference : sketch.external_references) {
                if (reference.source_document_id.empty() ||
                    reference.source_document_id ==
                        part->session.document().document_id) continue;
                document_dependencies.add_dependency(
                    part->session.document().document_id,
                    reference.source_document_id);
            }
        }
    }
    document_dependencies.add_dependency(
        dependent->source_document_id, prerequisite->source_document_id);
    std::size_t common_depth = 0;
    while (common_depth < dependent_instance_path.occurrence_ids.size() &&
           common_depth < prerequisite_instance_path.occurrence_ids.size() &&
           dependent_instance_path.occurrence_ids[common_depth] ==
               prerequisite_instance_path.occurrence_ids[common_depth]) {
        ++common_depth;
    }
    if (common_depth == dependent_instance_path.occurrence_ids.size() ||
        common_depth == prerequisite_instance_path.occurrence_ids.size()) {
        throw std::invalid_argument("External Sketch dependency branches are invalid");
    }
    std::string owner_id = top_assembly_document_id;
    if (common_depth != 0) {
        zima::assembly::InstancePath owner_path;
        owner_path.occurrence_ids.assign(
            dependent_instance_path.occurrence_ids.begin(),
            dependent_instance_path.occurrence_ids.begin() +
                static_cast<std::ptrdiff_t>(common_depth));
        const auto owner_occurrence = resolve_occurrence(
            top_assembly_document_id, owner_path);
        if (!owner_occurrence || owner_occurrence->source_kind !=
                zima::assembly::ComponentSourceKind::Assembly) {
            throw std::invalid_argument(
                "External Sketch dependency has no common Assembly owner");
        }
        owner_id = owner_occurrence->source_document_id;
    }
    auto* owner = open_assembly(owner_id);
    if (owner == nullptr) {
        throw std::runtime_error(
            "External Sketch dependency owner Assembly is not open");
    }
    const std::string& dependent_branch =
        dependent_instance_path.occurrence_ids[common_depth];
    const std::string& prerequisite_branch =
        prerequisite_instance_path.occurrence_ids[common_depth];
    const auto& current = owner->session.document();
    if (std::any_of(current.dependencies.begin(), current.dependencies.end(),
            [&](const auto& dependency) {
                return dependency.dependent_occurrence_id == dependent_branch &&
                    dependency.prerequisite_occurrence_id == prerequisite_branch &&
                    dependency.kind == zima::assembly::ComponentDependencyKind::
                        ExternalSketchReference;
            })) return;
    auto next = current;
    next.add_dependency(zima::assembly::AssemblyDocument::create_dependency(
        dependent_branch, prerequisite_branch,
        zima::assembly::ComponentDependencyKind::ExternalSketchReference));
    owner->session.commit(std::move(next));
}

zima::kernel::ViewerMesh Workspace::build_scene_with_part_override(
    const std::string& top_assembly_document_id,
    const zima::assembly::InstancePath& instance_path,
    zima::kernel::BodyResult calculated_source) const {
    const auto address = resolve_occurrence(top_assembly_document_id, instance_path);
    if (!address || address->source_kind != zima::assembly::ComponentSourceKind::Part) {
        throw std::invalid_argument("Part override requires an exact leaf Part occurrence");
    }
    const auto* top = open_assembly(top_assembly_document_id);
    const auto chain = top == nullptr ? std::nullopt
        : persisted_occurrence_chain(top->session.document(), instance_path);
    if (!chain) {
        throw std::runtime_error("Part override occurrence chain is unavailable");
    }
    for (auto iterator = chain->rbegin(); iterator != chain->rend(); ++iterator) {
        auto wrapper = zima::assembly::AssemblyDocument::create_default();
        auto occurrence = zima::assembly::AssemblyDocument::create_part_occurrence(
            "Part override", iterator->source_document_id, {},
            std::move(calculated_source));
        occurrence.occurrence_id = iterator->occurrence_id;
        occurrence.placement = iterator->placement;
        wrapper.components.push_back(std::move(occurrence));
        calculated_source = {};
        calculated_source.mesh = wrapper.build_scene();
    }
    const std::string target_path = instance_path.encoded();
    auto scene = open_assembly(top_assembly_document_id)->session.document().build_scene();
    std::vector<std::uint32_t> triangles;
    std::vector<zima::kernel::FaceReference> triangle_references;
    for (std::size_t triangle = 0;
         triangle < scene.triangle_references.size(); ++triangle) {
        if (scene.triangle_references[triangle].instance_path == target_path) continue;
        triangles.insert(triangles.end(), {
            scene.triangles[triangle * 3], scene.triangles[triangle * 3 + 1],
            scene.triangles[triangle * 3 + 2]});
        triangle_references.push_back(scene.triangle_references[triangle]);
    }
    scene.triangles = std::move(triangles);
    scene.triangle_references = std::move(triangle_references);
    std::erase_if(scene.edges, [&](const auto& edge) {
        return edge.reference.instance_path == target_path;
    });
    std::erase_if(scene.points, [&](const auto& point) {
        return point.reference.instance_path == target_path;
    });
    std::erase_if(scene.axes, [&](const auto& axis) {
        return axis.reference.instance_path == target_path;
    });
    std::erase_if(scene.dimensions, [&](const auto& dimension) {
        return dimension.reference.instance_path == target_path;
    });
    auto& scene_references = scene.original_references;
    std::vector<std::uint32_t> reference_triangles;
    std::vector<zima::kernel::FaceReference> reference_faces;
    for (std::size_t triangle = 0;
         triangle < scene_references.triangle_references.size(); ++triangle) {
        if (scene_references.triangle_references[triangle].instance_path ==
            target_path) continue;
        reference_triangles.insert(reference_triangles.end(), {
            scene_references.triangles[triangle * 3],
            scene_references.triangles[triangle * 3 + 1],
            scene_references.triangles[triangle * 3 + 2]});
        reference_faces.push_back(scene_references.triangle_references[triangle]);
    }
    scene_references.triangles = std::move(reference_triangles);
    scene_references.triangle_references = std::move(reference_faces);
    std::erase_if(scene_references.edges, [&](const auto& edge) {
        return edge.reference.instance_path == target_path;
    });
    std::erase_if(scene_references.points, [&](const auto& point) {
        return point.reference.instance_path == target_path;
    });
    std::erase_if(scene_references.axes, [&](const auto& axis) {
        return axis.reference.instance_path == target_path;
    });
    const auto offset = static_cast<std::uint32_t>(scene.vertices.size());
    scene.vertices.insert(scene.vertices.end(), calculated_source.mesh.vertices.begin(),
                          calculated_source.mesh.vertices.end());
    for (const auto index : calculated_source.mesh.triangles) {
        scene.triangles.push_back(offset + index);
    }
    scene.triangle_references.insert(scene.triangle_references.end(),
        calculated_source.mesh.triangle_references.begin(),
        calculated_source.mesh.triangle_references.end());
    scene.edges.insert(scene.edges.end(), calculated_source.mesh.edges.begin(),
                       calculated_source.mesh.edges.end());
    scene.points.insert(scene.points.end(), calculated_source.mesh.points.begin(),
                        calculated_source.mesh.points.end());
    scene.axes.insert(scene.axes.end(), calculated_source.mesh.axes.begin(),
                      calculated_source.mesh.axes.end());
    scene.dimensions.insert(scene.dimensions.end(),
        calculated_source.mesh.dimensions.begin(), calculated_source.mesh.dimensions.end());
    const auto& replacement_references = calculated_source.mesh.original_references;
    const auto reference_offset =
        static_cast<std::uint32_t>(scene_references.vertices.size());
    scene_references.vertices.insert(scene_references.vertices.end(),
        replacement_references.vertices.begin(), replacement_references.vertices.end());
    for (const auto index : replacement_references.triangles) {
        scene_references.triangles.push_back(reference_offset + index);
    }
    scene_references.triangle_references.insert(
        scene_references.triangle_references.end(),
        replacement_references.triangle_references.begin(),
        replacement_references.triangle_references.end());
    scene_references.edges.insert(scene_references.edges.end(),
        replacement_references.edges.begin(), replacement_references.edges.end());
    scene_references.points.insert(scene_references.points.end(),
        replacement_references.points.begin(), replacement_references.points.end());
    scene_references.axes.insert(scene_references.axes.end(),
        replacement_references.axes.begin(), replacement_references.axes.end());
    return scene;
}

std::string Workspace::insert_open_part(
    const std::string& assembly_document_id,
    const std::string& part_document_id,
    std::string occurrence_name) {
    auto* assembly = open_assembly(assembly_document_id);
    const auto* part = open_part(part_document_id);
    if (assembly == nullptr || part == nullptr) {
        throw std::invalid_argument("Insertion requires open Part and Assembly documents");
    }
    const auto& calculated = part->session.calculated_boundaries();
    if (part->session.document().history.empty() || calculated.empty()) {
        throw std::runtime_error("Open Part has no explicit calculated result");
    }
    auto next = assembly->session.document();
    auto occurrence = zima::assembly::AssemblyDocument::create_part_occurrence(
        std::move(occurrence_name), part_document_id, part->path,
        part_result(*part));
    const std::string occurrence_id = occurrence.occurrence_id;
    next.components.push_back(std::move(occurrence));
    static_cast<void>(next.build_scene());
    assembly->session.commit(std::move(next));
    return occurrence_id;
}

bool Workspace::assembly_reaches(
    const std::string& start_document_id,
    const std::string& target_document_id) const {
    std::unordered_set<std::string> visited;
    const std::function<bool(const std::string&)> reaches =
        [&](const std::string& current_id) {
            if (current_id == target_document_id) return true;
            if (!visited.insert(current_id).second) return false;
            const auto* assembly = open_assembly(current_id);
            if (assembly == nullptr) return false;
            for (const auto& occurrence : assembly->session.document().components) {
                if (occurrence.source_kind ==
                        zima::assembly::ComponentSourceKind::Assembly &&
                    reaches(occurrence.source_document_id)) {
                    return true;
                }
            }
            return false;
        };
    return reaches(start_document_id);
}

std::string Workspace::insert_open_assembly(
    const std::string& owner_assembly_document_id,
    const std::string& source_assembly_document_id,
    std::string occurrence_name) {
    auto* owner = open_assembly(owner_assembly_document_id);
    const auto* source = open_assembly(source_assembly_document_id);
    if (owner == nullptr || source == nullptr) {
        throw std::invalid_argument("Insertion requires two open Assembly documents");
    }
    if (assembly_reaches(source_assembly_document_id, owner_assembly_document_id)) {
        throw std::invalid_argument("Assembly insertion would create a dependency cycle");
    }
    auto next = owner->session.document();
    auto occurrence = zima::assembly::AssemblyDocument::create_assembly_occurrence(
        std::move(occurrence_name), source_assembly_document_id, source->path,
        source->session.document());
    const std::string occurrence_id = occurrence.occurrence_id;
    next.components.push_back(std::move(occurrence));
    static_cast<void>(next.build_scene());
    owner->session.commit(std::move(next));
    return occurrence_id;
}

zima::assembly::AssemblyDocument Workspace::refreshed_assembly(
    const std::string& assembly_document_id,
    std::vector<std::string>& recursion_stack) const {
    if (std::find(recursion_stack.begin(), recursion_stack.end(), assembly_document_id) !=
        recursion_stack.end()) {
        throw std::runtime_error("Open Assembly dependency chain contains a cycle");
    }
    const auto* assembly = open_assembly(assembly_document_id);
    if (assembly == nullptr) {
        throw std::invalid_argument("Regenerate target must be an open Assembly");
    }
    recursion_stack.push_back(assembly_document_id);
    auto refreshed = assembly->session.document();
    for (auto& occurrence : refreshed.components) {
        if (occurrence.source_kind == zima::assembly::ComponentSourceKind::Assembly) {
            if (open_assembly(occurrence.source_document_id) != nullptr) {
                auto nested = refreshed_assembly(
                    occurrence.source_document_id, recursion_stack);
                occurrence.calculated_source = {};
                occurrence.calculated_source.mesh = nested.build_scene();
                occurrence.nested_snapshot = nested.occurrence_snapshot();
                occurrence.source_path = open_assembly(
                    occurrence.source_document_id)->path;
            }
            continue;
        }
        const auto* part = open_part(occurrence.source_document_id);
        if (part == nullptr) continue;
        const auto& calculated = part->session.calculated_boundaries();
        if (part->session.document().history.empty() || calculated.empty()) {
            throw std::runtime_error(
                "An open Assembly dependency has no calculated Part result");
        }
        occurrence.calculated_source = part_result(*part);
        occurrence.source_path = part->path;
    }
    recursion_stack.pop_back();
    refreshed.calculate_mates();
    static_cast<void>(refreshed.build_scene());
    return refreshed;
}

void Workspace::regenerate_assembly_from_open_dependencies(
    const std::string& assembly_document_id) {
    auto* assembly = open_assembly(assembly_document_id);
    if (assembly == nullptr) {
        throw std::invalid_argument("Regenerate target must be an open Assembly");
    }
    std::vector<std::string> recursion_stack;
    auto refreshed = refreshed_assembly(assembly_document_id, recursion_stack);
    assembly->session.update_dependency_snapshots(std::move(refreshed));
}

}  // namespace zima::workspace
