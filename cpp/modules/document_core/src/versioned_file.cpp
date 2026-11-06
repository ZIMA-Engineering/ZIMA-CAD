#include <zima/document/versioned_file.hpp>
#include <zima/document/file_path.hpp>

#include <chrono>
#include <system_error>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace zima::document {
void publish_temporary_file(const std::filesystem::path& temporary,
                            const std::filesystem::path& target) {
    const auto source = native_file_io_path(temporary);
    const auto destination = native_file_io_path(target);
#ifdef _WIN32
    constexpr unsigned attempts = 40;
    std::error_code last_error;
    for (unsigned attempt = 0; attempt < attempts; ++attempt) {
        bool published = false;
        if (GetFileAttributesW(destination.c_str()) != INVALID_FILE_ATTRIBUTES) {
            published = ReplaceFileW(destination.c_str(), source.c_str(), nullptr,
                                     REPLACEFILE_WRITE_THROUGH, nullptr, nullptr);
        } else {
            published = MoveFileExW(source.c_str(), destination.c_str(),
                                    MOVEFILE_WRITE_THROUGH);
        }
        if (published) {
            return;
        }
        const auto code = GetLastError();
        last_error = std::error_code(static_cast<int>(code),
                                     std::system_category());
        if (code != ERROR_ACCESS_DENIED &&
            code != ERROR_SHARING_VIOLATION &&
            code != ERROR_LOCK_VIOLATION &&
            code != ERROR_UNABLE_TO_REMOVE_REPLACED &&
            code != ERROR_UNABLE_TO_MOVE_REPLACEMENT &&
            code != ERROR_UNABLE_TO_MOVE_REPLACEMENT_2) {
            break;
        }
        if (attempt + 1 < attempts)
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    throw std::filesystem::filesystem_error(
        "Cannot publish native document", temporary, target, last_error);
#else
    std::filesystem::rename(source, destination);
#endif
}
} // namespace zima::document
