#include <zima/workspace/file_rename_operations.hpp>
#include <zima/command_host/host.hpp>
#include <zima/document/file_path.hpp>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace zima;
namespace fs = std::filesystem;
namespace {
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
std::string read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary); require(static_cast<bool>(file), "Cannot read native fixture");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void write(const fs::path& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary); file << bytes; require(static_cast<bool>(file), "Cannot write fixture");
}
template<class Action> void rejects(Action action, const char* code) {
    try { action(); }
    catch (const workspace::FileRenameError& error) { require(error.code == code, "Wrong rename rejection"); return; }
    throw std::runtime_error("Invalid rename accepted");
}
struct Fixture {
    fs::path root, source, target, companion, renamed_companion, assembly_file, closed_file, work;
    workspace::Workspace live;
    document::PartDocument part;
    assembly::AssemblyDocument group;
    drawing::DrawingDocument drawing;
    std::map<fs::path, std::string> original;
    const kernel::BodyResult* cached{};
    Fixture(const fs::path& path, const document::PartDocument& input, const std::vector<kernel::BodyResult>& boundaries)
        : root(path), work(path / "work"), part(input) {
        fs::create_directories(root / "source"); fs::create_directories(work / "nested"); fs::create_directories(root / "outside");
        source = root / "source" / fs::path(u8"díl.prtz"); target = root / "source" / fs::path(u8"nový díl.prtz");
        companion = source; companion.replace_extension(".drwz");
        renamed_companion = target; renamed_companion.replace_extension(".drwz");
        assembly_file = root / "outside" / "open.asmz"; closed_file = work / "nested" / "closed.asmz";
        part.save(source, boundaries); part.save(source, boundaries);
        group = assembly::AssemblyDocument::create_default();
        group.components.push_back(assembly::AssemblyDocument::create_part_occurrence("saved component", part.document_id, source, boundaries.back()));
        group.save(assembly_file);
        auto closed = assembly::AssemblyDocument::create_default();
        closed.components.push_back(assembly::AssemblyDocument::create_part_occurrence("closed component", part.document_id, source, boundaries.back()));
        closed.save(closed_file);
        drawing = drawing::DrawingDocument::create_default();
        drawing.source_document_id = part.document_id; drawing.source_path = source; drawing.source_name = part.name;
        drawing.sheets.front().views.push_back(drawing::DrawingDocument::create_view(part.document_id, source, boundaries.back().mesh));
        drawing::BomRow row; row.item_number = 1; row.source_document_id = part.document_id;
        row.source_path = source; row.name = "saved row"; row.file_stem = document::path_to_utf8(source.stem());
        drawing.sheets.front().bom_rows.push_back(row); drawing.save(companion);
        for (const auto& file : {source, companion, assembly_file, closed_file}) original[file] = read(file);
        live.add_part(part, boundaries, source); live.add_assembly(group, assembly_file); live.add_drawing(drawing, companion);
        auto edited = part; edited.user_parameters["LIVE_ONLY"] = "one";
        live.open_part(part.document_id)->session.commit(edited, boundaries);
        edited.user_parameters["LIVE_ONLY"] = "two"; live.open_part(part.document_id)->session.commit(edited, boundaries);
        require(live.open_part(part.document_id)->session.undo(), "Part Undo fixture failed");
        auto edited_group = group; edited_group.components[0].name = "live component";
        live.open_assembly(group.document_id)->session.commit(edited_group);
        auto edited_drawing = drawing; edited_drawing.sheets.front().bom_rows[0].name = "live row";
        live.open_drawing(drawing.document_id)->commit(edited_drawing);
        live.display_top_level(group.document_id);
        require(live.activate_occurrence(group.document_id, {{group.components[0].occurrence_id}}).has_value(), "No occurrence context");
        live.refresh_source_geometry();
        cached = &live.open_part(part.document_id)->session.calculated_boundaries().back();
    }
    workspace::FileRenameJob job() {
        return workspace::prepare_document_file_rename(live, part.document_id, document::path_to_utf8(target.filename()), work);
    }
    void unchanged() const {
        for (const auto& [path, bytes] : original) require(read(path) == bytes, "Failed rename changed original bytes");
        require(!fs::exists(target) && live.open_part(part.document_id)->path == source &&
            live.open_part(part.document_id)->session.document().name == part.name &&
            cached == &live.open_part(part.document_id)->session.calculated_boundaries().back(),
            ("Failed rename changed native path, metadata or cached body: " + root.filename().string()).c_str());
    }
    void clean_staging() const {
        for (const auto& entry : fs::recursive_directory_iterator(root))
            require(!document::path_to_utf8(entry.path().filename()).starts_with(".zima-rename-"), "Owned staging was not cleaned");
    }
};
}
int main() {
    try {
        auto part = document::PartDocument::create_default(); part.name = "Source";
        part.history.push_back(document::PartDocument::create_box_container());
        kernel::OcctKernel kernel; const auto boundaries = kernel.evaluate_history(part.kernel_operations());
        require(!boundaries.empty() && boundaries.back().volume > 0, "No calculated rename fixture");
        const auto root = fs::canonical(fs::temp_directory_path()) / ("zima-native-rename-" + part.document_id);
        fs::create_directory(root);
        {
            Fixture fixture(root / "success", part, boundaries);
            const auto occurrence = fixture.live.active_occurrence_path();
            const auto source_cache = fixture.live.open_part(part.document_id)->source_geometry;
            require(source_cache.has_value(), "No shared Part source cache in fixture");
            {
                auto job = fixture.job(); job.stage(); fixture.unchanged();
                const auto result = job.commit(fixture.live);
                require(result.ok() && result.changed && result.updated_files.size() == 4 &&
                    result.recovery_paths.empty() && !fs::exists(fixture.source) && !fs::exists(fixture.companion) &&
                    fs::exists(fixture.target) && fs::exists(fixture.renamed_companion), "Native rename transaction failed");
                auto archive = fixture.source; archive += ".1";
                require(fs::exists(archive), "Rename moved or deleted old archives");
                require(fixture.live.active_document_id() == part.document_id &&
                    fixture.live.displayed_document_id() == fixture.group.document_id &&
                    fixture.live.active_occurrence_path() == occurrence &&
                    fixture.cached == &fixture.live.open_part(part.document_id)->session.calculated_boundaries().back(),
                    "Rename changed activation, occurrence path or body cache");
                fixture.live.refresh_source_geometry();
                require(fixture.live.open_part(part.document_id)->source_geometry->shares_with(*source_cache),
                    "Metadata rename rebuilt the unchanged shared Part source cache");
                auto& session = fixture.live.open_part(part.document_id)->session;
                require(session.is_dirty() && session.can_undo() && session.can_redo() &&
                    session.document().name == document::path_to_utf8(fixture.target.stem()) &&
                    session.document().user_parameters.at("LIVE_ONLY") == "one", "Rename lost live Part state/history");
                require(session.undo() && !session.is_dirty() && session.document().name == document::path_to_utf8(fixture.target.stem()) &&
                    session.redo() && session.redo() && session.document().user_parameters.at("LIVE_ONLY") == "two", "Rename broke Part Undo/Redo");
                const auto* open_group = fixture.live.open_assembly(fixture.group.document_id);
                const auto* open_drawing = fixture.live.open_drawing(fixture.drawing.document_id);
                require(open_group->session.is_dirty() && open_group->session.document().components[0].name == "live component" &&
                    open_group->session.document().components[0].source_path == fixture.target &&
                    open_drawing->is_dirty() && open_drawing->path == fixture.renamed_companion &&
                    open_drawing->document().sheets.front().bom_rows[0].name == "live row" &&
                    open_drawing->document().sheets.front().bom_rows[0].source_path == fixture.target,
                    "Rename saved/lost live Assembly/Drawing changes");
                const auto saved_part = document::PartDocument::load(fixture.target);
                const auto saved_group = assembly::AssemblyDocument::load(fixture.assembly_file);
                const auto saved_drawing = drawing::DrawingDocument::load(fixture.renamed_companion);
                const auto closed = assembly::AssemblyDocument::load(fixture.closed_file);
                require(saved_part.document_id == part.document_id && !saved_part.user_parameters.contains("LIVE_ONLY") &&
                    saved_group.components[0].name == "saved component" && saved_group.components[0].source_path == fixture.target &&
                    saved_drawing.source_path == fixture.target && saved_drawing.sheets.front().bom_rows[0].name == "saved row" &&
                    saved_drawing.sheets.front().views[0].source_path == fixture.target &&
                    closed.components[0].source_path == fixture.target,
                    "Disk dependency rename included unrelated live edits or missed closed/open-file references");
                require(!job.commit(fixture.live).ok(), "Rename job could be published twice");
            }
            fixture.clean_staging();
        }
        {
            Fixture fixture(root / "validation", part, boundaries);
            for (const auto& name : {"", ".", "..", "../x.prtz", "x/y", "x\\y", "x.", "x "})
                rejects([&] { static_cast<void>(workspace::prepare_document_file_rename(fixture.live, part.document_id, name, fixture.work)); }, "invalid_filename");
            rejects([&] { static_cast<void>(workspace::prepare_document_file_rename(fixture.live, "missing", "new.prtz", fixture.work)); }, "document_not_found");
            rejects([&] { static_cast<void>(workspace::prepare_document_file_rename(fixture.live, part.document_id, "new.asmz", fixture.work)); }, "document_type_mismatch");
            const auto generation = fixture.live.open_part(part.document_id)->session.data_generation();
            auto same = workspace::prepare_document_file_rename(fixture.live, part.document_id, document::path_to_utf8(fixture.source.filename()), fixture.work);
            same.stage(); const auto result = same.commit(fixture.live);
            require(result.ok() && !result.changed && fixture.live.open_part(part.document_id)->session.data_generation() == generation, "Same-name rename changed the model");
            fixture.unchanged(); fixture.clean_staging();
        }
        {
            Fixture fixture(root / "invalid_dependency", part, boundaries);
            const auto invalid = fixture.work / "nested" / "invalid.asmz";
            write(invalid, "not a native Assembly");
            auto directory = fixture.work;
            command_host::Host host(fixture.live, kernel, directory);
            const auto result = host.execute({{"command","rename_file"},{"arguments",{{"name","new.prtz"}}}});
            require(!result.ok && result.code == "rename_rejected" && !host.change(),
                "Unreadable native dependency was silently skipped");
            fixture.unchanged(); fixture.clean_staging();
            require(read(invalid) == "not a native Assembly", "Rename rewrote an unreadable dependency");
        }
        for (const std::string kind : {"model", "file", "added_dependency", "history_conflict"}) {
            Fixture fixture(root / kind, part, boundaries);
            if (kind == "history_conflict") {
                auto* state = fixture.live.open_assembly(fixture.group.document_id);
                auto wrong = state->session.document(); wrong.components[0].source_document_id = "wrong-source";
                state->session.commit(wrong); state->session.commit(fixture.group);
            }
            {
                auto job = fixture.job(); job.stage();
                if (kind == "model") {
                    auto* state = fixture.live.open_part(part.document_id);
                    auto changed = state->session.document(); changed.user_parameters["AFTER_STAGE"] = "changed";
                    state->session.commit(changed, boundaries);
                    fixture.cached = &state->session.calculated_boundaries().back();
                    require(job.commit(fixture.live).code == "stale_document" &&
                        state->session.document().user_parameters.at("AFTER_STAGE") == "changed",
                        "Changed live state accepted old rename or lost the newer user edit");
                } else if (kind == "file") {
                    write(fixture.source, fixture.original.at(fixture.source) + "changed");
                    require(job.commit(fixture.live).code == "stale_file", "Changed input accepted old rename");
                    write(fixture.source, fixture.original.at(fixture.source));
                } else if (kind == "added_dependency") {
                    auto added = assembly::AssemblyDocument::create_default();
                    added.save(fixture.work / "nested" / "new.asmz");
                    require(job.commit(fixture.live).code == "stale_file", "New dependency file was silently omitted");
                } else {
                    bool denied = false;
                    try { static_cast<void>(job.commit(fixture.live)); }
                    catch (const std::invalid_argument&) { denied = true; }
                    require(denied, "Historical identity conflict was accepted");
                }
                fixture.unchanged();
            }
            fixture.clean_staging();
        }
#ifdef _WIN32
        for (const std::string kind : {"locked_source", "locked_dependency", "locked_stage"}) {
            Fixture fixture(root / kind, part, boundaries);
            {
                auto job = fixture.job(); job.stage();
                fs::path locked = kind == "locked_source" ? fixture.source : fixture.closed_file;
                if (kind == "locked_stage") {
                    locked.clear();
                    for (const auto& entry : fs::recursive_directory_iterator(fixture.root))
                        if (entry.is_regular_file() && entry.path().filename() == fixture.target.filename() &&
                            entry.path().parent_path().filename() == "new") locked = entry.path();
                    require(!locked.empty(), "No staged native source was found");
                }
                const auto handle = CreateFileW(locked.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                require(handle != INVALID_HANDLE_VALUE, "Cannot lock rename fixture");
                const auto result = job.commit(fixture.live);
                CloseHandle(handle);
                require(!result.ok() && result.code == "file_io_error" && result.recovery_paths.empty() && !result.changed,
                    "Locked rename input did not roll back cleanly");
                fixture.unchanged();
            }
            fixture.clean_staging();
        }
#endif

        {
            Fixture fixture(root / "host", part, boundaries);
            auto directory = fixture.work; bool editing = true; int io_calls = 0;
            command_host::Options options;
            options.interaction = [&] { command_host::Interaction state; state.editing = editing; return state; };
            options.run_io = [&](std::function<void()> task) { ++io_calls; task(); };
            command_host::Host host(fixture.live, kernel, directory, options);
            const auto request = commands::Json{{"command","rename_file"},{"arguments",{{"name",document::path_to_utf8(fixture.target.filename())}}}};
            require(host.execute(request).code == "editing_in_progress" && io_calls == 0, "Rename command interrupted Properties");
            editing = false;
            require(!host.execute({{"command","rename_file"},{"arguments",{{"name",5}}}}).ok && io_calls == 0,
                "Invalid rename argument entered the I/O phase");
            require(host.execute({{"command","rename_file"},{"arguments",{{"document","missing"},{"name","new.prtz"}}}}).code == "document_not_found",
                "Rename accepted a stale document ID");
            const auto result = host.execute(request);
            const auto json = commands::Json::parse(result.json().dump());
            require(result.ok && io_calls == 1 && host.change() && host.change()->kind == command_host::ChangeKind::Rename &&
                json.at("data").at("document") == part.document_id && json.at("data").at("changed") == true &&
                json.at("data").at("updated_paths").size() == 4 && fs::exists(fixture.target) && !fs::exists(fixture.source),
                "Typed rename command lost file effects, Unicode or presentation change");
        }
        {
            Fixture fixture(root / "io_reentry", part, boundaries);
            auto directory = fixture.work;
            command_host::Options options; options.run_io = [&](std::function<void()> task) {
                task();
                auto* state = fixture.live.open_part(part.document_id);
                auto changed = state->session.document(); changed.user_parameters["AFTER_IO"] = "preserved";
                state->session.commit(changed, boundaries);
                fixture.cached = &state->session.calculated_boundaries().back();
            };
            command_host::Host host(fixture.live, kernel, directory, options);
            const auto result = host.execute({{"command","rename_file"},{"arguments",{{"name","new.prtz"}}}});
            require(!result.ok && result.code == "stale_document" && !host.change() &&
                fixture.live.open_part(part.document_id)->session.document().user_parameters.at("AFTER_IO") == "preserved",
                "I/O event-loop reentry let rename overwrite a newer edit");
            fixture.unchanged(); fixture.clean_staging();
        }
        // A dirty open Drawing is authoritative for automatic companion ownership.
        {
            Fixture fixture(root / "companion_owner", part, boundaries);
            auto* state = fixture.live.open_drawing(fixture.drawing.document_id);
            auto changed = state->document(); changed.source_document_id = "other-model";
            changed.source_path = fixture.root / "other.prtz"; state->commit(changed);
            {
                auto job = fixture.job(); job.stage(); const auto result = job.commit(fixture.live);
                require(result.ok() && fs::exists(fixture.companion) && !fs::exists(fixture.renamed_companion) &&
                    state->path == fixture.companion && state->document().source_document_id == "other-model" &&
                    state->document().sheets.front().views[0].source_path == fixture.target &&
                    drawing::DrawingDocument::load(fixture.companion).source_path == fixture.target,
                    "Companion filename overrode live ownership or saved unrelated owner edits");
            }
            fixture.clean_staging();
        }
        require(fs::canonical(root).parent_path() == fs::canonical(fs::temp_directory_path()), "Unsafe fixture cleanup");
        fs::remove_all(root);
        std::cout << "Native rename, saved/live dependencies, staging, rollback and cache/history passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
