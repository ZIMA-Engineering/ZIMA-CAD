#pragma once

#include <zima/document/part_document.hpp>

#include <cstdint>
#include <vector>

namespace zima::document {

class DocumentSession {
public:
    explicit DocumentSession(PartDocument document);

    [[nodiscard]] const PartDocument& document() const;
    [[nodiscard]] std::uint64_t revision() const;
    [[nodiscard]] bool is_dirty() const;
    [[nodiscard]] bool can_undo() const;
    [[nodiscard]] bool can_redo() const;

    void replace(PartDocument document);
    void commit(PartDocument document);
    bool undo();
    bool redo();
    void mark_saved();

private:
    struct State {
        PartDocument document;
        std::uint64_t revision{};
    };

    State current_;
    std::vector<State> undo_;
    std::vector<State> redo_;
    std::uint64_t next_revision_{1};
    std::uint64_t saved_revision_{};
};

}  // namespace zima::document
