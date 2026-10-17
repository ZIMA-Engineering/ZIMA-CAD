#include "file_manager_tab.hpp"
#include "file_entry_geometry.hpp"
#include "thumbnail_image.hpp"
#include "file_manager_includes.hpp"
#include "native_file_icon.hpp"
#include "directory_metadata.hpp"
#include "branding_image.hpp"

#include <QAction>
#include <QPersistentModelIndex>
#include <QHeaderView>
#include <QSlider>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QMouseEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QMessageBox>
#include <QStyleOptionViewItem>
#include <QApplication>
#include <QCheckBox>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QPlainTextEdit>
#include <QListWidget>
#include <QLineEdit>
#include <QLabel>
#include <QImage>
#include <QPainter>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QStackedWidget>
#include <QTreeView>
#include <QTreeWidget>
#include <QWebEngineView>
#include <QWebEnginePage>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <filesystem>

namespace {
class ItemGeometryProbe final : public QStyledItemDelegate {
public:
    zima::app::FileEntryGeometry geometry(QAbstractItemView* view,const QModelIndex& index)const {
        QStyleOptionViewItem option;option.initFrom(view);option.rect=view->visualRect(index);option.decorationSize=view->iconSize();initStyleOption(&option,index);
        return zima::app::file_entry_geometry(option,index,view);
    }
};
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void touch(const QString& path){QFile file(path);check(file.open(QIODevice::WriteOnly),"Cannot create synthetic file");file.write("synthetic");}
void verify_symbol_thumbnails(QApplication& application){
    const auto root=QString::fromStdString((std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/"config/symbols").generic_string());
    zima::app::ApplicationSettings settings;settings.file_manager.default_view="files";zima::app::FileManagerTab manager(settings,root);int checked=0;
    QDirIterator symbols(root,{"*.symz"},QDir::Files,QDirIterator::Subdirectories);while(symbols.hasNext()){const auto file=symbols.next();manager.navigate(QFileInfo(file).absolutePath());application.processEvents();auto* table=manager.findChild<QTreeWidget*>("fileManagerFiles");bool found=false;for(int row=0;row<table->topLevelItemCount();++row){auto* item=table->topLevelItem(row);if(item->data(0,Qt::UserRole).toString()!=file)continue;const auto preview=item->data(0,Qt::UserRole+4).toString();check(preview==zima::app::document_thumbnail_path(file)&&QImage(preview).size()==QSize(640,480)&&!item->icon(zima::app::file_preview_column).isNull(),"Factory symbol has no discoverable native thumbnail");found=true;}check(found,"Factory symbol is missing from File Manager");++checked;}check(checked==10,"Current factory symbol thumbnail coverage changed");
}
void verify_file_controls(QApplication& application){
    QTemporaryDir fixture;check(fixture.isValid(),"Cannot create file controls fixture");const QString root=fixture.path(),child=fixture.filePath("child");QDir().mkpath(child+"/0000-index");QDir().mkpath(root+"/0000-index");
    {QSettings legacy(root+"/0000-index/files.ini",QSettings::IniFormat);legacy.setValue("files/groups","zima-cad");legacy.setValue("group/zima-cad/patterns","*.prtz");legacy.setValue("group/zima-cad/disabledPatterns","*.prtz");legacy.setValue("custom/keep","authored");legacy.sync();QSettings parent(root+"/0000-index/filters.ini",QSettings::IniFormat);parent.setValue("Filters/Hide",QStringList{"*.matz"});parent.sync();QSettings local(child+"/0000-index/filters.ini",QSettings::IniFormat);local.setValue("Filters/Hide",QStringList{"*.asmz","*.txt"});local.setValue("custom/keep","local");local.sync();}
    for(const auto* name:{"Part.prtz","Assembly.asmz","Material.matz","Note.txt"})touch(QDir(child).filePath(name));
    zima::app::ApplicationSettings settings;settings.file_manager.default_view="files";settings.local_config_path=root+"/local.ini";
    zima::app::FileManagerTab manager(settings,root,child);manager.resize(1100,700);manager.show();application.processEvents();auto* table=manager.findChild<QTreeWidget*>("fileManagerFiles");
    int native_reads=0;manager.native_parameters_requested=[&](const QString&){++native_reads;return zima::app::FileManagerNativeParameters{};};manager.navigate(child);const int reads_before_filter=native_reads;
    const auto visible=[&](const QString& name){for(int row=0;row<table->topLevelItemCount();++row)if(table->topLevelItem(row)->text(0)==name)return !table->topLevelItem(row)->isHidden();return false;};
    auto* assembly=manager.findChild<QToolButton*>("fileManagerType_asmz");auto* material=manager.findChild<QToolButton*>("fileManagerType_matz");check(assembly&&!assembly->isChecked()&&!visible("Assembly.asmz")&&!visible("Note.txt")&&visible("Part.prtz")&&visible("Material.matz"),"Local exclusion patterns or ignored legacy policy were applied incorrectly");
    for(const auto* extension:{"prtz","asmz","drwz","matz","frmz","tblz","symz"}){auto* button=manager.findChild<QToolButton*>(QStringLiteral("fileManagerType_%1").arg(extension));check(button&&button->text()==extension&&!button->icon().isNull()&&button->toolButtonStyle()==Qt::ToolButtonTextBesideIcon,"Type toggle lost lowercase extension or its native icon");}
    auto* versions=manager.findChild<QToolButton*>("fileManagerFilterVersions");check(versions&&!versions->icon().isNull(),"Versions toggle has no icon");
    // Native icons may contain a few azure pixels; test the filled background.
    const auto check_azure=[&](QToolButton* button){QCursor::setPos(manager.mapToGlobal(QPoint(manager.width()+20,manager.height()+20)));application.processEvents();const auto capture=button->grab().toImage();int azure=0;for(int y=1;y<capture.height()-1;++y)for(int x=1;x<capture.width()-1;++x)if(capture.pixelColor(x,y)==QColor("#00D1FF"))++azure;check(button->isChecked()==(azure>20),"File filter checked state is not visibly azure");};
    check_azure(assembly);check_azure(material);
    assembly->click();material->click();check(visible("Assembly.asmz")&&!visible("Material.matz")&&visible("Part.prtz"),"Independent file type toggles failed");check(native_reads==reads_before_filter,"File type toggles reloaded native documents");
    check_azure(assembly);check_azure(material);versions->click();check_azure(versions);versions->click();check_azure(versions);for(int row=0;row<table->topLevelItemCount();++row)if(!table->topLevelItem(row)->data(0,Qt::UserRole+3).toBool())check(table->topLevelItem(row)->text(2)==QFileInfo(table->topLevelItem(row)->text(0)).suffix().toLower(),"Type column does not match lowercase filter labels");
    manager.navigate(root);manager.navigate(child);check(assembly->isChecked()&&!material->isChecked(),"File type choices did not survive navigation");QSettings policy(child+"/0000-index/filters.ini",QSettings::IniFormat);check(policy.value("Filters/Hide").toStringList().contains("*.matz")&&policy.value("custom/keep")=="local","Native file filter was not saved to filters.ini or dropped unknown settings");QSettings inherited(root+"/0000-index/files.ini",QSettings::IniFormat);check(inherited.value("custom/keep")=="authored"&&inherited.value("group/zima-cad/disabledPatterns")=="*.prtz","Child filters modified their parent policy");
    QTimer::singleShot(0,[&]{for(auto* widget:QApplication::topLevelWidgets())if(auto* menu=qobject_cast<QMenu*>(widget);menu&&menu->isVisible()){menu->actions().front()->trigger();menu->close();}});table->header()->customContextMenuRequested(QPoint(20,10));check(table->isColumnHidden(2),"Type column cannot be hidden");manager.navigate(child);check(table->isColumnHidden(2),"Type column visibility did not survive refresh");
    auto* filters=manager.findChild<QToolButton*>("fileManagerFilters");check(filters&&!filters->icon().isNull(),"File Filters button or icon is missing");filters->click();auto* editor=manager.findChild<QDialog*>("fileManagerFiltersDialog");check(editor,"File filters editor did not open");editor->findChild<QPlainTextEdit*>("fileManagerHiddenPatterns")->setPlainText("*.prtz");editor->reject();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);check(visible("Part.prtz"),"Filter Cancel changed visibility");
    filters->click();editor=manager.findChild<QDialog*>("fileManagerFiltersDialog");editor->findChild<QCheckBox*>("fileManagerInheritGlobalFilters")->setChecked(false);editor->findChild<QPlainTextEdit*>("fileManagerHiddenPatterns")->setPlainText("Note.txt");editor->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();check(visible("0000-index")&&visible("Material.matz")&&!visible("Note.txt"),"Local filters could not override the global metadata-folder exclusion");
    auto* directory_tree=manager.findChild<QTreeView*>("fileManagerSourceTree_working");auto* directory_proxy=qobject_cast<QSortFilterProxyModel*>(directory_tree->model());auto* directory_model=qobject_cast<QFileSystemModel*>(directory_proxy->sourceModel());check(directory_proxy->mapFromSource(directory_model->index(child+"/0000-index")).isValid(),"Metadata folder remains hard-coded hidden in the directory tree");
    settings.file_manager.hidden_patterns={"0000-index","*.matz"};manager.reload_settings(settings);check(visible("Material.matz"),"Local opt-out did not preserve visibility after changing global filters");filters->click();editor=manager.findChild<QDialog*>("fileManagerFiltersDialog");editor->findChild<QCheckBox*>("fileManagerInheritGlobalFilters")->setChecked(true);editor->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);check(!visible("0000-index")&&!visible("Material.matz"),"Global filters were not reapplied after enabling inheritance");
    auto* create=manager.findChild<QPushButton*>("fileManagerNewDirectory");check(create&&create->isEnabled(),"Writable folder creation is disabled");
    {QSettings metadata(child+"/0000-index/metadata.ini",QSettings::IniFormat);metadata.setValue("Directory/PreventParameterEdit",true);metadata.sync();}manager.navigate(child);check(create->isEnabled(),"Parameter protection incorrectly blocks folder creation");create->click();auto* naming=manager.findChild<QDialog*>("pathNameDialog");check(naming,"Folder naming dialog did not open");auto* name=naming->findChild<QLineEdit*>("pathName");name->setText("..");naming->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();check(naming->isVisible(),"Invalid folder name was accepted");name->setText("Created");naming->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();check(QDir(child+"/Created").exists(),"New folder was not created");QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    {QSettings metadata(child+"/0000-index/metadata.ini",QSettings::IniFormat);metadata.setValue("Directory/PreventRemoval",true);metadata.sync();}manager.navigate(child);check(!create->isEnabled(),"Content protection did not block folder creation");
    {QSettings metadata(child+"/0000-index/metadata.ini",QSettings::IniFormat);metadata.setValue("Directory/PreventRemoval",false);metadata.sync();}
    settings.file_manager.sources.push_back({"protected","Protected",child,child,true,true,false});manager.reload_settings(settings);check(!create->isEnabled(),"Read-only source permits folder creation");
}
template<class Predicate> bool wait_until(Predicate predicate,int timeout_ms=3000){QElapsedTimer timer;timer.start();while(timer.elapsed()<timeout_ms){QApplication::processEvents();if(predicate())return true;QThread::msleep(10);}return predicate();}
void verify_directory_includes(QApplication& application){
    QTemporaryDir fixture;check(fixture.isValid(),"Cannot create include fixture");
    const auto root=fixture.path(),exports=fixture.filePath("exports"),source=fixture.filePath("native"),images=fixture.filePath("images");
    for(const auto& path:{exports,source,images})check(QDir().mkpath(path+"/0000-index/thumbnails"),"Cannot create include directories");
    touch(exports+"/PART.step");touch(exports+"/PART.dxf");touch(exports+"/other.step");
    {QSettings metadata(source+"/0000-index/metadata.ini",QSettings::IniFormat);metadata.setValue("Directory/Version",2);metadata.setValue("Directory/Parameters","material");for(const auto* language:{"cs","en","de","fr","ru"}){metadata.setValue(QString("Parameters/material/Label/%1").arg(language),QString("Material-%1").arg(language));metadata.setValue(QString("Parts/PART/material/%1").arg(language),QString("S355-%1").arg(language));}metadata.sync();}
    {QSettings metadata(exports+"/0000-index/metadata.ini",QSettings::IniFormat);metadata.setValue("Directory/Version",2);metadata.setValue("Directory/Parameters",QVariant{});metadata.setValue("Directory/Label/cs","Exports");metadata.setValue("Directory/IncludeParameters","../native");metadata.setValue("Directory/IncludeThumbnails","../images");metadata.sync();}
    QImage image(20,20,QImage::Format_RGB32);image.fill(Qt::red);check(image.save(images+"/0000-index/thumbnails/PART.png"),"Cannot write included thumbnail");
    const auto read_bytes=[](const QString& path){QFile file(path);check(file.open(QIODevice::ReadOnly),"Cannot read include metadata");return file.readAll();};
    const auto source_bytes=read_bytes(source+"/0000-index/metadata.ini");
    zima::app::ApplicationSettings settings;settings.language="cs";settings.file_manager.default_view="files";settings.file_manager.sources.push_back({"included","Included",source,source,true,true,false});
    zima::app::FileManagerTab tab(settings,root,exports);tab.resize(1200,800);tab.show();application.processEvents();
    auto* table=tab.findChild<QTreeWidget*>("fileManagerFiles");
    const auto item_for=[&](const QString& name){for(int row=0;row<table->topLevelItemCount();++row)if(table->topLevelItem(row)->text(0)==name)return table->topLevelItem(row);throw std::runtime_error("Included export row is absent");};
    for(const auto* language:{"cs","en","de","fr","ru"}){
        settings.language=language;tab.reload_settings(settings);tab.navigate(exports);check(table->columnCount()==6&&table->headerItem()->text(5)==QString("Material-%1").arg(language),"Included parameter label did not follow language");
        for(const auto* name:{"PART.step","PART.dxf"}){auto* item=item_for(name);check(item->text(5)==QString("S355-%1").arg(language)&&item->data(0,Qt::UserRole+4).toString()==images+"/0000-index/thumbnails/PART.png","Relative includes did not match the export stem independently");}
        check(item_for("other.step")->text(5).isEmpty(),"Includes leaked a different file's parameters");
    }
    settings.language="cs";tab.reload_settings(settings);tab.navigate(exports);
    QStyleOptionViewItem option;const auto index=table->indexFromItem(item_for("PART.step"),5);auto* editor=table->itemDelegate()->createEditor(table,option,index);auto* line=qobject_cast<QLineEdit*>(editor);check(line,"Local include override is not editable");line->setText("Local override");table->itemDelegate()->setModelData(editor,table->model(),index);delete editor;tab.navigate(exports);check(item_for("PART.step")->text(5)=="Local override"&&item_for("PART.dxf")->text(5)=="S355-cs","Editing an include changed the source or another export");
    {QSettings local(exports+"/0000-index/metadata.ini",QSettings::IniFormat);local.setValue("Parts/PART.step/material/cs","");local.sync();}tab.navigate(exports);check(item_for("PART.step")->text(5).isEmpty(),"An explicit empty local override was ignored");
    image.fill(Qt::blue);check(image.save(exports+"/PART.png"),"Cannot write local thumbnail");tab.navigate(exports);check(item_for("PART.dxf")->data(0,Qt::UserRole+4).toString()==exports+"/PART.png","Local thumbnail did not override an include");QFile::remove(exports+"/PART.png");
    touch(source+"/PART.prtz");QString revision="1",native_value="Live source";int reads=0;
    tab.native_parameters_revision_requested=[&](const QString&){return revision;};tab.native_parameters_requested=[&](const QString&){++reads;zima::app::FileManagerNativeParameters result;result.order={"material"};result.values.insert("material",native_value);return result;};
    tab.navigate(exports);check(item_for("PART.dxf")->text(5)==native_value,"Native source did not override its stale mirror");const auto previous_reads=reads;tab.navigate(exports);check(reads==previous_reads,"Unchanged included native data was loaded again");revision="2";native_value="Unsaved source";tab.navigate(exports);check(item_for("PART.dxf")->text(5)==native_value&&reads>previous_reads,"Included open document revision did not invalidate metadata");
    check(read_bytes(source+"/0000-index/metadata.ini")==source_bytes,"Browsing or overriding an include changed source metadata");
    const auto show_properties=[&]()->QDialog*{
        auto* sources=tab.findChild<QListWidget*>("fileManagerSources");
        QTimer::singleShot(0,[&]{auto* menu=tab.findChild<QMenu*>("fileManagerSourceMenu");check(menu,"Include source menu missing");menu->findChild<QAction*>("fileManagerSourceProperties")->trigger();menu->close();});
        sources->customContextMenuRequested(sources->visualItemRect(sources->item(0)).center());
        auto* dialog=tab.findChild<QDialog*>("fileManagerDirectoryEditor");check(dialog,"Include directory editor missing");return dialog;
    };
    // Edit the working root through the existing source Properties action.
    const auto root_metadata=root+"/0000-index/metadata.ini";QDir().mkpath(root+"/0000-index");{QSettings local(root_metadata,QSettings::IniFormat);local.setValue("Directory/Version",3);local.sync();}
    auto* dialog=show_properties();auto* parameters=dialog->findChild<QWidget*>("fileManagerIncludeParameters");auto* previews=dialog->findChild<QWidget*>("fileManagerIncludeThumbnails");check(parameters&&previews&&(dialog->windowFlags()&Qt::SubWindow),"Includes do not use the shared properties window");
    parameters->findChild<QPlainTextEdit*>()->setPlainText("native");previews->findChild<QPlainTextEdit*>()->setPlainText("images");application.processEvents();check(parameters->findChild<QLabel*>("includeResolved")->text()==QDir::toNativeSeparators(source),"Resolved include path is not visible");
    const auto field_position=parameters->mapTo(dialog,QPoint{}).y();dialog->resize(dialog->width()+100,dialog->height()+120);application.processEvents();check(parameters->mapTo(dialog,QPoint{}).y()==field_position,"Resizing spread the include fields vertically");
    dialog->reject();application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);{QSettings local(root_metadata,QSettings::IniFormat);check(!local.contains("Directory/IncludeParameters"),"Cancel saved pending includes");}
    dialog=show_properties();parameters=dialog->findChild<QWidget*>("fileManagerIncludeParameters");
    QTimer chooser;chooser.setInterval(10);QObject::connect(&chooser,&QTimer::timeout,&tab,[&]{if(auto* browser=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())){chooser.stop();browser->setDirectory(source);browser->selectFile(source);QMetaObject::invokeMethod(browser,"accept",Qt::DirectConnection);}});chooser.start();parameters->findChild<QPushButton*>("includeBrowse")->click();chooser.stop();
    check(parameters->findChild<QPlainTextEdit*>()->toPlainText()=="native","Include directory chooser did not store a relative path");dialog->findChild<QWidget*>("fileManagerIncludeThumbnails")->findChild<QPlainTextEdit*>()->setPlainText("images");dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    {QSettings local(root_metadata,QSettings::IniFormat);check(local.value("Directory/IncludeParameters").toStringList()==QStringList{"native"}&&local.value("Directory/IncludeThumbnails").toStringList()==QStringList{"images"},"OK did not persist independent includes");}
    dialog=show_properties();parameters=dialog->findChild<QWidget*>("fileManagerIncludeParameters");check(parameters->findChild<QPlainTextEdit*>()->toPlainText()=="native","Reopening lost the include");parameters->findChild<QToolButton*>("includeClear")->click();dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    {QSettings local(root_metadata,QSettings::IniFormat);check(local.value("Directory/IncludeParameters").toStringList().isEmpty()&&local.value("Directory/IncludeThumbnails").toStringList()==QStringList{"images"},"Clearing parameters also cleared thumbnails");}
    dialog=show_properties();parameters=dialog->findChild<QWidget*>("fileManagerIncludeParameters");parameters->findChild<QPlainTextEdit*>()->setPlainText("missing");
    bool rejected=false;QTimer::singleShot(0,[&]{if(auto* message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){rejected=message->text().contains("missing");message->reject();}});dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();check(rejected&&dialog->isVisible(),"Invalid include was committed or closed its editor");dialog->reject();application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    const auto repository=QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().absoluteFilePath("../..");
    for(const auto* language:{"cs","en","de","fr","ru"}){
        {QSettings config(fixture.filePath("config.ini"),QSettings::IniFormat);config.setValue("Application/Language",language);config.setValue("Paths/Localization",QDir(repository).filePath("config/localization"));config.sync();}
        auto localized=zima::app::ApplicationSettings::load(root,QCoreApplication::applicationFilePath());check(localized.language==language&&localized.qt_translations.contains("Parametry ze zdrojových adresářů"),"Include catalog is unavailable");zima::app::apply_application_translations(application,localized);
        dialog=show_properties();bool translated=false;for(auto* label:dialog->findChildren<QLabel*>())translated=translated||label->text()==localized.qt_translations.value("Parametry ze zdrojových adresářů");check(translated,"Include UI label was not translated after changing language");
        const auto capture=qEnvironmentVariable("ZIMA_FILE_MANAGER_CAPTURE_INCLUDES");if(!capture.isEmpty()&&QString(language)=="cs"){application.processEvents();check(dialog->grab().save(capture),"Cannot capture include properties");}
        dialog->reject();application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    }
    {QSettings config(fixture.filePath("config.ini"),QSettings::IniFormat);config.setValue("Application/Language","cs");config.sync();}zima::app::apply_application_translations(application,zima::app::ApplicationSettings::load(root,QCoreApplication::applicationFilePath()));
    {QSettings linked(source+"/0000-index/metadata.ini",QSettings::IniFormat);linked.setValue("Directory/IncludeParameters","../exports");linked.sync();}tab.navigate(exports);check(!tab.findChild<QLabel*>("fileManagerIncludeStatus")->text().isEmpty(),"Cyclic includes did not report a visible error");
    {QSettings linked(source+"/0000-index/metadata.ini",QSettings::IniFormat);linked.remove("Directory/IncludeParameters");linked.sync();QSettings local(exports+"/0000-index/metadata.ini",QSettings::IniFormat);local.setValue("Directory/IncludeThumbnails","../missing");local.sync();}tab.navigate(exports);check(!tab.findChild<QLabel*>("fileManagerIncludeStatus")->text().isEmpty(),"Missing thumbnail include was silently ignored");
    const QStringList absolute{source,source};check(zima::app::directory_includes(exports,"Directory/IncludeParameters",&absolute).directories.size()==2,"Absolute or duplicate source resolution failed");
}
void verify_native_icons_and_cleanup(QApplication& application){
    QTemporaryDir fixture;check(fixture.isValid(),"Cannot create template fixture");const auto templates=fixture.filePath("templates");check(QDir().mkpath(templates+"/0000-index")&&QDir().mkpath(templates+"/child"),"Cannot create protected templates");
    const QMap<QString,QString> names{{"START.prtz","part"},{"START.PRTZ.2","part"},{"START.asmz","assembly"},{"START.ASMZ.12","assembly"},{"sheet.drwz","drawing"},{"frame.frmz.3","drawing-format"},{"title.tblz","title-block"},{"text.symz","symbol"}};
    for(auto it=names.cbegin();it!=names.cend();++it)touch(QDir(templates).filePath(it.key()));
    {QSettings metadata(templates+"/0000-index/metadata.ini",QSettings::IniFormat);metadata.setValue("Directory/Version",3);metadata.setValue("Directory/PreventRemoval",true);metadata.sync();}
    zima::app::ApplicationSettings settings;settings.file_manager.default_view="files";settings.file_manager.sources.push_back({"templates","Templates",templates,templates,true,false,true});
    zima::app::FileManagerTab tab(settings,fixture.path(),templates);tab.resize(1100,700);tab.show();application.processEvents();tab.findChild<QToolButton*>("fileManagerFilterVersions")->setChecked(true);
    auto* table=tab.findChild<QTreeWidget*>("fileManagerFiles");auto* clean=tab.findChild<QPushButton*>("fileManagerClean");check(!clean->isEnabled()&&!clean->toolTip().isEmpty(),"Protected templates do not explain disabled cleanup with Versions enabled");
    for(const auto* theme:{"light","dark","light"}){
        settings.theme=theme;zima::app::apply_application_appearance(application,settings);application.processEvents();
        for(int row=0;row<table->topLevelItemCount();++row){auto* item=table->topLevelItem(row);const auto icon_name=names.value(item->text(0));if(icon_name.isEmpty())continue;check(!item->icon(0).isNull()&&item->icon(0).pixmap(32,32).toImage()==zima::app::resource_icon(icon_name).pixmap(32,32).toImage(),"Native current/archive icon depends on system association or theme");}
    }
    check(zima::app::native_file_icon(QFileInfo(templates+"/foreign.prt")).isNull(),"Native icon mapping changed a foreign file association");
    auto* sources=tab.findChild<QListWidget*>("fileManagerSources");bool source_protected=false;
    QTimer::singleShot(0,[&]{auto* menu=tab.findChild<QMenu*>("fileManagerSourceMenu");if(!menu)return;auto* action=menu->findChild<QAction*>("fileManagerSourceClean");source_protected=action&&!action->isEnabled()&&!action->toolTip().isEmpty();menu->close();});sources->customContextMenuRequested(sources->visualItemRect(sources->item(1)).center());check(source_protected,"Source menu bypasses template protection");
    tab.navigate(templates+"/child");check(!clean->isEnabled(),"Inherited template protection did not disable cleanup");
    tab.navigate(templates);QTimer::singleShot(0,[&]{auto* menu=tab.findChild<QMenu*>("fileManagerSourceMenu");if(menu){menu->findChild<QAction*>("fileManagerSourceProperties")->trigger();menu->close();}});sources->customContextMenuRequested(sources->visualItemRect(sources->item(1)).center());auto* dialog=tab.findChild<QDialog*>("fileManagerDirectoryEditor");check(dialog,"Template Properties are unavailable");dialog->findChild<QCheckBox*>("fileManagerDirectoryPreventRemoval")->setChecked(false);dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();application.processEvents();check(clean->isEnabled()&&clean->toolTip().isEmpty(),"Explicitly removing template protection did not enable cleanup");
    for(auto it=names.cbegin();it!=names.cend();++it)check(QFileInfo::exists(QDir(templates).filePath(it.key())),"Changing protection removed a template or archive");
}

