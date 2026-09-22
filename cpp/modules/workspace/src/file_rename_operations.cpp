#include <zima/workspace/file_rename_operations.hpp>
#include <zima/workspace/native_documents.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/document/file_path.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>
#include <map>
#include <set>
#include <type_traits>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace zima::workspace {
namespace fs = std::filesystem;
namespace {
fs::path normalized_path(const fs::path& path) { return path.empty() ? path : fs::absolute(path).lexically_normal(); }
std::string extension(const fs::path& path) {
    auto result = document::path_to_utf8(path.extension());
    std::ranges::transform(result, result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}
bool regular(const fs::path& path) { return fs::is_regular_file(fs::symlink_status(path)); }
template<class T> const auto& model(const T& value) {
    if constexpr (std::is_same_v<T, DrawingState>) return value.document();
    else return value.session.document();
}
template<class T> const auto& session(const T& value) {
    if constexpr (std::is_same_v<T, DrawingState>) return value;
    else return value.session;
}
struct Receipt {
    std::string id;
    fs::path path;
    std::shared_ptr<const int> identity;
    std::uint64_t revision{}, generation{}, allocations{};
    std::size_t kind{};
    std::string drawing_source_id;
    bool operator==(const Receipt&) const = default;
};
std::vector<Receipt> receipts(const Workspace& live) {
    std::vector<Receipt> result;
    for (const auto& state : live.documents()) std::visit([&](const auto& value) {
        const auto source_id = [&]() -> std::string {
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, DrawingState>)
                return value.document().source_document_id;
            else return {};
        }();
        result.push_back({model(value).document_id, normalized_path(value.path), value.runtime_identity,
            session(value).revision(), session(value).data_generation(),
            model(value).dimension_identifiers.allocation_count(), state.index(), source_id});
    }, state);
    return result;
}
struct Stamp {
    fs::path path;
    std::uintmax_t size;
    fs::file_time_type modified;
    bool operator==(const Stamp&) const = default;
};
Stamp stamp(const fs::path& path) {
    if (!regular(path)) throw FileRenameError("file_not_found", "A native rename input is missing or is not a regular file.");
    return {path, fs::file_size(path), fs::last_write_time(path)};
}
bool may_reference(const fs::path& path, const std::vector<document::FileRelocation>& moves) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw fs::filesystem_error("read", path, std::make_error_code(std::errc::io_error));
    const std::string bytes{std::istreambuf_iterator<char>(stream), {}};
    if (stream.bad()) throw fs::filesystem_error("read", path, std::make_error_code(std::errc::io_error));
    // Candidate discovery only: current native documents persist stable IDs.
    // Also retain filename matches so mismatched path/identity references are
    // rejected by the real loader, rather than silently lost during rename.
    for (const auto& move : moves) {
        const auto filename = document::path_to_utf8(move.from.filename());
        const auto escaped = nlohmann::json(filename).dump(-1, ' ', true);
        if (bytes.find(move.document_id) != std::string::npos ||
            bytes.find(filename) != std::string::npos ||
            bytes.find(escaped.substr(1, escaped.size()-2)) != std::string::npos) return true;
    }
    return false;
}
}
struct FileRenameJob::Impl {
    std::string id, token;
    fs::path from, to, directory;
    std::vector<Receipt> open;
    std::vector<document::FileRelocation> moves;
    std::vector<Stamp> observed;
    std::vector<fs::path> candidates;
    struct File {
        fs::path source, target, staged, backup;
        bool backed_up{}, published{};
    };
    std::vector<File> files;
    std::map<fs::path, fs::path> directories;
    bool staged{}, finished{}, retain_recovery{};

