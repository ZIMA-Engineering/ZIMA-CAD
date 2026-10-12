#include "workspace_internal.hpp"
#include "../file_manager_tab.hpp"

#include <zima/document/file_path.hpp>
#include <zima/document/part_document.hpp>
#include <zima/assembly/assembly_document.hpp>
#include <zima/workspace/document_operations.hpp>
#include <zima/workspace/metadata_operations.hpp>

#include <algorithm>

namespace zima::app {
namespace {
std::filesystem::path native_path(const QString& path){
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::u8path(path.toStdString());
#endif
}
template<class Document> FileManagerNativeParameters parameters_from(const Document& document){
    FileManagerNativeParameters result;
    for(const auto& parameter:document.user_parameter_order)result.order.push_back(QString::fromStdString(parameter));
    for(const auto& [parameter,value]:document.user_parameters){const QString key=QString::fromStdString(parameter);result.values.insert(key,QString::fromStdString(value));if(!result.order.contains(key))result.order.push_back(key);}
    for(const auto& [parameter,labels]:document.user_parameter_labels){QMap<QString,QString> localized;for(const auto& [language,label]:labels)localized.insert(QString::fromStdString(language),QString::fromStdString(label));result.labels.insert(QString::fromStdString(parameter),std::move(localized));}
    return result;
}
template<class Document> void set_parameter(Document& document,const QString& parameter,const QString& value){
    const std::string name=parameter.toStdString(),text=value.toStdString();document.user_parameters[name]=text;document.user_parameter_values[name][""]=text;
    if(std::ranges::find(document.user_parameter_order,name)==document.user_parameter_order.end())document.user_parameter_order.push_back(name);
}
}

FileManagerTab* AssemblyWorkspaceWindow::open_file_manager(const QString& initial_path) {
    if(properties_dialog_||!active_sketch_id_.empty()) {
        state_->setText(tr("Nejprve dokončete nebo zrušte otevřené vlastnosti."));return nullptr;
    }
    const auto id=QStringLiteral("files:%1").arg(++file_manager_tab_sequence_,8,10,QLatin1Char('0'));
    const QString working=QString::fromStdString(document::path_to_utf8(working_directory_));
    auto* page=new FileManagerTab(application_settings_,working,initial_path,file_manager_stack_);
    active_browser_=nullptr;if(browser_stack_)browser_stack_->hide();
    page->setProperty("fileManagerTitle",tr("Správce souborů"));
    file_manager_tabs_[id]=page;file_manager_stack_->addWidget(page);active_file_manager_=page;
    page->open_file_requested=[this](const QString& path){static_cast<void>(open_document_path(path));};
    page->new_tab_requested=[this](const QString& path){open_file_manager(path);};
    page->settings_requested=[this]{
        show_global_settings();
        if(auto* dialog=dynamic_cast<GlobalSettingsDialog*>(global_settings_dialog_))dialog->show_file_manager();
    };
    page->native_parameters_requested=[this](const QString& path){return file_manager_native_parameters(path);};
    page->native_parameter_edit_requested=[this](const QString& path,const QString& parameter,const QString& value){return edit_file_manager_native_parameter(path,parameter,value);};
    page->title_changed=[this,id,page](const QString& title){
        page->setProperty("fileManagerTitle",title);
        for(int index=0;index<tabs_->count();++index)if(tabs_->tabData(index)==id){tabs_->setTabText(index,title.left(60));break;}
        if(active_file_manager_==page)setWindowTitle(QStringLiteral("ZIMA-CAD — %1 — %2").arg(instance_.label(),title));
    };
    page->navigate(page->current_directory(),false);
    refresh_tabs();file_manager_stack_->setCurrentWidget(page);update_document_area_visibility();
    return page;
}

void AssemblyWorkspaceWindow::select_file_manager(const QString& id) {
    const auto found=file_manager_tabs_.find(id);if(found==file_manager_tabs_.end())return;
    if(properties_dialog_||!active_sketch_id_.empty()){refresh_tabs();return;}
    active_browser_=nullptr;active_file_manager_=found->second;
    if(browser_stack_)browser_stack_->hide();
    file_manager_stack_->setCurrentWidget(found->second);update_document_area_visibility();
}

bool AssemblyWorkspaceWindow::close_file_manager(int index) {
    if(index<0||index>=tabs_->count())return false;
    const auto id=tabs_->tabData(index).toString();const auto found=file_manager_tabs_.find(id);
    if(found==file_manager_tabs_.end())return false;
    const bool active=active_file_manager_==found->second;auto* page=found->second;
    file_manager_tabs_.erase(found);file_manager_stack_->removeWidget(page);delete page;
    if(active){active_file_manager_=nullptr;if(!file_manager_tabs_.empty()){
        active_file_manager_=file_manager_tabs_.begin()->second;file_manager_stack_->setCurrentWidget(active_file_manager_);
    }}
    refresh_tabs();if(active&&!active_file_manager_)refresh_scene();else update_document_area_visibility();return true;
}

void AssemblyWorkspaceWindow::reload_file_managers() {
    for(const auto& [id,page]:file_manager_tabs_)page->reload_settings(application_settings_);
}

FileManagerNativeParameters AssemblyWorkspaceWindow::file_manager_native_parameters(const QString& path)const{
    FileManagerNativeParameters result;const auto requested=std::filesystem::absolute(native_path(path)).lexically_normal();
    try{
        for(const auto& state:workspace_.documents()){
            const auto found=std::visit([&](const auto& value)->std::optional<FileManagerNativeParameters>{
                if(value.path.empty()||std::filesystem::absolute(value.path).lexically_normal()!=requested)return std::nullopt;
                if constexpr(requires{value.session;})return parameters_from(value.session.document());
                else return std::nullopt;
            },state);if(found)return *found;
        }
        const QString suffix=QFileInfo(path).suffix().toLower();
        if(suffix=="prtz")return parameters_from(document::PartDocument::load(requested));
        if(suffix=="asmz")return parameters_from(assembly::AssemblyDocument::load(requested,{},false));
    }catch(const std::exception& error){result.error=QString::fromUtf8(error.what());}
    return result;
}

QString AssemblyWorkspaceWindow::edit_file_manager_native_parameter(const QString& path,const QString& parameter,const QString& value){
    const auto requested=std::filesystem::absolute(native_path(path)).lexically_normal();
    try{
        for(const auto& state:workspace_.documents()){
            const auto id=std::visit([&](const auto& open)->std::string{
                if(open.path.empty()||std::filesystem::absolute(open.path).lexically_normal()!=requested)return {};
                if constexpr(requires{open.session;})return open.session.document().document_id;else return {};
            },state);if(id.empty())continue;
            auto data=workspace::user_parameters(workspace_,id);const auto key=parameter.toStdString();data.flat[key]=value.toStdString();data.values[key][""]=value.toStdString();if(std::ranges::find(data.order,key)==data.order.end())data.order.push_back(key);
            if(!workspace::set_user_parameters(workspace_,id,std::move(data)))return tr("Dokument již není aktivní.");
            const auto saved=workspace::prepare_document_save(workspace_,id,requested).write();
            if(!workspace::complete_document_save(workspace_,saved))return tr("Dokument se během ukládání změnil. Zkuste úpravu znovu.");
            refresh_tabs();return {};
        }
        const QString suffix=QFileInfo(path).suffix().toLower();
        if(suffix=="prtz"){std::vector<kernel::BodyResult> calculated;auto document=document::PartDocument::load(requested,&calculated);set_parameter(document,parameter,value);document.save(requested,calculated);return {};}
        if(suffix=="asmz"){auto document=assembly::AssemblyDocument::load(requested,{},false);set_parameter(document,parameter,value);document.save(requested);return {};}
        return tr("Tento typ souboru nemá upravitelné nativní parametry.");
    }catch(const std::exception& error){return QString::fromUtf8(error.what());}
}

} // namespace zima::app
