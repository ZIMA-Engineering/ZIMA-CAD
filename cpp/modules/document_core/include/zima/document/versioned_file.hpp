#pragma once

#include <filesystem>
#include <algorithm>
#include <cctype>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

namespace zima::document {

[[nodiscard]] inline std::vector<std::filesystem::path> archive_paths(
    const std::filesystem::path& target) {
    std::vector<std::pair<unsigned long long, std::filesystem::path>> numbered;
    const auto directory = target.parent_path().empty()
        ? std::filesystem::current_path() : target.parent_path();
    if (!std::filesystem::is_directory(directory)) return {};
    const std::string prefix = target.filename().string() + ".";
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        const auto name = entry.path().filename().string();
        if (!name.starts_with(prefix)) continue;
        const auto suffix = name.substr(prefix.size());
        if (suffix.empty() || !std::ranges::all_of(
                suffix, [](unsigned char value) { return std::isdigit(value); })) continue;
        numbered.emplace_back(std::stoull(suffix), entry.path());
    }
    std::ranges::sort(numbered, {}, &std::pair<unsigned long long,
        std::filesystem::path>::first);
    std::vector<std::filesystem::path> result;
    for (auto& [version, path] : numbered) result.push_back(std::move(path));
    return result;
}

inline void archive_existing_file(const std::filesystem::path& target) {
    if (!std::filesystem::exists(target)) return;
    if (!std::filesystem::is_regular_file(target)) {
        throw std::runtime_error("Document target is not a regular file");
    }
    unsigned long long version = 1;
    const auto existing = archive_paths(target);
    if (!existing.empty()) {
        const auto suffix = existing.back().filename().string().substr(
            target.filename().string().size() + 1);
        version = std::stoull(suffix) + 1;
    }
    for (;;) {
        const auto archive = target.parent_path() /
            (target.filename().string() + "." + std::to_string(version++));
        std::error_code error;
        if (std::filesystem::copy_file(
                target, archive, std::filesystem::copy_options::none, error)) return;
        if (error == std::errc::file_exists) continue;
        throw std::filesystem::filesystem_error(
            "Cannot archive existing document", target, archive, error);
    }
}

}  // namespace zima::document
