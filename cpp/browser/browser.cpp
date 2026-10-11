#include "browser.hpp"
#include "credentials.hpp"
#include "../app/resource_icon.hpp"
#include "../app/file_dialog.hpp"
#include <zima/ui/properties_subwindow.hpp>

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QImage>
#include <QJsonObject>
#include <QLineEdit>
#include <QLockFile>
#include <QSet>
#include <QTreeWidgetItemIterator>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QSplitter>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWebEngineCookieStore>
#include <QWebEngineDownloadRequest>
#include <QWebEngineHistory>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>

namespace zima::browser {
namespace {
class Page final : public QWebEnginePage {
public:
    Page(Runtime& runtime, QObject* parent) : QWebEnginePage(runtime.profile(), parent), runtime_(runtime) {
        runtime.credentials()->attach(this);
    }
protected:
    QWebEnginePage* createWindow(WebWindowType) override {
        return runtime_.new_page ? runtime_.new_page() : nullptr;
    }
    bool acceptNavigationRequest(const QUrl& url, NavigationType type, bool main) override {
        // Web content cannot navigate to local files or CAD command schemes.
        if (main && !Bookmarks::allowed_url(url) && url != QUrl("about:blank")) return false;
        return QWebEnginePage::acceptNavigationRequest(url,type,main);
    }
private:
    Runtime& runtime_;
};
class BookmarkDialog final : public ui::PropertiesSubWindow {
public:
    BookmarkDialog(Bookmarks& store, QJsonObject original, QString folderId,
        QString name, QString url, bool folder, QWidget* owner)
        : PropertiesSubWindow(tr("Bookmark"),owner), store_(store), original_(std::move(original)), folder_(folder) {
        setObjectName("browserBookmarkDialog");
        setWindowTitle(tr("Bookmark")); set_initial_size({480,260});
        auto* form = new QFormLayout;
        name_ = new QLineEdit(name,this); name_->setObjectName("browserBookmarkName");
        url_ = new QLineEdit(url,this); url_->setObjectName("browserBookmarkUrl");
        folders_ = new QComboBox(this); folders_->setObjectName("browserBookmarkFolder");
        folders_->addItem(tr("Bookmarks"),QString{});
        for(const auto& value:store.items()) {
            const auto item=value.toObject();
            if(item.value("folder").toBool() && item.value("id")!=original_.value("id"))
                folders_->addItem(item.value("name").toString(),item.value("id").toString());
        }
        folders_->setCurrentIndex(std::max(0,folders_->findData(folderId)));
        form->addRow(tr("Name"),name_);
        if(!folder_)form->addRow(tr("Address"),url_);
        form->addRow(tr("Folder"),folders_);
        content_layout()->addLayout(form);content_layout()->addStretch();
    }
protected:
    bool submit() override {
        QString error;
        const auto url=QUrl::fromUserInput(url_->text().trimmed());
        bool ok;
        if(original_.isEmpty())ok=!store_.add(name_->text(),url,folders_->currentData().toString(),folder_,&error).isEmpty();
        else ok=store_.update(original_.value("id").toString(),name_->text(),url,folders_->currentData().toString(),&error);
        if(!ok)QMessageBox::warning(this,tr("Bookmarks"),error);
        return ok;
    }
private:
    Bookmarks& store_;QJsonObject original_;bool folder_;
    QLineEdit* name_{};QLineEdit* url_{};QComboBox* folders_{};
};
class BookmarkTree final : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;
    std::function<void(QTreeWidgetItem*)> middle_click;
protected:
    void mouseReleaseEvent(QMouseEvent* event) override {
        if(event->button()==Qt::MiddleButton) {
            if(auto* item=itemAt(event->position().toPoint());item&&middle_click)middle_click(item);
            event->accept();return;
        }
        QTreeWidget::mouseReleaseEvent(event);
    }
};
}
Runtime::Runtime(QString directory,std::function<QString()> workingDirectory,QWidget* owner)
    :QObject(owner),bookmarks(directory,this),directory_(std::move(directory)),
    working_directory_(std::move(workingDirectory)),owner_(owner) {}
