#pragma once
#include <filesystem>
#include <string>

namespace zima::document {
// Native paths use wide characters on Windows. Persisted text and command JSON
// always use UTF-8, independently of the machine's ANSI code page.
inline std::string path_to_utf8(const std::filesystem::path& path) {
    const auto text=path.generic_u8string();return {text.begin(),text.end()};
}
// Internal I/O spelling only: never persist this Windows namespace prefix or
// use it as a document/reference identity. Leave ordinary short paths intact.
inline std::filesystem::path native_file_io_path(const std::filesystem::path& path) {
#ifdef _WIN32
    const auto& original=path.native();
    if(original.starts_with(L"\\\\?\\")||original.starts_with(L"\\\\.\\"))return path;
    auto absolute=std::filesystem::absolute(path).lexically_normal();
    absolute.make_preferred();const auto& text=absolute.native();
    // Directory creation also needs room for the legacy 8.3 child suffix.
    if(text.size()<248)return path;
    if(text.starts_with(L"\\\\"))return std::filesystem::path(L"\\\\?\\UNC\\"+text.substr(2));
    return std::filesystem::path(L"\\\\?\\"+text);
#else
    return path;
#endif
}
} // namespace zima::document
