#include "file_manager_tab.hpp"
#include "file_entry_geometry.hpp"
#include "thumbnail_image.hpp"

#include <QAction>
#include <QPersistentModelIndex>
#include <QHeaderView>
#include <QSlider>
#include <QMouseEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QMessageBox>
#include <QStyleOptionViewItem>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QListWidget>
#include <QLineEdit>
#include <QLabel>
#include <QImage>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QStackedWidget>
#include <QTreeView>
#include <QTreeWidget>
#include <QWebEngineView>

#include <iostream>
#include <stdexcept>

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
template<class Predicate> bool wait_until(Predicate predicate,int timeout_ms=3000){QElapsedTimer timer;timer.start();while(timer.elapsed()<timeout_ms){QApplication::processEvents();if(predicate())return true;QThread::msleep(10);}return predicate();}
}

int main(int argc,char** argv){
    QApplication application(argc,argv);
    try{
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
            check(directories==312,"Factory directory label inventory differs from 312 directories");
            std::cout<<"Native Qt reading of all 1560 factory directory labels passed\n";
        }
        QTemporaryDir fixture(QDir::current().filePath("file-manager-test-XXXXXX"));check(fixture.isValid(),"Cannot create synthetic fixture");
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
        check(QDir().mkpath(project+"/SUBDIR/0000-index"),"Cannot create child preview fixture");
        {QImage preview(100,60,QImage::Format_RGB32);preview.fill(Qt::red);check(preview.save(project+"/SUBDIR/0000-index/01_cs.png"),"Cannot write localized preview");preview.fill(Qt::blue);check(preview.save(project+"/SUBDIR/0000-index/logo.png"),"Cannot write child branding");}

        zima::app::ApplicationSettings settings;settings.language="cs";settings.file_manager.default_view="overview";settings.file_manager.thumbnail_size=96;
        settings.file_manager.sources.push_back({"synthetic","Testovací knihovna",source,source,true,false,false});
        settings.file_manager.sources.push_back({"read-only","Knihovna jen pro čtení",read_only,read_only,true,true,false});
        zima::app::FileManagerTab first(settings,working,project);first.resize(1100,700);first.show();application.processEvents();QString native_edit,opened_file;
        first.open_file_requested=[&](const QString& path){opened_file=path;};
        first.native_parameters_requested=[](const QString& path){zima::app::FileManagerNativeParameters data;if(path.endsWith(".prtz",Qt::CaseInsensitive)){data.order={"native_length"};data.values.insert("native_length","125");data.labels["native_length"].insert("cs","Nativní délka");}return data;};
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
        {zima::app::FileManagerTab retained(settings,working,project);auto* retained_views=retained.findChild<QTabWidget*>("fileManagerViews");retained_views->setCurrentIndex(1);retained.set_working_directory(working);check(retained_views->currentIndex()==1,"Working-directory refresh reset the user's Files view");retained.navigate(project,false);check(retained_views->currentIndex()==1,"Same-directory refresh reset the user's Files view");retained.reload_settings(settings);check(retained_views->currentIndex()==1,"Settings reload reset the user's Files view");}
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
        check(!preview_row->icon(0).isNull()&&preview_row->icon(0).cacheKey()!=qvariant_cast<QIcon>(preview_index.data(Qt::DecorationRole)).cacheKey(),"Thumbnail replaced the file-type icon");
        for(int row=0;row<files->topLevelItemCount();++row){const auto index=files->indexFromItem(files->topLevelItem(row),zima::app::file_preview_column);if(qvariant_cast<QIcon>(index.data(Qt::DecorationRole)).isNull())continue;check(geometry.geometry(files,index).thumbnail.center().x()==preview_point.x(),"Preview images do not share one vertical line");}
        QStyleOptionViewItem preview_option;check(!files->itemDelegate()->createEditor(files,preview_option,preview_index),"Preview cell created a parameter editor");
        const auto name_geometry=geometry.geometry(files,files->indexFromItem(preview_row,0));const auto type_icon=files->style()->subElementRect(QStyle::SE_ItemViewItemDecoration,&name_geometry.content,files);const auto filename=files->style()->subElementRect(QStyle::SE_ItemViewItemText,&name_geometry.content,files);check(type_icon.right()<filename.left()&&filename.right()<preview_geometry.thumbnail.left(),"Type icon, filename and thumbnail are not in their independent visual order");
        check(first.findChild<QSlider*>("fileManagerThumbnailSize")->width()==300,"Thumbnail slider did not gain finer pointer control");
        QHelpEvent help(QEvent::ToolTip,preview_point,files->viewport()->mapToGlobal(preview_point));QApplication::sendEvent(files->viewport(),&help);check(wait_until([]{return QToolTip::isVisible()&&QToolTip::text().contains("data:image/png;base64,");},1500),"Hover did not show the enlarged thumbnail after native tooltip animation");QToolTip::hideText();opened_file.clear();QMouseEvent double_click(QEvent::MouseButtonDblClick,preview_point,files->viewport()->mapToGlobal(preview_point),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(files->viewport(),&double_click);application.processEvents();auto* inspector=first.findChild<QDialog*>("fileManagerImagePreview");check(inspector&&opened_file.isEmpty(),"Thumbnail double-click opened the CAD file instead of its image inspector");inspector->reject();application.processEvents();views->setCurrentIndex(0);

        {QSettings metadata(index+"/metadata.ini",QSettings::IniFormat);metadata.setValue("Directory/SubdirectoriesAsParts",true);metadata.sync();}QDir().mkpath(project+"/ZZZ-directory");first.navigate(project);for(const auto order:{Qt::AscendingOrder,Qt::DescendingOrder}){files->sortByColumn(0,order);check(files->topLevelItem(0)->data(0,Qt::UserRole+3).toBool(),"Directory group mixed with files");}QDir(project+"/ZZZ-directory").removeRecursively();{QSettings metadata(index+"/metadata.ini",QSettings::IniFormat);metadata.setValue("Directory/SubdirectoriesAsParts",false);metadata.sync();}first.navigate(project);

        first.navigate(html_directory);application.processEvents();auto* overview_stack=first.findChild<QStackedWidget*>("fileManagerOverviewStack");auto* overview_web=first.findChild<QWebEngineView*>("fileManagerOverviewWeb");check(overview_stack&&overview_web&&overview_stack->currentWidget()==overview_web,"Localized HTML did not replace the automatic overview");check(wait_until([&]{return QDir::cleanPath(overview_web->url().toLocalFile())==QDir::cleanPath(html_index+"/index_cs.html");}),"The active-language HTML overview did not take precedence over index.html");first.navigate(project);check(overview_stack->currentWidget()==first.findChild<QWidget*>("fileManagerAutomaticOverview"),"Automatic overview was not restored for a directory without HTML");

        auto files_settings=settings;files_settings.file_manager.default_view="files";
        zima::app::FileManagerTab second(files_settings,working,working);check(second.findChild<QTabWidget*>("fileManagerViews")->currentIndex()==1,"Initial Files preference was ignored");second.navigate(source);
        check(first.current_directory()==QDir::cleanPath(project)&&second.current_directory()==QDir::cleanPath(source),"File Manager tabs do not keep independent navigation state");
        first.navigate(working);check(views->count()==2&&views->isTabVisible(0)&&overview_title->text()=="working"&&overview_details->isHidden(),"Directory overview retained stale project information");
        check(files->columnCount()==6&&files->topLevelItemCount()==1,"Native parameters were not exposed without metadata");
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
        const QString inherited_parameters=working+"/params-child";QDir().mkpath(inherited_parameters);{QFile child(inherited_parameters+"/PANEL.prtz");check(child.open(QIODevice::WriteOnly),"Cannot create inherited parameter fixture");child.write("synthetic-native-fixture");}first.navigate(inherited_parameters);check(files->topLevelItemCount()==1&&!(files->topLevelItem(0)->flags()&Qt::ItemIsEditable),"Parameter protection was not inherited by a child directory");
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
