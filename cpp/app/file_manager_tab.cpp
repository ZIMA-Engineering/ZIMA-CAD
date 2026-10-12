#include "file_manager_tab.hpp"

#include "file_dialog.hpp"
#include "resource_icon.hpp"

#include <zima/ui/properties_subwindow.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QToolBox>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
namespace zima::app {
namespace {

QString clean_path(const QString& path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool path_belongs_to(const QString& path, const QString& root) {
    const QString clean = clean_path(path);
    const QString base = clean_path(root);
#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    return clean.compare(base, sensitivity) == 0 ||
        clean.startsWith(base + QLatin1Char('/'), sensitivity);
}

QString metadata_path(const QString& directory) {
    return QDir(directory).filePath(QStringLiteral("0000-index/metadata.ini"));
}

QString translated_value(QSettings& metadata, const QString& base,
                         const QString& language) {
    const QString selected = metadata.value(base + QLatin1Char('\\') + language).toString().trimmed();
    if (!selected.isEmpty()) return selected;
    for (const auto& fallback : {QStringLiteral("cs"), QStringLiteral("en"),
                                QStringLiteral("de"), QStringLiteral("fr"), QStringLiteral("ru")}) {
        const QString value = metadata.value(base + QLatin1Char('\\') + fallback).toString().trimmed();
        if (!value.isEmpty()) return value;
    }
    return metadata.value(base).toString().trimmed();
}

QString directory_label(const QString& directory, const QString& language) {
    QSettings metadata(metadata_path(directory), QSettings::IniFormat);
    const QString label = translated_value(metadata, QStringLiteral("Directory/Label"), language);
    return label.isEmpty() ? QFileInfo(directory).fileName() : label;
}

QString first_existing_image(const QString& directory, const QString& preferred,
                             const QString& language, const QStringList& bases) {
    const QDir index(QDir(directory).filePath(QStringLiteral("0000-index")));
    if (!preferred.trimmed().isEmpty()) {
        const QString candidate = index.filePath(preferred);
        if (QFileInfo::exists(candidate)) return candidate;
    }
    const QStringList suffixes{QStringLiteral("png"),QStringLiteral("jpg"),
        QStringLiteral("jpeg"),QStringLiteral("webp"),QStringLiteral("gif"),QStringLiteral("svg")};
    for (const auto& base : bases) {
        for (const auto& localized : {base + QLatin1Char('_') + language, base}) {
            for (const auto& suffix : suffixes) {
                const QString candidate=index.filePath(localized+QLatin1Char('.')+suffix);
                if(QFileInfo::exists(candidate))return candidate;
            }
        }
    }
    return {};
}

bool legacy_project(const QString& directory) {
    const QDir index(QDir(directory).filePath(QStringLiteral("0000-index")));
    const bool html = !index.entryList({QStringLiteral("index_*.html"),QStringLiteral("index.html")},QDir::Files).isEmpty();
    const bool image = !first_existing_image(directory,{},QStringLiteral("cs"),{QStringLiteral("01")}).isEmpty();
    return html && image;
}

bool is_project_directory(const QString& directory) {
    QSettings metadata(metadata_path(directory), QSettings::IniFormat);
    return metadata.value(QStringLiteral("Directory/Kind")).toString().compare(
        QStringLiteral("project"),Qt::CaseInsensitive)==0 || legacy_project(directory);
}

QString logical_file_id(const QFileInfo& info) {
    const auto name=info.fileName();
    const QRegularExpression proe(QStringLiteral(R"(^(.+)\.(?:prt|asm|drw)\.\d+$)"),QRegularExpression::CaseInsensitiveOption);
    const auto match=proe.match(name);if(match.hasMatch())return match.captured(1);
    const QString suffix=info.suffix().toLower();
    if(QStringList{"prtz","asmz","drwz"}.contains(suffix))return info.completeBaseName();
    return name;
}

bool is_version_file(const QFileInfo& info) {
    return QRegularExpression(QStringLiteral(R"(\.(?:prt|asm|drw|prtz|asmz|drwz)\.\d+$)"),
        QRegularExpression::CaseInsensitiveOption).match(info.fileName()).hasMatch();
}

bool copy_path(const QString& source, const QString& destination_directory, QString* error) {
    const QFileInfo from(source);
    const QString target=QDir(destination_directory).filePath(from.fileName());
    if(QFileInfo::exists(target)){
        if(error)*error=QObject::tr("Cíl již existuje: %1").arg(target);return false;
    }
    if(from.isFile()){
        if(QFile::copy(source,target))return true;
        if(error)*error=QObject::tr("Soubor nelze zkopírovat: %1").arg(source);return false;
    }
    if(!from.isDir()||!QDir().mkpath(target)){
        if(error)*error=QObject::tr("Adresář nelze zkopírovat: %1").arg(source);return false;
    }
    const QDir input(source);
    const auto entries=input.entryInfoList(QDir::Dirs|QDir::Files|QDir::NoDotAndDotDot|QDir::Hidden|QDir::System);
    for(const auto& entry:entries)if(!copy_path(entry.absoluteFilePath(),target,error)){
        QDir(target).removeRecursively();return false;
    }
    return true;
}

bool atomic_metadata_update(const QString& path, const QMap<QString,QVariant>& values, QString* error) {
    const QFileInfo target(path);if(!QDir().mkpath(target.absolutePath())){if(error)*error=QObject::tr("Adresář metadat nelze vytvořit.");return false;}
    QSettings output(path,QSettings::IniFormat);output.setAtomicSyncRequired(true);
    for(auto it=values.cbegin();it!=values.cend();++it)output.setValue(it.key(),it.value());output.sync();
    if(output.status()!=QSettings::NoError){
        if(error)*error=QObject::tr("Metadata nelze atomicky uložit.");return false;
    }
    return true;
}

struct ProjectValues {QString number,title,customer,description,preview,logo;};

class ProjectEditor final : public zima::ui::PropertiesSubWindow {
public:
    ProjectEditor(ProjectValues values,const QString& directory,const ApplicationSettings& settings,QWidget* parent)
        :PropertiesSubWindow(QObject::tr("Projektový přehled"),parent),values_(std::move(values)),directory_(directory),settings_(settings){
        auto* form=new QFormLayout;number_=new QLineEdit(values_.number,this);title_=new QLineEdit(values_.title,this);
        customer_=new QLineEdit(values_.customer,this);description_=new QPlainTextEdit(values_.description,this);description_->setMaximumHeight(100);
        preview_=new QLineEdit(values_.preview,this);logo_=new QLineEdit(values_.logo,this);
        const auto image_row=[&](QLineEdit* edit){auto* row=new QWidget(this);auto* layout=new QHBoxLayout(row);layout->setContentsMargins(0,0,0,0);layout->addWidget(edit,1);
            auto* browse=new QPushButton(QObject::tr("Procházet…"),row);layout->addWidget(browse);connect(browse,&QPushButton::clicked,this,[this,edit]{
                const QString path=open_file(this,QObject::tr("Vybrat obrázek"),directory_,QObject::tr("Obrázky (*.svg *.png *.jpg *.jpeg *.webp)"),settings_.translations);
                if(!path.isEmpty())edit->setText(path);
            });return row;};
        form->addRow(QObject::tr("Číslo projektu"),number_);form->addRow(QObject::tr("Název projektu"),title_);
        form->addRow(QObject::tr("Zákazník"),customer_);form->addRow(QObject::tr("Popis"),description_);
        form->addRow(QObject::tr("Hlavní obrázek"),image_row(preview_));form->addRow(QObject::tr("Logo projektu"),image_row(logo_));
        content_layout()->addLayout(form);set_initial_size({680,0});
    }
    ProjectValues result()const{return values_;}
protected:
    bool submit()override{
        if(title_->text().trimmed().isEmpty()){QMessageBox::warning(this,QObject::tr("Projektový přehled"),QObject::tr("Zadejte název projektu."));return false;}
        values_={number_->text().trimmed(),title_->text().trimmed(),customer_->text().trimmed(),description_->toPlainText().trimmed(),preview_->text().trimmed(),logo_->text().trimmed()};return true;
    }
private:
    ProjectValues values_;QString directory_;ApplicationSettings settings_;QLineEdit *number_{},*title_{},*customer_{},*preview_{},*logo_{};QPlainTextEdit* description_{};
};

QIcon entry_icon(const QFileInfo& info,const QString& directory,int size) {
    const QDir thumbnails(QDir(directory).filePath(QStringLiteral("0000-index/thumbnails")));
    const QString stem=logical_file_id(info);
    for(const auto& suffix:{QStringLiteral("png"),QStringLiteral("jpg"),QStringLiteral("jpeg"),QStringLiteral("webp")}){
        const QString candidate=thumbnails.filePath(stem+QLatin1Char('.')+suffix);if(QFileInfo::exists(candidate))return QIcon(candidate);
    }
    if(info.isDir()){
        const QString candidate=first_existing_image(info.absoluteFilePath(),{},QStringLiteral("cs"),{QStringLiteral("logo"),QStringLiteral("logo-text"),QStringLiteral("01")});
        if(!candidate.isEmpty())return QIcon(candidate);
    }
    if(info.suffix().compare("png",Qt::CaseInsensitive)==0||info.suffix().compare("jpg",Qt::CaseInsensitive)==0||
       info.suffix().compare("jpeg",Qt::CaseInsensitive)==0||info.suffix().compare("webp",Qt::CaseInsensitive)==0)return QIcon(info.absoluteFilePath());
    Q_UNUSED(size);
    return QFileIconProvider().icon(info);
}

class ParameterDelegate final : public QStyledItemDelegate {
public:
    ParameterDelegate(std::function<void(const QModelIndex&)> committed,QObject* parent)
        :QStyledItemDelegate(parent),committed_(std::move(committed)){setObjectName("fileManagerParameterDelegate");}
    QWidget* createEditor(QWidget* parent,const QStyleOptionViewItem& option,
                          const QModelIndex& index)const override{
        if(index.column()<6)return nullptr;
        return QStyledItemDelegate::createEditor(parent,option,index);
    }
    void setModelData(QWidget* editor,QAbstractItemModel* model,const QModelIndex& index)const override{
        QStyledItemDelegate::setModelData(editor,model,index);if(committed_)committed_(index);
    }
private:
    std::function<void(const QModelIndex&)> committed_;
};

} // namespace

FileManagerTab::FileManagerTab(const ApplicationSettings& settings,const QString& working_directory,
                               const QString& initial_path,QWidget* parent)
    :QWidget(parent),settings_(settings),working_directory_(clean_path(working_directory)){
    setObjectName("fileManagerTab");build_ui();rebuild_sources();
    navigate(initial_path.isEmpty()?working_directory_:initial_path);
}

void FileManagerTab::build_ui(){
    auto* root=new QVBoxLayout(this);root->setContentsMargins(0,0,0,0);root->setSpacing(3);
    auto* navigation=new QHBoxLayout;
    const auto nav_button=[&](const char* name,const QString& tooltip,const QString& icon){auto* button=new QToolButton(this);button->setObjectName(name);button->setIcon(resource_icon(icon));button->setToolTip(tooltip);navigation->addWidget(button);return button;};
    auto* home=nav_button("fileManagerHome",tr("Domů"),"web-home");back_=nav_button("fileManagerBack",tr("Zpět"),"web-back");
    forward_=nav_button("fileManagerForward",tr("Vpřed"),"web-forward");up_=nav_button("fileManagerUp",tr("Nahoru"),"select-parent");
    path_=new QLineEdit(this);path_->setObjectName("fileManagerPath");path_->setClearButtonEnabled(true);navigation->addWidget(path_,1);
    name_filter_=new QLineEdit(this);name_filter_->setObjectName("fileManagerNameFilter");name_filter_->setPlaceholderText(tr("Filtrovat podle názvu…"));name_filter_->setMaximumWidth(280);navigation->addWidget(name_filter_);
    auto* settings=nav_button("fileManagerSettings",tr("Nastavení Správce souborů"),"settings");root->addLayout(navigation);
    connect(home,&QToolButton::clicked,this,[this]{navigate(working_directory_);});connect(back_,&QToolButton::clicked,this,[this]{if(history_index_>0){--history_index_;navigate(history_.at(history_index_),false);}});
    connect(forward_,&QToolButton::clicked,this,[this]{if(history_index_+1<history_.size()){++history_index_;navigate(history_.at(history_index_),false);}});
    connect(up_,&QToolButton::clicked,this,[this]{QDir current(current_directory_);if(current.cdUp())navigate(current.absolutePath());});
    connect(path_,&QLineEdit::returnPressed,this,[this]{navigate(path_->text());});connect(name_filter_,&QLineEdit::textChanged,this,[this]{refresh_overview();refresh_files();});
    connect(settings,&QToolButton::clicked,this,[this]{if(settings_requested)settings_requested();});

    auto* splitter=new QSplitter(Qt::Horizontal,this);splitter->setObjectName("fileManagerSplitter");source_box_=new QToolBox(splitter);source_box_->setObjectName("fileManagerSources");source_box_->setMinimumWidth(220);
    views_=new QTabWidget(splitter);views_->setObjectName("fileManagerViews");
    project_page_=new QWidget(views_);auto* project_layout=new QVBoxLayout(project_page_);auto* project_header=new QHBoxLayout;
    project_title_=new QLabel(project_page_);auto title_font=project_title_->font();title_font.setPointSizeF(title_font.pointSizeF()+5);title_font.setBold(true);project_title_->setFont(title_font);project_header->addWidget(project_title_,1);
    project_logo_=new QLabel(project_page_);project_logo_->setFixedSize(180,52);project_logo_->setAlignment(Qt::AlignRight|Qt::AlignVCenter);project_header->addWidget(project_logo_);project_layout->addLayout(project_header);
    project_details_=new QLabel(project_page_);project_details_->setWordWrap(true);project_layout->addWidget(project_details_);
    project_image_=new QLabel(project_page_);project_image_->setAlignment(Qt::AlignCenter);project_image_->setMinimumSize(320,220);project_image_->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);project_layout->addWidget(project_image_,1);
    edit_project_=new QPushButton(resource_icon("properties"),tr("Upravit projekt"),project_page_);edit_project_->setObjectName("fileManagerEditProject");project_layout->addWidget(edit_project_,0,Qt::AlignRight);
    connect(edit_project_,&QPushButton::clicked,this,[this]{edit_project(current_directory_,false);});views_->addTab(project_page_,tr("Projekt"));

