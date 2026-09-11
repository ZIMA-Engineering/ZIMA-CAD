#pragma once

#include <zima/document/part_document.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace zima::document {

struct HistoryRollbackBoundary {
    std::size_t history_index{};
    std::optional<zima::kernel::BodyResult> input_body;
};

struct BooleanEditInputs {
    std::string target_id;
    std::string tool_id;
    zima::kernel::BodyResult target;
    zima::kernel::BodyResult tool;
};

class DocumentSession {
public:
    explicit DocumentSession(
        PartDocument document,
        std::vector<zima::kernel::BodyResult> calculated_boundaries = {});

    [[nodiscard]] const PartDocument& document() const;
    [[nodiscard]] std::uint64_t revision() const;
    [[nodiscard]] std::uint64_t data_generation() const { return data_generation_; }
    [[nodiscard]] bool is_dirty() const;
    [[nodiscard]] bool can_undo() const;
    [[nodiscard]] bool can_redo() const;
    [[nodiscard]] const std::vector<zima::kernel::BodyResult>&
        calculated_boundaries() const;
    [[nodiscard]] std::optional<zima::kernel::BodyResult>
        calculated_boundary(std::size_t operation_count) const;
    [[nodiscard]] std::optional<zima::kernel::BodyResult> calculated_body_boundary(
        const std::string& body_id, std::size_t operation_count) const;
    [[nodiscard]] std::optional<HistoryRollbackBoundary> rollback_boundary(
        const std::string& container_id) const;

    [[nodiscard]] zima::kernel::ViewerMesh body_context_mesh(const BodyHistoryGraph* context = nullptr) const;
    [[nodiscard]] std::optional<BooleanEditInputs> boolean_edit_inputs(const std::string& id) const;

    void replace(
        PartDocument document,
        std::vector<zima::kernel::BodyResult> calculated_boundaries = {});
    void commit(
        PartDocument document,
        std::vector<zima::kernel::BodyResult> calculated_boundaries = {});
    void update_calculated_boundaries(
        std::vector<zima::kernel::BodyResult> calculated_boundaries);
    bool undo();
    bool redo();
    // Editing focus only: keep geometry, dirty state and Undo/Redo intact.
    void activate_body(const std::string& id);
    void mark_saved();

private:
    struct State {
        PartDocument document;
        std::vector<zima::kernel::BodyResult> calculated_boundaries;
        std::uint64_t revision{};
        bool calculated_state_dirty{};
    };

    std::uint64_t data_generation_{};
    State current_;
    std::vector<State> undo_;
    std::vector<State> redo_;
    std::uint64_t next_revision_{1};
    std::uint64_t saved_revision_{};
    std::uint64_t saved_dimension_allocations_{};
};

}  // namespace zima::document
