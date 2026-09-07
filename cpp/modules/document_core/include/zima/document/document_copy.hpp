#pragma once
#include <filesystem>
#include <string>
namespace zima::document {
struct DocumentCopyIdentity {
    std::string document_id;
    std::filesystem::path source_path;
    std::filesystem::path target_path;
};
} // namespace zima::document
