#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace zima::workspace {
class Workspace;
struct FileRemovalError : std::runtime_error {
    std::string code;
    FileRemovalError(std::string code, const char* message)
        : std::runtime_error(message), code(std::move(code)) {}
};
struct FileRemovalResult {
    std::vector<std::filesystem::path> removed;
    std::uintmax_t removed_bytes{};
    std::string closed_document;
    std::filesystem::path failed_path;
    std::string code, message;
    [[nodiscard]] bool ok() const { return code.empty(); }
};
class FileRemovalPlan {
public:
    [[nodiscard]] const std::string& document_id() const { return id_; }
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }
    [[nodiscard]] bool has_unsaved_changes() const { return dirty_; }
private:
    friend FileRemovalPlan prepare_document_file_removal(
        const Workspace&, const std::string&, bool);
    friend FileRemovalResult remove_document_file(Workspace&, const FileRemovalPlan&);
    FileRemovalPlan() = default;
    std::string id_;
    std::filesystem::path path_;
    std::shared_ptr<const int> runtime_identity_;
    std::uint64_t revision_{}, generation_{}, allocations_{};
    std::uintmax_t size_{};
    std::filesystem::file_time_type modified_{};
    bool dirty_{};
};
// Preparing only reads. No close, write, deletion, calculation or model Undo.
// Targets an open Part/Assembly/Drawing by its document ID and stored file path.
[[nodiscard]] FileRemovalPlan prepare_document_file_removal(
    const Workspace&, const std::string& document_id,
    bool discard_unsaved_changes = false);
// Runs synchronously on the Workspace owner thread. Rechecks the exact snapshot,
// removes the current file, then closes it without Save.
// Any OS failure returns precise completed effects; file deletion has no Undo.
[[nodiscard]] FileRemovalResult remove_document_file(Workspace&, const FileRemovalPlan&);
} // namespace zima::workspace
