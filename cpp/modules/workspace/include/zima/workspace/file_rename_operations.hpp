#pragma once
#include <zima/workspace/workspace.hpp>

namespace zima::workspace {
struct FileRenameError : std::runtime_error {
    std::string code;
    FileRenameError(std::string code, const char* text) : std::runtime_error(text), code(std::move(code)) {}
};
struct FileRenameResult {
    std::string document_id;
    std::filesystem::path from, to;
    bool changed{};
    std::vector<std::filesystem::path> updated_files, recovery_paths;
    std::filesystem::path failed_path;
    std::string code, message;
    [[nodiscard]] bool ok() const { return code.empty(); }
};
// Captures only open-document tokens and paths, never copies their body caches.
class FileRenameJob {
public:
    FileRenameJob(FileRenameJob&&) noexcept;
    FileRenameJob& operator=(FileRenameJob&&) noexcept;
    ~FileRenameJob();
    // Private native snapshots; may run on a worker without accessing Workspace.
    void stage();
    // Owner thread. Validate again, publish files, then apply metadata without allocation.
    [[nodiscard]] FileRenameResult commit(Workspace&);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit FileRenameJob(std::unique_ptr<Impl>);
    friend FileRenameJob prepare_document_file_rename(
        const Workspace&, const std::string&, const std::string&, const std::filesystem::path&);
};
[[nodiscard]] FileRenameJob prepare_document_file_rename(
    const Workspace&, const std::string& document_id, const std::string& filename,
    const std::filesystem::path& working_directory);
} // namespace zima::workspace
