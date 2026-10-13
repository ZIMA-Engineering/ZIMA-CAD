#include "workspace_internal.hpp"
#include "../file_manager_tab.hpp"
#include "../model_preview_tab.hpp"

#include <zima/document/file_path.hpp>
#include <zima/document/part_document.hpp>
#include <zima/document/metadata.hpp>
#include <zima/assembly/assembly_document.hpp>
#include <zima/workspace/document_operations.hpp>
#include <zima/workspace/metadata_operations.hpp>

#include <algorithm>

namespace zima::app {
using workspace_detail::run_background_task;
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
    for(const auto& [parameter,values]:document.user_parameter_values){QMap<QString,QString> localized;for(const auto& [language,value]:values)localized.insert(QString::fromStdString(language),QString::fromStdString(value));const QString key=QString::fromStdString(parameter);result.localized_values.insert(key,std::move(localized));if(!result.order.contains(key))result.order.push_back(key);}
    return result;
}
void set_parameter_value(document::UserParameterData& data,const QString& parameter,const QString& value,const QString& language){
    const std::string name=parameter.toStdString();auto& values=data.values[name];
    const bool localized=std::ranges::any_of(values,[](const auto& entry){return !entry.first.empty();});
    values[localized?language.toStdString():std::string{}]=value.toStdString();
    if(std::ranges::find(data.order,name)==data.order.end())data.order.push_back(name);
    document::normalize_user_parameters(data);
}
template<class Document> void set_parameter(Document& doc,const QString& parameter,const QString& value,const QString& language){
    document::UserParameterData data{doc.user_parameters,doc.user_parameter_order,doc.user_parameter_labels,doc.user_parameter_values};set_parameter_value(data,parameter,value,language);
    doc.user_parameters=std::move(data.flat);doc.user_parameter_order=std::move(data.order);doc.user_parameter_labels=std::move(data.labels);doc.user_parameter_values=std::move(data.values);
}
}

