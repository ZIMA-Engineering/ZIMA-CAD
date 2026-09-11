#include <zima/command_host/host.hpp>
#include <zima/workspace/document_operations.hpp>

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
