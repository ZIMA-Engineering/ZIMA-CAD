#pragma once
#include <filesystem>

namespace zima::document {
// Publish a fully written sibling temporary file as the current native
// document. Windows retries short sharing violations caused by scanners that
// inspect the file; Linux uses its ordinary atomic rename contract.
void publish_temporary_file(const std::filesystem::path& temporary,
                            const std::filesystem::path& target);
} // namespace zima::document