void verify_reference_themes(QApplication& application,const QString& relative_root,const QString& leaf,int subject_count){
    const QString repository=QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().absoluteFilePath("../..");
    const QString root=QDir(repository).filePath(relative_root);
    const auto original=zima::app::ApplicationSettings::load();
    zima::app::ApplicationSettings settings;settings.language="cs";settings.theme="light";settings.file_manager.default_view="overview";
    settings.file_manager.company_website="https://example.com/company?name=O%27Reilly&source=tables";
    zima::app::apply_application_appearance(application,settings);
    zima::app::FileManagerTab tab(settings,root,root);tab.resize(1300,850);tab.show();application.processEvents();
    auto* overview=tab.findChild<QListWidget*>("fileManagerOverview");
    check(overview&&overview->count()==subject_count,"Reference root does not use the expected automatic subject overview");
    for(int i=0;i<overview->count();++i)check(!overview->item(i)->icon().isNull(),"Units directory preview is missing");
    tab.navigate(root+"/"+leaf);
    auto* web=tab.findChild<QWebEngineView*>("fileManagerOverviewWeb");check(web,"Unit table did not open in the native overview");
    bool loaded=false;QObject::connect(web,&QWebEngineView::loadFinished,&tab,[&](bool ok){loaded=ok;});
    const auto wait=[&](const auto& ready,const char* message){QElapsedTimer timer;timer.start();while(!ready()&&timer.elapsed()<10000){application.processEvents();QThread::msleep(10);}check(ready(),message);};
    const auto read=[&](const QString& script){
        struct Result{bool done{};QVariant value;};auto result=std::make_shared<Result>();
        web->page()->runJavaScript(script,[result](const QVariant& value){result->value=value;result->done=true;});
        wait([&]{return result->done;},"Native unit table JavaScript inspection timed out");return result->value;
    };
    wait([&]{return loaded;},"Native unit table did not finish loading");
    int reloads=0;QObject::connect(web,&QWebEngineView::loadStarted,&tab,[&]{++reloads;});
    for(const auto* language:{"cs","en","de","fr","ru"}){
        if(settings.language!=language){loaded=false;settings.language=language;tab.reload_settings(settings);wait([&]{return loaded;},"Localized native unit table did not load");}
        check(read("document.documentElement.lang").toString()==language,"Native unit table language differs from settings");
        check(read("document.querySelector('a.reference-brand')?.getAttribute('href')").toString()==settings.file_manager.company_website,"Reference logo ignored the configured company website");
        const int before=reloads;
        for(const auto* theme:{"light","dark","light"}){
            settings.theme=theme;zima::app::apply_application_appearance(application,settings);
            const bool dark=settings.theme=="dark";
            check(read("(() => {const rows=document.querySelectorAll('tbody tr');if(rows.length<2)return false;rows[0].cells[0].click();if(!rows[0].classList.contains('reference-selected-row'))return false;rows[1].cells[1].click();return document.querySelectorAll('.reference-selected-row').length===1&&rows[1].classList.contains('reference-selected-row')&&[...rows[1].cells].every(c=>getComputedStyle(c).backgroundColor==='rgb(0, 209, 255)'&&getComputedStyle(c).color==='rgb(16, 35, 44)')})()").toBool(),"Table reading marker did not move or cover the complete row");
            wait([&]{return read("document.documentElement.dataset.zimaTheme").toString()==theme;},"Unit table did not follow the application theme");
            const auto background=application.palette().color(QPalette::Base);
            check(read("getComputedStyle(document.body).backgroundColor").toString()==QString("rgb(%1, %2, %3)").arg(background.red()).arg(background.green()).arg(background.blue()),"Unit table CSS did not follow the application palette");
            check(read("getComputedStyle(document.querySelector('.reference-brand')).backgroundColor").toString()=="rgba(0, 0, 0, 0)","Reference logo regained an opaque background");
            check(read("getComputedStyle(document.documentElement).colorScheme").toString()==(dark?"dark":"light"),"SVG logo did not inherit the application color scheme");
            if(leaf=="01-GENERAL") {
                check(read("document.querySelectorAll('tbody tr').length").toInt()==392&&read("document.querySelectorAll('table').length").toInt()==1,"Merged metric table lost rows or retained split tables");
                check(read("document.querySelectorAll('tr.preferred-row').length").toInt()==25,"Merged metric table lost first-choice marking");
                check(read("(() => {const r=document.querySelector('.table-scroll--frozen'),h=r.querySelector('thead th'),b=r.querySelector('tbody td');r.scrollTop=0;const ht=h.getBoundingClientRect().top,bt=b.getBoundingClientRect().top;r.scrollTop=240;const ok=r.scrollTop>0&&Math.abs(h.getBoundingClientRect().top-ht)<1&&b.getBoundingClientRect().top<bt-100;r.scrollTop=0;return ok})()").toBool(),"Merged metric header moved or its body did not scroll");
                check(read("(() => {const r=document.querySelector('tr.preferred-row');return getComputedStyle(r.querySelector('th')).backgroundColor===getComputedStyle(r.querySelector('td')).backgroundColor&&getComputedStyle(r.querySelector('td')).backgroundColor!==getComputedStyle(document.body).backgroundColor})()").toBool(),"Preferred metric rows lost contrast or their sticky-column color");
            }
            if(leaf=="02-PRECISION"||leaf=="04-WHITWORTH"||leaf.startsWith("03-INCH/")||leaf.startsWith("06-TRAPEZOID/")||leaf=="01-SHAFT-END/03-TORQUE"||leaf=="03-FINE/03-ACCURACY")
                check(read("(() => {const regions=[...document.querySelectorAll('.table-scroll--frozen')];return regions.length>0&&regions.every(r=>{if(r.scrollHeight<=r.clientHeight)return true;const h=r.querySelector('thead th'),t=h.getBoundingClientRect().top;r.scrollTop=240;const ok=r.scrollTop>0&&Math.abs(h.getBoundingClientRect().top-t)<1;r.scrollTop=0;return ok})})()").toBool(),"Thread table header moved while scrolling");
        }
        check(reloads==before,"Changing unit table theme reloaded its page");
    }
    loaded=false;settings.file_manager.company_website.clear();tab.reload_settings(settings);wait([&]{return loaded;},"Unlinked company logo page did not load");
    check(read("!!document.querySelector('.reference-brand img')&&!document.querySelector('a.reference-brand').hasAttribute('href')").toBool(),"Clearing the company website did not retain an unlinked logo");
    zima::app::apply_application_appearance(application,original);
    std::cout<<relative_root.toStdString()<<": automatic overview, native HTML and five-language Light/Dark/Light without reload passed\n";
}
}

