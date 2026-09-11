#pragma once
#include <zima/workspace/workspace.hpp>

namespace zima::workspace {

class DocumentSave;

// Created only by a successful write; complete it on the Workspace owner thread.
class SavedDocument {
    friend class DocumentSave;
    friend DocumentSave prepare_document_save(const Workspace&, const std::string&, const std::filesystem::path&);
    friend bool complete_document_save(Workspace&, const SavedDocument&);
    std::string id_;
    std::shared_ptr<const int> runtime_identity_;
    std::filesystem::path original_path_, target_;
    std::size_t kind_{};
    std::uint64_t revision_{}, generation_{}, allocations_{};
    SavedDocument() = default;
};

// Owns an immutable snapshot. write() needs no Workspace, Qt or event loop.
class DocumentSave {
public:
    [[nodiscard]] SavedDocument write() const;
private:
    friend DocumentSave prepare_document_save(const Workspace&, const std::string&,
                                              const std::filesystem::path&);
    struct Part { document::PartDocument document; std::vector<kernel::BodyResult> boundaries; };
    std::variant<Part, assembly::AssemblyDocument, drawing::DrawingDocument> snapshot_;
    SavedDocument receipt_;
};

[[nodiscard]] DocumentSave prepare_document_save(const Workspace& workspace,
    const std::string& document_id, const std::filesystem::path& target);
// False if the document was closed or retargeted while writing. Never calculates
// geometry or changes active/displayed document. Newer edits remain dirty.
[[nodiscard]] bool complete_document_save(Workspace& workspace, const SavedDocument& saved);

enum class HistoryDirection { Undo, Redo };
[[nodiscard]] bool can_step_document_history(const Workspace& workspace,
    const std::string& document_id, HistoryDirection direction);
[[nodiscard]] bool step_document_history(Workspace& workspace,
    const std::string& document_id, HistoryDirection direction);

} // namespace zima::workspace
