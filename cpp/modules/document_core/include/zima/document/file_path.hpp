#pragma once
#include <filesystem>
#include <string>

namespace zima::document {
// Native paths use wide characters on Windows. Persisted text and command JSON
// always use UTF-8, independently of the machine's ANSI code page.
inline std::string path_to_utf8(const std::filesystem::path& path) {
    const auto text=path.generic_u8string();return {text.begin(),text.end()};
}
} // namespace zima::document
