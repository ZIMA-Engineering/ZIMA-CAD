#include <zima/workspace/workspace.hpp>

#include <algorithm>
#include <stdexcept>

namespace zima::workspace {

const std::string& Workspace::id_of(const DocumentState& state) {
    return std::visit([](const auto& document) -> const std::string& {
        using State = std::decay_t<decltype(document)>;
        if constexpr (std::is_same_v<State, PartState>) {
            return document.session.document().document_id;
        } else {
            return document.session.document().document_id;
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

const std::vector<DocumentState>& Workspace::documents() const { return documents_; }

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
        calculated.back());
    const std::string occurrence_id = occurrence.occurrence_id;
    next.components.push_back(std::move(occurrence));
    static_cast<void>(next.build_scene());
    assembly->session.commit(std::move(next));
    return occurrence_id;
}

bool Workspace::assembly_reaches(
    const std::string& start_document_id,
    const std::string& target_document_id) const {
    if (start_document_id == target_document_id) return true;
    const auto* assembly = open_assembly(start_document_id);
    if (assembly == nullptr) return false;
    for (const auto& occurrence : assembly->session.document().components) {
        if (occurrence.source_kind == zima::assembly::ComponentSourceKind::Assembly &&
            assembly_reaches(occurrence.source_document_id, target_document_id)) {
            return true;
        }
    }
    return false;
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
        source->session.document().build_scene());
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
        occurrence.calculated_source = calculated.back();
        occurrence.source_path = part->path;
    }
    recursion_stack.pop_back();
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
