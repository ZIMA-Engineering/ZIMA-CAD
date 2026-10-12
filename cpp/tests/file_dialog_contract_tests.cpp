#include "file_dialog.hpp"
#include "resource_icon.hpp"
#include <QApplication>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QAbstractProxyModel>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QPalette>
#include <QTemporaryDir>
#include <QTimer>
#include <QDir>
#include <QSettings>
#include <QWidget>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
}
int main(int argc,char** argv) {
    QApplication application(argc,argv);
    try {
        QTemporaryDir directory;
        check(directory.isValid(),"Cannot create file-dialog fixture");
        for(int i=0;i<512;++i)
            std::ofstream((directory.path()+QString("/part-%1.prtz").arg(i)).toStdString())<<"{}";
        const auto target=directory.path()+"/selected.PRTZ";
        const QString library=directory.filePath("structural");QDir().mkpath(library+"/0000-index");QSettings metadata(library+"/0000-index/metadata.ini",QSettings::IniFormat);
        const QStringList languages{"cs","en","de","fr","ru"},labels{"Konstrukční oceli","Structural steels","Baustähle","Aciers de construction","Конструкционные стали"};
        for(int index=0;index<languages.size();++index)metadata.setValue("Directory/Label/"+languages[index],labels[index]);metadata.sync();
        std::ofstream(target.toStdString())<<"{}";
        QWidget parent;parent.resize(600,400);parent.show();
        const auto original=application.palette();
        for(int iteration=0;iteration<6;++iteration) {
            const int language=iteration%5;application.setProperty("zimaLanguage",languages[language]);
            auto palette=original;
            if(iteration%3==1) {
                palette.setColor(QPalette::Window,QColor("#25292e"));
                palette.setColor(QPalette::Base,QColor("#25292e"));
                palette.setColor(QPalette::Text,Qt::white);
                palette.setColor(QPalette::WindowText,Qt::white);
            }
            application.setPalette(palette);
            QTimer heartbeat;heartbeat.setInterval(20);
            QElapsedTimer elapsed;elapsed.start();
            int beats=0;bool icon=false,visited=false,label_seen=false;
            QObject::connect(&heartbeat,&QTimer::timeout,[&] {
                auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
                if(!dialog)return;
                ++beats;
                auto* proxy=dialog->proxyModel();
                auto* files=proxy?qobject_cast<QFileSystemModel*>(proxy->sourceModel()):nullptr;
                if(files) {
                    const auto folder=proxy->mapFromSource(files->index(library));if(folder.isValid()){label_seen=folder.data(Qt::DisplayRole).toString()==labels[language];check(files->filePath(proxy->mapToSource(folder))==library,"Localized label changed the real directory path");}
                    const auto index=files->index(target);
                    const auto actual=proxy->mapFromSource(index);
                    if(actual.isValid()) {
                        const auto value=qvariant_cast<QIcon>(actual.data(Qt::DecorationRole));
                        icon=!value.isNull()&&!value.pixmap(QSize(20,20)).isNull();
                        // Exercise the application's palette-aware cache while
                        // the filesystem gatherer continues filling the model.
                        check(!zima::app::resource_icon("part").isNull(),"Part icon is absent");
                        visited=true;
                    }
                }
                if(elapsed.elapsed()<2000)return;
                if(iteration%2==0)dialog->reject();
                else {
                    dialog->selectFile(target);
                    if(auto* edit=dialog->findChild<QLineEdit*>("fileNameEdit"))edit->setText(target);
                    QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
                }
            });
            heartbeat.start();
            const auto selected=zima::app::open_file(&parent,"File selection regression",directory.path(),"Part (*.prtz *.PRTZ)");
            heartbeat.stop();
            check(visited&&icon&&beats>=20,"File selection did not remain responsive while idle");check(label_seen,"Open dialog did not use localized directory metadata");
            check(iteration%2==0?selected.isEmpty():selected==target,"File selection changed OK/Cancel behavior");
            application.processEvents();
        }
        application.setPalette(original);
        std::cout<<"Dense file selection: idle responsiveness, concurrent icon queries, uppercase extension, Light/Dark/Light and repeated OK/Cancel passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
