#pragma once

#include <zima/document/part_document.hpp>
#include <zima/document/file_relocation.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <memory>
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
        std::vector<zima::kernel::BodyResult> calculated_boundaries = {},
        NativeCalculatedCachePtr native_cache = {});

    DocumentSession(const DocumentSession&);
    // Private read/calculation snapshot only; never publish over a live Undo session.
    [[nodiscard]] DocumentSession current_state_copy() const;
    DocumentSession& operator=(const DocumentSession&);
    DocumentSession(DocumentSession&&) noexcept = default;
    DocumentSession& operator=(DocumentSession&&) noexcept = default;

    [[nodiscard]] const PartDocument& document() const;
    [[nodiscard]] std::uint64_t revision() const;
    [[nodiscard]] std::uint64_t data_generation() const { return data_generation_; }
    [[nodiscard]] bool is_dirty() const;
    [[nodiscard]] bool can_undo() const;
    [[nodiscard]] bool can_redo() const;
    [[nodiscard]] const std::vector<zima::kernel::BodyResult>&
        calculated_boundaries() const;
    [[nodiscard]] NativeCalculatedCachePtr native_calculated_cache() const {
        return current_->native_cache;
    }
    [[nodiscard]] std::optional<zima::kernel::BodyResult>
        calculated_boundary(std::size_t operation_count) const;
    [[nodiscard]] std::optional<zima::kernel::BodyResult> calculated_body_boundary(
        const std::string& body_id, std::size_t operation_count) const;
    [[nodiscard]] std::optional<HistoryRollbackBoundary> rollback_boundary(
        const std::string& container_id) const;

    [[nodiscard]] zima::kernel::ViewerMesh body_context_mesh(const BodyHistoryGraph* context = nullptr) const;
    [[nodiscard]] std::optional<BooleanEditInputs> boolean_edit_inputs(const std::string& id) const;

    // Workspace-owned transaction routing; copied sessions deliberately omit it.
    std::function<bool(PartDocument&, std::vector<zima::kernel::BodyResult>&)> commit_interceptor;
    // Metadata commits preserve the immutable calculated packet. Family
    // workspaces may still redirect them through their shared transaction.
    std::function<bool(PartDocument&)> metadata_commit_interceptor;
    void update_family_evaluated(FamilyDocument);
    void replace(
        PartDocument document,
        std::vector<zima::kernel::BodyResult> calculated_boundaries = {});
    void commit(
        PartDocument document,
        std::vector<zima::kernel::BodyResult> calculated_boundaries = {});
    // Commit document-only state such as visibility or a history cursor. The
    // calculated geometry is immutable and shared with the new Undo revision.
    void commit_metadata(PartDocument document);
    void update_calculated_boundaries(
        std::vector<zima::kernel::BodyResult> calculated_boundaries);
    // Publish the derived physical analysis of a matching save snapshot only.
    // Preserves authored geometry, revision and both Undo/Redo stacks.
    void update_save_analysis(PartDocument, std::vector<zima::kernel::BodyResult>);
    // Attach only the cache produced by a successful save of this exact state.
    // This is disposable serialization data and does not change dirty state.
    void update_saved_native_cache(NativeCalculatedCachePtr value) {
        current_->native_cache=std::move(value);
    }
    // Borrowed candidate for validation before a cross-document history step.
    [[nodiscard]] const PartDocument* history_document(bool redo) const noexcept;
    bool undo();
    bool redo();
    // Editing focus only: keep geometry, dirty state and Undo/Redo intact.
    void activate_body(const std::string& id);
    // Append a deferred metadata batch. Owner/session storage must stay stable
    // until the caller applies or discards it on the Workspace owner thread.
    void prepare_native_file_rebase(FileRelocationEdits&);
    // Metadata-only rebase after native file relocation; preserves history,
    // calculated geometry and dirty state. Never invokes the modeling kernel.
    void rebase_native_files(std::span<const FileRelocation>);
    void mark_saved();

private:
    DocumentSession(const DocumentSession&, bool include_history);
    struct State {
        PartDocument document;
        std::shared_ptr<const std::vector<zima::kernel::BodyResult>> calculated_boundaries;
        NativeCalculatedCachePtr native_cache;
        std::uint64_t revision{};
        bool calculated_state_dirty{};
        bool externally_mutable{};
    };

    using States = std::vector<std::shared_ptr<State>>;
    [[nodiscard]] static States copy_states(const States&);
    bool step(States& from, States& to);

    std::uint64_t data_generation_{};
    // Current state is always exclusive. Only inactive history may be shared
    // across session copies; step/rebase detach before making it writable.
    std::shared_ptr<State> current_;
    States undo_;
    States redo_;
    std::uint64_t next_revision_{1};
    std::uint64_t saved_revision_{};
    std::uint64_t saved_dimension_allocations_{};
};

}  // namespace zima::document
