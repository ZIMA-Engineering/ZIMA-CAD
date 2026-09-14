#pragma once
#include <filesystem>
#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>
#include <zima/document/file_path.hpp>

namespace zima::document {
// Decimal text avoids overflow and preserves the exact suffix in file queries.
[[nodiscard]] inline std::string archive_version(const std::filesystem::path& path) {
    auto suffix = path_to_utf8(path.extension());
    if (suffix.size() < 2 || suffix.front() != '.') return {};
    suffix.erase(0, 1);
    if (!std::ranges::all_of(suffix, [](unsigned char c) { return c >= '0' && c <= '9'; })) return {};
    return suffix;
}
[[nodiscard]] inline std::string normalized_archive_version(std::string value) {
    const auto first = value.find_first_not_of('0');
    return first == std::string::npos ? "0" : value.substr(first);
}
[[nodiscard]] inline bool archive_version_less(
    const std::filesystem::path& left, const std::filesystem::path& right) {
    const auto a = normalized_archive_version(archive_version(left));
    const auto b = normalized_archive_version(archive_version(right));
    if (a.size() != b.size()) return a.size() < b.size();
    if (a != b) return a < b;
    return left.native() < right.native();
}
[[nodiscard]] inline std::string next_archive_version(std::string value) {
    value = normalized_archive_version(std::move(value));
    for (auto it = value.rbegin(); it != value.rend(); ++it) {
        if (*it != '9') { ++*it; return value; }
        *it = '0';
    }
    return "1" + value;
}
[[nodiscard]] inline std::vector<std::filesystem::path> archive_paths(
    const std::filesystem::path& target) {
    const auto directory = target.parent_path().empty()
        ? std::filesystem::current_path() : target.parent_path();
    if (!std::filesystem::is_directory(directory)) return {};
    std::vector<std::filesystem::path> result;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!std::filesystem::is_regular_file(entry.symlink_status())) continue;
        if (entry.path().filename().stem() != target.filename() ||
            archive_version(entry.path()).empty()) continue;
        result.push_back(entry.path());
    }
    std::ranges::sort(result, archive_version_less);
    return result;
}
inline void archive_existing_file(const std::filesystem::path& target) {
    if (!std::filesystem::exists(target)) return;
    if (!std::filesystem::is_regular_file(target))
        throw std::runtime_error("Document target is not a regular file");
    const auto existing = archive_paths(target);
    auto version = existing.empty() ? std::string{"1"}
        : next_archive_version(archive_version(existing.back()));
    for (;;) {
        // Appending ASCII to the native path preserves Unicode on Windows.
        auto archive = target;
        archive += "." + version;
        std::error_code error;
        if (std::filesystem::copy_file(target, archive, std::filesystem::copy_options::none, error)) return;
        if (error == std::errc::file_exists) { version = next_archive_version(std::move(version)); continue; }
        throw std::filesystem::filesystem_error("Cannot archive existing document", target, archive, error);
    }
}
} // namespace zima::document
