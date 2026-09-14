#include <zima/command_host/host.hpp>
#include <zima/document/versioned_file.hpp>
#include <zima/workspace/archive_operations.hpp>
#include <fstream>
#include <iostream>
#include <limits>
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
void write(const fs::path& path, std::string_view value = "abc") {
    std::ofstream out(path, std::ios::binary); out << value;
    require(static_cast<bool>(out), "Fixture write failed");
}
std::string read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
fs::path version(fs::path path, std::string_view suffix) { path += "." + std::string(suffix); return path; }
}
int main() {
    try {
        auto part = document::PartDocument::create_default();
        const auto root = fs::canonical(fs::temp_directory_path()) / ("zima-archives-" + part.document_id);
        fs::create_directory(root);
        const auto directory = root / fs::path(u8"český adresář"); fs::create_directory(directory);
        const auto file = directory / fs::path(u8"díl žluťoučký.prtz");
        write(file, "current");
        const std::string huge(30, '9'), successor = "1" + std::string(30, '0');
        for (const auto& n : {std::string{"10"}, std::string{"2"}, std::string{"001"}, huge}) write(version(file, n));
        write(version(file, "x")); write(directory / "notes.txt.1"); write(directory / "other.prtz.1.tmp");
        const auto subdir = directory / "subdir"; fs::create_directory(subdir); write(subdir / "nested.prtz.1");
        fs::create_directory(version(file, "12"));
        std::error_code symlink_error;
        const auto link = version(file, "15"); fs::create_symlink(file, link, symlink_error);
        const auto archives = workspace::document_archives(file);
        require(archives.size() == 4 && archives[0].version == "001" && archives[1].version == "2" &&
            archives[2].version == "10" && archives[3].version == huge, "Archive numeric order or filtering failed");
        require(archives[0].size == 3 && archives[0].path.is_absolute(), "Archive metadata is wrong");
        document::archive_existing_file(file);
        require(fs::exists(version(file, successor)) && read(version(file, successor)) == "current" &&
            read(file) == "current", "Unicode save archival or arbitrary-size decimal increment failed");
        for (const auto* ext : {".ASMZ", ".drwz", ".frmz", ".tblz"}) {
            auto base = directory / (std::string("other") + ext);
            write(version(base, "2")); write(version(base, "10"));
        }
        const auto groups = workspace::directory_archives(directory);
        require(groups.size() == 5 && groups.at(file).size() == 5, "Directory native grouping or recursion contract failed");
        require(workspace::archives_to_remove(groups, std::numeric_limits<std::size_t>::max()).empty(),
            "Large keep count overflowed");
        workspace::Workspace live; live.add_part(part, {}, file); live.activate(part.document_id); live.display_top_level(part.document_id);
        auto working = directory;
        kernel::OcctKernel kernel;
        bool editing = false;
        command_host::Options options; options.interaction = [&] { command_host::Interaction state; state.editing = editing; return state; };
        command_host::Host host(live, kernel, working, options);
        const auto call = [&](const char* name, Json args = Json::object()) {
            return host.execute({{"command", name}, {"arguments", std::move(args)}});
        };
        const auto checked = [&](const char* name, Json args = Json::object()) {
            auto result = call(name, std::move(args));
            if (!result.ok) throw std::runtime_error(std::string(name) + ": " + result.code + ": " + result.message);
            return result.data;
        };
        const auto text = document::path_to_utf8(file.filename());
        const auto snapshot = command_host::documents(live);
        const auto generation = live.open_part(part.document_id)->session.data_generation();
        const auto listed = checked("file.archives.list", {{"path", text}});
        require(listed.at("count") == 5 && listed.at("groups")[0].at("archives")[3].at("version") == huge &&
            listed.at("total_size_bytes") == 19 && !host.change(), "CLI listing changed state or lost exact numbers/bytes");
        require(checked("directory.archives.list").at("count") == 13, "Working-directory archive query failed");
        for (const auto& invalid : std::vector<Json>{
                {{"path", text}, {"keep", -1}}, {{"path", text}, {"keep", 1.5}}, {{"path", text}, {"keep", true}},
                {{"path", text}}, {{"path", text}, {"keep", 1}, {"unexpected", true}}}) {
            require(!call("file.archives.prune", invalid).ok && workspace::document_archives(file).size() == 5 &&
                !host.change(), "Invalid prune mutated files");
        }
        require(call("file.archives.list", {{"path", "notes.txt"}}).code == "unsupported_format", "Non-native file accepted");
        require(call("file.archives.list", {{"path", ""}}).code == "missing_argument", "Empty file path accepted");
        require(call("directory.archives.list", {{"path", "missing"}}).code == "invalid_directory", "Missing directory accepted");
        require(!checked("file.archives.prune", {{"path", text}, {"keep", std::numeric_limits<std::size_t>::max()}})
            .at("changed").get<bool>() && !host.change(), "CLI maximum keep count overflowed");
        const auto text_noop = host.execute_text("directory.archives.prune 1000");
        require(text_noop.ok && !text_noop.data.at("changed").get<bool>() && !host.change(),
            "Text directory prune cannot use the default working directory");
        editing = true;
        require(call("file.archives.list", {{"path", text}}).ok &&
            call("file.archives.prune", {{"path", text}, {"keep", 0}}).code == "editing_in_progress",
            "Archive command guard is inconsistent");
        editing = false;
        const auto pruned = checked("file.archives.prune", {{"path", text}, {"keep", 2}});
        require(pruned.at("changed") == true && pruned.at("removed_paths").size() == 3 &&
            pruned.at("removed_bytes") == 9 && host.change()->kind == command_host::ChangeKind::Files,
            "File prune result is wrong");
        require(fs::exists(file) && fs::exists(version(file, huge)) && fs::exists(version(file, successor)) &&
            !fs::exists(version(file, "10")) && fs::is_directory(version(file, "12")) &&
            fs::exists(directory / "notes.txt.1") && fs::exists(subdir / "nested.prtz.1"),
            "Prune touched current, retained, unrelated or nested files");
        require(!checked("file.archives.prune", {{"path", text}, {"keep", 2}}).at("changed").get<bool>() &&
            !host.change(), "No-op prune emitted a change");
        const auto all = checked("directory.archives.prune", {{"keep", 1}});
        require(all.at("removed_paths").size() == 5 && checked("directory.archives.list").at("count") == 5,
            "Directory keep applies globally instead of per document");
        require(command_host::documents(live) == snapshot &&
            live.open_part(part.document_id)->session.data_generation() == generation &&
            !live.open_part(part.document_id)->session.can_undo(), "Archive operation mutated the open document or calculated geometry");
        // Changed snapshot must reject every removal before touching another file.
        const auto stale_base = directory / "stale.prtz";
        write(version(stale_base, "1")); write(version(stale_base, "2"));
        const auto stale = workspace::document_archives(stale_base);
        write(stale[1].path, "longer");
        auto removal = workspace::remove_archives(stale);
        require(removal.code == "stale_archive" && removal.removed.empty() && fs::exists(stale[0].path),
            "Stale batch was partly removed");
        auto valid = workspace::document_archives(stale_base);
        auto invalid = valid.front(); invalid.path = file;
        removal = workspace::remove_archives({valid.front(), invalid});
        require(removal.code == "invalid_archive" && removal.removed.empty() && fs::exists(valid.front().path),
            "Current document admitted into removal batch");
        removal = workspace::remove_archives({valid.front(), valid.front()});
        require(removal.code == "invalid_archive" && removal.removed.empty(), "Duplicate removal accepted");
#ifdef _WIN32
        // Hold an actual Windows file handle without delete sharing. The CRT
        // may remove a read-only file, so a permission-bit fixture is unreliable.
        const auto held = CreateFileW(valid[1].path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(held != INVALID_HANDLE_VALUE, "Cannot hold archive failure fixture");
        const auto failed = call("file.archives.prune", {{"path",document::path_to_utf8(stale_base)},{"keep",0}});
        CloseHandle(held);
        const auto record = Json::parse(failed.json().dump());
        require(!failed.ok && failed.code == "archive_io_error" && record.at("data").at("removed_paths").size() == 1 &&
            record.at("data").at("failed_path") == document::path_to_utf8(valid[1].path) &&
            failed.message.find("Archives removed before the failure: 1") != std::string::npos &&
            host.change() && host.change()->kind == command_host::ChangeKind::Files &&
            !fs::exists(valid[0].path) && fs::exists(valid[1].path),
            "Partial CLI OS failure lost file effects, JSON data or visible removed count");
#endif
        checked("directory.archives.prune", {{"keep", 0}});
        require(checked("directory.archives.list").at("count") == 0 && fs::exists(file),
            "Pruning all archives deleted a current file or left selected archives");
        // Only our unique directory immediately under the canonical temporary root.
        require(fs::canonical(root).parent_path() == fs::canonical(fs::temp_directory_path()), "Unsafe fixture cleanup");
        fs::remove_all(root);
        std::cout << "Archive numerical order, Unicode, grouping, CLI, retention, stale batches and OS failures passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
