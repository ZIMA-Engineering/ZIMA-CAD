#include <zima/workspace/archive_operations.hpp>
#include <zima/document/versioned_file.hpp>
#include <array>
#include <cctype>
#include <set>

namespace zima::workspace {
namespace fs = std::filesystem;
namespace {
bool native_extension(const fs::path& path) {
    auto ext = document::path_to_utf8(path.extension());
    std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    static constexpr std::array extensions{".prtz", ".asmz", ".drwz", ".frmz", ".tblz"};
    return std::ranges::find(extensions, ext) != extensions.end();
}
fs::path absolute_path(const fs::path& path) {
    if (path.empty()) throw ArchiveError("invalid_path", "Specify a file or directory path.");
    return fs::absolute(path).lexically_normal();
}
ArchiveFile describe(const fs::path& path) {
    return {path, document::archive_version(path), fs::file_size(path), fs::last_write_time(path)};
}
}
std::vector<ArchiveFile> document_archives(const fs::path& path) {
    const auto target = absolute_path(path);
    if (!native_extension(target))
        throw ArchiveError("unsupported_format", "Archive operations require a native document or template path.");
    std::vector<ArchiveFile> result;
    for (const auto& file : document::archive_paths(target)) result.push_back(describe(file));
    return result;
}
ArchiveGroups directory_archives(const fs::path& path) {
    const auto directory = absolute_path(path);
    if (!fs::is_directory(directory))
        throw ArchiveError("invalid_directory", "The archive directory does not exist.");
    ArchiveGroups result;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (!fs::is_regular_file(entry.symlink_status()) || document::archive_version(entry.path()).empty()) continue;
        const auto base = entry.path().parent_path() / entry.path().stem();
        if (native_extension(base)) result[base].push_back(describe(entry.path()));
    }
    for (auto& [base, files] : result)
        std::ranges::sort(files, [](const ArchiveFile& a, const ArchiveFile& b) {
            return document::archive_version_less(a.path, b.path);
        });
    return result;
}
std::vector<ArchiveFile> archives_to_remove(const ArchiveGroups& groups, std::size_t keep_latest) {
    std::vector<ArchiveFile> result;
    for (const auto& [base, files] : groups) {
        const auto count = files.size() > keep_latest ? files.size() - keep_latest : 0;
        result.insert(result.end(), files.begin(), files.begin() + count);
    }
    return result;
}
ArchiveRemoval remove_archives(const std::vector<ArchiveFile>& files) {
    ArchiveRemoval result;
    const auto fail = [&](const fs::path& path, const char* code, std::string message) {
        result.failed_path = path; result.code = code; result.message = std::move(message);
        return result;
    };
    std::set<fs::path> seen;
    for (const auto& file : files) {
        if (!file.path.is_absolute() || !native_extension(file.path.stem()) ||
            document::archive_version(file.path).empty() ||
            document::archive_version(file.path) != file.version || !seen.insert(file.path.lexically_normal()).second)
            return fail(file.path, "invalid_archive", "Only distinct numbered native archive files may be removed.");
        std::error_code error;
        const auto status = fs::symlink_status(file.path, error);
        if (error || !fs::is_regular_file(status))
            return fail(file.path, "stale_archive", "The archive files changed; list them again before deleting.");
        const auto size = fs::file_size(file.path, error);
        if (error || size != file.size)
            return fail(file.path, "stale_archive", "The archive files changed; list them again before deleting.");
        const auto time = fs::last_write_time(file.path, error);
        if (error || time != file.modified)
            return fail(file.path, "stale_archive", "The archive files changed; list them again before deleting.");
    }
    for (const auto& file : files) {
        std::error_code error;
        if (!fs::remove(file.path, error))
            return fail(file.path, "archive_io_error", error ? error.message() : "The archive file no longer exists.");
        result.removed.push_back(file.path);
        result.removed_bytes += file.size;
    }
    return result;
}
} // namespace zima::workspace
