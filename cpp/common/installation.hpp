#pragma once
#include <filesystem>
#include <optional>
#include <vector>

namespace zima::distribution {
namespace fs = std::filesystem;
struct Installation {
    fs::path root;
    fs::path runtime;
    fs::path config;
    fs::path platform_config;
};

// Only the running executable establishes installation ownership. Project/CWD
// files and environment variables cannot redirect the writable portable root.
inline std::optional<Installation> locate_installation(const fs::path& executable) {
    const auto folder = fs::weakly_canonical(executable).parent_path();
#ifdef _WIN32
    const auto runtime = folder;
    const fs::path platform = "windows";
#else
    if (folder.filename() != "bin") return {};
    const auto runtime = folder.parent_path();
    const fs::path platform = "linux";
#endif
    if (runtime.parent_path().filename() != platform) return {};
    auto root = runtime.parent_path().parent_path();
    if (root.filename() == "custom") root = root.parent_path();
    if (!fs::is_regular_file(root / "launcher.ini") ||
        !fs::is_regular_file(runtime / "version.json") ||
        !fs::is_regular_file(runtime / "config/config.ini")) return {};
    return Installation{root, runtime, root / "config/config.ini", root / "config" / platform / "config.ini"};
}
inline std::vector<fs::path> config_layers(const Installation& installed) {
    return {installed.runtime / "config/config.ini", installed.config, installed.platform_config};
}
} // namespace zima::distribution