FileManagerTab* AssemblyWorkspaceWindow::open_file_manager(const QString& initial_path) {
    if(properties_dialog_||!active_sketch_id_.empty()) {
        state_->setText(tr("Nejprve dokončete nebo zrušte otevřené vlastnosti."));return nullptr;
    }
    const auto id=QStringLiteral("files:%1").arg(++file_manager_tab_sequence_,8,10,QLatin1Char('0'));
    const QString working=QString::fromStdString(document::path_to_utf8(working_directory_));
    auto* page=new FileManagerTab(application_settings_,working,initial_path,file_manager_stack_);
    page->set_working_directory_action(working_directory_action_);
    active_preview_=nullptr;
    active_browser_=nullptr;if(browser_stack_)browser_stack_->hide();
    page->setProperty("fileManagerTitle",tr("Správce souborů"));
    file_manager_tabs_[id]=page;file_manager_stack_->addWidget(page);active_file_manager_=page;
    page->preview_file_requested=[this](const QString& path){open_model_preview(path);};
    page->open_file_requested=[this](const QString& path){static_cast<void>(open_document_path(path));};
    page->new_tab_requested=[this](const QString& path){open_file_manager(path);};
    page->working_directory_requested=[this](const QString& path){
        const auto target=std::filesystem::u8path(path.toStdString());
        if(!std::filesystem::is_directory(target)){report_operation_error(tr("Neplatný adresář"),tr("Vybraná cesta není existující adresář."));return;}
        try {change_working_directory(target,true);}
        catch(const std::exception& error){report_operation_error(tr("Pracovní adresář je obsazený"),QObject::tr(error.what()));return;}
        state_->setText(tr("Pracovní adresář: %1").arg(QDir::toNativeSeparators(path)));
    };
    page->settings_requested=[this]{
        show_global_settings();
        if(auto* dialog=dynamic_cast<GlobalSettingsDialog*>(global_settings_dialog_))dialog->show_file_manager();
    };
    page->native_parameters_requested=[this](const QString& path){return file_manager_native_parameters(path);};
    page->native_parameters_revision_requested=[this](const QString& path){
        const auto requested=std::filesystem::absolute(native_path(path)).lexically_normal();
        for(const auto& state:workspace_.documents()){
            const auto revision=std::visit([&](const auto& open)->QString{
                if constexpr(requires{open.session;})if(!open.path.empty()&&std::filesystem::absolute(open.path).lexically_normal()==requested)
                    return QString::fromStdString(open.session.document().document_id)+QLatin1Char(':')+QString::number(open.session.revision())+QLatin1Char(':')+QString::number(open.session.is_dirty());
                return {};
            },state);if(!revision.isEmpty())return revision;
        }return QString{};
    };
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
    active_preview_=nullptr;
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

void AssemblyWorkspaceWindow::open_model_preview(const QString& path){
    if(properties_dialog_||!active_sketch_id_.empty()){state_->setText(tr("Nejprve dokončete nebo zrušte otevřené vlastnosti."));return;}
    const QString canonical=QFileInfo(path).canonicalFilePath();if(canonical.isEmpty())return;
    const QString id="preview:"+canonical;auto found=preview_tabs_.find(id);if(found!=preview_tabs_.end()){select_model_preview(id);refresh_tabs();return;}
    begin_status_operation(tr("Načítám náhled: %1").arg(QFileInfo(path).fileName()));
    std::unique_ptr<ModelPreviewTab> pending;
    try{
        if(QFileInfo(path).suffix().compare("dxf",Qt::CaseInsensitive)==0){pending=std::make_unique<ModelPreviewTab>(canonical,preview_stack_);pending->load_dxf();}
        else{auto data=run_background_task([canonical]{return load_step_preview(canonical);});pending=std::make_unique<ModelPreviewTab>(canonical,preview_stack_);pending->set_step(std::move(data));}
        auto* page=pending.release();preview_tabs_[id]=page;preview_stack_->addWidget(page);active_preview_=page;active_browser_=nullptr;active_file_manager_=nullptr;
        preview_stack_->setCurrentWidget(page);refresh_tabs();update_document_area_visibility();finish_status_operation(tr("Náhled načten."));
    }catch(const std::exception& error){finish_status_operation(tr("Načtení náhledu selhalo"),false);report_operation_error(tr("Načtení náhledu selhalo"),QObject::tr(error.what()));}
}
void AssemblyWorkspaceWindow::select_model_preview(const QString& id){
    const auto found=preview_tabs_.find(id);if(found==preview_tabs_.end())return;
    if(properties_dialog_||!active_sketch_id_.empty()){refresh_tabs();return;}
    active_browser_=nullptr;active_file_manager_=nullptr;active_preview_=found->second;preview_stack_->setCurrentWidget(found->second);update_document_area_visibility();
}
bool AssemblyWorkspaceWindow::close_model_preview(int index){
    if(index<0||index>=tabs_->count())return false;const QString id=tabs_->tabData(index).toString();const auto found=preview_tabs_.find(id);if(found==preview_tabs_.end())return false;
    const bool active=active_preview_==found->second;auto* page=found->second;preview_tabs_.erase(found);preview_stack_->removeWidget(page);delete page;
    if(active){active_preview_=nullptr;if(!preview_tabs_.empty()){active_preview_=preview_tabs_.begin()->second;preview_stack_->setCurrentWidget(active_preview_);}}
    refresh_tabs();if(active&&!active_preview_)refresh_scene();else update_document_area_visibility();return true;
}

FileManagerNativeParameters AssemblyWorkspaceWindow::file_manager_native_parameters(const QString& path)const{
    FileManagerNativeParameters result;const auto requested=std::filesystem::absolute(native_path(path)).lexically_normal();
    try{
        for(const auto& state:workspace_.documents()){
            const auto found=std::visit([&](const auto& value)->std::optional<FileManagerNativeParameters>{
                if(value.path.empty()||std::filesystem::absolute(value.path).lexically_normal()!=requested)return std::nullopt;
                if constexpr(requires{value.session;}){auto data=parameters_from(value.session.document());data.unsaved=value.session.is_dirty();return data;}
                else return std::nullopt;
            },state);if(found)return *found;
        }
        const QString suffix=QFileInfo(path).suffix().toLower();
        if(QStringList{"prtz","asmz","frmz","tblz","symz"}.contains(suffix)){
            const auto data=document::read_native_user_parameters(requested);
            struct ParameterInspection {
                const decltype(data.flat)& user_parameters;
                const decltype(data.order)& user_parameter_order;
                const decltype(data.labels)& user_parameter_labels;
                const decltype(data.values)& user_parameter_values;
            };
            return parameters_from(ParameterInspection{data.flat,data.order,data.labels,data.values});
        }
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
            auto data=workspace::user_parameters(workspace_,id);set_parameter_value(data,parameter,value,application_settings_.language);
            if(!workspace::set_user_parameters(workspace_,id,std::move(data)))return tr("Dokument již není aktivní.");
            const auto saved=workspace::prepare_document_save(workspace_,id,requested).write();
            if(!workspace::complete_document_save(workspace_,saved))return tr("Dokument se během ukládání změnil. Zkuste úpravu znovu.");
            refresh_tabs();return {};
        }
        const QString suffix=QFileInfo(path).suffix().toLower();
        if(QStringList{"prtz","frmz","tblz","symz"}.contains(suffix)){std::vector<kernel::BodyResult> calculated;auto document=document::PartDocument::load(requested,&calculated);set_parameter(document,parameter,value,application_settings_.language);document.save(requested,calculated);return {};}
        if(suffix=="asmz"){auto document=assembly::AssemblyDocument::load(requested,{},false);set_parameter(document,parameter,value,application_settings_.language);document.save(requested);return {};}
        return tr("Tento typ souboru nemá upravitelné nativní parametry.");
    }catch(const std::exception& error){return QString::fromUtf8(error.what());}
}

} // namespace zima::app
