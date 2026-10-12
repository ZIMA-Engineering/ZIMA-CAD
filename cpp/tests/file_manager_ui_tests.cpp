#include "file_manager_tab.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
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
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void touch(const QString& path){QFile file(path);check(file.open(QIODevice::WriteOnly),"Cannot create synthetic file");file.write("synthetic");}
template<class Predicate> bool wait_until(Predicate predicate,int timeout_ms=3000){QElapsedTimer timer;timer.start();while(timer.elapsed()<timeout_ms){QApplication::processEvents();if(predicate())return true;QThread::msleep(10);}return predicate();}
}

int main(int argc,char** argv){
    QApplication application(argc,argv);
    try{
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
            metadata.setValue("Directory/Label\\cs","Ukázkový projekt");metadata.setValue("Project/Number","DEMO-001");metadata.setValue("Project/Title\\cs","Ukázkový projekt");
            metadata.setValue("Directory/SubdirectoriesAsParts",false);
            metadata.setValue("Directory/Parameters","material,note");metadata.setValue("Parameters/material/Label\\cs","Materiál");metadata.setValue("Parameters/note/Label\\cs","Poznámka");
            metadata.setValue("Parts/BRACKET\\material","S355");metadata.sync();check(metadata.status()==QSettings::NoError,"Cannot write synthetic metadata");
        }
        {QSettings metadata(html_index+"/metadata.ini",QSettings::IniFormat);metadata.setValue("Directory/Version",3);metadata.setValue("Directory/Kind","directory");metadata.setValue("Directory/Id","synthetic-html");metadata.setValue("Directory/Label\\cs","HTML tabulka");metadata.setValue("Directory/LogoMode","logo");metadata.setValue("Directory/Logo","logo.svg");metadata.sync();}
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
        check(sources&&sources->count()==3&&source_trees&&source_trees->count()==3,"Working directory and configured sources are not separate source pages");
        auto* source_tree=first.findChild<QTreeView*>("fileManagerSourceTree_synthetic");check(source_tree,"Configured source tree is missing");
        QModelIndex project_index,html_tree_index;for(int attempt=0;attempt<50&&(!project_index.isValid()||!html_tree_index.isValid());++attempt){application.processEvents();for(int row=0;row<source_tree->model()->rowCount(source_tree->rootIndex());++row){const auto candidate=source_tree->model()->index(row,0,source_tree->rootIndex());const auto accessible=candidate.data(Qt::AccessibleTextRole).toString();if(candidate.data().toString()=="Ukázkový projekt")project_index=candidate;if(accessible=="HTML tabulka")html_tree_index=candidate;}if(!project_index.isValid()||!html_tree_index.isValid())QThread::msleep(10);}check(project_index.isValid(),"Source tree did not use the localized metadata directory label");source_tree->expand(project_index);application.processEvents();
        check(html_tree_index.isValid()&&html_tree_index.data().toString().isEmpty()&&html_tree_index.data(Qt::AccessibleTextRole).toString()=="HTML tabulka","Logo-only directory did not hide only the visual label");
        bool exposed_index=false;for(int row=0;row<source_tree->model()->rowCount(project_index);++row)if(source_tree->model()->index(row,0,project_index).data().toString().compare("0000-index",Qt::CaseInsensitive)==0)exposed_index=true;
        check(!exposed_index,"Source tree exposed the internal 0000-index directory");
        sources->setCurrentRow(0);sources->itemClicked(sources->item(0));check(first.current_directory()==QDir::cleanPath(working),"Clicking the source itself did not open its root");
        sources->setCurrentRow(1);sources->itemClicked(sources->item(1));check(first.current_directory()==QDir::cleanPath(source),"Clicking a configured source did not open its root");check(files->topLevelItemCount()==2&&files->topLevelItem(0)->text(2)=="Ukázkový projekt","Files view did not use the localized metadata directory label");
        bool source_properties_action=false;QTimer::singleShot(0,[&]{auto* menu=first.findChild<QMenu*>("fileManagerSourceMenu");if(!menu)return;auto* action=menu->findChild<QAction*>("fileManagerSourceProperties");source_properties_action=action;if(action)action->trigger();menu->close();});sources->customContextMenuRequested(sources->visualItemRect(sources->item(1)).center());application.processEvents();check(source_properties_action,"Configured source does not offer directory properties");auto* directory_editor=first.findChild<QDialog*>("fileManagerDirectoryEditor");check(directory_editor,"Directory properties editor did not open for the configured source");auto* en_label=directory_editor->findChild<QLineEdit*>("fileManagerDirectoryLabel_en");auto* subdirectories=directory_editor->findChild<QCheckBox*>("fileManagerDirectorySubdirectories");auto* protection=directory_editor->findChild<QCheckBox*>("fileManagerDirectoryPreventRemoval");auto* parameters=directory_editor->findChild<QTableWidget*>("fileManagerDirectoryParameters");check(en_label&&subdirectories&&protection&&parameters,"Directory properties fields are incomplete");en_label->setText("Engineering library");subdirectories->setChecked(true);protection->setChecked(true);parameters->insertRow(0);parameters->setItem(0,0,new QTableWidgetItem("note"));parameters->setItem(0,1,new QTableWidgetItem("Poznámka"));for(int column=2;column<6;++column)parameters->setItem(0,column,new QTableWidgetItem("Note"));auto* directory_buttons=directory_editor->findChild<QDialogButtonBox*>();check(directory_buttons&&directory_buttons->button(QDialogButtonBox::Ok),"Directory properties confirmation is missing");directory_buttons->button(QDialogButtonBox::Ok)->click();application.processEvents();QSettings directory_metadata(source+"/0000-index/metadata.ini",QSettings::IniFormat);check(directory_metadata.value("Directory/Label\\en").toString()=="Engineering library"&&directory_metadata.value("Directory/SubdirectoriesAsParts").toBool()&&directory_metadata.value("Directory/PreventRemoval").toBool()&&directory_metadata.value("Directory/Parameters").toString()=="note","Directory properties were not persisted");check(!first.findChild<QPushButton*>("fileManagerMove")->isEnabled()&&!first.findChild<QPushButton*>("fileManagerDelete")->isEnabled(),"Protected directory still enabled destructive file operations");
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
        const auto capture=[&](const QString& name){application.processEvents();if(!capture_directory.isEmpty()){QDir().mkpath(capture_directory);check(first.grab().save(QDir(capture_directory).filePath(name)),"Cannot save overview verification image");}};
        QPalette light=original_palette;light.setColor(QPalette::Window,QColor("#efefef"));light.setColor(QPalette::Button,QColor("#efefef"));light.setColor(QPalette::ButtonText,Qt::black);light.setColor(QPalette::Base,Qt::white);light.setColor(QPalette::AlternateBase,QColor("#f6f6f6"));light.setColor(QPalette::Text,Qt::black);light.setColor(QPalette::WindowText,Qt::black);light.setColor(QPalette::Mid,QColor("#b0b0b0"));application.setPalette(light);
        capture("overview-light.png");QPalette dark=light;dark.setColor(QPalette::Window,QColor("#202020"));dark.setColor(QPalette::Button,QColor("#303030"));dark.setColor(QPalette::ButtonText,Qt::white);dark.setColor(QPalette::Base,QColor("#252525"));dark.setColor(QPalette::AlternateBase,QColor("#303030"));dark.setColor(QPalette::Text,Qt::white);dark.setColor(QPalette::WindowText,Qt::white);dark.setColor(QPalette::Mid,QColor("#606060"));application.setPalette(dark);capture("overview-dark.png");check(overview->palette().color(QPalette::Text)==QColor(Qt::white),"Overview did not follow a live dark palette");application.setPalette(light);capture("overview-light-restored.png");check(overview->palette().color(QPalette::Text)==QColor(Qt::black),"Overview did not restore the live light palette");application.setPalette(original_palette);
        overview->itemClicked(overview->item(0));check(first.current_directory()==QDir::cleanPath(project+"/SUBDIR"),"Single-clicking an overview tile did not navigate");first.navigate(project);
        check(files&&files->topLevelItemCount()==3&&files->columnCount()==9,"File table did not load metadata and native parameter columns");
        bool bracket_found=false;for(int row=0;row<files->topLevelItemCount();++row){auto* item=files->topLevelItem(row);if(item->text(2)!="BRACKET.prtz")continue;bracket_found=true;check(item->text(6)=="S355"&&item->text(8)=="125","Metadata or native parameter was not loaded");files->itemDoubleClicked(item,8);check(opened_file.isEmpty(),"Double-clicking a parameter cell opened the file");files->itemDoubleClicked(item,2);check(opened_file==QDir::cleanPath(project+"/BRACKET.prtz"),"Double-clicking the file-name cell did not open the file");const auto cell=files->indexFromItem(item,8);QStyleOptionViewItem option;auto* editor=files->itemDelegate()->createEditor(files,option,cell);auto* line=qobject_cast<QLineEdit*>(editor);check(line,"Parameter delegate did not create an editor");line->setText("150");files->itemDelegate()->setModelData(editor,files->model(),cell);delete editor;}
        check(bracket_found,"Native Part is missing from the file table");
        check(files->editTriggers()==QAbstractItemView::SelectedClicked,"Parameter cells do not use the ZIMA-Parts selected-click editing behavior");
        auto* name_filter=first.findChild<QLineEdit*>("fileManagerColumnFilter_2");auto* type_filter=first.findChild<QLineEdit*>("fileManagerColumnFilter_3");check(name_filter&&type_filter,"Per-column file filters are missing");
        name_filter->setText("BRACKET");check(!files->topLevelItem(0)->isHidden()||!files->topLevelItem(1)->isHidden()||!files->topLevelItem(2)->isHidden(),"Name filter hid every matching file");int visible=0;for(int row=0;row<files->topLevelItemCount();++row)visible+=!files->topLevelItem(row)->isHidden();check(visible==1,"Name column filter did not isolate the matching file");name_filter->clear();type_filter->setText("ASMZ");visible=0;for(int row=0;row<files->topLevelItemCount();++row)visible+=!files->topLevelItem(row)->isHidden();check(visible==1,"Type column filter did not isolate the assembly");type_filter->clear();
        check(native_edit=="BRACKET.prtz:native_length=150","Native parameter edit was not routed to the document provider");
        QSettings edited(index+"/metadata.ini",QSettings::IniFormat);if(edited.value("Parts/BRACKET\\native_length").toString()!="150"||edited.value("Files/BRACKET/Provider").toString()!="zima-cad"){
            for(const auto& key:edited.allKeys())std::cerr<<key.toStdString()<<'='<<edited.value(key).toString().toStdString()<<'\n';
            throw std::runtime_error("Native parameter mirror or provider was not saved");
        }

        first.navigate(html_directory);application.processEvents();auto* overview_stack=first.findChild<QStackedWidget*>("fileManagerOverviewStack");auto* overview_web=first.findChild<QWebEngineView*>("fileManagerOverviewWeb");check(overview_stack&&overview_web&&overview_stack->currentWidget()==overview_web,"Localized HTML did not replace the automatic overview");check(wait_until([&]{return QDir::cleanPath(overview_web->url().toLocalFile())==QDir::cleanPath(html_index+"/index_cs.html");}),"The active-language HTML overview did not take precedence over index.html");first.navigate(project);check(overview_stack->currentWidget()==first.findChild<QWidget*>("fileManagerAutomaticOverview"),"Automatic overview was not restored for a directory without HTML");

        auto files_settings=settings;files_settings.file_manager.default_view="files";
        zima::app::FileManagerTab second(files_settings,working,working);check(second.findChild<QTabWidget*>("fileManagerViews")->currentIndex()==1,"Initial Files preference was ignored");second.navigate(source);
        check(first.current_directory()==QDir::cleanPath(project)&&second.current_directory()==QDir::cleanPath(source),"File Manager tabs do not keep independent navigation state");
        first.navigate(working);check(views->count()==2&&views->isTabVisible(0)&&overview_title->text()=="working"&&overview_details->isHidden(),"Directory overview retained stale project information");
        check(files->columnCount()==7&&files->topLevelItemCount()==1,"Native parameters were not exposed without metadata");
        const auto native_cell=files->indexFromItem(files->topLevelItem(0),6);QStyleOptionViewItem option;auto* editor=files->itemDelegate()->createEditor(files,option,native_cell);auto* line=qobject_cast<QLineEdit*>(editor);check(line,"Native parameter was not editable without metadata");line->setText("175");files->itemDelegate()->setModelData(editor,files->model(),native_cell);delete editor;
        QSettings created(working+"/0000-index/metadata.ini",QSettings::IniFormat);check(created.value("Directory/Version").toInt()==3&&created.value("Directory/Kind").toString()=="directory","Editing a native parameter did not initialize metadata v3");
        check(created.value("Parts/PANEL\\native_length").toString()=="175"&&created.value("Files/PANEL/Provider").toString()=="zima-cad","New metadata did not mirror the native edit");
        first.navigate(read_only);check(files->topLevelItemCount()==1,"Read-only source did not display its native file");
        check(!(files->topLevelItem(0)->flags()&Qt::ItemIsEditable),"Read-only source exposed editable parameter cells");
        check(!QFileInfo::exists(read_only+"/0000-index/metadata.ini"),"Browsing a read-only source created metadata");
        bool read_only_properties=false;QTimer::singleShot(0,[&]{auto* menu=first.findChild<QMenu*>("fileManagerSourceMenu");if(!menu)return;auto* action=menu->findChild<QAction*>("fileManagerSourceProperties");read_only_properties=action;if(action)action->trigger();menu->close();});sources->customContextMenuRequested(sources->visualItemRect(sources->item(2)).center());application.processEvents();check(read_only_properties,"Read-only source does not offer directory properties");directory_editor=first.findChild<QDialog*>("fileManagerDirectoryEditor");check(directory_editor,"Directory properties did not open for a read-only source");en_label=directory_editor->findChild<QLineEdit*>("fileManagerDirectoryLabel_en");protection=directory_editor->findChild<QCheckBox*>("fileManagerDirectoryPreventRemoval");directory_buttons=directory_editor->findChild<QDialogButtonBox*>();check(en_label&&en_label->isEnabled()&&protection&&protection->isEnabled()&&directory_buttons&&directory_buttons->button(QDialogButtonBox::Ok)->isEnabled(),"Read-only source disabled directory metadata editing");en_label->setText("Protected library");protection->setChecked(true);directory_buttons->button(QDialogButtonBox::Ok)->click();application.processEvents();QSettings protected_metadata(read_only+"/0000-index/metadata.ini",QSettings::IniFormat);check(protected_metadata.value("Directory/Label\\en").toString()=="Protected library"&&protected_metadata.value("Directory/PreventRemoval").toBool(),"Read-only source did not save editable directory metadata");
        std::cout<<"File Manager sources, project detection, overview, parameters and independent tabs passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
