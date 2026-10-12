#include "workspace_internal.hpp"
#include "../../browser/browser.hpp"
#include <QWebEnginePage>

namespace zima::app {
void AssemblyWorkspaceWindow::ensure_browser_runtime() {
    if(!browser_runtime_) {
        const auto config=application_settings_.installation_root.isEmpty()
            ?QFileInfo(application_settings_.base_config_path).absolutePath()
            :application_settings_.installation_root+"/config";
        browser_runtime_=new browser::Runtime(QDir(config).filePath("www"),[this]{
            return QString::fromStdWString(working_directory_.wstring());
        },this);
        browser_runtime_->configure(application_settings_.browser);
        connect(browser_runtime_,&browser::Runtime::status,this,[this](const QString& text){state_->setText(text);});
        browser_runtime_->new_page=[this]() -> QWebEnginePage* {
            const auto tab=open_browser(QUrl("about:blank"));return tab?tab->page():nullptr;
        };
        QString error;
        if(!browser_runtime_->bookmarks.reload(&error))report_operation_error(tr("Web browser"),error);
    }
}
browser::Tab* AssemblyWorkspaceWindow::open_browser(const QUrl& requested) {
    if(properties_dialog_||!active_sketch_id_.empty()) {
        state_->setText(tr("Nejprve dokončete nebo zrušte otevřené vlastnosti."));return nullptr;
    }
    ensure_browser_runtime();
    const auto id=QStringLiteral("web:%1").arg(++browser_tab_sequence_,8,10,QLatin1Char('0'));
    auto* page=new browser::Tab(*browser_runtime_,browser_stack_);
    active_file_manager_=nullptr;if(file_manager_stack_)file_manager_stack_->hide();
    browser_tabs_[id]=page;browser_stack_->addWidget(page);active_browser_=page;
    connect(page,&browser::Tab::title_changed,this,[this,id,page]{
        for(int i=0;i<tabs_->count();++i)if(tabs_->tabData(i)==id)tabs_->setTabText(i,page->title().left(60));
    });
    connect(page,&browser::Tab::settings_requested,this,[this]{
        show_global_settings();if(auto* dialog=dynamic_cast<GlobalSettingsDialog*>(global_settings_dialog_))dialog->show_browser();
    });
    refresh_tabs();browser_stack_->setCurrentWidget(page);update_document_area_visibility();
    page->navigate(requested.isEmpty()?QUrl(application_settings_.browser.home.isEmpty()?"about:blank":application_settings_.browser.home):requested);
    return page;
}
void AssemblyWorkspaceWindow::select_browser(const QString& id) {
    const auto found=browser_tabs_.find(id);if(found==browser_tabs_.end())return;
    if(properties_dialog_||!active_sketch_id_.empty()) {
        refresh_tabs();return;
    }
    active_file_manager_=nullptr;if(file_manager_stack_)file_manager_stack_->hide();
    active_browser_=found->second;browser_stack_->setCurrentWidget(found->second);
    QString error;if(!browser_runtime_->bookmarks.reload(&error))state_->setText(error);
    update_document_area_visibility();
}
bool AssemblyWorkspaceWindow::close_browser(int index) {
    if(index<0||index>=tabs_->count())return false;
    const auto id=tabs_->tabData(index).toString();const auto found=browser_tabs_.find(id);
    if(found==browser_tabs_.end())return false;
    const bool active=active_browser_==found->second;
    auto* page=found->second;browser_tabs_.erase(found);browser_stack_->removeWidget(page);delete page;
    if(active) {
        active_browser_=nullptr;
        if(!browser_tabs_.empty()){active_browser_=browser_tabs_.begin()->second;browser_stack_->setCurrentWidget(active_browser_);}
    }
    refresh_tabs();
    if(active&&!active_browser_)refresh_scene();else update_document_area_visibility();
    return true;
}
} // namespace zima::app
