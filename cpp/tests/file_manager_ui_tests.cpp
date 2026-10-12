#include "file_manager_tab.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QListWidget>
#include <QLineEdit>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolBox>
#include <QTreeWidget>

#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void touch(const QString& path){QFile file(path);check(file.open(QIODevice::WriteOnly),"Cannot create synthetic file");file.write("synthetic");}
}

int main(int argc,char** argv){
    QApplication application(argc,argv);
    try{
        QTemporaryDir fixture(QDir::current().filePath("file-manager-test-XXXXXX"));check(fixture.isValid(),"Cannot create synthetic fixture");
        const QString working=fixture.filePath("working"),source=fixture.filePath("library"),project=source+"/DEMO-001",index=project+"/0000-index";
        QDir root(fixture.path());
        if(!root.mkdir("working"))throw std::runtime_error(("Cannot create synthetic working directory: "+fixture.path()).toStdString());
        if(!root.mkpath("library/DEMO-001/0000-index"))throw std::runtime_error(("Cannot create synthetic project directory: "+fixture.path()).toStdString());
        touch(working+"/PANEL.prtz");touch(project+"/BRACKET.prtz");touch(project+"/ASSEMBLY.asmz");touch(project+"/notes.txt");
        {
            QSettings metadata(index+"/metadata.ini",QSettings::IniFormat);
            metadata.setValue("Directory/Version",3);metadata.setValue("Directory/Kind","project");metadata.setValue("Directory/Id","synthetic-project");
            metadata.setValue("Directory/Label\\cs","Ukázkový projekt");metadata.setValue("Project/Number","DEMO-001");metadata.setValue("Project/Title\\cs","Ukázkový projekt");
            metadata.setValue("Directory/Parameters","material,note");metadata.setValue("Parameters/material/Label\\cs","Materiál");metadata.setValue("Parameters/note/Label\\cs","Poznámka");
            metadata.setValue("Parts/BRACKET\\material","S355");metadata.sync();check(metadata.status()==QSettings::NoError,"Cannot write synthetic metadata");
        }

        zima::app::ApplicationSettings settings;settings.language="cs";settings.file_manager.default_view="overview";settings.file_manager.thumbnail_size=96;
        settings.file_manager.sources.push_back({"synthetic","Testovací knihovna",source,source,true,false,false});
        zima::app::FileManagerTab first(settings,working,project);first.resize(1100,700);QString native_edit;
        first.native_parameters_requested=[](const QString& path){zima::app::FileManagerNativeParameters data;if(path.endsWith(".prtz",Qt::CaseInsensitive)){data.order={"native_length"};data.values.insert("native_length","125");data.labels["native_length"].insert("cs","Nativní délka");}return data;};
        first.native_parameter_edit_requested=[&](const QString& path,const QString& parameter,const QString& value){native_edit=QFileInfo(path).fileName()+QLatin1Char(':')+parameter+QLatin1Char('=')+value;return QString{};};
        first.navigate(working);first.navigate(project);
        auto* sources=first.findChild<QToolBox*>("fileManagerSources");auto* views=first.findChild<QTabWidget*>("fileManagerViews");
        auto* overview=first.findChild<QListWidget*>("fileManagerOverview");auto* files=first.findChild<QTreeWidget*>("fileManagerFiles");
        check(sources&&sources->count()==2,"Working directory and configured source are not separate source pages");
        check(views&&views->isTabVisible(0),"Project view is not available for metadata v3 project");
        check(overview&&overview->count()==3,"Overview did not load synthetic directory entries or exposed 0000-index");
        check(files&&files->topLevelItemCount()==3&&files->columnCount()==9,"File table did not load metadata and native parameter columns");
        bool bracket_found=false;for(int row=0;row<files->topLevelItemCount();++row){auto* item=files->topLevelItem(row);if(item->text(2)!="BRACKET.prtz")continue;bracket_found=true;check(item->text(6)=="S355"&&item->text(8)=="125","Metadata or native parameter was not loaded");const auto cell=files->indexFromItem(item,8);QStyleOptionViewItem option;auto* editor=files->itemDelegate()->createEditor(files,option,cell);auto* line=qobject_cast<QLineEdit*>(editor);check(line,"Parameter delegate did not create an editor");line->setText("150");files->itemDelegate()->setModelData(editor,files->model(),cell);delete editor;}
        check(bracket_found,"Native Part is missing from the file table");
        check(native_edit=="BRACKET.prtz:native_length=150","Native parameter edit was not routed to the document provider");
        QSettings edited(index+"/metadata.ini",QSettings::IniFormat);if(edited.value("Parts/BRACKET\\native_length").toString()!="150"||edited.value("Files/BRACKET/Provider").toString()!="zima-cad"){
            for(const auto& key:edited.allKeys())std::cerr<<key.toStdString()<<'='<<edited.value(key).toString().toStdString()<<'\n';
            throw std::runtime_error("Native parameter mirror or provider was not saved");
        }

        zima::app::FileManagerTab second(settings,working,working);second.navigate(source);
        check(first.current_directory()==QDir::cleanPath(project)&&second.current_directory()==QDir::cleanPath(source),"File Manager tabs do not keep independent navigation state");
        first.navigate(working);check(!views->isTabVisible(0),"Project view remained visible outside a project directory");
        check(files->columnCount()==7&&files->topLevelItemCount()==1,"Native parameters were not exposed without metadata");
        const auto native_cell=files->indexFromItem(files->topLevelItem(0),6);QStyleOptionViewItem option;auto* editor=files->itemDelegate()->createEditor(files,option,native_cell);auto* line=qobject_cast<QLineEdit*>(editor);check(line,"Native parameter was not editable without metadata");line->setText("175");files->itemDelegate()->setModelData(editor,files->model(),native_cell);delete editor;
        QSettings created(working+"/0000-index/metadata.ini",QSettings::IniFormat);check(created.value("Directory/Version").toInt()==3&&created.value("Directory/Kind").toString()=="directory","Editing a native parameter did not initialize metadata v3");
        check(created.value("Parts/PANEL\\native_length").toString()=="175"&&created.value("Files/PANEL/Provider").toString()=="zima-cad","New metadata did not mirror the native edit");
        std::cout<<"File Manager sources, project detection, overview, parameters and independent tabs passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
