#include "drawing_window.hpp"
#include "drawing_balloon_dialog.hpp"
#include <zima/workspace/workspace.hpp>
#include <zima/kernel/stable_id.hpp>
#include <QAction>
#include <QContextMenuEvent>
#include <QMenu>
#include <QKeyEvent>
#include <QFile>
#include <iostream>
namespace {
void check(bool ok,const char* m){if(!ok)throw std::runtime_error(m);}
void flush(){QApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}
void mouse(QWidget* w,QEvent::Type type,QPointF p,Qt::MouseButton button,Qt::MouseButtons buttons){QMouseEvent e(type,p,QPointF(w->mapToGlobal(p.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(w,&e);flush();}
void pick(QWidget* w,QPointF p){mouse(w,QEvent::MouseMove,p,Qt::NoButton,Qt::NoButton);mouse(w,QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton);mouse(w,QEvent::MouseButtonRelease,p,Qt::LeftButton,Qt::NoButton);}
}
int verify_drawing_balloon_ui(){using namespace zima;using namespace drawing;try{
    workspace::Workspace live;auto doc=DrawingDocument::create_default();auto& sheet=doc.sheets.front();
    kernel::ViewerMesh mesh;mesh.edges={{{{0,0,0},{30,0,0}},{"pin","edge","1:a"}},{{{0,20,0},{30,20,0}},{"leaf","edge","1:s4:leaf"}}};
    mesh.edges.push_back({{{0,0,0},{30,0,0}},{"pin","other-edge","1:a"}});
    auto view=DrawingDocument::create_view("assembly",{},mesh,ViewOrientation::Top);view.camera={{1,0,0},{0,1,0},{0,0,1}};view.x=120;view.y=130;
    refresh_view_geometry(view,mesh);sheet.views={view};sheet.bom_source_document_id="assembly";
    BomRow pin;pin.item_number=1;pin.name="Pin";pin.occurrence_paths={"1:a"};BomRow sub;sub.item_number=2;sub.name="Subassembly";sub.occurrence_paths={"1:s"};sheet.bom_rows={pin,sub};
    live.add_drawing(doc);app::DrawingWindow window(&live,false);window.resize(1300,900);window.edit_workspace_document(doc.document_id);window.show();flush();
    auto* canvas=window.findChild<QWidget*>("drawingCanvas");auto* action=window.findChild<QAction*>("drawingBalloonAction");
    check(canvas&&action&&action->isEnabled()&&!action->icon().isNull(),"Balloons toolbar action missing");
    const auto dialog=[&]()->app::DrawingBalloonDialog*{for(auto* child:window.findChildren<QDialog*>())if(auto* d=dynamic_cast<app::DrawingBalloonDialog*>(child);d&&d->isVisible())return d;return nullptr;};
    const auto point=[&](double x,double y){const auto box=window.sheet_rectangle_for_test();const double scale=box.height()/sheet.height_mm();return QPointF(box.right()-view.x*scale+x*view.scale*scale,box.bottom()-view.y*scale-y*view.scale*scale);};
    const auto stored=[&]()->const auto&{return live.open_drawing(doc.document_id)->document().sheets[0].balloons;};
    const auto choose=[&]{action->trigger();flush();check(dialog(),"Balloons window did not open");if(dialog()->choosing_view())pick(canvas,point(15,0));check(dialog()->view_id()==view.id,"View reference picker did not confirm the offered view");};
    choose();check(dialog()->parentWidget()==&window&&(dialog()->windowFlags()&Qt::WindowType_Mask)==Qt::SubWindow,"Balloons properties are not internal");
    check(dialog()->buttons()->standardButtons()==(QDialogButtonBox::Ok|QDialogButtonBox::Cancel),"Balloons have an extra commit action");
    dialog()->findChild<QPushButton*>("drawingBalloonShowAll")->click();flush();
    check(dialog()->staged_sheet().balloons.size()==2&&stored().empty(),"Show all is not a transient first-level preview");
    if(qEnvironmentVariableIsSet("ZIMA_BALLOON_CAPTURE"))window.grab().save(qEnvironmentVariable("ZIMA_BALLOON_CAPTURE")+".dialog.png");
    dialog()->reject();flush();check(stored().empty(),"Cancel committed automatic balloons");
    choose();dialog()->findChild<QPushButton*>("drawingBalloonShowAll")->click();flush();
    mouse(canvas,QEvent::MouseButtonPress,QPointF(20,20),Qt::MiddleButton,Qt::MiddleButton);mouse(canvas,QEvent::MouseButtonRelease,QPointF(20,20),Qt::MiddleButton,Qt::NoButton);
    check(dialog()&&stored().empty(),"Short MMB committed balloon changes");
    mouse(canvas,QEvent::MouseButtonDblClick,QPointF(20,20),Qt::MiddleButton,Qt::MiddleButton);mouse(canvas,QEvent::MouseButtonRelease,QPointF(20,20),Qt::MiddleButton,Qt::NoButton);
    check(!dialog()&&stored().size()==2,"Double MMB over View failed to commit balloons");
    const auto id=stored()[0].id;check(stored()[0].text_height==5&&stored()[1].item_number==2,"Balloon appearance or first-level numbering is wrong");
    auto center=window.balloon_handle_for_test(id);auto endpoint=window.balloon_handle_for_test(id,1);check(center&&endpoint,"Balloon grips missing");
    const auto original=stored()[0].position;pick(canvas,*center);const auto movement=QPointF(-45,-30);
    mouse(canvas,QEvent::MouseButtonPress,*center,Qt::LeftButton,Qt::LeftButton);mouse(canvas,QEvent::MouseMove,*center+movement,Qt::NoButton,Qt::LeftButton);mouse(canvas,QEvent::MouseButtonRelease,*center+movement,Qt::LeftButton,Qt::NoButton);
    check(stored()[0].position!=original,"Center grip did not move the balloon");
    auto* state=live.open_drawing(doc.document_id);state->undo();window.edit_workspace_document(doc.document_id);flush();check(stored()[0].position==original,"Undo failed to restore balloon placement");
    center=window.balloon_handle_for_test(id);endpoint=window.balloon_handle_for_test(id,1);pick(canvas,*center);
    mouse(canvas,QEvent::MouseButtonPress,*endpoint,Qt::LeftButton,Qt::LeftButton);mouse(canvas,QEvent::MouseMove,point(25,20),Qt::NoButton,Qt::LeftButton);mouse(canvas,QEvent::MouseButtonRelease,point(25,20),Qt::LeftButton,Qt::NoButton);
    check(stored()[0].attachment.reference.instance_path=="1:s4:leaf"&&stored()[0].item_number==2,"Endpoint grip failed to reattach to the first-level subassembly");
    endpoint=window.balloon_handle_for_test(id,1);const auto attachment=stored()[0].attachment;
    mouse(canvas,QEvent::MouseButtonPress,*endpoint,Qt::LeftButton,Qt::LeftButton);mouse(canvas,QEvent::MouseMove,QPointF(25,25),Qt::NoButton,Qt::LeftButton);mouse(canvas,QEvent::MouseButtonRelease,QPointF(25,25),Qt::LeftButton,Qt::NoButton);
    check(stored()[0].attachment==attachment,"Empty endpoint drop destroyed the stored reference");
    center=window.balloon_handle_for_test(id);pick(canvas,*center);
    QContextMenuEvent context(QContextMenuEvent::Mouse,center->toPoint(),canvas->mapToGlobal(center->toPoint()));QApplication::sendEvent(canvas,&context);flush();
    auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());check(menu,"Balloon context menu missing");
    QAction* properties{};for(auto* a:menu->actions())if(a->objectName()=="drawingBalloonPropertiesAction")properties=a;
    check(properties,"Balloon properties context action missing");properties->trigger();menu->close();flush();check(dialog()&&dialog()->selected_id()==id,"Context did not open the same balloon dialog");
    auto* references=dialog()->findChild<QTableWidget*>("drawingBalloonReferences");pick(references->viewport(),references->visualItemRect(references->item(1,0)).center());
    check(dialog()->entering()&&references->selectionMode()==QAbstractItemView::NoSelection,"Attachment did not own shared reference input");
    pick(canvas,point(10,0));check(dialog()->selected_balloon()->item_number==1&&stored()[0].item_number==2,"Properties reattachment is not transient");
    dialog()->reject();flush();check(stored()[0].item_number==2,"Cancel committed replacement");
    choose();dialog()->findChild<QPushButton*>("drawingBalloonEraseAll")->click();dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
    check(std::ranges::none_of(stored(),[](const auto& b){return b.visible;}),"Erase all did not hide selected view balloons");
    choose();dialog()->findChild<QPushButton*>("drawingBalloonShowAll")->click();dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
    check(std::ranges::all_of(stored(),[](const auto& b){return b.visible;}),"Show all failed to restore hidden positions");
    choose();dialog()->findChild<QPushButton*>("drawingBalloonAdd")->click();pick(canvas,point(10,0));check(dialog()->placing(),"Manual balloon did not enter placement");pick(canvas,point(-20,-20));dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
    check(stored().size()==4&&stored().back().item_number==1,"Manual balloon creation failed");
    center=window.balloon_handle_for_test(id);endpoint=window.balloon_handle_for_test(id,1);pick(canvas,*center);
    mouse(canvas,QEvent::MouseButtonPress,*endpoint,Qt::LeftButton,Qt::LeftButton);mouse(canvas,QEvent::MouseMove,point(25,0),Qt::NoButton,Qt::LeftButton);
    const auto before_cycle=window.document_for_test().sheets[0].balloons[0].attachment.reference.semantic_key;
    QContextMenuEvent cycle(QContextMenuEvent::Mouse,point(25,0).toPoint(),canvas->mapToGlobal(point(25,0).toPoint()));QApplication::sendEvent(canvas,&cycle);flush();
    mouse(canvas,QEvent::MouseButtonRelease,point(25,0),Qt::LeftButton,Qt::NoButton);
    check(stored()[0].attachment.reference.semantic_key!=before_cycle&&stored()[0].attachment.reference.instance_path=="1:a","Endpoint release did not confirm the RMB-cycled candidate");
    const auto root=std::filesystem::canonical(std::filesystem::temp_directory_path());const auto dir=root/("zima-balloon-ui-"+kernel::make_stable_id());std::filesystem::create_directory(dir);
    window.export_dxf(dir/"balloons.dxf");window.export_pdf(dir/"balloons.pdf");window.export_jpg(dir/"balloons.jpg");
    for(const auto* file:{"balloons.dxf","balloons.pdf","balloons.jpg"})check(std::filesystem::file_size(dir/file)>100,"Balloon export is empty");
    QFile dxf(QString::fromStdString((dir/"balloons.dxf").string()));check(dxf.open(QIODevice::ReadOnly),"DXF cannot reopen");
    const auto lines=dxf.readAll().split('\n');int numbers=0;QString kind,text;double height=0;
    const auto finish_entity=[&]{if(kind=="TEXT"&&(text=="1"||text=="2")){check(std::abs(height-5)<1e-6,"Exported balloon text is not 5 paper mm");++numbers;}};
    for(int i=0;i+1<lines.size();i+=2){const auto code=lines[i].trimmed().toInt();if(code==0){finish_entity();kind=QString::fromUtf8(lines[i+1]);text.clear();height=0;}else if(code==1)text=QString::fromUtf8(lines[i+1]);else if(code==40)height=lines[i+1].toDouble();}
    finish_entity();check(numbers==int(stored().size()),"DXF omitted balloon numbers or exported them as curves");dxf.close();
    if(qEnvironmentVariableIsSet("ZIMA_BALLOON_CAPTURE"))window.grab().save(qEnvironmentVariable("ZIMA_BALLOON_CAPTURE"));
    check(std::filesystem::canonical(dir).parent_path()==root,"Invalid cleanup path");std::filesystem::remove_all(dir);
    std::cout<<"Drawing balloon UI creation, selection, grips, undo, visibility and exports passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
