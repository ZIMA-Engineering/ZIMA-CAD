#pragma once
#include <filesystem>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace zima::document {
// Native document identity does not change when its file is renamed.
struct FileRelocation {
    std::string document_id;
    std::filesystem::path from, to;
};
// Short-lived metadata edit batch. Collect every edit before applying any.
// The target strings/paths must remain alive and unmoved until apply().
class FileRelocationEdits {
public:
    explicit FileRelocationEdits(std::span<const FileRelocation>);
    void document_name(const std::string& id, std::string& name);
    void source_filename(const std::string& id, std::string& filename);
    void source_reference(const std::string& id, std::filesystem::path& path,
        const std::filesystem::path& owning_file, std::string* name = nullptr);
    [[nodiscard]] bool empty() const { return names_.empty() && paths_.empty(); }
    [[nodiscard]] std::size_t edit_count() const { return names_.size() + paths_.size(); }
    void track_generation(std::uint64_t&);
    void apply() noexcept;
private:
    std::vector<FileRelocation> relocations_;
    std::vector<std::pair<std::string*, std::string>> names_;
    std::vector<std::pair<std::filesystem::path*, std::filesystem::path>> paths_;
    std::vector<std::uint64_t*> generations_;
    void name(std::string&, const FileRelocation&);
};
} // namespace zima::document
