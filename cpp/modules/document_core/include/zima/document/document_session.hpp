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

class DocumentSession {
public:
    explicit DocumentSession(
        PartDocument document,
        std::vector<zima::kernel::BodyResult> calculated_boundaries = {});

    [[nodiscard]] const PartDocument& document() const;
    [[nodiscard]] std::uint64_t revision() const;
    [[nodiscard]] bool is_dirty() const;
    [[nodiscard]] bool can_undo() const;
    [[nodiscard]] bool can_redo() const;
    [[nodiscard]] const std::vector<zima::kernel::BodyResult>&
        calculated_boundaries() const;
    [[nodiscard]] std::optional<zima::kernel::BodyResult>
        calculated_boundary(std::size_t operation_count) const;
    [[nodiscard]] std::optional<HistoryRollbackBoundary> rollback_boundary(
        const std::string& container_id) const;

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
    void mark_saved();

private:
    struct State {
        PartDocument document;
        std::vector<zima::kernel::BodyResult> calculated_boundaries;
        std::uint64_t revision{};
        bool calculated_state_dirty{};
    };

    State current_;
    std::vector<State> undo_;
    std::vector<State> redo_;
    std::uint64_t next_revision_{1};
    std::uint64_t saved_revision_{};
};

}  // namespace zima::document
