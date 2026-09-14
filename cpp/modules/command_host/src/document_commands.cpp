#include <zima/command_host/host.hpp>
#include <zima/workspace/document_operations.hpp>
#include <zima/workspace/archive_operations.hpp>
#include <zima/workspace/file_removal_operations.hpp>
#include <limits>

namespace zima::command_host {
namespace {
std::string path_text(const std::filesystem::path& path) {
    const auto text=path.generic_u8string();return {text.begin(),text.end()};
}
std::filesystem::path resolve(const std::string& text,const std::filesystem::path& directory) {
    auto path=std::filesystem::u8path(text);
    if(path.is_relative())path=directory/path;
    return std::filesystem::absolute(path).lexically_normal();
}
}
void Host::register_document_commands() {
    using Type = commands::ArgumentType;
    for (const bool directory_scope : {false, true}) {
        const std::string prefix = directory_scope ? "directory.archives." : "file.archives.";
        for (const bool prune : {false, true}) {
            std::vector<commands::Argument> parameters;
            if (prune && directory_scope) parameters.push_back({"keep", true, Type::Integer});
            parameters.push_back({"path", !directory_scope});
            if (prune && !directory_scope) parameters.push_back({"keep", true, Type::Integer});
            dispatcher_.add({prefix + (prune ? "prune" : "list"),
                tr(prune ? "Remove older native archives and keep the requested newest count per document."
                         : "List numbered native archives in numerical order without opening documents."),
                std::move(parameters), prune}, [this, directory_scope, prune](const Json& args) {
                try {
                    const auto raw = args.value("path", std::string{});
                    if (args.contains("path") && raw.empty())
                        return Result::failure("invalid_path", tr("Specify a file or directory path."));
                    const auto path = args.contains("path") ? resolve(raw, directory_) : directory_;
                    std::size_t keep = 0;
                    if (prune) {
                        const auto& value = args.at("keep");
                        // JSON signed/unsigned comparisons can wrap at UINT64_MAX.
                        const bool invalid = value.is_number_unsigned()
                            ? value.get<std::uint64_t>() > std::numeric_limits<std::size_t>::max()
                            : value.get<std::int64_t>() < 0 ||
                                static_cast<std::uint64_t>(value.get<std::int64_t>()) > std::numeric_limits<std::size_t>::max();
                        if (invalid)
                            return Result::failure("invalid_arguments", tr("The number of archives to keep must be a nonnegative integer."));
                        keep = value.get<std::size_t>();
                    }
                    workspace::ArchiveGroups groups;
                    workspace::ArchiveRemoval removal;
                    std::vector<workspace::ArchiveFile> selected;
                    bool complete = false;
                    io([&] {
                        groups = directory_scope ? workspace::directory_archives(path)
                            : workspace::ArchiveGroups{{path, workspace::document_archives(path)}};
                        if (prune) {
                            selected = workspace::archives_to_remove(groups, keep);
                            removal = workspace::remove_archives(selected);
                        }
                        complete = true;
                    });
                    if (!complete) throw std::runtime_error("I/O runner did not complete archive operation");
                    Json data{{"path", path_text(path)}, {"scope", directory_scope ? "directory" : "file"}};
                    if (!prune) {
                        data["groups"] = Json::array();
                        std::uintmax_t total = 0;
                        std::size_t count = 0;
                        for (const auto& [base, archives] : groups) {
                            Json files = Json::array();
                            for (const auto& file : archives) {
                                files.push_back({{"path", path_text(file.path)}, {"version", file.version}, {"size_bytes", file.size}});
                                total += file.size; ++count;
                            }
                            data["groups"].push_back({{"document_path", path_text(base)}, {"archives", std::move(files)}});
                        }
                        data["count"] = count; data["total_size_bytes"] = total;
                    } else {
                        data["keep"] = keep;
                        data["selected_count"] = selected.size();
                        data["removed_paths"] = Json::array();
                        for (const auto& file : removal.removed) data["removed_paths"].push_back(path_text(file));
                        data["removed_bytes"] = removal.removed_bytes;
                        data["changed"] = !removal.removed.empty();
                        if (!removal.removed.empty()) change_ = Change{ChangeKind::Files, {}};
                        if (!removal.ok()) {
                            const auto message = tr(removal.message.c_str()) + "\n" + path_text(removal.failed_path) +
                                "\n" + tr("Archives removed before the failure:") + " " + std::to_string(removal.removed.size());
                            auto result = Result::failure(removal.code, message);
                            data["failed_path"] = path_text(removal.failed_path);
                            result.data = std::move(data);
                            return result;
                        }
                    }
                    return Result::success(std::move(data));
                } catch (const workspace::ArchiveError& error) { return Result::failure(error.code, tr(error.what())); }
                  catch (const std::filesystem::filesystem_error& error) { return Result::failure("archive_io_error", tr(error.what())); }
            });
        }
    }
    dispatcher_.add({"delete_file", tr("Delete the saved file of an open document; optionally include archives."),
        {{"document", false}, {"archives", false, Type::Boolean}, {"discard", false, Type::Boolean}}, true},
        [this](const Json& args) {
            const auto id = args.value("document", workspace_.active_document_id());
            if (id.empty()) return Result::failure("no_document", tr("Není otevřený dokument."));
            if (interaction().template_document)
                return Result::failure("unsupported_document", tr("Tento příkaz není dostupný při úpravě šablony."));
            try {
                const auto plan = workspace::prepare_document_file_removal(
                    workspace_, id, args.value("archives", false), args.value("discard", false));
                const auto removed = workspace::remove_document_file(workspace_, plan);
                Json paths = Json::array();
                for (const auto& path : removed.removed) paths.push_back(path_text(path));
                Json data{{"document", id}, {"path", path_text(plan.path())},
                    {"closed", !removed.closed_document.empty()}, {"changed", !removed.removed.empty()},
                    {"removed_paths", std::move(paths)}, {"removed_bytes", removed.removed_bytes}};
                if (!removed.closed_document.empty()) change_ = Change{ChangeKind::Close, workspace_.displayed_document_id()};
                else if (!removed.removed.empty()) change_ = Change{ChangeKind::Files, {}};
                if (!removed.ok()) {
                    data["failed_path"] = path_text(removed.failed_path);
                    auto result = Result::failure(removed.code, tr(removed.message.c_str()) + "\n" +
                        path_text(removed.failed_path) + "\n" + tr("Files removed before the failure:") + " " +
                        std::to_string(removed.removed.size()));
                    result.data = std::move(data); return result;
                }
                return Result::success(std::move(data));
            } catch (const workspace::FileRemovalError& error) { return Result::failure(error.code, tr(error.what())); }
              catch (const workspace::ArchiveError& error) { return Result::failure(error.code, tr(error.what())); }
              catch (const std::filesystem::filesystem_error& error) { return Result::failure("file_io_error", tr(error.what())); }
              catch (const std::exception& error) { return Result::failure("file_rejected", tr(error.what())); }
        });
    dispatcher_.add({"pwd",tr("Zobrazit pracovní adresář."),{},false},[this](const Json&) {
        return Result::success({{"path",path_text(directory_)}});
    });
    dispatcher_.add({"cd",tr("Změnit pracovní adresář: cd cesta."),{{"path",true}},true},[this](const Json& args) {
        const auto path=resolve(args["path"].get<std::string>(),directory_);
        if(!std::filesystem::is_directory(path))return Result::failure("invalid_directory",tr("Pracovní adresář neexistuje."));
        directory_=path;change_=Change{ChangeKind::Directory,{}};
        return Result::success({{"path",path_text(directory_)}});
    });
    dispatcher_.add({"activate",tr("Zobrazit otevřený dokument podle jeho ID."),{{"document",true}},true},[this](const Json& args) {
        const auto id=args["document"].get<std::string>();
        if(!workspace_.find(id))return Result::failure("document_not_found",tr("Dokument není otevřený."));
        activate(id);change_=Change{ChangeKind::Activate,id};return Result::success(documents(workspace_));
    });
    dispatcher_.add({"close",tr("Zavřít dokument; volba discard výslovně zahodí neuložené změny."),
        {{"document",false},{"discard",false,commands::ArgumentType::Boolean}},true},[this](const Json& args) {
        const auto id=args.value("document",workspace_.active_document_id());
        if(id.empty())return Result::failure("no_document",tr("Není otevřený dokument."));
        const auto result=workspace::close_document(workspace_,id,args.value("discard",false));
        if(result==workspace::CloseDocumentResult::NotOpen)return Result::failure("document_not_found",tr("Dokument není otevřený."));
        if(result==workspace::CloseDocumentResult::UnsavedChanges)return Result::failure("unsaved_changes",tr("Dokument má neuložené změny. Uložte jej nebo povolte zahození volbou discard."));
        change_=Change{ChangeKind::Close,workspace_.displayed_document_id()};
        return Result::success(documents(workspace_));
    });
    dispatcher_.add({"save_as",tr("Uložit nezávislou kopii včetně navázaných výkresů: save_as cesta."),
        {{"path",true},{"document",false}},true},[this](const Json& args) {
        const auto checked=target(args);if(!checked.ok)return checked;
        if(interaction().template_document)return Result::failure("unsupported_document",tr("Tento příkaz není dostupný při úpravě šablony."));
        const auto id=workspace_.active_document_id();
        const auto path=resolve(args["path"].get<std::string>(),directory_);
        if(options_.progress)options_.progress(Activity::Write,path);
        auto snapshot=workspace_;std::vector<std::filesystem::path> files;bool finished=false;
        const auto directory=directory_;
        io([snapshot=std::move(snapshot),id,path,directory,&files,&finished] {
            files=snapshot.save_copy(id,path,directory);finished=true;
        });
        if(!finished)throw std::runtime_error("I/O runner did not complete native copying");
        directory_=path.parent_path();change_=Change{ChangeKind::Copy,id};
        Json paths=Json::array();for(const auto& file:files)paths.push_back(path_text(file));
        return Result::success({{"paths",std::move(paths)},{"document",id}});
    });
}
} // namespace zima::command_host