int main(int argc,char** argv){
    QApplication application(argc,argv);std::cout.setf(std::ios::unitbuf);
    try{
        {
            const QString path=QFileInfo("resources/branding/ZIMA-Engineering.svg").absoluteFilePath();
            QPalette light=application.palette(),dark=light;
            light.setColor(QPalette::Window,Qt::white);dark.setColor(QPalette::Window,QColor("#20262d"));
            const auto a=zima::app::branding_pixmap(path,{510,100},light).toImage();
            const auto b=zima::app::branding_pixmap(path,{510,100},dark).toImage();
            check(!a.isNull()&&a.size()==b.size(),"Adaptive logo lost its size");
            int ink=0,green=0,white=0,transparent=0;
            for(int y=0;y<a.height();++y)for(int x=0;x<a.width();++x){
                const auto ca=a.pixelColor(x,y),cb=b.pixelColor(x,y);
                check(ca.alpha()==cb.alpha(),"Logo theme changed transparency");
                if(ca.alpha()==0){++transparent;continue;}
                if(ca.alpha()!=255)continue;
                if(ca==QColor("#1C1B17")){++ink;check(cb==QColor("#FEFEFE"),"Dark logo ink remained black");}
                if(ca==QColor("#80AA1A")){++green;check(ca==cb,"Logo theme changed brand green");}
                if(ca==Qt::white||ca==QColor("#FEFEFE")){++white;check(ca==cb,"Logo theme inverted white lettering");}
            }
            check(ink>100&&green>100&&white>100&&transparent>100,"Logo color fixture does not cover its artwork");
            check(zima::app::branding_pixmap(path,{510,100},light).toImage()==a,"Light/Dark/Light logo cache is stale");
            QTemporaryDir legacy;
            QFile original(path);check(original.open(QIODevice::ReadOnly),"Cannot read authored logo fixture");
            auto bytes=original.readAll();
            bytes.replace("@media (prefers-color-scheme:dark) {.st1{fill:#FEFEFE;}}","");
            bytes.replace("@media print {.st1{fill:#1C1B17;}}","");
            const QString legacy_path=legacy.filePath("logo-text.svg");
            QFile copy(legacy_path);check(copy.open(QIODevice::WriteOnly)&&copy.write(bytes)==bytes.size(),"Cannot create legacy logo fixture");copy.close();
            check(zima::app::branding_pixmap(legacy_path,{510,100},dark).toImage()==b,"Previously imported ZIMA logo did not adapt");
            std::cout<<"Adaptive native logo: transparent, unchanged green/white and Light/Dark/Light ink passed\n";
        }
        verify_file_controls(application);
        verify_symbol_thumbnails(application);
        verify_directory_includes(application);
        verify_native_icons_and_cleanup(application);
        verify_reference_themes(application,"config/lib/03-ENGINEERING-TABLES/01-GENERAL-STANDARDS/01-UNITS","01-BASE-UNITS",7);
        verify_reference_themes(application,"config/lib/03-ENGINEERING-TABLES/04-SHAFTS-SPLINES-GEARS","06-GEARS/09-SPAN",6);
        verify_reference_themes(application,"config/lib/03-ENGINEERING-TABLES/05-FASTENERS","04-SCREWS/01-HEX-HEAD/01-GRADE-AB",6);
        verify_reference_themes(application,"config/lib/03-ENGINEERING-TABLES/05-FASTENERS","01-CAPACITY",6);
        verify_reference_themes(application,"config/lib/03-ENGINEERING-TABLES/05-FASTENERS","02-HOLES",6);
        verify_reference_themes(application,"config/lib/03-ENGINEERING-TABLES/05-FASTENERS","04-SCREWS/01-HEX-HEAD/03-FULL-AB",6);
        verify_reference_themes(application,"config/lib/03-ENGINEERING-TABLES/03-THREADS/02-METRIC","01-GENERAL",3);
        verify_reference_themes(application,"config/lib/03-ENGINEERING-TABLES/03-THREADS/02-METRIC","02-PRECISION",3);
        verify_reference_themes(application,"config/lib/03-ENGINEERING-TABLES/03-THREADS","04-WHITWORTH",9);
        verify_reference_themes(application,"config/lib/03-ENGINEERING-TABLES/03-THREADS","03-INCH/02-SIZES",9);
        verify_reference_themes(application,"config/lib/03-ENGINEERING-TABLES/03-THREADS","06-TRAPEZOID/01-SIZES",9);
        verify_reference_themes(application,"config/lib/03-ENGINEERING-TABLES/04-SHAFTS-SPLINES-GEARS","01-SHAFT-END/03-TORQUE",6);
        verify_reference_themes(application,"config/lib/03-ENGINEERING-TABLES/04-SHAFTS-SPLINES-GEARS","03-FINE/03-ACCURACY",6);
        const QString factory_directory=qEnvironmentVariable("ZIMA_FILE_MANAGER_VERIFY_FACTORY_LABELS");
        if(!factory_directory.isEmpty()){
            check(QFileInfo(factory_directory).isDir(),"Factory label verification directory is missing");int directories=0;
            QDirIterator metadata_files(factory_directory,{"metadata.ini"},QDir::Files,QDirIterator::Subdirectories);
            while(metadata_files.hasNext()){
                QSettings metadata(metadata_files.next(),QSettings::IniFormat);
                for(const auto& language:{"cs","en","de","fr","ru"}){
                    const QVariant value=metadata.value(QStringLiteral("Directory/Label/%1").arg(language));
                    if(value.metaType().id()!=QMetaType::QString||value.toString().trimmed().isEmpty())throw std::runtime_error((metadata.fileName()+QLatin1Char(':')+QString::fromLatin1(language)+QStringLiteral(": ")+QString::fromLatin1(value.metaType().name()?value.metaType().name():"invalid")+QStringLiteral(" — factory label is not a complete native Qt string")).toStdString());
                }
                ++directories;
            }
            check(directories==332,"Factory directory label inventory differs from 332 directories");
            std::cout<<"Native Qt reading of all 1660 factory directory labels passed\n";
        }
        if(qEnvironmentVariableIsSet("ZIMA_VERIFY_REFERENCE_TABLES_ONLY"))return 0;
        QTemporaryDir fixture(QDir::current().filePath("file-manager-test-XXXXXX"));check(fixture.isValid(),"Cannot create synthetic fixture");
        {
            const QString directory=fixture.filePath("foreign-previews");check(QDir().mkpath(directory),"Cannot create foreign preview fixtures");
            const QStringList names{"model.step","model.STP","model.iges","model.IGS","drawing.DXF","notes.txt"};
            for(const auto& name:names)touch(QDir(directory).filePath(name));
            zima::app::ApplicationSettings preview_settings;preview_settings.file_manager.default_view="files";
            zima::app::FileManagerTab preview_tab(preview_settings,directory,directory);preview_tab.resize(1100,700);preview_tab.show();application.processEvents();
            auto* table=preview_tab.findChild<QTreeWidget*>("fileManagerFiles");QString preview_request,open_request;
            preview_tab.preview_file_requested=[&](const QString& path){preview_request=path;};
            preview_tab.open_file_requested=[&](const QString& path){open_request=path;};
            QPixmap thumbnail(16,16);thumbnail.fill(Qt::red);
            for(int row=0;row<table->topLevelItemCount();++row){
                auto* item=table->topLevelItem(row);const QString path=item->data(0,Qt::UserRole).toString();
                for(const bool populated:{false,true}){
                    item->setIcon(zima::app::file_preview_column,populated?QIcon(thumbnail):QIcon{});
                    table->scrollToItem(item);application.processEvents();preview_request.clear();open_request.clear();
                    const auto point=table->visualRect(table->indexFromItem(item,zima::app::file_preview_column)).center();
                    QMouseEvent click(QEvent::MouseButtonDblClick,point,table->viewport()->mapToGlobal(point),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(table->viewport(),&click);
                    check(open_request.isEmpty(),"Preview cell opened the filename");
                    check(QFileInfo(path).suffix()=="txt"?preview_request.isEmpty():preview_request==path,"Foreign preview depends on thumbnail or extension case");
                }
                QFile original(path);check(original.open(QIODevice::ReadOnly)&&original.readAll()=="synthetic","Preview dispatch modified its source");
            }
        }
        const QString working=fixture.filePath("working"),source=fixture.filePath("library"),read_only=fixture.filePath("read-only"),project=source+"/DEMO-001",index=project+"/0000-index",html_directory=source+"/HTML-TABLE",html_index=html_directory+"/0000-index";
        QDir root(fixture.path());
        if(!root.mkdir("working"))throw std::runtime_error(("Cannot create synthetic working directory: "+fixture.path()).toStdString());
        if(!root.mkdir("read-only"))throw std::runtime_error(("Cannot create synthetic read-only source: "+fixture.path()).toStdString());
        if(!root.mkpath("library/DEMO-001/0000-index"))throw std::runtime_error(("Cannot create synthetic project directory: "+fixture.path()).toStdString());
        if(!root.mkpath("library/DEMO-001/SUBDIR")||!root.mkpath("library/HTML-TABLE/0000-index"))throw std::runtime_error("Cannot create synthetic child directories");
        touch(working+"/PANEL.prtz");touch(read_only+"/LOCKED.prtz");touch(project+"/BRACKET.prtz");touch(project+"/ASSEMBLY.asmz");touch(project+"/notes.txt");touch(project+"/SUBDIR/child.txt");
        {QFile html(html_index+"/index_cs.html");check(html.open(QIODevice::WriteOnly),"Cannot create localized HTML overview");html.write("<!doctype html><title>Localized table</title>");QFile generic(html_index+"/index.html");check(generic.open(QIODevice::WriteOnly),"Cannot create generic HTML overview");generic.write("<!doctype html><title>Generic table</title>");QFile logo(html_index+"/logo.svg");check(logo.open(QIODevice::WriteOnly),"Cannot create SVG directory logo");logo.write("<svg xmlns='http://www.w3.org/2000/svg' width='80' height='20'><rect width='80' height='20'/></svg>");}
        {
            QSettings metadata(index+"/metadata.ini",QSettings::IniFormat);
            metadata.setValue("Directory/Version",3);metadata.setValue("Directory/Kind","project");metadata.setValue("Directory/Id","synthetic-project");
            metadata.setValue("Directory/Label/cs","Ukázkový projekt");metadata.setValue("Project/Number","DEMO-001");metadata.setValue("Project/Title/cs","Ukázkový projekt");
            metadata.setValue("Directory/SubdirectoriesAsParts",false);
            metadata.setValue("Directory/Parameters","material,note");metadata.setValue("Parameters/material/Label/cs","Materiál");metadata.setValue("Parameters/note/Label/cs","Poznámka");
            metadata.setValue("Parts/BRACKET/material","S355");metadata.sync();check(metadata.status()==QSettings::NoError,"Cannot write synthetic metadata");
        }
        {QSettings metadata(html_index+"/metadata.ini",QSettings::IniFormat);metadata.setValue("Directory/Version",3);metadata.setValue("Directory/Kind","directory");metadata.setValue("Directory/Id","synthetic-html");metadata.setValue("Directory/Label/cs","HTML tabulka");metadata.setValue("Directory/LogoMode","logo");metadata.setValue("Directory/Logo","logo.svg");metadata.sync();}
        {QFile metadata(index+"/metadata.ini");check(metadata.open(QIODevice::ReadOnly),"Cannot read parameter-list fixture");QByteArray text=metadata.readAll();metadata.close();check(text.contains("Parameters=\"material,note\""),"Scalar parameter fixture differs from Qt INI serialization");text.replace("Parameters=\"material,note\"","Parameters=material, note");check(metadata.open(QIODevice::WriteOnly|QIODevice::Truncate),"Cannot write native INI parameter list");metadata.write(text);}
        {QSettings metadata(index+"/metadata.ini",QSettings::IniFormat);metadata.setValue("Directory/Parameters","material,note,native_length");metadata.sync();}
        check(QDir().mkpath(project+"/SUBDIR/0000-index"),"Cannot create child preview fixture");
        {QImage preview(100,60,QImage::Format_RGB32);preview.fill(Qt::red);check(preview.save(project+"/SUBDIR/0000-index/01_cs.png"),"Cannot write localized preview");preview.fill(Qt::blue);check(preview.save(project+"/SUBDIR/0000-index/logo.png"),"Cannot write child branding");}

        zima::app::ApplicationSettings settings;settings.language="cs";settings.file_manager.default_view="overview";settings.file_manager.thumbnail_size=96;
        settings.file_manager.sources.push_back({"synthetic","Testovací knihovna",source,source,true,false,false});
        settings.file_manager.sources.push_back({"read-only","Knihovna jen pro čtení",read_only,read_only,true,true,false});
        zima::app::FileManagerTab first(settings,working,project);first.resize(1100,700);first.show();application.processEvents();QString native_edit,opened_file;
        first.open_file_requested=[&](const QString& path){opened_file=path;};
        first.native_parameters_requested=[](const QString& path){zima::app::FileManagerNativeParameters data;if(path.endsWith(".prtz",Qt::CaseInsensitive)){data.order={"native_length","hidden_native"};data.values.insert("hidden_native","999");data.values.insert("native_length","125");data.labels["native_length"].insert("cs","Nativní délka");}return data;};
        first.native_parameter_edit_requested=[&](const QString& path,const QString& parameter,const QString& value){native_edit=QFileInfo(path).fileName()+QLatin1Char(':')+parameter+QLatin1Char('=')+value;return QString{};};
        first.navigate(working);first.navigate(project);
        auto* sources=first.findChild<QListWidget*>("fileManagerSources");auto* source_trees=first.findChild<QStackedWidget*>("fileManagerSourceTrees");auto* views=first.findChild<QTabWidget*>("fileManagerViews");
        auto* overview=first.findChild<QListWidget*>("fileManagerOverview");auto* files=first.findChild<QTreeWidget*>("fileManagerFiles");
        auto* working_field=first.findChild<QLineEdit*>("fileManagerPath");check(working_field&&working_field->isReadOnly()&&working_field->text()==QDir::toNativeSeparators(working),"Working directory field is editable or follows browsing");
        QString requested_working;first.working_directory_requested=[&](const QString& path){requested_working=path;};
        if(!factory_directory.isEmpty()){
            auto factory_settings=settings;factory_settings.file_manager.sources.push_back({"factory-verification","Factory",factory_directory,factory_directory,true,true,false});
            zima::app::FileManagerTab factory(factory_settings,working,factory_directory);
            QSettings factory_metadata(QDir(factory_directory).filePath("0000-index/metadata.ini"),QSettings::IniFormat);
            for(const auto& language:{"cs","en","de","fr","ru"}){
                factory_settings.language=language;factory.reload_settings(factory_settings);
                check(factory.findChild<QLabel*>("fileManagerOverviewTitle")->text()==factory_metadata.value(QStringLiteral("Directory/Label/%1").arg(language)).toString(),"Factory overview did not display the active-language root label");
                auto* cards=factory.findChild<QListWidget*>("fileManagerOverview");check(cards&&cards->count()==18,"Factory overview did not show all engineering subject directories");
                QSettings child_metadata(QDir(factory_directory).filePath("01-GENERAL-STANDARDS/0000-index/metadata.ini"),QSettings::IniFormat);
                check(cards->item(0)->text()==child_metadata.value(QStringLiteral("Directory/Label/%1").arg(language)).toString(),"Factory overview card did not display the active-language subject label");
            }
        }
        const QString authored=fixture.filePath("earlier-cad-metadata");check(QDir().mkpath(authored+"/0000-index"),"Cannot create earlier authored metadata fixture");
        {QSettings metadata(authored+"/0000-index/metadata.ini",QSettings::IniFormat);metadata.setValue("Directory/Version",3);metadata.setValue("Directory/Label\\cs","Dříve upravený název");metadata.sync();}
        {zima::app::FileManagerTab earlier(settings,authored,authored);check(earlier.findChild<QLabel*>("fileManagerOverviewTitle")->text()=="Dříve upravený název","Previously authored CAD directory labels were lost");}
        check(sources&&sources->count()==3&&source_trees&&source_trees->count()==3,"Working directory and configured sources are not separate source pages");
        auto* source_tree=first.findChild<QTreeView*>("fileManagerSourceTree_synthetic");check(source_tree,"Configured source tree is missing");
        QPersistentModelIndex project_index,html_tree_index;for(int attempt=0;attempt<50&&(!project_index.isValid()||!html_tree_index.isValid());++attempt){application.processEvents();for(int row=0;row<source_tree->model()->rowCount(source_tree->rootIndex());++row){const auto candidate=source_tree->model()->index(row,0,source_tree->rootIndex());const auto accessible=candidate.data(Qt::AccessibleTextRole).toString();if(candidate.data().toString()=="Ukázkový projekt")project_index=candidate;if(accessible=="HTML tabulka")html_tree_index=candidate;}if(!project_index.isValid()||!html_tree_index.isValid())QThread::msleep(10);}check(project_index.isValid(),"Source tree did not use the localized metadata directory label");source_tree->expand(project_index);application.processEvents();
        check(html_tree_index.isValid()&&html_tree_index.data().toString().isEmpty()&&html_tree_index.data(Qt::AccessibleTextRole).toString()=="HTML tabulka","Logo-only directory did not hide only the visual label");
        bool exposed_index=false;for(int row=0;row<source_tree->model()->rowCount(project_index);++row)if(source_tree->model()->index(row,0,project_index).data().toString().compare("0000-index",Qt::CaseInsensitive)==0)exposed_index=true;
        check(!exposed_index,"Source tree exposed the internal 0000-index directory");
        sources->setCurrentRow(0);sources->itemClicked(sources->item(0));check(first.current_directory()==QDir::cleanPath(working),"Clicking the source itself did not open its root");
        sources->setCurrentRow(1);sources->itemClicked(sources->item(1));check(first.current_directory()==QDir::cleanPath(source),"Clicking a configured source did not open its root");check(files->topLevelItemCount()==2&&files->topLevelItem(0)->text(0)=="Ukázkový projekt","Files view did not use the localized metadata directory label");
        bool working_source_action=false;QTimer::singleShot(0,[&]{auto* menu=first.findChild<QMenu*>("fileManagerSourceMenu");if(!menu)return;auto* action=menu->findChild<QAction*>("fileManagerSetWorkingDirectory");working_source_action=action&&action->isEnabled();if(action)action->trigger();menu->close();});sources->customContextMenuRequested(sources->visualItemRect(sources->item(1)).center());check(working_source_action&&requested_working==QDir::cleanPath(source),"Source root does not request the working directory through its context menu");
        requested_working.clear();source_tree->scrollTo(project_index);application.processEvents();bool working_tree_action=false;QTimer::singleShot(0,[&]{auto* menu=first.findChild<QMenu*>("fileManagerTreeMenu");if(!menu)return;auto* action=menu->findChild<QAction*>("fileManagerSetWorkingDirectory");working_tree_action=action&&action->isEnabled();if(action)action->trigger();menu->close();});source_tree->customContextMenuRequested(source_tree->visualRect(project_index).center());check(working_tree_action&&requested_working==QDir::cleanPath(project),"Directory tree does not request the selected working directory");if(!project_index.data(Qt::ToolTipRole).toString().contains(QDir::toNativeSeparators(project)))throw std::runtime_error("Directory tooltip actual="+project_index.data(Qt::ToolTipRole).toString().toStdString()+" expected="+QDir::toNativeSeparators(project).toStdString());
        bool source_properties_action=false;QTimer::singleShot(0,[&]{auto* menu=first.findChild<QMenu*>("fileManagerSourceMenu");if(!menu)return;auto* action=menu->findChild<QAction*>("fileManagerSourceProperties");source_properties_action=action;if(action)action->trigger();menu->close();});sources->customContextMenuRequested(sources->visualItemRect(sources->item(1)).center());application.processEvents();check(source_properties_action,"Configured source does not offer directory properties");auto* directory_editor=first.findChild<QDialog*>("fileManagerDirectoryEditor");check(directory_editor,"Directory properties editor did not open for the configured source");auto* en_label=directory_editor->findChild<QLineEdit*>("fileManagerDirectoryLabel_en");auto* subdirectories=directory_editor->findChild<QCheckBox*>("fileManagerDirectorySubdirectories");auto* protection=directory_editor->findChild<QCheckBox*>("fileManagerDirectoryPreventRemoval");auto* parameters=directory_editor->findChild<QTableWidget*>("fileManagerDirectoryParameters");check(en_label&&subdirectories&&protection&&parameters,"Directory properties fields are incomplete");en_label->setText("Engineering library");subdirectories->setChecked(true);protection->setChecked(true);parameters->insertRow(0);parameters->setItem(0,0,new QTableWidgetItem("note"));parameters->setItem(0,1,new QTableWidgetItem("Poznámka"));for(int column=2;column<6;++column)parameters->setItem(0,column,new QTableWidgetItem("Note"));auto* directory_buttons=directory_editor->findChild<QDialogButtonBox*>();check(directory_buttons&&directory_buttons->button(QDialogButtonBox::Ok),"Directory properties confirmation is missing");directory_buttons->button(QDialogButtonBox::Ok)->click();application.processEvents();QSettings directory_metadata(source+"/0000-index/metadata.ini",QSettings::IniFormat);check(directory_metadata.value("Directory/Label/en").toString()=="Engineering library"&&directory_metadata.value("Directory/SubdirectoriesAsParts").toBool()&&directory_metadata.value("Directory/PreventRemoval").toBool()&&directory_metadata.value("Directory/Parameters").toString()=="note","Directory properties were not persisted");check(!first.findChild<QPushButton*>("fileManagerMove")->isEnabled()&&!first.findChild<QPushButton*>("fileManagerDelete")->isEnabled(),"Protected directory still enabled destructive file operations");
        bool source_project_action=false;QTimer::singleShot(0,[&]{auto* menu=first.findChild<QMenu*>("fileManagerSourceMenu");if(!menu)return;auto* action=menu->findChild<QAction*>("fileManagerSourceProject");source_project_action=action&&action->isEnabled();if(action)action->trigger();menu->close();});
        sources->customContextMenuRequested(sources->visualItemRect(sources->item(1)).center());application.processEvents();check(source_project_action,"Configured source does not offer project overview creation");
        auto* project_editor=first.findChild<QDialog*>("fileManagerProjectEditor");check(project_editor,"Project overview editor did not open for the configured source");project_editor->accept();application.processEvents();
        QSettings source_metadata(source+"/0000-index/metadata.ini",QSettings::IniFormat);check(source_metadata.value("Directory/Version").toInt()==3&&source_metadata.value("Directory/Kind").toString()=="project","Project overview was not created at the configured source root");
        check(views&&views->count()==2,"File Manager must expose only Overview and Files");first.navigate(project);
        auto* overview_title=first.findChild<QLabel*>("fileManagerOverviewTitle");auto* overview_details=first.findChild<QLabel*>("fileManagerOverviewDetails");
        check(overview_title&&overview_title->text()=="Ukázkový projekt"&&overview_details&&overview_details->text().contains("DEMO-001"),"Project information is not integrated into Overview");
        check(overview&&overview->count()==1&&overview->item(0)->text()=="SUBDIR","Automatic directory overview exposed files or 0000-index");
        check(overview->item(0)->icon().pixmap(100,60).toImage().pixelColor(50,30)==QColor(Qt::red),"Child localized preview did not take precedence over its logo");
        const QPalette original_palette=application.palette();
        const QString capture_directory=qEnvironmentVariable("ZIMA_FILE_MANAGER_CAPTURE_DIRECTORY");
        const auto capture=[&](const QString& name){application.processEvents();const int tree_bottom=source_tree->mapTo(&first,QPoint(0,source_tree->height()-1)).y();const int overview_bottom=overview->mapTo(&first,QPoint(0,overview->height()-1)).y();check(std::abs(tree_bottom-overview_bottom)<=1,"Overview and directory tree bottom edges differ");const int right=overview->mapTo(&first,QPoint(overview->width()-1,0)).x();const int panel_right=views->mapTo(&first,QPoint(views->width()-1,0)).x();check(std::abs(right-panel_right)<=1,"Overview retains unused right margin");if(!capture_directory.isEmpty()){QDir().mkpath(capture_directory);check(first.grab().save(QDir(capture_directory).filePath(name)),"Cannot save overview verification image");}};
        QPalette light=original_palette;light.setColor(QPalette::Window,QColor("#efefef"));light.setColor(QPalette::Button,QColor("#efefef"));light.setColor(QPalette::ButtonText,Qt::black);light.setColor(QPalette::Base,Qt::white);light.setColor(QPalette::AlternateBase,QColor("#f6f6f6"));light.setColor(QPalette::Text,Qt::black);light.setColor(QPalette::WindowText,Qt::black);light.setColor(QPalette::Mid,QColor("#b0b0b0"));application.setPalette(light);
        capture("overview-light.png");QPalette dark=light;dark.setColor(QPalette::Window,QColor("#202020"));dark.setColor(QPalette::Button,QColor("#303030"));dark.setColor(QPalette::ButtonText,Qt::white);dark.setColor(QPalette::Base,QColor("#252525"));dark.setColor(QPalette::AlternateBase,QColor("#303030"));dark.setColor(QPalette::Text,Qt::white);dark.setColor(QPalette::WindowText,Qt::white);dark.setColor(QPalette::Mid,QColor("#606060"));application.setPalette(dark);capture("overview-dark.png");check(overview->palette().color(QPalette::Text)==QColor(Qt::white),"Overview did not follow a live dark palette");application.setPalette(light);capture("overview-light-restored.png");check(overview->palette().color(QPalette::Text)==QColor(Qt::black),"Overview did not restore the live light palette");application.setPalette(original_palette);
        overview->itemClicked(overview->item(0));check(first.current_directory()==QDir::cleanPath(project+"/SUBDIR"),"Single-clicking an overview tile did not navigate");first.navigate(project);
        check(files&&files->topLevelItemCount()==3&&files->columnCount()==8,"File table did not load metadata and native parameter columns");
        bool bracket_found=false;for(int row=0;row<files->topLevelItemCount();++row){auto* item=files->topLevelItem(row);if(item->text(0)!="BRACKET.prtz")continue;bracket_found=true;check(item->text(5)=="S355"&&item->text(7)=="125","Metadata or native parameter was not loaded");files->itemDoubleClicked(item,7);check(opened_file.isEmpty(),"Double-clicking a parameter cell opened the file");files->itemDoubleClicked(item,0);check(opened_file==QDir::cleanPath(project+"/BRACKET.prtz"),"Double-clicking the file-name cell did not open the file");const auto cell=files->indexFromItem(item,7);QStyleOptionViewItem option;auto* editor=files->itemDelegate()->createEditor(files,option,cell);auto* line=qobject_cast<QLineEdit*>(editor);check(line,"Parameter delegate did not create an editor");line->setText("150");files->itemDelegate()->setModelData(editor,files->model(),cell);delete editor;}
        check(bracket_found,"Native Part is missing from the file table");
        {zima::app::FileManagerTab retained(settings,working,project);auto* retained_views=retained.findChild<QTabWidget*>("fileManagerViews");retained_views->setCurrentIndex(1);retained.set_working_directory(working);check(retained_views->currentIndex()==1,"Working-directory refresh reset the user's Files view");
        auto* retained_sources=retained.findChild<QListWidget*>("fileManagerSources");
        const int selected=retained_sources->currentRow();
        retained.set_working_directory(project);check(retained.current_directory()==QDir::cleanPath(project)&&retained_sources->currentRow()==selected,"Overlapping working directory switched the configured source");
        retained.set_working_directory(source);check(retained.current_directory()==QDir::cleanPath(project)&&retained_sources->currentRow()==selected,"Working root change navigated away from the open chapter");
        retained.working_directory_requested=[&](const QString& directory){retained.set_working_directory(directory);};
        QTimer::singleShot(0,[&]{auto* menu=retained.findChild<QMenu*>("fileManagerSourceMenu");check(menu,"Working root source menu missing");menu->findChild<QAction*>("fileManagerSetWorkingDirectory")->trigger();menu->close();});
        retained_sources->customContextMenuRequested(retained_sources->visualItemRect(retained_sources->item(selected)).center());
        check(retained.current_directory()==QDir::cleanPath(project)&&retained_sources->currentRow()==selected,"Set Working Directory context action changed browsing context");
        retained.navigate(project,false);check(retained_views->currentIndex()==1,"Same-directory refresh reset the user's Files view");retained.reload_settings(settings);check(retained_views->currentIndex()==1,"Settings reload reset the user's Files view");}
        check(files->editTriggers()==QAbstractItemView::SelectedClicked,"Parameter cells do not use the ZIMA-Parts selected-click editing behavior");
        auto* name_filter=first.findChild<QLineEdit*>("fileManagerColumnFilter_0");auto* type_filter=first.findChild<QLineEdit*>("fileManagerColumnFilter_2");check(name_filter&&type_filter,"Per-column file filters are missing");
        name_filter->setText("BRACKET");check(!files->topLevelItem(0)->isHidden()||!files->topLevelItem(1)->isHidden()||!files->topLevelItem(2)->isHidden(),"Name filter hid every matching file");int visible=0;for(int row=0;row<files->topLevelItemCount();++row)visible+=!files->topLevelItem(row)->isHidden();check(visible==1,"Name column filter did not isolate the matching file");name_filter->clear();type_filter->setText("ASMZ");visible=0;for(int row=0;row<files->topLevelItemCount();++row)visible+=!files->topLevelItem(row)->isHidden();check(visible==1,"Type column filter did not isolate the assembly");type_filter->clear();
        check(native_edit=="BRACKET.prtz:native_length=150","Native parameter edit was not routed to the document provider");
        QSettings edited(index+"/metadata.ini",QSettings::IniFormat);if(edited.value("Parts/BRACKET/native_length").toString()!="150"||edited.value("Files/BRACKET/Provider").toString()!="zima-cad"){
            for(const auto& key:edited.allKeys())std::cerr<<key.toStdString()<<'='<<edited.value(key).toString().toStdString()<<'\n';
            throw std::runtime_error("Native parameter mirror or provider was not saved");
        }

        auto* sizes=first.findChild<QSlider*>("fileManagerThumbnailSize");check(sizes,"Thumbnail size control is missing");
        check(sizes->value()==96&&sizes->minimum()==48&&sizes->maximum()==192,"Thumbnail default/range is not enlarged");
        check(!first.findChild<QLineEdit*>("fileManagerColumnFilter_1"),"Preview column has a meaningless text filter");
        sizes->setValue(72);check(files->iconSize()==QSize(72,72)&&files->columnWidth(zima::app::file_preview_column)==76,"Thumbnail slider did not change file previews and column width");
        check(!views->tabIcon(0).isNull()&&!views->tabIcon(1).isNull(),"Overview and Files icons are missing");
        check(files->columnWidth(0)<=360&&files->header()->sectionsMovable(),"Filename column stretches or cannot be reordered");
        files->header()->moveSection(0,2);application.processEvents();check(name_filter->x()==files->header()->sectionViewportPosition(0)+2,"Filter did not move with its column");files->header()->moveSection(2,0);
        views->setCurrentIndex(1);application.processEvents();
        const int previous_width=files->columnWidth(7);files->setColumnWidth(7,1600);application.processEvents();
        auto* horizontal=files->horizontalScrollBar();check(horizontal->maximum()>0,"Wide file table did not expose horizontal scrolling");
        const auto check_filter_alignment=[&]{application.processEvents();for(int column=0;column<files->columnCount();++column){auto* filter=first.findChild<QLineEdit*>(QStringLiteral("fileManagerColumnFilter_%1").arg(column));if(filter)check(filter->x()==files->header()->sectionViewportPosition(column)+2,"Filter did not scroll with its column");}};
        for(const int position:{horizontal->maximum()/2,horizontal->maximum(),0,horizontal->maximum()/3}){horizontal->setValue(position);check_filter_alignment();}
        files->header()->moveSection(0,2);check_filter_alignment();files->setColumnWidth(2,files->columnWidth(2)+20);check_filter_alignment();files->header()->moveSection(2,0);check_filter_alignment();
        files->setColumnWidth(7,previous_width);horizontal->setValue(0);check_filter_alignment();
        files->clearSelection();auto* a=files->topLevelItem(0);auto* b=files->topLevelItem(1);auto* c=files->topLevelItem(2);a->setSelected(true);b->setSelected(true);a->setCheckState(0,Qt::Checked);check(b->checkState(0)==Qt::Checked&&c->checkState(0)==Qt::Unchecked,"Checking selected rows affected the wrong group");a->setCheckState(0,Qt::Unchecked);check(b->checkState(0)==Qt::Unchecked,"Unchecking selected rows did not propagate");files->clearSelection();
        views->setCurrentIndex(1);application.processEvents();files->clearSelection();a->setSelected(true);b->setSelected(true);
        QStyleOptionViewItem checkbox;checkbox.rect=files->visualItemRect(a);checkbox.features=QStyleOptionViewItem::HasCheckIndicator|QStyleOptionViewItem::HasDecoration|QStyleOptionViewItem::HasDisplay;checkbox.checkState=a->checkState(0);checkbox.icon=a->icon(0);checkbox.decorationSize=files->iconSize();checkbox.text=a->text(0);
        const QPoint click=files->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator,&checkbox,files).center();
        QMouseEvent press(QEvent::MouseButtonPress,click,files->viewport()->mapToGlobal(click),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(files->viewport(),&press);QMouseEvent release(QEvent::MouseButtonRelease,click,files->viewport()->mapToGlobal(click),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(files->viewport(),&release);
        check(a->checkState(0)==Qt::Checked&&b->checkState(0)==Qt::Checked&&c->checkState(0)==Qt::Unchecked&&files->selectedItems().size()==2,"Actual checkbox click lost the selected row group");a->setCheckState(0,Qt::Unchecked);files->clearSelection();
        {QImage thumbnail(320,240,QImage::Format_RGB32);thumbnail.fill(Qt::red);QDir().mkpath(index+"/thumbnails");check(thumbnail.save(index+"/thumbnails/BRACKET.png")&&thumbnail.scaled(240,320,Qt::IgnoreAspectRatio).save(index+"/thumbnails/ASSEMBLY.png"),"Cannot write thumbnail fixtures");}
        first.navigate(project);application.processEvents();QTreeWidgetItem* preview_row{};for(int row=0;row<files->topLevelItemCount();++row)if(files->topLevelItem(row)->text(0)=="BRACKET.prtz")preview_row=files->topLevelItem(row);check(preview_row&&!preview_row->data(0,Qt::UserRole+4).toString().isEmpty(),"Parts-compatible basename thumbnail was not discovered");
        const auto check_uniform_rows=[&]{
            application.processEvents();
            for(int row=0;row<files->topLevelItemCount();++row){
                auto* item=files->topLevelItem(row);if(item->isHidden())continue;
                check(files->visualItemRect(item).height()==sizes->value()+4,
                    "Files row height differs for a directory, empty preview or image aspect ratio");
            }
        };
        for(const int size:{48,72,144,192,72}){sizes->setValue(size);check_uniform_rows();}
        ItemGeometryProbe geometry;const auto preview_index=files->indexFromItem(preview_row,zima::app::file_preview_column);const auto preview_geometry=geometry.geometry(files,preview_index);const QPoint preview_point=preview_geometry.thumbnail.center();check(files->indexAt(preview_point)==preview_index,"Thumbnail point does not resolve to its real styled file cell");
        // Inspect pixels drawn by the actual delegate, not only its cell bounds.
        // QIcon's raster engine does not itself enlarge a small source image.
        const auto original_preview=preview_row->icon(zima::app::file_preview_column);
        const auto preview_palette=files->palette();
        const QString svg_preview=fixture.filePath("small-preview.svg");
        {QFile svg(svg_preview);check(svg.open(QIODevice::WriteOnly),"Cannot create small SVG preview");svg.write("<svg xmlns='http://www.w3.org/2000/svg' width='10' height='6'><rect width='10' height='6' fill='#ff0000'/></svg>");}
        for(bool dark:{false,true,false}) {
            auto palette=preview_palette;palette.setColor(QPalette::Base,dark?QColor("#252525"):QColor(Qt::white));palette.setColor(QPalette::Text,dark?Qt::white:Qt::black);files->setPalette(palette);
            for(const auto source_size:{QSize(10,6),QSize(6,10),QSize(320,240),QSize(240,320)}) {
                QPixmap raster(source_size);raster.fill(Qt::red);
                for(bool svg:{false,true}) {
                    if(svg&&source_size!=QSize(10,6))continue;
                    preview_row->setIcon(zima::app::file_preview_column,svg?QIcon(svg_preview):QIcon(raster));
                    for(int size:{48,96,144,192})for(qreal ratio:{1.,2.}) {
                        QStyleOptionViewItem option;option.initFrom(files);option.rect={0,0,size+4,size+4};option.decorationSize={size,size};option.state|=QStyle::State_Enabled;option.state&=~QStyle::State_Selected;
                        files->setIconSize({size,size});QImage rendered(QSize(size+4,size+4)*ratio,QImage::Format_ARGB32);rendered.setDevicePixelRatio(ratio);rendered.fill(Qt::transparent);
                        {QPainter painter(&rendered);files->itemDelegate()->paint(&painter,option,preview_index);}
                        QRect pixels;
                        for(int y=0;y<rendered.height();++y)for(int x=0;x<rendered.width();++x){const auto color=rendered.pixelColor(x,y);if(color.red()>200&&color.green()<80&&color.blue()<80)pixels=pixels.united(QRect(x,y,1,1));}
                        const auto expected=source_size.scaled(QSize(size,size),Qt::KeepAspectRatio)*ratio;
                        check(std::abs(pixels.width()-expected.width())<=2&&std::abs(pixels.height()-expected.height())<=2,"Preview does not fill its cell at the selected size or changes image proportions");
                    }
                }
            }
        }
        preview_row->setIcon(zima::app::file_preview_column,original_preview);files->setPalette(preview_palette);files->setIconSize({sizes->value(),sizes->value()});application.processEvents();
        check(!preview_row->icon(0).isNull()&&preview_row->icon(0).cacheKey()!=qvariant_cast<QIcon>(preview_index.data(Qt::DecorationRole)).cacheKey(),"Thumbnail replaced the file-type icon");
        for(int row=0;row<files->topLevelItemCount();++row){const auto index=files->indexFromItem(files->topLevelItem(row),zima::app::file_preview_column);if(qvariant_cast<QIcon>(index.data(Qt::DecorationRole)).isNull())continue;check(geometry.geometry(files,index).thumbnail.center().x()==preview_point.x(),"Preview images do not share one vertical line");}
        QStyleOptionViewItem preview_option;check(!files->itemDelegate()->createEditor(files,preview_option,preview_index),"Preview cell created a parameter editor");
        const auto name_geometry=geometry.geometry(files,files->indexFromItem(preview_row,0));const auto type_icon=files->style()->subElementRect(QStyle::SE_ItemViewItemDecoration,&name_geometry.content,files);const auto filename=files->style()->subElementRect(QStyle::SE_ItemViewItemText,&name_geometry.content,files);check(type_icon.right()<filename.left()&&filename.right()<preview_geometry.thumbnail.left(),"Type icon, filename and thumbnail are not in their independent visual order");
        {auto* slider=first.findChild<QSlider*>("fileManagerThumbnailSize");auto* create=first.findChild<QPushButton*>("fileManagerNewDirectory");auto* clean=first.findChild<QPushButton*>("fileManagerClean");check(slider->width()>=80&&slider->width()<=300&&slider->geometry().center().y()==create->geometry().center().y()&&slider->geometry().center().y()==clean->geometry().center().y()&&slider->x()>create->x()&&slider->x()<clean->x(),"Thumbnail size control must share the compact action row next to New Folder");}
        QHelpEvent help(QEvent::ToolTip,preview_point,files->viewport()->mapToGlobal(preview_point));QApplication::sendEvent(files->viewport(),&help);check(wait_until([]{return QToolTip::isVisible()&&QToolTip::text().contains("data:image/png;base64,");},1500),"Hover did not show the enlarged thumbnail after native tooltip animation");QToolTip::hideText();opened_file.clear();QMouseEvent double_click(QEvent::MouseButtonDblClick,preview_point,files->viewport()->mapToGlobal(preview_point),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(files->viewport(),&double_click);application.processEvents();auto* inspector=first.findChild<QDialog*>("fileManagerImagePreview");check(inspector&&opened_file.isEmpty(),"Thumbnail double-click opened the CAD file instead of its image inspector");inspector->reject();application.processEvents();views->setCurrentIndex(0);

        {QSettings metadata(index+"/metadata.ini",QSettings::IniFormat);metadata.setValue("Directory/SubdirectoriesAsParts",true);metadata.sync();}QDir().mkpath(project+"/ZZZ-directory");first.navigate(project);for(const auto order:{Qt::AscendingOrder,Qt::DescendingOrder}){files->sortByColumn(0,order);check(files->topLevelItem(0)->data(0,Qt::UserRole+3).toBool(),"Directory group mixed with files");}QDir(project+"/ZZZ-directory").removeRecursively();{QSettings metadata(index+"/metadata.ini",QSettings::IniFormat);metadata.setValue("Directory/SubdirectoriesAsParts",false);metadata.sync();}first.navigate(project);

        first.navigate(html_directory);application.processEvents();auto* overview_stack=first.findChild<QStackedWidget*>("fileManagerOverviewStack");auto* overview_web=first.findChild<QWebEngineView*>("fileManagerOverviewWeb");check(overview_stack&&overview_web&&overview_stack->currentWidget()==overview_web,"Localized HTML did not replace the automatic overview");check(wait_until([&]{return QDir::cleanPath(overview_web->url().toLocalFile())==QDir::cleanPath(html_index+"/index_cs.html");}),"The active-language HTML overview did not take precedence over index.html");first.navigate(project);check(overview_stack->currentWidget()==first.findChild<QWidget*>("fileManagerAutomaticOverview"),"Automatic overview was not restored for a directory without HTML");

        { // Both localized artwork pages and the neutral fallback are optional.
            for(const auto& name:{"01_cs.html","01.html"}){QFile page(html_index+"/"+name);check(page.open(QIODevice::WriteOnly),"Cannot create artwork HTML fixture");page.write("<!doctype html><title>Artwork</title>");}
            first.navigate(html_directory);
            check(wait_until([&]{return overview_web->url().toLocalFile().endsWith("/index_cs.html");}),"Artwork page replaced an existing index");
            first.navigate(project);check(QFile::remove(html_index+"/index_cs.html")&&QFile::remove(html_index+"/index.html"),"Cannot retire index fixtures");
            first.navigate(html_directory);
            check(wait_until([&]{return overview_web->url().toLocalFile().endsWith("/01_cs.html");}),"Localized artwork page was not selected");
            auto russian=settings;russian.language="ru";first.reload_settings(russian);
            check(wait_until([&]{return overview_web->url().toLocalFile().endsWith("/01.html");}),"Missing artwork locale did not use the neutral fallback");
            first.reload_settings(settings);first.navigate(project);
        }
        auto files_settings=settings;files_settings.file_manager.default_view="files";
        zima::app::FileManagerTab second(files_settings,working,working);check(second.findChild<QTabWidget*>("fileManagerViews")->currentIndex()==1,"Initial Files preference was ignored");second.navigate(source);
        check(first.current_directory()==QDir::cleanPath(project)&&second.current_directory()==QDir::cleanPath(source),"File Manager tabs do not keep independent navigation state");
        first.navigate(working);check(views->count()==2&&views->isTabVisible(0)&&overview_title->text()=="working"&&overview_details->isHidden(),"Directory overview retained stale project information");
        check(files->columnCount()==5&&files->topLevelItemCount()==1,"Native data added an unconfigured column");
        check(!QFileInfo::exists(working+"/0000-index/metadata.ini"),"Browsing native data created a column definition");
        first.navigate(project);first.navigate(working);check(files->columnCount()==5,"Refresh rediscovered unconfigured columns");
        QDir().mkpath(working+"/0000-index");{QSettings metadata(working+"/0000-index/metadata.ini",QSettings::IniFormat);metadata.setValue("Directory/Parameters","native_length");metadata.sync();}
        first.navigate(project);first.navigate(working);check(files->columnCount()==6,"Configured native column was not displayed");
        const auto native_cell=files->indexFromItem(files->topLevelItem(0),5);QStyleOptionViewItem option;auto* editor=files->itemDelegate()->createEditor(files,option,native_cell);auto* line=qobject_cast<QLineEdit*>(editor);check(line,"Native parameter was not editable without metadata");line->setText("175");files->itemDelegate()->setModelData(editor,files->model(),native_cell);delete editor;
        QSettings created(working+"/0000-index/metadata.ini",QSettings::IniFormat);check(created.value("Directory/Version").toInt()==3&&created.value("Directory/Kind").toString()=="directory","Editing a native parameter did not initialize metadata v3");
        check(created.value("Parts/PANEL/native_length").toString()=="175"&&created.value("Files/PANEL/Provider").toString()=="zima-cad","New metadata did not mirror the native edit");
        first.navigate(read_only);check(files->topLevelItemCount()==1,"Read-only source did not display its native file");
        check(!(files->topLevelItem(0)->flags()&Qt::ItemIsEditable),"Read-only source exposed editable parameter cells");
        check(!QFileInfo::exists(read_only+"/0000-index/metadata.ini"),"Browsing a read-only source created metadata");
        bool read_only_properties=false;QTimer::singleShot(0,[&]{auto* menu=first.findChild<QMenu*>("fileManagerSourceMenu");if(!menu)return;auto* action=menu->findChild<QAction*>("fileManagerSourceProperties");read_only_properties=action;if(action)action->trigger();menu->close();});sources->customContextMenuRequested(sources->visualItemRect(sources->item(2)).center());application.processEvents();check(read_only_properties,"Read-only source does not offer directory properties");directory_editor=first.findChild<QDialog*>("fileManagerDirectoryEditor");check(directory_editor,"Directory properties did not open for a read-only source");en_label=directory_editor->findChild<QLineEdit*>("fileManagerDirectoryLabel_en");protection=directory_editor->findChild<QCheckBox*>("fileManagerDirectoryPreventRemoval");directory_buttons=directory_editor->findChild<QDialogButtonBox*>();check(en_label&&en_label->isEnabled()&&protection&&protection->isEnabled()&&directory_buttons&&directory_buttons->button(QDialogButtonBox::Ok)->isEnabled(),"Read-only source disabled directory metadata editing");en_label->setText("Protected library");protection->setChecked(true);directory_buttons->button(QDialogButtonBox::Ok)->click();application.processEvents();QSettings protected_metadata(read_only+"/0000-index/metadata.ini",QSettings::IniFormat);check(protected_metadata.value("Directory/Label/en").toString()=="Protected library"&&protected_metadata.value("Directory/PreventRemoval").toBool(),"Read-only source did not save editable directory metadata");
        first.navigate(working);const QString archive_a=working+"/cleanup.prtz.1",archive_b=working+"/orphan.SYMZ.999999999999999999999999",locked=working+"/locked/deeper";QDir().mkpath(locked);QDir().mkpath(working+"/locked/0000-index");{QSettings metadata(working+"/locked/0000-index/metadata.ini",QSettings::IniFormat);metadata.setValue("Directory/PreventRemoval",true);metadata.sync();}
        for(const auto& path:{archive_a,archive_b,locked+"/protected.prtz.2"}){QFile file(path);check(file.open(QIODevice::WriteOnly),"Cannot create cleanup fixture");file.write("archive");}
        bool cleanup_preview=false;QTimer::singleShot(0,[&]{auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());if(!box)return;const QString details=box->detailedText();cleanup_preview=details.contains(archive_a)&&details.contains(archive_b)&&!details.contains("protected.prtz.2")&&!details.contains("PANEL.prtz");box->reject();});first.findChild<QPushButton*>("fileManagerClean")->click();check(cleanup_preview&&QFileInfo::exists(archive_a)&&QFileInfo::exists(archive_b)&&QFileInfo::exists(locked+"/protected.prtz.2"),"Cleanup preview, inherited protection or Cancel failed");

        bool cleanup_confirmed=false;QTimer::singleShot(0,[&]{auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());if(!box)return;cleanup_confirmed=true;box->done(QMessageBox::Yes);});first.findChild<QPushButton*>("fileManagerClean")->click();check(cleanup_confirmed&&!QFileInfo::exists(archive_a)&&!QFileInfo::exists(archive_b)&&QFileInfo::exists(locked+"/protected.prtz.2")&&QFileInfo::exists(working+"/PANEL.prtz"),"Confirmed cleanup did not recycle only the disposable unprotected archive fixtures");

        const QString delete_file=working+"/recycle-test.txt",delete_folder=working+"/recycle-folder";
        QDir().mkpath(delete_folder);for(const auto& path:{delete_file,delete_folder+"/child.txt"}){QFile file(path);check(file.open(QIODevice::WriteOnly),"Cannot create recycle fixture");file.write("recyclable");}
        {QSettings metadata(working+"/0000-index/metadata.ini",QSettings::IniFormat);metadata.setValue("Directory/SubdirectoriesAsParts",true);metadata.sync();}
        first.navigate(working);files->clearSelection();for(int row=0;row<files->topLevelItemCount();++row){auto* item=files->topLevelItem(row);const QString path=item->data(0,Qt::UserRole).toString();item->setCheckState(0,path==delete_file||path==delete_folder?Qt::Checked:Qt::Unchecked);}
        bool delete_cancelled=false;QTimer::singleShot(0,[&]{auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());if(!box)return;delete_cancelled=box->text().contains("koše");box->done(QMessageBox::No);});first.findChild<QPushButton*>("fileManagerDelete")->click();check(delete_cancelled&&QFileInfo::exists(delete_file)&&QFileInfo::exists(delete_folder+"/child.txt"),"Cancelled recycle changed the selected file or folder");
        bool delete_confirmed=false;QTimer::singleShot(0,[&]{auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());if(!box)return;delete_confirmed=true;box->button(QMessageBox::Yes)->click();});first.findChild<QPushButton*>("fileManagerDelete")->click();check(delete_confirmed&&!QFileInfo::exists(delete_file)&&!QFileInfo::exists(delete_folder)&&QFileInfo::exists(working+"/PANEL.prtz"),"Delete did not recycle the selected file and folder while preserving the model");

        QString protected_before;{QSettings metadata(working+"/0000-index/metadata.ini",QSettings::IniFormat);protected_before=metadata.value("Parts/PANEL/native_length").toString();metadata.setValue("Directory/PreventParameterEdit",true);metadata.sync();}
        first.navigate(working);QTreeWidgetItem* locked_native{};for(int row=0;row<files->topLevelItemCount();++row)if(files->topLevelItem(row)->text(0)=="PANEL.prtz")locked_native=files->topLevelItem(row);check(locked_native&&!(locked_native->flags()&Qt::ItemIsEditable),"Parameter protection did not lock native cells");
        const auto locked_cell=files->indexFromItem(locked_native,5);check(!files->itemDelegate()->createEditor(files,option,locked_cell),"Protected cell created an editor");native_edit.clear();QLineEdit blocked_editor;blocked_editor.setText("999");files->itemDelegate()->setModelData(&blocked_editor,files->model(),locked_cell);check(native_edit.isEmpty(),"Protected delegate called the native edit callback");QSettings locked_metadata(working+"/0000-index/metadata.ini",QSettings::IniFormat);check(locked_metadata.value("Parts/PANEL/native_length").toString()==protected_before,"Protected delegate changed persisted parameters");
        const QString inherited_parameters=working+"/params-child";QDir().mkpath(inherited_parameters+"/0000-index");{QSettings metadata(inherited_parameters+"/0000-index/metadata.ini",QSettings::IniFormat);metadata.setValue("Directory/Parameters","native_length");metadata.sync();}{QFile child(inherited_parameters+"/PANEL.prtz");check(child.open(QIODevice::WriteOnly),"Cannot create inherited parameter fixture");child.write("synthetic-native-fixture");}first.navigate(inherited_parameters);check(files->topLevelItemCount()==1&&!(files->topLevelItem(0)->flags()&Qt::ItemIsEditable),"Parameter protection was not inherited by a child directory");
        first.navigate(working);bool parameter_properties=false;QTimer::singleShot(0,[&]{auto* menu=first.findChild<QMenu*>("fileManagerSourceMenu");if(!menu)return;auto* action=menu->findChild<QAction*>("fileManagerSourceProperties");parameter_properties=action;if(action)action->trigger();menu->close();});sources->customContextMenuRequested(sources->visualItemRect(sources->item(0)).center());check(parameter_properties,"Working directory properties are unavailable");directory_editor=first.findChild<QDialog*>("fileManagerDirectoryEditor");auto* parameter_protection=directory_editor?directory_editor->findChild<QCheckBox*>("fileManagerDirectoryPreventParameterEdit"):nullptr;check(parameter_protection&&parameter_protection->isChecked(),"Directory properties did not expose stored parameter protection");parameter_protection->setChecked(false);directory_editor->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();first.navigate(inherited_parameters);check(files->topLevelItem(0)->flags()&Qt::ItemIsEditable,"Removing parameter protection did not restore child editing");first.navigate(working);
        first.set_working_directory(source);check(working_field->text()==QDir::toNativeSeparators(source)&&second.current_directory()==QDir::cleanPath(source),"Updating the working-directory field changed another tab navigation");first.set_working_directory(working);
        const QString preferences=fixture.filePath("preferences");QDir().mkpath(preferences);
        auto persisted_settings=settings;persisted_settings.base_config_path=preferences+"/config.ini";
        {QSettings layout(preferences+"/file-manager-layout.ini",QSettings::IniFormat);layout.setValue("Table/Order",QStringList{"type","file","size","modified"});layout.setValue("Widths/file",210);layout.setValue("Thumbnails/Size",48);layout.sync();}
        zima::app::FileManagerTab migrated(persisted_settings,working,working);
        auto* migrated_files=migrated.findChild<QTreeWidget*>("fileManagerFiles");auto* migrated_size=migrated.findChild<QSlider*>("fileManagerThumbnailSize");
        check(migrated_size->value()==96&&migrated_files->columnWidth(0)==210,"Preview migration lost filename width or retained the old small default");
        check(migrated_files->header()->logicalIndex(0)==2&&migrated_files->header()->logicalIndex(1)==0&&migrated_files->header()->logicalIndex(2)==1,"Preview migration did not preserve semantic column order");
        migrated_size->setValue(144);migrated.navigate(working);
        zima::app::FileManagerTab restored(persisted_settings,working,working);
        check(restored.findChild<QSlider*>("fileManagerThumbnailSize")->value()==144&&restored.findChild<QTreeWidget*>("fileManagerFiles")->columnWidth(1)==148,"Preview size/width was not restored in a new tab");
        for(const auto background:{QColor(Qt::white),QColor("#252525")}) {
            QImage input(100,100,QImage::Format_ARGB32);input.fill(background);
            for(int y=40;y<60;++y)for(int x=35;x<65;++x)input.setPixelColor(x,y,Qt::red);
            const auto compact=zima::app::compact_thumbnail_image(input);
            check(compact.size()==QSize(34,24)&&compact.pixelColor(2,2)==QColor(Qt::red),"Thumbnail retained empty margin or clipped foreground");
            if(background!=QColor(Qt::white)) {
                input.setPixelColor(92,82,QColor(background.red()+1,background.green(),background.blue()));
                check(zima::app::compact_thumbnail_image(input).width()==62,"Thumbnail clipped a foreground pixel close to the background");
            }
        }
        for(bool dark:{false,true}) {
            QImage input(100,100,QImage::Format_ARGB32);
            for(int y=0;y<100;++y){const int channel=dark?80-y/2:180+y/2;for(int x=0;x<100;++x)input.setPixelColor(x,y,QColor(channel,channel,channel));}
            for(int y=40;y<60;++y)for(int x=35;x<65;++x)input.setPixelColor(x,y,Qt::red);
            const auto compact=zima::app::compact_thumbnail_image(input);
            check(compact.size()==QSize(34,24)&&compact.pixelColor(2,2)==QColor(Qt::red),"Thumbnail retained the exact vertical View gradient or clipped foreground");
            auto faint=input;const auto background=faint.pixelColor(0,82);faint.setPixelColor(92,82,QColor(background.red()+1,background.green(),background.blue()));
            check(zima::app::compact_thumbnail_image(faint).width()==62,"Gradient crop clipped a near-background foreground pixel");
            auto edge=input;edge.setPixelColor(0,50,Qt::red);
            check(zima::app::compact_thumbnail_image(edge)==edge,"Gradient crop changed an image with foreground on its side border");
            edge=input;edge.setPixelColor(50,0,Qt::red);
            check(zima::app::compact_thumbnail_image(edge)==edge,"Gradient crop changed an image with foreground on its top border");
            edge=input;for(int x=0;x<100;++x)edge.setPixelColor(x,20,Qt::black);
            check(zima::app::compact_thumbnail_image(edge)==edge,"Gradient detection accepted a nonmonotone image background");
        }
        const QString order=working+"/sort-proof";
        for(const auto name:{QString("01-zeta"),QString("02-alpha")}) {
            const QString index=order+"/"+name+"/0000-index";QDir().mkpath(index);
            QSettings localized(index+"/metadata.ini",QSettings::IniFormat);
            localized.setValue("Directory/Label/cs",name.startsWith("01")?"Zeta":"Alfa");
            localized.setValue("Directory/Label/en",name.startsWith("01")?"Zulu":"Alpha");localized.sync();
        }
        first.navigate(order);files->sortItems(0,Qt::AscendingOrder);check_uniform_rows();
        check(files->topLevelItemCount()==2&&files->topLevelItem(0)->text(0)=="Zeta"&&files->topLevelItem(1)->text(0)=="Alfa","Directory table sorts translated labels instead of actual filenames");
        files->sortItems(0,Qt::DescendingOrder);check(files->topLevelItem(0)->text(0)=="Alfa","Descending filename order is incorrect");
        auto english=settings;english.language="en";first.reload_settings(english);first.navigate(order);files=first.findChild<QTreeWidget*>("fileManagerFiles");files->sortItems(0,Qt::AscendingOrder);
        check(files->topLevelItem(0)->text(0)=="Zulu","Changing language reordered directories");
        std::cout<<"File Manager sources, project detection, overview, parameters and independent tabs passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
