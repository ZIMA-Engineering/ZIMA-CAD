#include <zima/command_host/host.hpp>
#include "../../../browser/bookmarks.hpp"
#include <QJsonDocument>
#include <QJsonObject>

namespace zima::command_host {
void Host::register_browser_commands(){
    const auto mutate=[this](const std::function<Result(browser::Bookmarks&)>& action){
        if(options_.browser_directory.empty())return Result::failure("browser_unavailable",tr("Browser data directory is not configured."));
        browser::Bookmarks store(QString::fromStdWString(options_.browser_directory.wstring()));
        QString error;if(!store.reload(&error))return Result::failure("bookmark_error",error.toStdString());
        auto result=action(store);if(result.ok&&options_.bookmarks_changed)options_.bookmarks_changed();return result;
    };
    dispatcher_.add({"browser.open",tr("Open a web tab in the desktop application."),{{"url",false}},false},[this](const Json& args){
        const auto url=args.value("url",std::string{});
        if(!url.empty()&&!browser::Bookmarks::allowed_url(QUrl(QString::fromStdString(url))))return Result::failure("invalid_url",tr("Enter a valid HTTP or HTTPS address."));
        if(!options_.open_browser)return Result::failure("desktop_required",tr("This command requires the desktop application."));
        return options_.open_browser(url)?Result::success():Result::failure("editing_in_progress",tr("Nejprve dokončete nebo zrušte otevřenou editaci."));
    });
    dispatcher_.add({"browser.bookmarks",tr("List saved bookmarks and folders."),{},false},[mutate](const Json&){
        return mutate([](browser::Bookmarks& store){return Result::success(Json::parse(QJsonDocument(store.items()).toJson().toStdString()));});
    });
    dispatcher_.add({"browser.bookmark.add",tr("Save a web bookmark."),{{"name"},{"url"},{"parent",false}},true},[mutate](const Json& args){
        return mutate([&](browser::Bookmarks& store){QString error;
            const auto id=store.add(QString::fromStdString(args.at("name").get<std::string>()),QUrl(QString::fromStdString(args.at("url").get<std::string>())),QString::fromStdString(args.value("parent",std::string{})),false,&error);
            return id.isEmpty()?Result::failure("bookmark_error",error.toStdString()):Result::success({{"id",id.toStdString()}});
        });
    });
    dispatcher_.add({"browser.folder.add",tr("Create a bookmark folder."),{{"name"},{"parent",false}},true},[mutate](const Json& args){
        return mutate([&](browser::Bookmarks& store){QString error;
            const auto id=store.add(QString::fromStdString(args.at("name").get<std::string>()),{},QString::fromStdString(args.value("parent",std::string{})),true,&error);
            return id.isEmpty()?Result::failure("bookmark_error",error.toStdString()):Result::success({{"id",id.toStdString()}});
        });
    });
    dispatcher_.add({"browser.bookmark.edit",tr("Rename or move a bookmark or folder."),{{"id"},{"name"},{"url",false},{"parent",false}},true},[mutate](const Json& args){
        return mutate([&](browser::Bookmarks& store){QString error;
            const auto id=QString::fromStdString(args.at("id").get<std::string>());
            QJsonObject original;
            for(const auto& value:store.items())if(value.toObject().value("id").toString()==id){original=value.toObject();break;}
            const auto url=args.contains("url")?QString::fromStdString(args.at("url").get<std::string>()):original.value("url").toString();
            const auto parent=args.contains("parent")?QString::fromStdString(args.at("parent").get<std::string>()):original.value("parent").toString();
            const auto ok=store.update(id,QString::fromStdString(args.at("name").get<std::string>()),QUrl(url),parent,&error);
            return ok?Result::success():Result::failure("bookmark_error",error.toStdString());
        });
    });
    dispatcher_.add({"browser.bookmark.remove",tr("Remove a bookmark or folder and its contents."),{{"id"}},true},[mutate](const Json& args){
        return mutate([&](browser::Bookmarks& store){QString error;
            const auto ok=store.remove(QString::fromStdString(args.at("id").get<std::string>()),&error);
            return ok?Result::success():Result::failure("bookmark_error",error.toStdString());
        });
    });
}
}