Runtime::~Runtime() { delete credentials_;delete profile_; }
Credentials* Runtime::credentials(){profile();return credentials_;}
QWebEngineProfile* Runtime::profile() {
    if(profile_)return profile_;
    QDir().mkpath(directory_);
    // Chromium profiles must not be opened simultaneously by two processes.
    // Instance ownership is established before opening the persistent profile.
    profile_lock_=std::make_unique<QLockFile>(QDir(directory_).filePath("profile.lock"));
    if(profile_lock_->tryLock(0)) {
        profile_=new QWebEngineProfile(QStringLiteral("ZIMA-CAD"),this);
        profile_->setPersistentStoragePath(QDir(directory_).filePath("profile"));
        profile_->setCachePath(QDir(directory_).filePath("cache"));
        profile_->setPersistentCookiesPolicy(QWebEngineProfile::AllowPersistentCookies);
    } else {
        profile_=new QWebEngineProfile(this);
        emit status(tr("Another application instance uses the browser profile. This window uses temporary cookies."));
    }
    profile_->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls,false);
    profile_->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls,false);
    credentials_=new Credentials(directory_,preferences,owner_);
    credentials_->install_script(profile_);
    profile_->cookieStore()->setCookieFilter([this](const QWebEngineCookieStore::FilterRequest& request) {
        return !block_third_party_.load() || !request.thirdParty;
    });
    connect(profile_,&QWebEngineProfile::downloadRequested,this,[this](QWebEngineDownloadRequest* request) {
        QString target=QDir(working_directory_()).filePath(QFileInfo(request->suggestedFileName()).fileName());
        // Never overwrite a file without the ordinary save chooser.
        if(preferences.ask_download_path||QFileInfo::exists(target)) {
            target=app::save_file(owner_,tr("Download"),target,tr("All files (*)"));
            if(target.isEmpty()){request->cancel();return;}
        }
        const QFileInfo file(target);
        request->setDownloadDirectory(file.absolutePath());request->setDownloadFileName(file.fileName());
        connect(request,&QWebEngineDownloadRequest::stateChanged,this,[this,request] {
            if(request->state()==QWebEngineDownloadRequest::DownloadCompleted)
                emit status(tr("Downloaded: %1").arg(QDir(request->downloadDirectory()).filePath(request->downloadFileName())));
            else if(request->state()==QWebEngineDownloadRequest::DownloadInterrupted)
                emit status(tr("Download failed. Error code: %1.").arg(static_cast<int>(request->interruptReason())));
        });
        request->accept(); emit status(tr("Downloading: %1").arg(file.fileName()));
    });
    return profile_;
}
void Runtime::configure(Preferences value){preferences=std::move(value);block_third_party_=preferences.block_third_party_cookies;emit preferences_changed();}
void Runtime::clear_cookies(){if(profile_)profile_->cookieStore()->deleteAllCookies();}
void Runtime::clear_cache(){if(profile_)profile_->clearHttpCache();}
void Runtime::show_passwords(){credentials()->show_manager();}

