#include "../browser/bookmarks.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QJsonObject>
#include <QLockFile>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
QByteArray bytes(const QString& path){QFile file(path);check(file.open(QIODevice::ReadOnly),"Cannot read fixture");return file.readAll();}
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    try{
        QTemporaryDir directory;check(directory.isValid(),"No temporary directory");
        const auto path=directory.filePath("www");
        zima::browser::Bookmarks first(path),second(path);
        QString error;check(first.reload(&error),"First initialization failed");
        check(first.items().size()==1&&first.items()[0].toObject().value("id")=="zima-official","Missing factory bookmark");
        check(first.remove("zima-official",&error),"Cannot remove factory bookmark");
        check(second.reload(&error)&&second.items().isEmpty(),"Removed factory bookmark reappeared");
        const auto folder=first.add(QString::fromUtf8("Projekt — Žluťoučký"),{}, {},true,&error);
        check(!folder.isEmpty(),"Unicode folder failed");
        const auto a=first.add("Supplier",QUrl("https://example.com/a"),folder,false,&error);
        check(!a.isEmpty(),"Bookmark insert failed");
        // second has an intentionally stale snapshot; writing must merge fresh disk state.
        const auto b=second.add("Portal",QUrl("https://example.org"),{},false,&error);
        check(!b.isEmpty()&&second.items().size()==3,"Concurrent writer discarded existing bookmarks");
        check(first.reload(&error)&&first.items().size()==3,"Cross-instance reload failed");
        const auto child=first.add("Child",{},folder,true,&error);check(!child.isEmpty(),"Subfolder failed");
        const auto before=bytes(path+"/bookmarks.json");
        check(!first.update(folder,"Cycle",{},child,&error),"Folder cycle accepted");
        check(bytes(path+"/bookmarks.json")==before,"Rejected cycle changed the file");
        check(first.add("Bad",QUrl("javascript:alert(1)"),{},false,&error).isEmpty(),"Unsafe URL accepted");
        check(first.add("Bad",QUrl("https://user:secret@example.com"),{},false,&error).isEmpty(),"URL credentials persisted");
        check(bytes(path+"/bookmarks.json")==before,"Rejected URL changed the file");
        check(first.update(a,"Renamed",QUrl("https://example.com/new"),{},&error),"Move and rename failed");
        check(second.remove(folder,&error),"Recursive delete failed");
        check(second.items().size()==2,"Folder deletion deleted a moved bookmark or left descendants");
        check(first.reload(&error)&&first.items().size()==2,"Deletion reload failed");
        QImage icon(32,32,QImage::Format_ARGB32);icon.fill(Qt::red);
        check(first.save_icon(a,icon,&error),"Favicon persistence failed");
        check(QFile::exists(path+"/icons/"+a+".png"),"Favicon absent");
        check(!first.save_icon("../outside",icon,&error),"Favicon path escape accepted");
        QLockFile lock(path+"/bookmarks.lock");check(lock.tryLock(),"No fixture lock");
        const auto locked=bytes(path+"/bookmarks.json");
        check(first.add("Locked",QUrl("https://example.com"),{},false,&error).isEmpty(),"Concurrent lock ignored");
        check(bytes(path+"/bookmarks.json")==locked,"Locked write changed file");lock.unlock();
        QFile corrupt(path+"/bookmarks.json");check(corrupt.open(QIODevice::WriteOnly),"No corrupt fixture");
        corrupt.write("{broken");corrupt.close();
        check(!first.reload(&error),"Corrupt store silently reset");
        check(first.items().size()==2,"Failed read discarded usable in-memory bookmarks");
        check(first.add("No reset",QUrl("https://example.com"),{},false,&error).isEmpty(),"Corrupt store overwritten");
        check(bytes(path+"/bookmarks.json")=="{broken","Corrupt file not preserved");
        std::cout<<"Browser bookmark persistence and concurrent editing contracts passed.\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
