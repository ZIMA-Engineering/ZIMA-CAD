#pragma once
#include <zima/command_host/host.hpp>

namespace zima::cli {
struct Settings {
    command_host::Settings documents;
    std::map<std::string,std::string> translations;
    [[nodiscard]] std::string translate(const char* source,const char* context="QMainWindow") const;
};
// Read only. Relative configured paths belong to the layer that supplied them.
[[nodiscard]] Settings load_settings(const std::filesystem::path& executable,
    const std::filesystem::path& working_directory,const std::filesystem::path& explicit_config);
} // namespace zima::cli
