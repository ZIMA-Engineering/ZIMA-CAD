#pragma once
#include <zima/workspace/component_operations.hpp>
#include <functional>
namespace zima::workspace {
struct ComponentSourceOpen {
    std::string document_id;
    std::filesystem::path path;
    assembly::InstancePath source_instance_path;
    bool opened{};
};
// Explicit Open resolves the exact occurrence (or original of a derived copy).
// No source body calculation or parent regeneration. Activation is caller-owned.
[[nodiscard]] ComponentSourceOpen open_component_source(Workspace&,const std::string& top,
    const assembly::InstancePath&,
    const std::function<void(std::function<void()>)>& read_runner={},
    const std::function<void(const std::filesystem::path&)>& before_read={});
}
