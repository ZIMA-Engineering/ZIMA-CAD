#include <zima/document/file_relocation.hpp>
#include <zima/document/file_path.hpp>
#include <set>
#include <algorithm>
#include <stdexcept>

namespace zima::document {
namespace fs = std::filesystem;
FileRelocationEdits::FileRelocationEdits(std::span<const FileRelocation> relocations)
    : relocations_(relocations.begin(), relocations.end()) {
    std::set<std::string> identities;
    std::set<fs::path> sources, targets;
    for (const auto& file : relocations) {
        if (file.document_id.empty() || !file.from.is_absolute() || !file.to.is_absolute() ||
            file.from.filename().empty() || file.to.filename().empty() ||
            file.from.native().find(fs::path::value_type{}) != fs::path::string_type::npos ||
            file.to.native().find(fs::path::value_type{}) != fs::path::string_type::npos ||
            file.from.lexically_normal() != file.from || file.to.lexically_normal() != file.to)
            throw std::invalid_argument("File relocation requires document identity and normalized absolute paths.");
        if (!identities.insert(file.document_id).second || !sources.insert(file.from).second ||
            !targets.insert(file.to).second)
            throw std::invalid_argument("File relocation contains duplicate document identities or paths.");
    }
}
void FileRelocationEdits::name(std::string& value, const FileRelocation& file) {
    auto next = path_to_utf8(file.to.stem());
    if (value != next) names_.emplace_back(&value, std::move(next));
}
void FileRelocationEdits::document_name(const std::string& id, std::string& value) {
    for (const auto& file : relocations_)
        if (file.document_id == id) { name(value, file); return; }
}
void FileRelocationEdits::source_filename(const std::string& id, std::string& value) {
    for (const auto& file : relocations_) if (file.document_id == id) {
        auto next = path_to_utf8(file.to.filename());
        if (value != next) names_.emplace_back(&value, std::move(next));
        return;
    }
}
void FileRelocationEdits::source_reference(const std::string& id, fs::path& path,
        const fs::path& owning_file, std::string* source_name) {
    fs::path resolved;
    if (!path.empty()) {
        if (!path.is_absolute() && !owning_file.is_absolute())
            throw std::invalid_argument("A relative native reference requires its owning document path.");
        resolved = (path.is_absolute() ? path : owning_file.parent_path() / path).lexically_normal();
    }
    for (const auto& file : relocations_) {
        const bool same_path = !resolved.empty() && resolved == file.from;
        if (same_path && id != file.document_id)
            throw std::invalid_argument("Native reference path and document identity disagree.");
        if (id != file.document_id) continue;
        if (path != file.to) paths_.emplace_back(&path, file.to);
        if (source_name) name(*source_name, file);
        return;
    }
}
void FileRelocationEdits::track_generation(std::uint64_t& generation) {
    if (std::ranges::find(generations_, &generation) == generations_.end())
        generations_.push_back(&generation);
}
void FileRelocationEdits::apply() noexcept {
    for (auto& [target, next] : names_) target->swap(next);
    for (auto& [target, next] : paths_) target->swap(next);
    for (auto* generation : generations_) ++*generation;
    names_.clear(); paths_.clear(); generations_.clear();
}
} // namespace zima::document
