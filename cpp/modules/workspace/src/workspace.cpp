#include <zima/workspace/appearance_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/workspace.hpp>
#include <zima/workspace/reference_sources.hpp>
#include <zima/workspace/document_dependencies.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <zima/document/feature_sketches.hpp>
#include <zima/document/object_annotation_frames.hpp>
#include <zima/assembly/physical_properties.hpp>
#include <zima/kernel/occt_kernel.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace zima::workspace {
namespace {

void append_mesh(zima::kernel::ViewerMesh& target,
                 const zima::kernel::ViewerMesh& source) {
    target.points.insert(target.points.end(), source.points.begin(), source.points.end());
    target.axes.insert(target.axes.end(), source.axes.begin(), source.axes.end());
    target.edges.insert(target.edges.end(), source.edges.begin(), source.edges.end());
    target.constraint_markers.insert(target.constraint_markers.end(),
        source.constraint_markers.begin(), source.constraint_markers.end());
    auto& references = target.original_references;
    references.edges.insert(references.edges.end(), source.original_references.edges.begin(), source.original_references.edges.end());
    references.points.insert(references.points.end(), source.original_references.points.begin(), source.original_references.points.end());
    references.axes.insert(references.axes.end(), source.original_references.axes.begin(), source.original_references.axes.end());
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
    auto result = part.session.calculated_boundaries().empty() ? zima::kernel::BodyResult{}
        : part.session.calculated_boundaries().back();
    const auto& document = part.session.document();
    for (const auto& sketch : document.sketches) {
        if (sketch.suppressed) continue;
        const auto owner = std::ranges::find(document.history, sketch.owner_container_id,
            &zima::document::HistoryContainer::id);
        if (owner != document.history.end() && (owner->suppressed ||
            owner->feature_kind != zima::document::FeatureKind::Sketch)) continue;
        auto mesh = sketch.viewer_mesh();
        if (const auto* body = document.body_owner_for_object(sketch.id)) {
            if (!body->visible) continue;
            mesh = document.place_body_mesh(std::move(mesh),body->scope.id);
        }
        append_mesh(result.mesh,mesh);
    }
    append_mesh(result.mesh, document.construction_viewer_mesh());
    // Publish the persisted datum frames in the component snapshot when it
    // is explicitly inserted or regenerated. Display visibility stays local
    // to the editing View; mate resolution uses this reference packet.
    zima::kernel::ViewerMesh origins;
    origins.original_references = document.body_origin_reference_geometry();
    append_mesh(result.mesh, origins);
    origins.original_references = document.history_origin_reference_geometry_before({});
    append_mesh(result.mesh, origins);
    result.mesh.annotation_frames=zima::document::part_annotation_envelopes(document,result.mesh);
    return result;
}

zima::kernel::BodySnapshot part_snapshot(const PartState& part) {
    if (!part.source_geometry || part.source_generation!=part.session.data_generation()) {
        auto body=part_result(part);
        body.body_boundaries.clear();body.body_inputs.clear();
        part.source_geometry=zima::kernel::BodySnapshot(std::move(body));
        part.source_generation=part.session.data_generation();
    }
    return *part.source_geometry;
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
    const zima::assembly::InstancePath& instance_path,
    const zima::assembly::AssemblyDocument* source_override = nullptr) {
    if (instance_path.occurrence_ids.empty()) return std::nullopt;
    std::vector<PersistedOccurrence> result;
    const std::vector<zima::assembly::OccurrenceSnapshot>* snapshots = nullptr;
    const auto* source = source_override && source_override->document_id == top_assembly.document_id
        ? source_override : &top_assembly;
    for (std::size_t depth = 0; depth < instance_path.occurrence_ids.size(); ++depth) {
        if (source) {
            const auto* occurrence = source->find_occurrence(
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
        source = source_override && result.back().source_kind == zima::assembly::ComponentSourceKind::Assembly &&
            result.back().source_document_id == source_override->document_id ? source_override : nullptr;
        if (depth + 1 < instance_path.occurrence_ids.size() &&
            result.back().source_kind != zima::assembly::ComponentSourceKind::Assembly &&
            result.back().source_kind != zima::assembly::ComponentSourceKind::Pattern) return std::nullopt;
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
            return document.document().document_id;
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
    if(removed_active||removed_displayed)active_occurrence_path_.clear();
    documents_.erase(found);
    if (documents_.empty()) {
        active_document_id_.clear();
        displayed_document_id_.clear();
        return true;
    }
    const std::string replacement = id_of(documents_[
        std::min(index, documents_.size() - 1)]);
    if (removed_displayed) displayed_document_id_ = !removed_active && find(active_document_id_)
        ? active_document_id_ : replacement;
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

const std::string& Workspace::active_occurrence_path() const {return active_occurrence_path_;}

void Workspace::activate(const std::string& document_id) {
    if (find(document_id) == nullptr) {
        throw std::invalid_argument("Cannot activate a document outside the Workspace");
    }
    if(active_document_id_!=document_id)active_occurrence_path_.clear();
    active_document_id_ = document_id;
}

void Workspace::display_top_level(const std::string& document_id) {
    if (find(document_id) == nullptr) {
        throw std::invalid_argument("Cannot display a document outside the Workspace");
    }
    if(displayed_document_id_!=document_id||active_document_id_==document_id)active_occurrence_path_.clear();
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

void Workspace::refresh_source_geometry() {
    std::set<std::string> visiting;
    std::map<std::string,zima::kernel::BodySnapshot> assembly_sources;
    const auto refresh=[&](const auto& self,zima::assembly::AssemblyDocument& document,
            const std::filesystem::path& owner_file)->bool {
        if (!visiting.insert(document.document_id).second)
            throw std::runtime_error("Cyclic Assembly source dependency");
        bool changed=false;
        for (auto& component:document.components) {
            // Assembly-owned operations keep their calculated result until the
            // user explicitly regenerates that operation.
            if (component.derived_copy || std::ranges::any_of(document.cuts,[&](const auto& cut) {
                return !cut.definition.suppressed && std::ranges::find(cut.target_occurrence_ids,
                    component.occurrence_id)!=cut.target_occurrence_ids.end();
            })) continue;
            auto file=component.source_path;
            if(file.is_relative())file=owner_file.parent_path()/file;
            if (component.source_kind==zima::assembly::ComponentSourceKind::Part) {
                const auto* part=open_part(component.source_document_id);
                if(part && !file.empty())
                    native_part_cache_.erase(std::filesystem::absolute(file).lexically_normal());
                if(!part && !file.empty() && std::filesystem::is_regular_file(file)) {
                    file=std::filesystem::absolute(file).lexically_normal();
                    const auto modified=std::filesystem::last_write_time(file);
                    auto cached=native_part_cache_.find(file);
                    if(cached==native_part_cache_.end()||cached->second.modified!=modified) {
                        std::vector<zima::kernel::BodyResult> boundaries;
                        auto source=zima::document::PartDocument::load(file,&boundaries);
                        cached=native_part_cache_.insert_or_assign(file,NativePartCache{modified,
                            PartState{zima::document::DocumentSession(std::move(source),std::move(boundaries)),file}}).first;
                    }
                    part=&cached->second.part;
                }
                if (part) {
                    if(part->session.document().document_id!=component.source_document_id)
                        throw std::runtime_error("Part source document identity mismatch");
                    const auto source=part_snapshot(*part);
                    if (!source.shares_with(component.calculated_source)) {
                        component.calculated_source=source;
                        component.body_color=part->session.document().body_color;
                        component.face_colors=part->session.document().face_colors;
                        component.appearance=part_appearance(part->session.document());
                        component.density_kg_mm3=zima::document::material_density_kg_mm3(part->session.document());
                        component.mass_volume_mm3=std::abs(source->volume);
                        changed=true;
                    }
                }
                continue;
            }
            if (component.source_kind!=zima::assembly::ComponentSourceKind::Assembly) continue;
            zima::assembly::AssemblyDocument nested;
            std::string source_stamp="assembly-display:"+component.source_document_id+":";
            if(auto* open=open_assembly(component.source_document_id)) {
                source_stamp+="open:"+std::to_string(open->session.revision());
                nested=open->session.document();
                file=open->path;
                if(!file.empty())native_assembly_cache_.erase(std::filesystem::absolute(file).lexically_normal());
                if(self(self,nested,file))open->session.update_source_geometry(nested);
            } else {
                if(file.empty()||!std::filesystem::is_regular_file(file))continue;
                file=std::filesystem::absolute(file).lexically_normal();
                const auto modified=std::filesystem::last_write_time(file);
                auto cached=native_assembly_cache_.find(file);
                if(cached==native_assembly_cache_.end()||cached->second.modified!=modified) {
                    auto loaded=zima::assembly::AssemblyDocument::load(file);
                    cached=native_assembly_cache_.insert_or_assign(file,
                        NativeAssemblyCache{modified,std::move(loaded)}).first;
                }
                source_stamp+="file:"+std::to_string(modified.time_since_epoch().count());
                nested=cached->second.document;
                if(self(self,nested,file))cached->second.document=nested;
            }
            if(nested.document_id!=component.source_document_id)
                throw std::runtime_error("Assembly source document identity mismatch");
            bool same=component.calculated_source->source_fingerprint==source_stamp &&
                component.nested_snapshot==nested.occurrence_snapshot() &&
                component.calculated_source->body_outputs.size()==nested.components.size();
            if(same)for(const auto& child:nested.components) {
                const auto found=component.calculated_source->body_outputs.find(child.occurrence_id);
                if(found==component.calculated_source->body_outputs.end()||
                    !found->second.shares_with(child.calculated_source)) {same=false;break;}
            }
            const auto sharing_key=source_stamp+":"+file.generic_string();
            if(const auto shared=assembly_sources.find(sharing_key);shared!=assembly_sources.end()) {
                if(!component.calculated_source.shares_with(shared->second)) {
                    component.calculated_source=shared->second;
                    component.nested_snapshot=nested.occurrence_snapshot();
                    changed=true;
                }
            } else {
                if(!same) {
                    const auto source=zima::assembly::AssemblyDocument::create_assembly_occurrence(
                        component.name,component.source_document_id,file,nested);
                    auto body=source.calculated_source.get();body.source_fingerprint=source_stamp;
                    component.calculated_source=std::move(body);
                    component.nested_snapshot=source.nested_snapshot;
                    changed=true;
                }
                assembly_sources.emplace(sharing_key,component.calculated_source);
            }
        }
        visiting.erase(document.document_id);
        return changed;
    };
    for(auto& state:documents_)if(auto* assembly=std::get_if<AssemblyState>(&state)) {
        auto document=assembly->session.document();
        if(refresh(refresh,document,assembly->path))
            assembly->session.update_source_geometry(std::move(document));
    }
}

zima::kernel::ViewerMesh Workspace::authoritative_viewer_mesh(
    const std::string& document_id) const {
    if (const auto* part = open_part(document_id)) {
        if (part->session.calculated_boundaries().empty()) {
            if(part->session.document().kernel_operations().empty())return part_result(*part).mesh;
            throw std::runtime_error("Open Part has no calculated body");
        }
        return part_result(*part).mesh;
    }
    if (const auto* assembly = open_assembly(document_id))
        return assembly->session.document().build_scene();
    throw std::invalid_argument("Drawing source must be an open Part or Assembly");
}

const std::vector<DocumentState>& Workspace::documents() const { return documents_; }
std::vector<DocumentState>& Workspace::documents() { return documents_; }

std::optional<OccurrenceAddress> Workspace::resolve_occurrence(
    const std::string& top_assembly_document_id,
    const zima::assembly::InstancePath& instance_path) const {
    const auto* top = open_assembly(top_assembly_document_id);
    if (top == nullptr) return std::nullopt;
    return resolve_document_occurrence(top->session.document(), instance_path);
}

std::optional<OccurrenceAddress> resolve_document_occurrence(
    const assembly::AssemblyDocument& top, const assembly::InstancePath& instance_path,
    const assembly::AssemblyDocument* source_override) {
    const auto chain = persisted_occurrence_chain(top, instance_path, source_override);
    if (!chain) return std::nullopt;
    const auto& occurrence = chain->back();
    std::string owner_id=top.document_id;
    // A virtual Pattern instance is owned by the surrounding real Assembly.
    for(std::size_t i=chain->size()-1;i>0;--i)if((*chain)[i-1].source_kind==assembly::ComponentSourceKind::Assembly) {
        owner_id=(*chain)[i-1].source_document_id;break;
    }
    return OccurrenceAddress{owner_id, occurrence.occurrence_id,
        occurrence.source_document_id, occurrence.source_kind, instance_path};
}

std::optional<std::filesystem::path> Workspace::occurrence_source_file(
    const std::string& top_id, const zima::assembly::InstancePath& requested) const {
    const auto* top = open_assembly(top_id);
    if (!top || requested.occurrence_ids.empty()) return std::nullopt;
    const auto path = derived_source_path(top_id, requested);
    const auto* document = &top->session.document();
    auto owner_file = top->path;
    std::optional<zima::assembly::AssemblyDocument> loaded;
    for (std::size_t i = 0; i < path.occurrence_ids.size(); ++i) {
        const auto* occurrence = document->find_occurrence(path.occurrence_ids[i]);
        if (!occurrence || occurrence->source_path.empty()) return std::nullopt;
        auto source_file = occurrence->source_path;
        const auto source_id = occurrence->source_document_id;
        if (source_file.is_relative()) source_file = owner_file.parent_path() / source_file;
        source_file = source_file.lexically_normal();
        if (i + 1 == path.occurrence_ids.size()) return source_file;
        if (occurrence->source_kind != zima::assembly::ComponentSourceKind::Assembly)
            return std::nullopt;
        if (const auto* open = open_assembly(source_id)) {
            document = &open->session.document();
            owner_file = open->path.empty() ? source_file : open->path;
        } else {
            loaded = zima::assembly::AssemblyDocument::load(source_file);
            if (loaded->document_id != source_id) return std::nullopt;
            document = &*loaded;
            owner_file = source_file;
        }
    }
    return std::nullopt;
}

zima::assembly::InstancePath Workspace::derived_source_path(
    const std::string& top_id,const zima::assembly::InstancePath& path) const {
    const auto* top=open_assembly(top_id);if(!top)return path;
    const auto snapshot=top->session.document().occurrence_snapshot();
    const auto* siblings=&snapshot;auto result=path;
    for(std::size_t position=0;position<result.occurrence_ids.size();++position) {
        auto& id=result.occurrence_ids[position];
        std::unordered_set<std::string> visited;
        const zima::assembly::OccurrenceSnapshot* selected{};
        for(;;) {
            if(!visited.insert(id).second)throw std::invalid_argument("Cyclic derived occurrence");
            const auto found=std::find_if(siblings->begin(),siblings->end(),[&](const auto& c){return c.occurrence_id==id;});
            if(found==siblings->end())throw std::invalid_argument("Derived occurrence source is missing");
            selected=&*found;
            if(selected->derived_source_id.empty())break;
            id=selected->derived_source_id;
            if(selected->pattern_group&&position+1<result.occurrence_ids.size()&&
                std::ranges::any_of(selected->children,[&](const auto& c){return c.occurrence_id==result.occurrence_ids[position+1];}))
                result.occurrence_ids.erase(result.occurrence_ids.begin()+static_cast<std::ptrdiff_t>(position+1));
        }
        siblings=&selected->children;
    }
    return result;
}

std::optional<OccurrenceAddress> Workspace::activate_occurrence(
    const std::string& top_assembly_document_id,
    const zima::assembly::InstancePath& instance_path) {
    if (open_assembly(top_assembly_document_id) == nullptr) {
        throw std::invalid_argument(
            "Occurrence activation requires an open top-level Assembly");
    }
    auto address = resolve_occurrence(top_assembly_document_id, derived_source_path(top_assembly_document_id,instance_path));
    if (!address || find(address->source_document_id) == nullptr) return std::nullopt;
    display_top_level(top_assembly_document_id);
    activate(address->source_document_id);
    active_occurrence_path_=address->instance_path.encoded();
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
    return context_original_reference_geometry(*this,top_assembly_document_id,
        dependent_instance_path,source_document_id);
}

bool Workspace::refresh_context_external_references(
    zima::document::PartDocument& document) const {
    using Context = std::tuple<std::string, std::string, std::string>;
    std::set<Context> contexts;
    std::vector<std::pair<std::string*,zima::sketcher::Sketch>> owned;
    for(auto& container:document.history)
        zima::document::visit_feature_sketches(container,[&](auto& data,std::size_t) {
            owned.emplace_back(&data,zima::sketcher::Sketch::from_serialized(data));
        });
    std::vector<zima::sketcher::Sketch*> sketches;
    for(auto& sketch:document.sketches)sketches.push_back(&sketch);
    for(auto& entry:owned)sketches.push_back(&entry.second);
    for(auto& section:document.sections)sketches.push_back(&section.sketch);
    for (const auto* source : sketches) {
        const auto& sketch=*source;
        for (const auto& reference : sketch.external_references) {
            if (reference.context_assembly_document_id.empty()) continue;
            contexts.emplace(reference.context_assembly_document_id,
                reference.context_instance_path,
                reference.source_document_id);
        }
    }
    bool changed = false;
    std::unordered_set<zima::sketcher::Sketch*> changed_sketches;
    for (const auto& [assembly_id, dependent_path, source_document_id] : contexts) {
        zima::kernel::ViewerReferenceGeometry geometry;
        if (open_assembly(assembly_id) != nullptr) {
            geometry = authoritative_external_reference_geometry(
                assembly_id, zima::assembly::InstancePath::decode(dependent_path),
                source_document_id);
        }
        for (auto* target : sketches) {
            auto& sketch=*target;
            const bool owns_context = std::any_of(
                sketch.external_references.begin(),
                sketch.external_references.end(), [&](const auto& reference) {
                    return reference.context_assembly_document_id == assembly_id &&
                        reference.context_instance_path == dependent_path &&
                        reference.source_document_id == source_document_id;
                });
            if (owns_context && sketch.refresh_external_references(
                    source_document_id, document.sketch_reference_geometry_for(sketch, geometry))) {
                changed = true;
                changed_sketches.insert(&sketch);
            }
        }
    }
    for(auto& [data,sketch]:owned)if(changed_sketches.contains(&sketch))*data=sketch.serialized();
    return changed;
}

void Workspace::synchronize_external_sketch_dependencies() {
    reconcile_external_sketch_dependencies(*this);
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
    scene.constraint_markers.insert(scene.constraint_markers.end(),
        calculated_source.mesh.constraint_markers.begin(),
        calculated_source.mesh.constraint_markers.end());
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

zima::kernel::ViewerMesh Workspace::build_scene_with_assembly_override(
    const std::string& top_assembly_document_id,
    const zima::assembly::InstancePath& instance_path,
    const zima::assembly::AssemblyDocument& calculated_source) const {
    if (instance_path.occurrence_ids.empty()) {
        throw std::invalid_argument("Assembly override requires an exact instance path");
    }
    const auto rebuild = [&](const auto& self,
                             const zima::assembly::AssemblyDocument& owner,
                             std::size_t depth) -> zima::assembly::AssemblyDocument {
        auto result = owner;
        auto* occurrence = result.find_occurrence(
            instance_path.occurrence_ids[depth]);
        if (occurrence == nullptr || occurrence->source_kind !=
                zima::assembly::ComponentSourceKind::Assembly) {
            throw std::invalid_argument(
                "Assembly override path does not resolve to an Assembly occurrence");
        }
        zima::assembly::AssemblyDocument nested;
        if (depth + 1 == instance_path.occurrence_ids.size()) {
            if (occurrence->source_document_id != calculated_source.document_id) {
                throw std::invalid_argument(
                    "Assembly override source does not match the exact occurrence");
            }
            nested = calculated_source;
        } else {
            const auto* source = open_assembly(occurrence->source_document_id);
            if (source == nullptr) {
                throw std::invalid_argument(
                    "Nested Assembly override requires its open owner chain");
            }
            nested = self(self, source->session.document(), depth + 1);
        }
        zima::kernel::BodyResult snapshot;
        snapshot.mesh = nested.build_scene();
        for (const auto& child : nested.components)
            snapshot.body_outputs.emplace(child.occurrence_id, child.calculated_source);
        occurrence->calculated_source = std::move(snapshot);
        occurrence->nested_snapshot = nested.occurrence_snapshot();
        return result;
    };
    const auto* top = open_assembly(top_assembly_document_id);
    if (top == nullptr) {
        throw std::invalid_argument("Assembly override target must be open");
    }
    return rebuild(rebuild, top->session.document(), 0).build_scene();
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
    if (part->session.document().history.empty() ||
            (calculated.empty() && !part->session.document().kernel_operations().empty())) {
        throw std::runtime_error("Open Part has no explicit calculated result");
    }
    auto next = assembly->session.document();
    auto occurrence = zima::assembly::AssemblyDocument::create_part_occurrence(
        std::move(occurrence_name), part_document_id, part->path,
        part_snapshot(*part));
    occurrence.density_kg_mm3=zima::document::material_density_kg_mm3(part->session.document());
    occurrence.body_color = part->session.document().body_color;
    occurrence.appearance = part_appearance(part->session.document());
    occurrence.face_colors = part->session.document().face_colors;
    const std::string occurrence_id = occurrence.occurrence_id;
    next.components.push_back(std::move(occurrence));
    static_cast<void>(next.build_scene());
    zima::document::refresh_physical_relations(next,zima::assembly::physical_values(next));
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
    std::vector<std::string> recursion_stack;
    auto calculated_source = refreshed_assembly(
        source_assembly_document_id, recursion_stack);
    calculate_assembly_cuts(calculated_source);
    auto occurrence = zima::assembly::AssemblyDocument::create_assembly_occurrence(
        std::move(occurrence_name), source_assembly_document_id, source->path,
        calculated_source);
    zima::assembly::capture_nested_mass(occurrence,calculated_source);
    const std::string occurrence_id = occurrence.occurrence_id;
    next.components.push_back(std::move(occurrence));
    static_cast<void>(next.build_scene());
    zima::document::refresh_physical_relations(next,zima::assembly::physical_values(next));
    owner->session.commit(std::move(next));
    return occurrence_id;
}

zima::assembly::AssemblyDocument Workspace::refreshed_assembly(
    const std::string& assembly_document_id,
    std::vector<std::string>& recursion_stack,
    const std::filesystem::path& source_path) const {
    if (std::find(recursion_stack.begin(), recursion_stack.end(), assembly_document_id) !=
        recursion_stack.end()) {
        throw std::runtime_error("Open Assembly dependency chain contains a cycle");
    }
    const auto* assembly = open_assembly(assembly_document_id);
    auto refreshed = assembly ? assembly->session.document()
        : zima::assembly::AssemblyDocument::load(source_path);
    if (refreshed.document_id != assembly_document_id)
        throw std::runtime_error("Assembly dependency document identity mismatch");
    const auto owner_path = assembly ? assembly->path : source_path;
    recursion_stack.push_back(assembly_document_id);
    for (auto& occurrence : refreshed.components) {
        if(occurrence.derived_copy)continue;
        auto dependency_path = occurrence.source_path;
        if (dependency_path.is_relative()) dependency_path = owner_path.parent_path() / dependency_path;
        if (occurrence.source_kind == zima::assembly::ComponentSourceKind::Assembly) {
            {
                auto nested = refreshed_assembly(
                    occurrence.source_document_id, recursion_stack, dependency_path);
                calculate_assembly_cuts(nested);
                occurrence.calculated_source = zima::assembly::AssemblyDocument::create_assembly_occurrence(
                    occurrence.name, occurrence.source_document_id, dependency_path, nested).calculated_source;
                zima::assembly::capture_nested_mass(occurrence,nested);
                occurrence.nested_snapshot = nested.occurrence_snapshot();
                if (const auto* open = open_assembly(occurrence.source_document_id))
                    occurrence.source_path = open->path;
            }
            continue;
        }
        const auto* part = open_part(occurrence.source_document_id);
        std::optional<PartState> loaded;
        if (!part) {
            std::vector<zima::kernel::BodyResult> boundaries;
            auto source = zima::document::PartDocument::load(dependency_path, &boundaries);
            if (source.document_id != occurrence.source_document_id)
                throw std::runtime_error("Part dependency document identity mismatch");
            loaded.emplace(PartState{zima::document::DocumentSession(std::move(source), std::move(boundaries)), dependency_path});
            part = &*loaded;
        }
        const auto& calculated = part->session.calculated_boundaries();
        if (part->session.document().history.empty() ||
            (calculated.empty() && !part->session.document().kernel_operations().empty())) {
            throw std::runtime_error(
                "An open Assembly dependency has no calculated Part result");
        }
        occurrence.calculated_source = part_snapshot(*part);
        occurrence.density_kg_mm3=zima::document::material_density_kg_mm3(part->session.document());
        occurrence.body_color = part->session.document().body_color;
        occurrence.appearance = part_appearance(part->session.document());
        occurrence.face_colors = part->session.document().face_colors;
        occurrence.source_path = part->path;
    }
    recursion_stack.pop_back();
    refreshed.resolve_constructions();
    refreshed.calculate_placement_references();
    zima::kernel::OcctKernel mirror_kernel;refreshed.calculate_derived_copies(mirror_kernel);
    static_cast<void>(refreshed.build_scene());
    return refreshed;
}

void Workspace::calculate_assembly_cuts(
    zima::assembly::AssemblyDocument& document) const {
    zima::kernel::OcctKernel kernel;
    for (auto& cut : document.cuts) {
        cut.input_component_bodies.clear();
        for (const auto& component : document.components) {
            cut.input_component_bodies.emplace(
                component.occurrence_id, component.calculated_source);
        }
        if (cut.definition.suppressed || cut.target_occurrence_ids.empty()) continue;
        zima::document::PartDocument cutter;
        cutter.document_precision = document.document_precision;
        cutter.user_parameters = document.user_parameters;
        cutter.relations = document.relations;
        cutter.sketches = document.sketches;
        cutter.constructions = document.constructions;
        cutter.history = {cut.definition};
        auto operations = cutter.kernel_operations(true);
        if (operations.size() != 1) {
            throw std::runtime_error("Assembly cut did not produce one cutter operation");
        }
        operations.front().operation = zima::kernel::BooleanOperation::Add;
        const auto boundaries = kernel.evaluate_history(operations);
        if (boundaries.empty()) throw std::runtime_error("Assembly cut body is empty");
        for (const auto& target_id : cut.target_occurrence_ids) {
            auto* target = document.find_occurrence(target_id);
            if (target == nullptr) {
                throw std::runtime_error(
                    "Assembly cut target must be an immediate component occurrence");
            }
            if (target->suppressed) continue;
            target->calculated_source = kernel.subtract_bodies(
                zima::assembly::calculate_component_body(*target,kernel), boundaries.back(),
                {target->placement.x, target->placement.y, target->placement.z},
                {target->placement.rotation_x, target->placement.rotation_y,
                 target->placement.rotation_z}, operations.front().boolean_tolerance,
                operations.front().mesh_deflection);
        }
    }
    document.calculate_derived_copies(kernel);
    if(!document.sections.empty()){
        auto mesh=document.build_scene();
        append_mesh(mesh,document.origin_viewer_mesh());
        append_mesh(mesh,document.construction_viewer_mesh());
        zima::document::resolve_section_placements(document.sections,mesh.original_references);
    }
}

zima::assembly::AssemblyDocument Workspace::prepare_assembly_calculation(
    const std::string& assembly_document_id) const {
    if(!open_assembly(assembly_document_id))
        throw std::invalid_argument("Regenerate target must be an open Assembly");
    std::vector<std::string> recursion_stack;
    return refreshed_assembly(assembly_document_id,recursion_stack);
}

void Workspace::regenerate_assembly_from_open_dependencies(
    const std::string& assembly_document_id) {
    auto* assembly = open_assembly(assembly_document_id);
    if (assembly == nullptr) {
        throw std::invalid_argument("Regenerate target must be an open Assembly");
    }
    auto refreshed = prepare_assembly_calculation(assembly_document_id);
    assembly->session.update_dependency_snapshots(std::move(refreshed));
}

}  // namespace zima::workspace
