#include <zima/workspace/file_removal_operations.hpp>
#include <zima/workspace/document_operations.hpp>
#include <zima/workspace/native_documents.hpp>
#include <algorithm>
#include <type_traits>

namespace zima::workspace {
namespace fs = std::filesystem;
namespace {
template<class State>
const auto& model(const State& state) {
    if constexpr(std::is_same_v<State, DrawingState>) return state.document();
    else return state.session.document();
}
template<class State>
const auto& session(const State& state) {
    if constexpr(std::is_same_v<State, DrawingState>) return state;
    else return state.session;
}
bool same_archives(const std::vector<ArchiveFile>& a, const std::vector<ArchiveFile>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
        [](const auto& left, const auto& right) {
            return left.path == right.path && left.version == right.version &&
                left.size == right.size && left.modified == right.modified;
        });
}
}
FileRemovalPlan prepare_document_file_removal(
    const Workspace& live, const std::string& id, bool archives, bool discard) {
    const auto* state = live.find(id);
    if (!state) throw FileRemovalError("document_not_found", "The document is not open.");
    FileRemovalPlan plan;
    plan.id_ = id; plan.include_archives_ = archives;
    std::visit([&](const auto& value) {
        if (value.path.empty() || value.path.native().find(fs::path::value_type{}) != std::string::npos)
            throw FileRemovalError("path_required", "The document has no saved native file.");
        plan.path_ = fs::absolute(value.path).lexically_normal();
        plan.runtime_identity_ = value.runtime_identity;
        plan.revision_ = session(value).revision();
        plan.generation_ = session(value).data_generation();
        plan.allocations_ = model(value).dimension_identifiers.allocation_count();
    }, *state);
    static_cast<void>(native_document_type(plan.path_));
    if (!fs::is_regular_file(fs::symlink_status(plan.path_)))
        throw FileRemovalError("file_not_found", "The current native file is missing or is not a regular file.");
    plan.dirty_ = document_needs_save(live, id);
    if (plan.dirty_ && !discard)
        throw FileRemovalError("unsaved_changes", "The document has unsaved changes. Save it or explicitly allow discard.");
    plan.size_ = fs::file_size(plan.path_);
    plan.modified_ = fs::last_write_time(plan.path_);
    if (archives) plan.archives_ = document_archives(plan.path_);
    return plan;
}
FileRemovalResult remove_document_file(Workspace& live, const FileRemovalPlan& plan) {
    FileRemovalResult result;
    const auto fail = [&](const fs::path& path, const char* code, std::string message) {
        result.failed_path = path; result.code = code; result.message = std::move(message);
        return result;
    };
    const auto* state = live.find(plan.id_);
    if (!state || !std::visit([&](const auto& value) {
            return !value.path.empty() && fs::absolute(value.path).lexically_normal() == plan.path_ &&
                value.runtime_identity == plan.runtime_identity_ &&
                session(value).revision() == plan.revision_ &&
                session(value).data_generation() == plan.generation_ &&
                model(value).dimension_identifiers.allocation_count() == plan.allocations_;
        }, *state))
        return fail(plan.path_, "stale_document", "The document changed before file deletion.");
    try {
        if (!fs::is_regular_file(fs::symlink_status(plan.path_)) ||
            fs::file_size(plan.path_) != plan.size_ || fs::last_write_time(plan.path_) != plan.modified_)
            return fail(plan.path_, "stale_file", "The native file changed before deletion.");
        if (plan.include_archives_ && !same_archives(plan.archives_, document_archives(plan.path_)))
            return fail(plan.path_, "stale_archive", "The archive files changed; list them again before deleting.");
    } catch (const fs::filesystem_error& error) {
        return fail(plan.path_, "file_io_error", error.code().message());
    }
    std::error_code error;
    if (!fs::remove(plan.path_, error))
        return fail(plan.path_, "file_io_error", error ? error.message() : "The native file changed before deletion.");
    result.removed.push_back(plan.path_);
    result.removed_bytes = plan.size_;
    // The exact current state was checked immediately before deletion. Asking
    // Save after removing a clean file would recreate that very file.
    if (close_document(live, plan.id_, true) != CloseDocumentResult::Closed)
        return fail(plan.path_, "close_failed", "The deleted document could not be closed.");
    result.closed_document = plan.id_;
    const auto removed_archives = remove_archives(plan.archives_);
    result.removed.insert(result.removed.end(), removed_archives.removed.begin(), removed_archives.removed.end());
    result.removed_bytes += removed_archives.removed_bytes;
    if (!removed_archives.ok())
        return fail(removed_archives.failed_path, removed_archives.code.c_str(), removed_archives.message);
    return result;
}
} // namespace zima::workspace
