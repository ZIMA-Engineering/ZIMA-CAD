#include "../browser/browser.hpp"
#include <QApplication>
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QImage>
#include <QJsonObject>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QPushButton>
#include <QScopeGuard>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QWebEnginePage>
#include <iostream>
#include <stdexcept>
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct HiddenWindows:QObject {
    bool eventFilter(QObject* object,QEvent* event) override {
        if(event->type()==QEvent::Show)if(auto* widget=qobject_cast<QWidget*>(object))widget->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
}
int main(int argc,char** argv) {
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QApplication application(argc,argv);HiddenWindows hidden;application.installEventFilter(&hidden);
    try {
        QTemporaryDir directory;check(directory.isValid(),"Cannot isolate browser bookmark test");
        QMainWindow owner;owner.setAttribute(Qt::WA_DontShowOnScreen);owner.resize(1200,900);
        zima::browser::Runtime runtime(directory.filePath("www"),[&]{return directory.path();},&owner);
        QString error;check(runtime.bookmarks.reload(&error),"Cannot initialize temporary bookmarks");
        const auto id=runtime.bookmarks.add("Ferona",QUrl("https://example.invalid/ferona"),{},false,&error);
        check(!id.isEmpty(),"Cannot create synthetic Ferona bookmark");
        auto* tab=new zima::browser::Tab(runtime,&owner);owner.setCentralWidget(tab);
        auto* second=new zima::browser::Tab(runtime,&owner);second->hide();
        const auto cleanup=qScopeGuard([&]{delete second;delete owner.takeCentralWidget();});
        owner.show();application.processEvents();
        auto* tree=tab->findChild<QTreeWidget*>("browserBookmarks");check(tree,"Missing browser bookmark Tree");
        unsigned edits{};
        for(unsigned i=0;i<16;++i) {
            QTreeWidgetItem* row{};for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString()==id){row=*it;break;}
            check(row,"Synthetic bookmark disappeared");tree->setCurrentItem(row);tree->scrollToItem(row);application.processEvents();
            const auto at=tree->visualItemRect(row).center();bool refreshed=false,selected=false;
            QTimer::singleShot(0,&owner,[&]{
                auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());
                if(!menu)for(auto* widget:QApplication::topLevelWidgets())if(auto* candidate=qobject_cast<QMenu*>(widget);candidate&&candidate->isVisible()){menu=candidate;break;}
                if(!menu)return;
                QImage icon(32,32,QImage::Format_ARGB32);icon.fill(i%2?Qt::red:Qt::green);
                refreshed=runtime.bookmarks.save_icon(id,icon,&error); // Real icon notification rebuilds both Trees during QMenu::exec().
                QAction* edit{};for(auto* action:menu->actions())if(action->text()=="Edit bookmark"){edit=action;break;}
                if(!edit){menu->close();return;}
                const auto position=menu->actionGeometry(edit).center();menu->setActiveAction(edit);
                for(auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                    QMouseEvent event(type,QPointF(position),QPointF(menu->mapToGlobal(position)),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                        type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(menu,&event);
                }
                selected=true;
            });
            QTimer deadline;deadline.setSingleShot(true);QObject::connect(&deadline,&QTimer::timeout,&owner,[]{for(auto* widget:QApplication::topLevelWidgets())if(auto* menu=qobject_cast<QMenu*>(widget);menu&&menu->isVisible())menu->close();});deadline.start(2000);
            QMetaObject::invokeMethod(tree,"customContextMenuRequested",Qt::DirectConnection,Q_ARG(QPoint,at));deadline.stop();application.processEvents();
            check(refreshed&&selected,"Bookmark refresh/context-menu interaction was not exercised");
            auto* dialog=owner.findChild<QDialog*>("browserBookmarkDialog");check(dialog&&dialog->isVisible(),"Edit bookmark did not open its properties");
            auto* name=dialog->findChild<QLineEdit*>("browserBookmarkName");check(name&&name->text()==(i?QString("Ferona %1").arg(i-1):QString("Ferona")),"Context menu edited a replaced or unrelated Tree item");
            name->setText(QString("Ferona %1").arg(i));auto* buttons=dialog->findChild<QDialogButtonBox*>();check(buttons,"Bookmark properties have no shared confirmation");
            buttons->button(QDialogButtonBox::Ok)->click();application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();
            ++edits;
        }
        check(runtime.bookmarks.reload(&error),"Edited bookmarks cannot reopen");
        bool found=false;for(const auto& value:runtime.bookmarks.items())if(value.toObject().value("id")==id) {
            found=true;check(value.toObject().value("name")=="Ferona 15"&&value.toObject().value("url")=="https://example.invalid/ferona","Bookmark edit changed URL or lost its saved name");
        }
        check(found,"Edited bookmark lost its persistent identity");
        std::cout<<"Bookmark context menu: "<<edits<<" icon-triggered Tree replacements, actual Edit selection, shared properties OK, second-tab refresh and store reopen passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
