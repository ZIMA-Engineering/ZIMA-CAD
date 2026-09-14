#include <zima/workspace/file_removal_operations.hpp>
#include <zima/workspace/document_operations.hpp>
#include <zima/command_host/host.hpp>
#include <zima/document/file_path.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace zima;
namespace fs = std::filesystem;
using commands::Json;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Action>
void expect_error(Action action, const char* code) {
    try { action(); }
    catch (const workspace::FileRemovalError& error) { require(error.code == code, "Wrong file-removal error"); return; }
    throw std::runtime_error("Rejected file-removal input succeeded");
}
void write(const fs::path& path, std::string_view content) {
    std::ofstream out(path, std::ios::binary); out << content; require(static_cast<bool>(out), "Fixture write failed");
}
std::string read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
fs::path first_archive(fs::path path) { path += ".1"; return path; }
}
int main() {
    try {
        auto part = document::PartDocument::create_default();
        part.history.push_back(document::PartDocument::create_box_container());
        kernel::OcctKernel kernel;
        const auto boundaries = kernel.evaluate_history(part.kernel_operations());
        require(!boundaries.empty() && boundaries.back().volume > 0, "No fixture body");
        const auto root = fs::canonical(fs::temp_directory_path()) / ("zima-file-removal-" + part.document_id);
        fs::create_directory(root);
        for (const int kind : {0, 1, 2}) {
            workspace::Workspace live;
            const auto path = root / fs::path(kind == 0 ? u8"díl.prtz" : kind == 1 ? u8"sestava.asmz" : u8"výkres.drwz");
            std::string id;
            if (kind == 0) { id = part.document_id; live.add_part(part, boundaries, path); }
            else if (kind == 1) { auto doc = assembly::AssemblyDocument::create_default(); id = doc.document_id; live.add_assembly(doc, path); }
            else { auto doc = drawing::DrawingDocument::create_default(); id = doc.document_id; live.add_drawing(doc, path); }
            live.activate(id); live.display_top_level(id);
            for (int i = 0; i < 2; ++i) {
                const auto saved = workspace::prepare_document_save(live, id, path).write();
                require(workspace::complete_document_save(live, saved), "Fixture save failed");
            }
            const auto snapshot = command_host::documents(live);
            const auto plan = workspace::prepare_document_file_removal(live, id, kind == 1);
            require(command_host::documents(live) == snapshot && fs::exists(path) && fs::exists(first_archive(path)),
                "Preparing file deletion changed files or model");
            const auto removed = workspace::remove_document_file(live, plan);
            require(removed.ok() && removed.closed_document == id && !fs::exists(path) && live.size() == 0 &&
                live.active_document_id().empty() && live.displayed_document_id().empty(),
                "Native file deletion failed to close its own document");
            require(fs::exists(first_archive(path)) == (kind != 1) &&
                removed.removed.size() == (kind == 1 ? 2 : 1), "Archive selection or completed effects wrong");
            require(!workspace::remove_document_file(live, plan).ok(), "Stale deletion plan reused");
        }
        workspace::Workspace live;
        const auto path = root / fs::path(u8"chráněný díl.prtz");
        part.save(path, boundaries); part.save(path, boundaries);
        live.add_part(part, boundaries, path); live.activate(part.document_id); live.display_top_level(part.document_id);
        auto working = root;
        bool editing = false;
        command_host::Options options; options.interaction = [&] { command_host::Interaction x; x.editing = editing; return x; };
        command_host::Host host(live, kernel, working, options);
        const auto call = [&](Json args = Json::object()) { return host.execute({{"command", "delete_file"}, {"arguments", std::move(args)}}); };
        require(call({{"document","missing"}}).code == "document_not_found", "Unknown document accepted");
        require(!call({{"archives","yes"}}).ok && fs::exists(path), "Invalid archive flag deleted a file");
        editing = true; require(call().code == "editing_in_progress" && fs::exists(path), "Delete interrupted Properties");
        editing = false;
        auto* state = live.open_part(part.document_id);
        const auto original_volume = state->session.calculated_boundaries().back().volume;
        const auto plan = workspace::prepare_document_file_removal(live, part.document_id, true);
        auto edited = state->session.document(); edited.name = "unsaved model edit";
        state->session.commit(edited, boundaries);
        require(workspace::remove_document_file(live, plan).code == "stale_document" && fs::exists(path),
            "Changed model accepted an earlier deletion plan");
        expect_error([&] { static_cast<void>(workspace::prepare_document_file_removal(live, part.document_id)); },
            "unsaved_changes");
        require(call().code == "unsaved_changes" && state->session.can_undo() &&
            state->session.calculated_boundaries().back().volume == original_volume, "Dirty-state refusal damaged model/history");
        const auto bytes = read(path);
        auto allowed = workspace::prepare_document_file_removal(live, part.document_id, true, true);
        write(path, bytes + "changed");
        require(workspace::remove_document_file(live, allowed).code == "stale_file" && fs::exists(path) &&
            fs::exists(first_archive(path)), "Changed file was deleted with archives");
        write(path, bytes);
        allowed = workspace::prepare_document_file_removal(live, part.document_id, true, true);
        write(first_archive(path), "changed backup");
        require(workspace::remove_document_file(live, allowed).code == "stale_archive" && fs::exists(path),
            "Changed archive batch deleted the current file first");
#ifdef _WIN32
        auto held = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(held != INVALID_HANDLE_VALUE, "Cannot lock current fixture");
        const auto denied = call({{"archives",true},{"discard",true}});
        CloseHandle(held);
        require(!denied.ok && denied.code == "file_io_error" && denied.data.at("removed_paths").empty() &&
            !denied.data.at("closed").get<bool>() && fs::exists(path) && fs::exists(first_archive(path)) &&
            live.open_part(part.document_id) && !host.change(), "Locked current file removed archives or closed the model");
        held = CreateFileW(first_archive(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(held != INVALID_HANDLE_VALUE, "Cannot lock archive fixture");
        const auto partial = call({{"archives",true},{"discard",true}});
        CloseHandle(held);
        const auto partial_json = Json::parse(partial.json().dump());
        require(!partial.ok && partial.code == "archive_io_error" && partial_json.at("data").at("closed") == true &&
            partial_json.at("data").at("removed_paths").size() == 1 && !fs::exists(path) &&
            fs::exists(first_archive(path)) && !live.find(part.document_id) &&
            host.change() && host.change()->kind == command_host::ChangeKind::Close,
            "Archive failure lost the successful file deletion/close from the CLI result");
#else
        require(call({{"archives",true},{"discard",true}}).ok && !fs::exists(path), "Explicit discard failed");
#endif
        // Runtime identity must distinguish a new opening with the same document ID.
        part.save(path, boundaries); live.add_part(part, boundaries, path);
        live.activate(part.document_id); live.display_top_level(part.document_id);
        allowed = workspace::prepare_document_file_removal(live, part.document_id);
        require(live.remove(part.document_id), "Fixture close failed");
        live.add_part(part, boundaries, path); live.activate(part.document_id); live.display_top_level(part.document_id);
        require(workspace::remove_document_file(live, allowed).code == "stale_document" && fs::exists(path),
            "Earlier plan deleted a reopened document");
        auto group = assembly::AssemblyDocument::create_default();
        auto occurrence = assembly::AssemblyDocument::create_part_occurrence("part", part.document_id, path, boundaries.back());
        group.components.push_back(occurrence);
        const auto group_id = group.document_id;
        live.add_assembly(group, root / "context.asmz"); live.display_top_level(group_id);
        require(live.activate_occurrence(group_id, {{occurrence.occurrence_id}}).has_value(), "Cannot activate fixture occurrence");
        const auto deletion = workspace::remove_document_file(live,
            workspace::prepare_document_file_removal(live, part.document_id));
        require(deletion.ok() && live.active_document_id() == group_id && live.displayed_document_id() == group_id &&
            live.active_occurrence_path().empty() && !live.find(part.document_id) &&
            live.open_assembly(group_id)->session.document().components[0].source_document_id == part.document_id &&
            live.open_assembly(group_id)->session.document().components[0].calculated_source->volume == original_volume,
            "Closing the deleted active source destroyed its parent context or source identity");
        require(fs::canonical(root).parent_path() == fs::canonical(fs::temp_directory_path()), "Unsafe fixture cleanup");
        fs::remove_all(root);
        std::cout << "Native file deletion, discard, snapshots, failures, CLI and occurrence context passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
