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
// Explicit activation shares source opening, keeps the top-level scene and
// records the exact canonical occurrence. No body calculation or mate solving.
[[nodiscard]] ComponentSourceOpen activate_component_source(Workspace&,const std::string& top,
    const assembly::InstancePath&,
    const std::function<void(std::function<void()>)>& read_runner={},
    const std::function<void(const std::filesystem::path&)>& before_read={});
[[nodiscard]] bool deactivate_component_source(Workspace&);
// Relocate an existing source by identity. Keeps occurrence IDs, placement,
// references and calculated Assembly operation results; no regeneration.
void relink_component_source(Workspace&,const std::string& owner,const std::string& occurrence,
    const std::filesystem::path&,const std::function<void(std::function<void()>)>& read_runner={});
// Explicit Open resolves the exact occurrence (or original of a derived copy).
// No source body calculation or parent regeneration. Activation is caller-owned.
[[nodiscard]] ComponentSourceOpen open_component_source(Workspace&,const std::string& top,
    const assembly::InstancePath&,
    const std::function<void(std::function<void()>)>& read_runner={},
    const std::function<void(const std::filesystem::path&)>& before_read={});
}
