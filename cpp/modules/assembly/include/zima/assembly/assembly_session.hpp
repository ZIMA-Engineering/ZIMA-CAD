#pragma once

#include <zima/assembly/assembly_document.hpp>
#include <zima/document/file_relocation.hpp>

#include <cstdint>
#include <vector>
#include <memory>

namespace zima::assembly {

class AssemblySession {
public:
    explicit AssemblySession(AssemblyDocument document);
    AssemblySession(const AssemblySession&);
    AssemblySession& operator=(const AssemblySession&);
    AssemblySession(AssemblySession&&) noexcept = default;
    AssemblySession& operator=(AssemblySession&&) noexcept = default;

    [[nodiscard]] const AssemblyDocument& document() const;
    [[nodiscard]] std::uint64_t revision() const;
    [[nodiscard]] std::uint64_t data_generation() const { return data_generation_; }
    [[nodiscard]] bool is_dirty() const;
    [[nodiscard]] bool can_undo() const;
    [[nodiscard]] bool can_redo() const;
    void replace(AssemblyDocument document);
    void commit(AssemblyDocument document);
    void update_dependency_snapshots(AssemblyDocument document);
    // Display cache update only. Does not create an edit/Undo item or solve mates.
    void update_source_geometry(AssemblyDocument document);
    bool undo();
    bool redo();
    // Append a deferred metadata batch. Owner/session storage must stay stable
    // until the caller applies or discards it on the Workspace owner thread.
    void prepare_native_file_rebase(document::FileRelocationEdits&, const std::filesystem::path& owning_file);
    // Metadata-only rebase after native file relocation; preserves history,
    // calculated geometry and dirty state. Never invokes the modeling kernel.
    void rebase_native_files(std::span<const document::FileRelocation>, const std::filesystem::path& owning_file);
    void mark_saved();

private:
    struct State {
        AssemblyDocument document;
        std::uint64_t revision{};
        bool dependency_state_dirty{};
    };
    using States = std::vector<std::unique_ptr<State>>;
    [[nodiscard]] static States copy_states(const States&);
    bool step(States& from, States& to);
    std::uint64_t data_generation_{};
    std::unique_ptr<State> current_;
    States undo_;
    States redo_;
    std::uint64_t next_revision_{1};
    std::uint64_t saved_revision_{};
    std::uint64_t saved_dimension_allocations_{};
};

}  // namespace zima::assembly