    overview_=new QListWidget(views_);overview_->setObjectName("fileManagerOverview");overview_->setViewMode(QListView::IconMode);overview_->setResizeMode(QListView::Adjust);overview_->setMovement(QListView::Static);overview_->setWordWrap(true);overview_->setSpacing(8);overview_->setUniformItemSizes(true);
    connect(overview_,&QListWidget::itemDoubleClicked,this,[this](QListWidgetItem* item){open_entry(item->data(Qt::UserRole).toString());});views_->addTab(overview_,tr("Přehled"));

    auto* files_page=new QWidget(views_);auto* files_layout=new QVBoxLayout(files_page);files_layout->setContentsMargins(0,0,0,0);files_=new QTreeWidget(files_page);files_->setObjectName("fileManagerFiles");files_->setRootIsDecorated(false);files_->setAlternatingRowColors(true);files_->setSelectionMode(QAbstractItemView::ExtendedSelection);files_->setSortingEnabled(true);
    connect(files_,&QTreeWidget::itemDoubleClicked,this,[this](QTreeWidgetItem* item,int){open_entry(item->data(0,Qt::UserRole).toString());});files_layout->addWidget(files_,1);
    auto* actions=new QHBoxLayout;const auto filter=[&](const QString& text,const char* name){auto* button=new QToolButton(files_page);button->setText(text);button->setCheckable(true);button->setObjectName(name);actions->addWidget(button);connect(button,&QToolButton::toggled,this,[this]{refresh_overview();refresh_files();});return button;};
    filter_asmz_=filter("ASMZ","fileManagerFilterAsmz");filter_prtz_=filter("PRTZ","fileManagerFilterPrtz");filter_drwz_=filter("DRWZ","fileManagerFilterDrwz");filter_versions_=filter(tr("Verze"),"fileManagerFilterVersions");
    actions->addStretch();const auto operation=[&](const QString& text,const char* name){auto* button=new QPushButton(text,files_page);button->setObjectName(name);actions->addWidget(button);return button;};
    copy_=operation(tr("Kopírovat…"),"fileManagerCopy");copy_->setIcon(resource_icon("file-copy"));move_=operation(tr("Přesunout…"),"fileManagerMove");move_->setIcon(resource_icon("file-move"));remove_=operation(tr("Smazat…"),"fileManagerDelete");remove_->setIcon(resource_icon("delete"));files_layout->addLayout(actions);
    connect(copy_,&QPushButton::clicked,this,[this]{copy_selected(false);});connect(move_,&QPushButton::clicked,this,[this]{copy_selected(true);});connect(remove_,&QPushButton::clicked,this,&FileManagerTab::delete_selected);
    files_->setItemDelegate(new ParameterDelegate([this](const QModelIndex& index){save_parameter(index);},files_));
    views_->addTab(files_page,tr("Soubory"));splitter->addWidget(source_box_);splitter->addWidget(views_);splitter->setStretchFactor(0,0);splitter->setStretchFactor(1,1);splitter->setSizes({270,900});root->addWidget(splitter,1);
    connect(source_box_,&QToolBox::currentChanged,this,[this](int index){if(changing_source_||index<0||index>=sources_.size())return;navigate(sources_.at(index).source.resolved_path);});
}

void FileManagerTab::rebuild_sources(){
    while(source_box_->count()){auto* page=source_box_->widget(0);source_box_->removeItem(0);delete page;}sources_.clear();
    FileManagerSource working;working.id="working";working.name=tr("Pracovní adresář");working.path=working_directory_;working.resolved_path=working_directory_;working.enabled=true;working.read_only=false;working.factory=true;
    QVector<FileManagerSource> all{working};for(const auto& source:settings_.file_manager.sources)if(source.enabled)all.push_back(source);
    for(auto source:all){
        auto* tree=new QTreeView(source_box_);tree->setObjectName(QStringLiteral("fileManagerSourceTree_%1").arg(source.id));tree->setHeaderHidden(true);tree->setAnimated(false);tree->setContextMenuPolicy(Qt::CustomContextMenu);
        auto* model=new QFileSystemModel(tree);QDir::Filters model_filters=QDir::Dirs|QDir::NoDotAndDotDot;
        if(settings_.file_manager.show_hidden)model_filters|=QDir::Hidden|QDir::System;
        model->setFilter(model_filters);
        model->setRootPath(source.resolved_path);tree->setModel(model);tree->setRootIndex(model->index(source.resolved_path));
        for(int column=1;column<model->columnCount();++column)tree->hideColumn(column);
        const int index=source_box_->addItem(tree,source.name);sources_.push_back({source,model,tree});
        connect(tree,&QTreeView::clicked,this,[this,model](const QModelIndex& selected){const QFileInfo info=model->fileInfo(selected);if(info.isDir())navigate(info.absoluteFilePath());});
        connect(tree,&QTreeView::customContextMenuRequested,this,[this,tree](const QPoint& point){show_tree_menu(tree,point);});
        if(!QFileInfo(source.resolved_path).isDir()){source_box_->setItemIcon(index,resource_icon("suppress"));source_box_->setItemToolTip(index,tr("Zdroj není dostupný: %1").arg(source.resolved_path));}
        else source_box_->setItemIcon(index,source.id=="working"?resource_icon("web-home"):resource_icon("web-folder"));
    }
}

int FileManagerTab::source_index_for(const QString& path)const{
    int best=-1;qsizetype length=-1;for(int i=0;i<sources_.size();++i)if(path_belongs_to(path,sources_.at(i).source.resolved_path)&&sources_.at(i).source.resolved_path.size()>length){best=i;length=sources_.at(i).source.resolved_path.size();}return best;
}

void FileManagerTab::navigate(const QString& requested,bool track_history){
    const QString target=clean_path(requested);if(!QFileInfo(target).isDir()){if(QFileInfo(target).isFile()){open_entry(target);return;}QMessageBox::warning(this,tr("Správce souborů"),tr("Adresář neexistuje: %1").arg(target));return;}
    const int source=source_index_for(target);if(source<0){QMessageBox::warning(this,tr("Správce souborů"),tr("Adresář nepatří do žádného zapnutého zdroje dat."));return;}
    current_directory_=target;changing_source_=true;source_box_->setCurrentIndex(source);changing_source_=false;
    auto& page=sources_[source];const auto index=page.model->index(target);page.tree->setCurrentIndex(index);page.tree->scrollTo(index);
    for(auto parent=index.parent();parent.isValid()&&parent!=page.tree->rootIndex();parent=parent.parent())page.tree->expand(parent);
    path_->setText(QDir::toNativeSeparators(target));
    if(track_history){while(history_.size()>history_index_+1)history_.removeLast();if(history_.isEmpty()||history_.last()!=target){history_.push_back(target);history_index_=history_.size()-1;}}
    back_->setEnabled(history_index_>0);forward_->setEnabled(history_index_+1<history_.size());up_->setEnabled(target!=clean_path(page.source.resolved_path));
    refresh_directory();
}

void FileManagerTab::refresh_directory(){
    refresh_project();refresh_overview();refresh_files();update_actions();
    const QString label=directory_label(current_directory_,settings_.language);if(title_changed)title_changed(tr("Soubory – %1").arg(label));
}

void FileManagerTab::refresh_project(){
    QSettings metadata(metadata_path(current_directory_),QSettings::IniFormat);const bool project=is_project_directory(current_directory_);views_->setTabVisible(0,project);
    if(!project){if(views_->currentIndex()==0)views_->setCurrentIndex(settings_.file_manager.default_view=="files"?2:1);return;}
    const QString title=translated_value(metadata,"Project/Title",settings_.language);project_title_->setText(title.isEmpty()?directory_label(current_directory_,settings_.language):title);
    const QString number=metadata.value("Project/Number").toString().trimmed(),customer=metadata.value("Project/Customer").toString().trimmed(),description=translated_value(metadata,"Project/Description",settings_.language);
    QStringList details;if(!number.isEmpty())details<<tr("Číslo projektu: %1").arg(number);if(!customer.isEmpty())details<<tr("Zákazník: %1").arg(customer);if(!description.isEmpty())details<<description;project_details_->setText(details.join(QStringLiteral("\n")));
    const QString preview=first_existing_image(current_directory_,metadata.value("Project/Preview").toString(),settings_.language,{"01"});
    project_image_->setPixmap(preview.isEmpty()?QPixmap{}:QPixmap(preview).scaled(project_image_->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation));
    QString logo=first_existing_image(current_directory_,metadata.value("Project/Logo").toString(),settings_.language,{"logo","logo-text","ZIMA-Engineering"});if(logo.isEmpty())logo=settings_.file_manager.company_logo;
    project_logo_->setPixmap(logo.isEmpty()?QPixmap{}:QPixmap(logo).scaled(project_logo_->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation));
    edit_project_->setEnabled(!current_source_read_only());if(history_.size()==1&&settings_.file_manager.default_view=="project")views_->setCurrentIndex(0);
}

static bool filter_entry(const QFileInfo& info,const QString& text,bool versions,bool asmz,bool prtz,bool drwz){
    if(!text.isEmpty()&&!info.fileName().contains(text,Qt::CaseInsensitive))return false;if(!versions&&is_version_file(info))return false;if(info.isDir())return true;
    const bool type_filter=asmz||prtz||drwz;if(!type_filter)return true;const auto suffix=info.suffix().toLower();return (asmz&&suffix=="asmz")||(prtz&&suffix=="prtz")||(drwz&&suffix=="drwz");
}

void FileManagerTab::refresh_overview(){
    overview_->clear();overview_->setIconSize({settings_.file_manager.thumbnail_size,settings_.file_manager.thumbnail_size});overview_->setGridSize({settings_.file_manager.thumbnail_size+48,settings_.file_manager.thumbnail_size+54});
    QDir directory(current_directory_);auto filters=QDir::Dirs|QDir::Files|QDir::NoDotAndDotDot;if(settings_.file_manager.show_hidden)filters|=QDir::Hidden|QDir::System;
    for(const auto& info:directory.entryInfoList(filters,QDir::DirsFirst|QDir::Name|QDir::IgnoreCase)){
        if(info.fileName().compare("0000-index",Qt::CaseInsensitive)==0||!filter_entry(info,name_filter_->text(),filter_versions_->isChecked(),filter_asmz_->isChecked(),filter_prtz_->isChecked(),filter_drwz_->isChecked()))continue;
        const QString label=info.isDir()?directory_label(info.absoluteFilePath(),settings_.language):info.fileName();auto* item=new QListWidgetItem(entry_icon(info,current_directory_,settings_.file_manager.thumbnail_size),label,overview_);item->setData(Qt::UserRole,info.absoluteFilePath());item->setToolTip(info.absoluteFilePath());
    }
}

void FileManagerTab::refresh_files(){
    refreshing_files_=true;files_->clear();QSettings metadata(metadata_path(current_directory_),QSettings::IniFormat);const int version=metadata.value("Directory/Version",2).toInt();
    QStringList parameters=metadata.value("Directory/Parameters").toString().split(',',Qt::SkipEmptyParts);for(auto& value:parameters)value=value.trimmed();parameters.removeDuplicates();
    QDir directory(current_directory_);auto filters=QDir::Dirs|QDir::Files|QDir::NoDotAndDotDot;if(settings_.file_manager.show_hidden)filters|=QDir::Hidden|QDir::System;
    const auto entries=directory.entryInfoList(filters,QDir::DirsFirst|QDir::Name|QDir::IgnoreCase);
    QMap<QString,FileManagerNativeParameters> native_parameters;
    if(native_parameters_requested)for(const auto& info:entries){const QString suffix=info.suffix().toLower();if(!info.isFile()||(suffix!="prtz"&&suffix!="asmz"))continue;const QString stamp=info.lastModified().toUTC().toString(Qt::ISODateWithMs)+QLatin1Char(':')+QString::number(info.size());auto cached=native_parameter_cache_.constFind(info.absoluteFilePath());FileManagerNativeParameters data;if(cached!=native_parameter_cache_.cend()&&cached->first==stamp)data=cached->second;else{data=native_parameters_requested(info.absoluteFilePath());if(data.error.isEmpty())native_parameter_cache_.insert(info.absoluteFilePath(),{stamp,data});}if(!data.error.isEmpty())continue;for(const auto& parameter:data.order)if(!parameters.contains(parameter))parameters.push_back(parameter);for(auto it=data.values.cbegin();it!=data.values.cend();++it)if(!parameters.contains(it.key()))parameters.push_back(it.key());native_parameters.insert(info.absoluteFilePath(),std::move(data));}
    QStringList headers{tr("Vybrat"),tr("Náhled"),tr("Název"),tr("Typ"),tr("Velikost"),tr("Změněno")};
    for(const auto& parameter:parameters){QString label=translated_value(metadata,QStringLiteral("Parameters/%1/Label").arg(parameter),settings_.language);if(label.isEmpty())for(auto it=native_parameters.cbegin();it!=native_parameters.cend();++it){const auto labels=it->labels.value(parameter);label=labels.value(settings_.language,labels.value(QString{}));if(!label.isEmpty())break;}headers<<(label.isEmpty()?parameter:label);}files_->setColumnCount(headers.size());files_->setHeaderLabels(headers);
    for(int p=0;p<parameters.size();++p)files_->headerItem()->setData(6+p,Qt::UserRole,parameters[p]);
    files_->header()->setSectionResizeMode(0,QHeaderView::ResizeToContents);files_->header()->setSectionResizeMode(1,QHeaderView::ResizeToContents);files_->header()->setSectionResizeMode(2,QHeaderView::Stretch);
    QMap<QString,QVariant> mirror_updates;if(version>=3&&!current_source_read_only())mirror_updates.insert("Directory/Parameters",parameters.join(','));
    for(const auto& info:entries){
        if(info.fileName().compare("0000-index",Qt::CaseInsensitive)==0||!filter_entry(info,name_filter_->text(),filter_versions_->isChecked(),filter_asmz_->isChecked(),filter_prtz_->isChecked(),filter_drwz_->isChecked()))continue;
        auto* item=new QTreeWidgetItem(files_);item->setData(0,Qt::UserRole,info.absoluteFilePath());item->setData(0,Qt::UserRole+1,logical_file_id(info));item->setCheckState(0,Qt::Unchecked);item->setIcon(1,entry_icon(info,current_directory_,32));item->setText(2,info.fileName());item->setText(3,info.isDir()?tr("Adresář"):info.suffix().toUpper());item->setText(4,info.isDir()?QString{}:QLocale().formattedDataSize(info.size()));item->setText(5,QLocale().toString(info.lastModified(),QLocale::ShortFormat));
        const QString id=logical_file_id(info);const auto native=native_parameters.constFind(info.absoluteFilePath());item->setData(0,Qt::UserRole+2,native==native_parameters.cend()?QStringLiteral("metadata"):QStringLiteral("zima-cad"));
        metadata.beginGroup("Parts");for(int p=0;p<parameters.size();++p){const QString key=id+QLatin1Char('\\')+parameters[p];QString value=native==native_parameters.cend()?QString{}:native->values.value(parameters[p]);if(value.isEmpty())value=metadata.value(key).toString();if(value.isEmpty())value=metadata.value(key+QLatin1Char('\\')+settings_.language).toString();if(value.isEmpty())for(const auto& lang:{"cs","en","de","fr","ru"}){value=metadata.value(key+QLatin1Char('\\')+lang).toString();if(!value.isEmpty())break;}item->setText(6+p,value);if(!current_source_read_only()&&(version>=3||native!=native_parameters.cend()))item->setFlags(item->flags()|Qt::ItemIsEditable);if(native!=native_parameters.cend())mirror_updates.insert(QStringLiteral("Parts/%1\\%2").arg(id,parameters[p]),value);}metadata.endGroup();
        if(native!=native_parameters.cend()){mirror_updates.insert(QStringLiteral("Files/%1/Provider").arg(id),"zima-cad");mirror_updates.insert(QStringLiteral("Files/%1/Path").arg(id),info.fileName());mirror_updates.insert(QStringLiteral("Files/%1/ModifiedUtc").arg(id),info.lastModified().toUTC().toString(Qt::ISODateWithMs));}
    }
    if(!mirror_updates.isEmpty()){QString error;if(!atomic_metadata_update(metadata_path(current_directory_),mirror_updates,&error)&&isVisible())QMessageBox::warning(this,tr("Správce souborů"),error);}
    files_->resizeColumnToContents(0);files_->setColumnWidth(1,48);refreshing_files_=false;
}

void FileManagerTab::update_actions(){const bool writable=!current_source_read_only();move_->setEnabled(writable);remove_->setEnabled(writable);copy_->setEnabled(true);}
void FileManagerTab::save_parameter(const QModelIndex& index){
    if(refreshing_files_||index.column()<6||current_source_read_only())return;auto* item=files_->itemFromIndex(index);auto* header=files_->headerItem();if(!item||!header)return;
    const QString parameter=header->data(index.column(),Qt::UserRole).toString(),id=item->data(0,Qt::UserRole+1).toString();if(parameter.isEmpty()||id.isEmpty())return;
    if(item->data(0,Qt::UserRole+2).toString()=="zima-cad"){
        if(!native_parameter_edit_requested){if(isVisible())QMessageBox::warning(this,tr("Správce souborů"),tr("Nativní parametry nyní nelze upravit."));refresh_files();return;}
        const QString native_error=native_parameter_edit_requested(item->data(0,Qt::UserRole).toString(),parameter,item->text(index.column()));
        if(!native_error.isEmpty()){if(isVisible())QMessageBox::critical(this,tr("Uložení selhalo"),native_error);refresh_files();return;}
        native_parameter_cache_.remove(item->data(0,Qt::UserRole).toString());
    }
    QSettings existing(metadata_path(current_directory_),QSettings::IniFormat);
    QMap<QString,QVariant> update{{QStringLiteral("Parts/%1\\%2").arg(id,parameter),item->text(index.column())}};
    if(existing.value("Directory/Version",2).toInt()<3){
        QStringList parameters;for(int column=6;column<files_->columnCount();++column){const QString key=header->data(column,Qt::UserRole).toString();if(!key.isEmpty())parameters.push_back(key);}
        update.insert("Directory/Version",3);update.insert("Directory/Kind",existing.value("Directory/Kind","directory"));
        update.insert("Directory/Id",existing.value("Directory/Id",QUuid::createUuid().toString(QUuid::WithoutBraces)));
        update.insert("Directory/Parameters",parameters.join(','));
    }
    if(item->data(0,Qt::UserRole+2).toString()=="zima-cad"){
        const QString file_path=item->data(0,Qt::UserRole).toString();
        update.insert(QStringLiteral("Files/%1/Provider").arg(id),"zima-cad");
        update.insert(QStringLiteral("Files/%1/Path").arg(id),QFileInfo(file_path).fileName());
        update.insert(QStringLiteral("Files/%1/ModifiedUtc").arg(id),QFileInfo(file_path).lastModified().toUTC().toString(Qt::ISODateWithMs));
    }
    QString error;
    if(atomic_metadata_update(metadata_path(current_directory_),update,&error))return;
    if(isVisible())QMessageBox::critical(this,tr("Uložení selhalo"),error);else qWarning().noquote()<<error;refresh_files();
}
bool FileManagerTab::current_source_read_only()const{const int index=source_index_for(current_directory_);return index<0||sources_.at(index).source.read_only;}
QString FileManagerTab::current_source_label()const{const int index=source_index_for(current_directory_);return index<0?QString{}:sources_.at(index).source.name;}

void FileManagerTab::open_entry(const QString& path){const QFileInfo info(path);if(info.isDir()){navigate(path);return;}const QString suffix=info.suffix().toLower();if(QStringList{"prtz","asmz","drwz","frmz","tblz","symz"}.contains(suffix)&&open_file_requested){open_file_requested(path);return;}QDesktopServices::openUrl(QUrl::fromLocalFile(path));}

void FileManagerTab::show_tree_menu(QTreeView* tree,const QPoint& point){const QModelIndex index=tree->indexAt(point);if(!index.isValid())return;auto* model=qobject_cast<QFileSystemModel*>(tree->model());if(!model)return;const QFileInfo info=model->fileInfo(index);if(!info.isDir())return;const int source_index=source_index_for(info.absoluteFilePath());if(source_index<0)return;QMenu menu(this);auto* open=menu.addAction(resource_icon("web-folder"),tr("Otevřít"));auto* tab=menu.addAction(resource_icon("new"),tr("Otevřít v novém tabu"));menu.addSeparator();auto* project=menu.addAction(resource_icon("properties"),is_project_directory(info.absoluteFilePath())?tr("Upravit projektový přehled"):tr("Vytvořit projektový přehled"));project->setEnabled(!sources_.at(source_index).source.read_only);const auto selected=menu.exec(tree->viewport()->mapToGlobal(point));if(selected==open)navigate(info.absoluteFilePath());else if(selected==tab&&new_tab_requested)new_tab_requested(info.absoluteFilePath());else if(selected==project)edit_project(info.absoluteFilePath(),!is_project_directory(info.absoluteFilePath()));}

void FileManagerTab::edit_project(const QString& directory,bool create){
    QSettings metadata(metadata_path(directory),QSettings::IniFormat);const int version=metadata.value("Directory/Version",create?3:2).toInt();if(version<3&&!create){QMessageBox::information(this,tr("Projektový přehled"),tr("Tento adresář používá starší metadata. Úpravy budou dostupné po řízeném převodu na verzi 3."));return;}
    ProjectValues values{metadata.value("Project/Number").toString(),translated_value(metadata,"Project/Title",settings_.language),metadata.value("Project/Customer").toString(),translated_value(metadata,"Project/Description",settings_.language),metadata.value("Project/Preview").toString(),metadata.value("Project/Logo").toString()};if(values.title.isEmpty())values.title=directory_label(directory,settings_.language);
    auto* dialog=new ProjectEditor(values,directory,settings_,window());dialog->setAttribute(Qt::WA_DeleteOnClose);connect(dialog,&QDialog::accepted,this,[this,dialog,directory]{auto values=dialog->result();QMap<QString,QVariant> updates{{"Directory/Version",3},{"Directory/Kind","project"},{"Directory/Label\\"+settings_.language,values.title},{"Project/Number",values.number},{"Project/Title\\"+settings_.language,values.title},{"Project/Customer",values.customer},{"Project/Description\\"+settings_.language,values.description}};
        if(!QFileInfo(metadata_path(directory)).exists())updates.insert("Directory/Id",QUuid::createUuid().toString(QUuid::WithoutBraces));
        const auto import_image=[&](const QString& selected,const QString& base)->QString{if(selected.isEmpty())return {};const QFileInfo source(selected);if(!source.exists())return selected;const QString index=QDir(directory).filePath("0000-index");QDir().mkpath(index);const QString target=QDir(index).filePath(base+QLatin1Char('.')+source.suffix().toLower());if(clean_path(selected)!=clean_path(target)){QFile input(selected);if(input.open(QIODevice::ReadOnly)){QSaveFile output(target);if(output.open(QIODevice::WriteOnly)){output.write(input.readAll());output.commit();}}}return QFileInfo(target).fileName();};
        updates.insert("Project/Preview",import_image(values.preview,"01"));updates.insert("Project/Logo",import_image(values.logo,"logo"));QString error;if(!atomic_metadata_update(metadata_path(directory),updates,&error)){QMessageBox::critical(this,tr("Uložení selhalo"),error);return;}if(current_directory_==clean_path(directory))refresh_directory();});dialog->show();
}

QStringList FileManagerTab::selected_paths()const{QStringList paths;for(int i=0;i<files_->topLevelItemCount();++i){auto* item=files_->topLevelItem(i);if(item->checkState(0)==Qt::Checked)paths<<item->data(0,Qt::UserRole).toString();}return paths;}

void FileManagerTab::copy_selected(bool move){const auto paths=selected_paths();if(paths.isEmpty()){QMessageBox::information(this,tr("Správce souborů"),tr("Nejprve zaškrtněte soubory nebo adresáře."));return;}const QString destination=choose_directory(this,move?tr("Přesunout do"):tr("Kopírovat do"),working_directory_,settings_.translations);if(destination.isEmpty())return;for(const auto& path:paths){QString error;if(!copy_path(path,destination,&error)){QMessageBox::critical(this,tr("Operace se nezdařila"),error);return;}if(move){const QFileInfo info(path);bool removed=info.isDir()?QDir(path).removeRecursively():QFile::remove(path);if(!removed){QMessageBox::critical(this,tr("Operace se nezdařila"),tr("Kopie vznikla, ale původní položku nelze odstranit: %1").arg(path));return;}}}refresh_directory();}

void FileManagerTab::delete_selected(){const auto paths=selected_paths();if(paths.isEmpty()){QMessageBox::information(this,tr("Správce souborů"),tr("Nejprve zaškrtněte soubory nebo adresáře."));return;}if(QMessageBox::warning(this,tr("Smazat vybrané položky"),tr("Opravdu chcete trvale smazat %1 vybraných položek?").arg(paths.size()),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;for(const auto& path:paths){const QFileInfo info(path);const bool removed=info.isDir()?QDir(path).removeRecursively():QFile::remove(path);if(!removed){QMessageBox::critical(this,tr("Odstranění selhalo"),tr("Položku nelze odstranit: %1").arg(path));break;}}refresh_directory();}

void FileManagerTab::set_working_directory(const QString& path){working_directory_=clean_path(path);if(!sources_.isEmpty()){sources_[0].source.path=working_directory_;sources_[0].source.resolved_path=working_directory_;sources_[0].model->setRootPath(working_directory_);sources_[0].tree->setRootIndex(sources_[0].model->index(working_directory_));}const QString target=source_index_for(current_directory_)<0?working_directory_:current_directory_;history_.clear();history_index_=-1;navigate(target);}

void FileManagerTab::reload_settings(const ApplicationSettings& settings){const QString current=current_directory_;settings_=settings;rebuild_sources();history_.clear();history_index_=-1;navigate(source_index_for(current)>=0?current:working_directory_);}

} // namespace zima::app