Tab::Tab(Runtime& runtime,QWidget* parent):QWidget(parent),runtime_(runtime) {
    setObjectName("browserTab");
    auto* layout=new QHBoxLayout(this);layout->setContentsMargins(0,0,0,0);
    auto* splitter=new QSplitter(Qt::Horizontal,this);layout->addWidget(splitter);
    auto* left=new QWidget(splitter);auto* leftLayout=new QVBoxLayout(left);
    leftLayout->setContentsMargins(0,0,0,0);leftLayout->setSpacing(0);
    auto* header=new QToolBar(left);header->setIconSize({18,18});
    header->addAction(app::resource_icon("settings"),tr("Browser settings"),this,[this]{emit settings_requested();});
    auto* add=header->addAction(app::resource_icon("new"),tr("Add bookmark or folder"));
    leftLayout->addWidget(header);
    auto* tree=new BookmarkTree(left);tree_=tree;tree->setObjectName("browserBookmarks");
    tree->setHeaderLabel(tr("Bookmarks"));tree->setIconSize({18,18});tree->setContextMenuPolicy(Qt::CustomContextMenu);
    leftLayout->addWidget(tree,1);
    auto* right=new QWidget(splitter);auto* rightLayout=new QVBoxLayout(right);
    rightLayout->setContentsMargins(0,0,0,0);rightLayout->setSpacing(0);
    auto* toolbar=new QToolBar(right);toolbar->setObjectName("browserNavigation");toolbar->setIconSize({18,18});
    auto* home=toolbar->addAction(app::resource_icon("web-home"),tr("Home"));home->setObjectName("browserHome");
    auto* back=toolbar->addAction(app::resource_icon("web-back"),tr("Back"));back->setObjectName("browserBack");
    auto* forward=toolbar->addAction(app::resource_icon("web-forward"),tr("Forward"));forward->setObjectName("browserForward");
    auto* refresh=toolbar->addAction(app::resource_icon("web-refresh"),tr("Reload"));refresh->setObjectName("browserReload");
    address_=new QLineEdit(toolbar);address_->setObjectName("browserAddress");address_->setPlaceholderText(tr("Address"));
    address_->setAccessibleName(tr("Address"));address_->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
    toolbar->addWidget(address_);
    pin_=toolbar->addAction(app::resource_icon("web-pin"),tr("Save bookmark"));pin_->setObjectName("browserPin");pin_->setCheckable(true);
    rightLayout->addWidget(toolbar);
    view_=new QWebEngineView(right);view_->setObjectName("browserView");view_->setPage(new Page(runtime_,view_));rightLayout->addWidget(view_,1);
    splitter->setStretchFactor(0,0);splitter->setStretchFactor(1,1);splitter->setSizes({280,920});
    connect(home,&QAction::triggered,this,[this]{navigate(QUrl(runtime_.preferences.home.isEmpty()?"about:blank":runtime_.preferences.home));});
    connect(back,&QAction::triggered,view_,&QWebEngineView::back);
    connect(forward,&QAction::triggered,view_,&QWebEngineView::forward);
    connect(refresh,&QAction::triggered,view_,&QWebEngineView::reload);
    connect(address_,&QLineEdit::returnPressed,this,[this]{navigate(QUrl::fromUserInput(address_->text().trimmed()));});
    connect(pin_,&QAction::triggered,this,[this]{update_pin();edit_bookmark(nullptr,false,true);});
    connect(view_,&QWebEngineView::urlChanged,this,[this](const QUrl& url){address_->setText(url.toString());update_pin();});
    connect(view_,&QWebEngineView::titleChanged,this,[this]{emit title_changed();});
    const auto history=[this,back,forward]{back->setEnabled(view_->history()->canGoBack());forward->setEnabled(view_->history()->canGoForward());};
    connect(view_,&QWebEngineView::loadFinished,this,[history](bool){history();});history();
    connect(view_,&QWebEngineView::iconChanged,this,[this](const QIcon& icon){
        for(const auto& v:runtime_.bookmarks.items())if(v.toObject().value("url").toString()==url().toString())
            runtime_.bookmarks.save_icon(v.toObject().value("id").toString(),icon.pixmap(32,32).toImage());
    });
    connect(&runtime_.bookmarks,&Bookmarks::changed,this,&Tab::refresh_tree);
    connect(tree,&QTreeWidget::itemClicked,this,[this](QTreeWidgetItem* item,int){
        if(!item->data(0,Qt::UserRole+1).toBool())navigate(QUrl(item->data(0,Qt::UserRole+2).toString()));
    });
    tree->middle_click=[this](QTreeWidgetItem* item){
        if(!item->data(0,Qt::UserRole+1).toBool()&&runtime_.new_page)
            if(auto* page=runtime_.new_page())page->setUrl(QUrl(item->data(0,Qt::UserRole+2).toString()));
    };
    connect(add,&QAction::triggered,this,[this,header]{
        QMenu menu(this);auto* bookmark=menu.addAction(tr("Add bookmark"));auto* folder=menu.addAction(tr("Add folder"));
        const auto action=menu.exec(header->mapToGlobal(QPoint(0,header->height())));
        if(action)edit_bookmark(nullptr,action==folder,action==bookmark);
    });
    connect(tree,&QTreeWidget::customContextMenuRequested,this,[this](const QPoint& position){
        auto* item=tree_->itemAt(position);if(!item)return;
        QMenu menu(this);QAction* open=nullptr;
        if(!item->data(0,Qt::UserRole+1).toBool())open=menu.addAction(tr("Open in new tab"));
        auto* edit=menu.addAction(tr("Edit bookmark"));auto* remove=menu.addAction(tr("Remove"));
        const auto action=menu.exec(tree_->viewport()->mapToGlobal(position));
        if(action==open&&open&&runtime_.new_page){if(auto* page=runtime_.new_page())page->setUrl(QUrl(item->data(0,Qt::UserRole+2).toString()));}
        else if(action==edit)edit_bookmark(item,item->data(0,Qt::UserRole+1).toBool());
        else if(action==remove){
            if(item->childCount()&&QMessageBox::question(this,tr("Remove folder"),tr("Remove this folder and all its bookmarks?"))!=QMessageBox::Yes)return;
            QString error;if(!runtime_.bookmarks.remove(item->data(0,Qt::UserRole).toString(),&error))report(error);
        }
    });
    refresh_tree();
}
QWebEnginePage* Tab::page()const{return view_->page();}
QString Tab::title()const{return view_->title().isEmpty()?tr("Web browser"):view_->title();}
QUrl Tab::url()const{return view_->url();}
void Tab::navigate(const QUrl& value){
    if(value==QUrl("about:blank")||Bookmarks::allowed_url(value))view_->setUrl(value);
    else report(tr("Enter a valid HTTP or HTTPS address."));
}
void Tab::report(const QString& error){QMessageBox::warning(this,tr("Web browser"),error);}
QString Tab::selected_folder()const{
    auto* item=tree_->currentItem();if(!item)return {};
    if(!item->data(0,Qt::UserRole+1).toBool())item=item->parent();
    return item?item->data(0,Qt::UserRole).toString():QString{};
}
void Tab::edit_bookmark(QTreeWidgetItem* item,bool folder,bool currentPage){
    QJsonObject original;
    if(item)for(const auto& v:runtime_.bookmarks.items())if(v.toObject().value("id")==item->data(0,Qt::UserRole).toString())original=v.toObject();
    if(!item&&currentPage)for(const auto& v:runtime_.bookmarks.items())
        if(v.toObject().value("url").toString()==url().toString()){original=v.toObject();break;}
    auto* dialog=new BookmarkDialog(runtime_.bookmarks,original,
        !original.isEmpty()?original.value("parent").toString():selected_folder(),
        !original.isEmpty()?original.value("name").toString():currentPage?title():QString{},
        !original.isEmpty()?original.value("url").toString():currentPage?url().toString():QString{},folder,window());
    if(currentPage) {
        const auto savedUrl=url().toString();const auto icon=view_->icon().pixmap(32,32).toImage();
        connect(dialog,&QDialog::accepted,this,[this,savedUrl,icon]{
            for(const auto& v:runtime_.bookmarks.items())if(v.toObject().value("url").toString()==savedUrl)
                runtime_.bookmarks.save_icon(v.toObject().value("id").toString(),icon);
        });
    }
    dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->show();
}
void Tab::update_pin(){
    bool saved=false;for(const auto& v:runtime_.bookmarks.items())if(v.toObject().value("url").toString()==url().toString())saved=true;
    pin_->setChecked(saved);pin_->setEnabled(Bookmarks::allowed_url(url()));
}
void Tab::refresh_tree(){
    const auto selected=tree_->currentItem()?tree_->currentItem()->data(0,Qt::UserRole).toString():QString{};
    QSet<QString> expanded;for(QTreeWidgetItemIterator it(tree_);*it;++it)if((*it)->isExpanded())expanded.insert((*it)->data(0,Qt::UserRole).toString());
    tree_->clear();QMap<QString,QTreeWidgetItem*> nodes;
    const auto items=runtime_.bookmarks.items();
    for(const auto& v:items){const auto item=v.toObject();auto* node=new QTreeWidgetItem;const auto id=item.value("id").toString();
        node->setText(0,item.value("name").toString());node->setData(0,Qt::UserRole,id);
        node->setData(0,Qt::UserRole+1,item.value("folder").toBool());node->setData(0,Qt::UserRole+2,item.value("url").toString());
        node->setToolTip(0,item.value("url").toString());
        QIcon icon(QDir(runtime_.bookmarks.directory()).filePath("icons/"+id+".png"));
        node->setIcon(0,item.value("folder").toBool()?app::resource_icon("web-folder"):icon.isNull()?app::resource_icon("web"):icon);
        nodes[id]=node;
    }
    for(const auto& v:items){const auto item=v.toObject();auto* node=nodes.value(item.value("id").toString());
        if(auto* parent=nodes.value(item.value("parent").toString()))parent->addChild(node);else tree_->addTopLevelItem(node);
        node->setExpanded(expanded.contains(item.value("id").toString()));
        if(item.value("id")==selected)tree_->setCurrentItem(node);
    }
    update_pin();
}
} // namespace zima::browser
