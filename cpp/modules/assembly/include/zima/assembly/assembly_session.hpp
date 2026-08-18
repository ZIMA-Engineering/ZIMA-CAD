#pragma once

#include <zima/assembly/assembly_document.hpp>

#include <cstdint>
#include <vector>

namespace zima::assembly {

class AssemblySession {
public:
    explicit AssemblySession(AssemblyDocument document);

    [[nodiscard]] const AssemblyDocument& document() const;
    [[nodiscard]] std::uint64_t revision() const;
    [[nodiscard]] bool is_dirty() const;
    [[nodiscard]] bool can_undo() const;
    [[nodiscard]] bool can_redo() const;
    void replace(AssemblyDocument document);
    void commit(AssemblyDocument document);
    void update_dependency_snapshots(AssemblyDocument document);
    bool undo();
    bool redo();
    void mark_saved();

private:
    struct State {
        AssemblyDocument document;
        std::uint64_t revision{};
        bool dependency_state_dirty{};
    };
    State current_;
    std::vector<State> undo_;
    std::vector<State> redo_;
    std::uint64_t next_revision_{1};
    std::uint64_t saved_revision_{};
};

}  // namespace zima::assembly