    ~Impl() {
        if (retain_recovery) return;
        for (const auto& [parent, scratch] : directories) {
            // Only directories created by this exact job are eligible for cleanup.
            if (!scratch.is_absolute() || scratch.parent_path() != parent ||
                scratch.filename() != fs::path(".zima-rename-" + token)) continue;
            std::error_code error; fs::remove_all(scratch, error);
        }
    }
    fs::path scratch(const fs::path& parent) {
        if (const auto found = directories.find(parent); found != directories.end()) return found->second;
        const auto path = parent / (".zima-rename-" + token);
        if (!path.is_absolute() || path.parent_path() != parent || !fs::create_directory(path))
            throw FileRenameError("staging_failed", "Cannot create a private native rename staging directory.");
        directories.emplace(parent, path);
        fs::create_directory(path / "new"); fs::create_directory(path / "original");
        return path;
    }
    std::vector<fs::path> inputs() const {
        std::set<fs::path> paths{from};
        const auto add = [&](const fs::directory_entry& entry) {
            const auto suffix = extension(entry.path());
            if ((suffix == ".asmz" || suffix == ".drwz") && fs::is_regular_file(entry.symlink_status()))
                paths.insert(normalized_path(entry.path()));
        };
        for (const auto& entry : fs::directory_iterator(from.parent_path())) add(entry);
        if (!directory.empty() && fs::is_directory(directory)) {
            for (fs::recursive_directory_iterator it(directory), end; it != end; ++it) {
                const auto path = normalized_path(it->path());
                const bool owned = std::ranges::any_of(directories, [&](const auto& pair) { return pair.second == path; });
                if (owned) { it.disable_recursion_pending(); continue; }
                add(*it);
            }
        }
        for (const auto& state : open)
            if (state.kind != 0 && !state.path.empty() && regular(state.path)) paths.insert(state.path);
        std::vector<fs::path> ordered{from};
        for (const auto& path : paths) if (path != from) ordered.push_back(path);
        return ordered;
    }
    void destination_available(const fs::path& target,const fs::path& source) const {
        bool same_spelling=false;
#ifdef _WIN32
        same_spelling=CompareStringOrdinal(target.c_str(),-1,source.c_str(),-1,TRUE)==CSTR_EQUAL;
#endif
        // The transaction stages originals away before publication, so a
        // case-only rename is safe on a case-insensitive filesystem. A hard
        // link with another spelling is still a genuine occupied destination.
        if (fs::exists(target)&&!(same_spelling&&fs::equivalent(target,source)))
            throw FileRenameError("destination_exists", "The native rename destination already exists.");
        for (const auto& state : open) if (state.path == target&&state.path!=source)
            throw FileRenameError("destination_open", "Another open document already uses the native rename destination.");
    }
};
FileRenameJob::FileRenameJob(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
FileRenameJob::FileRenameJob(FileRenameJob&&) noexcept = default;
FileRenameJob& FileRenameJob::operator=(FileRenameJob&&) noexcept = default;
FileRenameJob::~FileRenameJob() = default;

FileRenameJob prepare_document_file_rename(const Workspace& live, const std::string& id,
        const std::string& filename, const fs::path& working_directory,
        const std::function<std::string(const std::string&)>& normalize_name) {
    const auto* state = live.find(id);
    if (!state) throw FileRenameError("document_not_found", "The document is not open.");
    if(family_owner(live,id)!=id)
        throw FileRenameError("family_instance", "Rename the family variant through its name, not its owning native file.");
    if (filename.empty() || filename == "." || filename == ".." || filename.back() == '.' || filename.back() == ' ' ||
        filename.find_first_of("\\/:*?\"<>|") != std::string::npos ||
        std::ranges::any_of(filename, [](unsigned char c) { return c < 32; }))
        throw FileRenameError("invalid_filename", "Enter a filename without directories or forbidden characters.");
    auto impl = std::make_unique<FileRenameJob::Impl>();
    impl->id = id; impl->open = receipts(live); impl->directory = normalized_path(working_directory);
    impl->from = std::visit([](const auto& value) { return normalized_path(value.path); }, *state);
    if (impl->from.empty()) throw FileRenameError("path_required", "The document has no saved native file.");
    if (static_cast<std::size_t>(native_document_type(impl->from)) != state->index())
        throw FileRenameError("document_type_mismatch", "Document type does not match target path");
    static_cast<void>(stamp(impl->from));
    auto name = fs::u8path(filename);
    if (name.extension().empty()) name += impl->from.extension();
    if (extension(name) != extension(impl->from))
        throw FileRenameError("document_type_mismatch", "Renaming a native file must preserve its document extension.");
    if (assembly::is_skeleton_file(impl->from) && !assembly::is_skeleton_file(name))
        name = fs::u8path(document::path_to_utf8(name.stem()) + "_skeleton" + document::path_to_utf8(name.extension()));
    if(normalize_name)name=fs::u8path(normalize_name(document::path_to_utf8(name)));
    impl->to = impl->from.parent_path() / name;
    for (const auto& open_state : live.documents()) if (const auto* owner = std::get_if<AssemblyState>(&open_state)) {
        const auto count = std::ranges::count_if(owner->session.document().components, [&](const auto& component) {
            return !component.derived_copy && component.source_kind == assembly::ComponentSourceKind::Part &&
                assembly::is_skeleton_file(component.source_document_id == id ? impl->to : component.source_path);
        });
        if (count > 1) throw FileRenameError("duplicate_skeleton", "An Assembly can contain only one Skeleton.");
    }
    if (impl->to != impl->from) impl->destination_available(impl->to,impl->from);
    impl->token = document::PartDocument::create_default().document_id;
    impl->moves.push_back({id, impl->from, impl->to});
    return FileRenameJob(std::move(impl));
}
void FileRenameJob::stage() {
    auto& job = *impl_;
    if (job.staged || job.finished || !job.directories.empty())
        throw FileRenameError("invalid_operation_state", "Native rename staging can run only once.");
    if (job.from == job.to) { job.staged = true; return; }
    // Only a Drawing with a persisted owner qualifies as an automatic companion.
    if (native_document_type(job.from) != NativeDocumentType::Drawing) {
        auto companion = job.from; companion.replace_extension(".drwz");
        if (regular(companion)) {
            const auto before = stamp(companion);
            auto drawing = read_native_document(companion);
            if (stamp(companion) != before) throw FileRenameError("stale_file", "A native rename input changed during staging.");
            bool belongs = drawing.is_drawing_for(job.id);
            for (const auto& open : job.open)
                if (open.kind == 2 && open.id == drawing.id() &&
                    (open.path == companion || (!open.path.empty() && fs::equivalent(open.path, companion))))
                    belongs = open.drawing_source_id == job.id;
            if (belongs) {
                auto target = job.to; target.replace_extension(".drwz");
                job.destination_available(target,companion);
                job.moves.push_back({drawing.id(), companion, target});
            }
        }
    }
    job.candidates = job.inputs();
    std::map<std::string, fs::path> identities;
    std::set<fs::path> targets;
    for (const auto& input : job.candidates) {
        const auto before = stamp(input);
        const bool open = std::ranges::any_of(job.open, [&](const auto& state) { return state.path == input; });
        if (input != job.from && !open && !may_reference(input, job.moves)) {
            if (stamp(input) != before) throw FileRenameError("stale_file", "A native rename input changed during staging.");
            job.observed.push_back(before);
            continue;
        }
        auto loaded = read_native_document(input,{},false);
        if (input == job.from && loaded.id() != job.id)
            throw FileRenameError("stale_document", "The saved native file has a different document identity.");
        for (const auto& state : job.open)
            if (state.path == input && state.id != loaded.id())
                throw FileRenameError("stale_document", "The saved native file has a different document identity.");
        if (const auto old = identities.find(loaded.id()); old != identities.end()) {
            if (fs::equivalent(old->second, input)) { job.observed.push_back(before); continue; }
            if (std::ranges::any_of(job.moves, [&](const auto& move) { return move.document_id == loaded.id(); }))
                throw FileRenameError("duplicate_document_identity", "Different native files have the renamed document identity.");
        }
        identities.emplace(loaded.id(), input);
        fs::path target = input;
        for (const auto& move : job.moves) if (move.from == input) target = move.to;
        const bool changed = loaded.rebase_native_files(job.moves);
        if (stamp(input) != before) throw FileRenameError("stale_file", "A native rename input changed during staging.");
        job.observed.push_back(before);
        if (!changed && target == input) continue;
        if (!targets.insert(target).second)
            throw FileRenameError("destination_exists", "The native rename destination already exists.");
        const auto scratch = job.scratch(target.parent_path());
        const auto staged = scratch / "new" / target.filename();
        const auto backup = scratch / "original" / input.filename();
        loaded.write(staged, target);
        if (read_native_document(staged,{},false).id() != loaded.id())
            throw FileRenameError("staging_failed", "Staged native document identity does not match its source.");
        job.files.push_back({input, target, staged, backup});
    }
    if (job.inputs() != job.candidates)
        throw FileRenameError("stale_file", "The set of native dependency files changed during staging.");
    job.staged = true;
}
FileRenameResult FileRenameJob::commit(Workspace& live) {
    auto& job = *impl_;
    FileRenameResult result; result.document_id = job.id; result.from = job.from; result.to = job.to;
    const auto fail = [&](const char* code, std::string message, const fs::path& path) {
        result.code = code; result.message = std::move(message); result.failed_path = path;
        return result;
    };
    if (!job.staged || job.finished)
        return fail("invalid_operation_state", "Native rename has not been staged or has already finished.", job.from);
    if (receipts(live) != job.open)
        return fail("stale_document", "Open documents changed before native file rename.", job.from);
    if (job.from == job.to) { job.finished = true; return result; }
    for(const auto& file:job.files) {live.reserve_file(file.source);live.reserve_file(file.target);}
    try {
        for (const auto& before : job.observed)
            if (stamp(before.path) != before)
                return fail("stale_file", "A native rename input changed before publication.", before.path);
        if (job.inputs() != job.candidates)
            return fail("stale_file", "The set of native dependency files changed before publication.", job.from);
        for (const auto& move : job.moves) job.destination_available(move.to,move.from);
    } catch (const FileRenameError& error) { return fail(error.code.c_str(), error.what(), job.from); }
      catch (const fs::filesystem_error& error) { return fail("file_io_error", error.what(), error.path1()); }

    // Complete allocation and all history validation before moving an original file.
    document::FileRelocationEdits edits(job.moves);
    std::vector<std::pair<fs::path*, fs::path>> path_edits;
    std::vector<PartState*> current_source_caches;
    for (auto& state : live.documents()) std::visit([&](auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, PartState>) {
            if (value.source_geometry && value.source_generation == value.session.data_generation())
                current_source_caches.push_back(&value);
            value.session.prepare_native_file_rebase(edits);
        }
        else if constexpr (std::is_same_v<T, AssemblyState>) value.session.prepare_native_file_rebase(edits, normalized_path(value.path));
        else value.prepare_native_file_rebase(edits);
        for (const auto& move : job.moves)
            if (model(value).document_id == move.document_id) path_edits.emplace_back(&value.path, move.to);
    }, state);
    // Result storage must also be allocated before publication.
    for (const auto& file : job.files) result.updated_files.push_back(file.target);
    job.finished = true;
    const fs::path* failed_path = &job.from;
    try {
        for (auto& file : job.files) {
            failed_path = &file.source;
            fs::rename(file.source, file.backup); file.backed_up = true;
        }
        for (auto& file : job.files) {
            failed_path = &file.target;
            fs::rename(file.staged, file.target); file.published = true;
        }
    } catch (const std::exception& error) {
        for (auto it = job.files.rbegin(); it != job.files.rend(); ++it) if (it->published) {
            std::error_code rollback; fs::rename(it->target, it->staged, rollback);
            if (rollback) job.retain_recovery = true;
            else it->published = false;
        }
        for (auto it = job.files.rbegin(); it != job.files.rend(); ++it) if (it->backed_up) {
            std::error_code rollback;
            const bool occupied = fs::exists(it->source, rollback);
            if (occupied || rollback) job.retain_recovery = true;
            else { fs::rename(it->backup, it->source, rollback); if (rollback) job.retain_recovery = true; }
        }
        result.updated_files.clear();
        if (job.retain_recovery) {
            result.changed = true;
            for (const auto& file : job.files) if (file.published) result.updated_files.push_back(file.target);
            for (const auto& [parent, scratch] : job.directories) result.recovery_paths.push_back(scratch);
        }
        return fail(job.retain_recovery ? "file_rename_recovery_required" : "file_io_error", error.what(), *failed_path);
    }
    edits.apply();
    // Only file metadata changed. Preserve already current shared source geometry.
    for (auto* state : current_source_caches) state->source_generation = state->session.data_generation();
    for (auto& [path, target] : path_edits) path->swap(target);
    result.changed = true;
    return result;
}
} // namespace zima::workspace
