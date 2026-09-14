#pragma once
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace zima::workspace {
struct ArchiveError : std::runtime_error {
    std::string code;
    ArchiveError(std::string code, const char* message)
        : std::runtime_error(message), code(std::move(code)) {}
};
struct ArchiveFile {
    std::filesystem::path path;
    std::string version;
    std::uintmax_t size{};
    std::filesystem::file_time_type modified{};
};
using ArchiveGroups = std::map<std::filesystem::path, std::vector<ArchiveFile>>;
// Only direct, regular numbered files; never follows a file symlink or recurses.
// Results are oldest first. Missing base documents are allowed.
[[nodiscard]] std::vector<ArchiveFile> document_archives(const std::filesystem::path&);
[[nodiscard]] ArchiveGroups directory_archives(const std::filesystem::path&);
[[nodiscard]] std::vector<ArchiveFile> archives_to_remove(const ArchiveGroups&, std::size_t keep_latest);
struct ArchiveRemoval {
    std::vector<std::filesystem::path> removed;
    std::uintmax_t removed_bytes{};
    std::filesystem::path failed_path;
    std::string code, message;
    [[nodiscard]] bool ok() const { return code.empty(); }
};
// Validates the whole snapshot first. A later OS failure reports precisely
// which files were already removed; filesystem deletion is not model Undo.
[[nodiscard]] ArchiveRemoval remove_archives(const std::vector<ArchiveFile>&);
} // namespace zima::workspace
