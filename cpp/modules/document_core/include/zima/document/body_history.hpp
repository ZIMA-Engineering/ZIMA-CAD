#pragma once

#include <zima/document/history_identity.hpp>
#include <zima/document/derived_copy.hpp>
#include <zima/document/placement_types.hpp>
#include <zima/kernel/geometry_kernel.hpp>
#include <string_view>
#include <functional>

namespace zima::document {

// Ownership/order model for independent Part branches. It stores no OCCT
// objects: activation, insertion and reorder are document-only transactions.
struct BodyPlacement {
    std::string id;
    Placement placement;
    [[nodiscard]] zima::kernel::Vec3 translation() const { return {placement.x,placement.y,placement.z}; }
    [[nodiscard]] zima::kernel::Vec3 rotation_degrees() const {
        return {placement.rotation_x,placement.rotation_y,placement.rotation_z};
    }
    bool operator==(const BodyPlacement&) const = default;
};

struct BodyBoolean {
    std::string id;
    std::string name;
    zima::kernel::BodyCombination operation{zima::kernel::BodyCombination::Subtract};
    std::string target_id;
    std::string tool_id;
    bool visible{true};
    bool operator==(const BodyBoolean&) const = default;
};

struct BodyHistory {
    BodyPlacement scope;
    std::string name;
    bool visible{true};
    std::vector<PartHistoryEntry> entries;
    std::size_t cursor{};
    // Explicit placement/reference dependencies between independent bodies.
    std::vector<std::string> dependencies;
    std::optional<DerivedCopyParameters> derived_copy;
    [[nodiscard]] ContainerOrigin origin() const { return create_container_origin(scope.id); }
    bool operator==(const BodyHistory&) const = default;
};

struct BodyHistoryBoundary {
    std::string body_id;
    std::size_t entry_count{};
    bool operator==(const BodyHistoryBoundary&) const = default;
};

class BodyHistoryGraph {
public:
    using CompileEntry = std::function<std::optional<zima::kernel::HistoryOperation>(const PartHistoryEntry&)>;
    [[nodiscard]] const std::vector<BodyHistory>& bodies() const { return bodies_; }
    [[nodiscard]] const std::vector<BodyBoolean>& booleans() const { return booleans_; }
    [[nodiscard]] const std::vector<std::string>& order() const { return order_; }
    [[nodiscard]] const BodyBoolean* find_boolean(const std::string& id) const;
    [[nodiscard]] std::string create_boolean(std::string name, zima::kernel::BodyCombination operation,
        std::string target_id, std::string tool_id);
    void update_boolean(BodyBoolean operation);
    void erase_step(const std::string& id);
    void move_step(const std::string& id, std::size_t destination);
    [[nodiscard]] const std::string& active_body_id() const { return active_; }
    [[nodiscard]] std::size_t insertion_cursor() const { return cursor_; }
    [[nodiscard]] const BodyHistory* find(const std::string& id) const;
    [[nodiscard]] const BodyHistory* owner(const std::string& entry_id) const;
    [[nodiscard]] std::string create_body(std::string name);
    void update_body(BodyHistory body);
    [[nodiscard]] std::string create_derived_copy(BodyHistory body);
    void activate(const std::string& id);
    void set_insertion_cursor(std::size_t cursor);
    void set_history_cursor(const std::string& body_id, std::size_t cursor);
    void insert(PartHistoryEntry entry);
    void move_body(const std::string& id, std::size_t destination);
    [[nodiscard]] BodyHistoryBoundary rollback_before(const std::string& entry_id) const;
    [[nodiscard]] std::vector<std::string> available_before(std::size_t boundary) const;
    [[nodiscard]] std::vector<std::string> visible_context() const;
    [[nodiscard]] std::vector<zima::kernel::HistoryOperation> compile(const CompileEntry& compiler) const;
    [[nodiscard]] std::string serialized() const;
    [[nodiscard]] static BodyHistoryGraph from_serialized(std::string_view source);
    void validate() const;
    bool operator==(const BodyHistoryGraph&) const = default;
private:
    std::vector<BodyHistory> bodies_;
    std::vector<BodyBoolean> booleans_;
    std::vector<std::string> order_;
    void sort_bodies();
    std::string active_;
    std::size_t cursor_{};
};

} // namespace zima::document
