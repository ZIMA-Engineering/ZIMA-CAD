#include "../app/cam_tools_dialog.hpp"
#include "../app/cam_operations_dialog.hpp"
#include "../app/cam_nc_dialog.hpp"
#include <QApplication>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QMouseEvent>
#include <iostream>

using namespace zima;
namespace {
int checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class T>T* field(QObject* owner,const char* name){auto* w=owner->findChild<T*>(name);check(w,"Missing CAM editor field");return w;}
void flush(QApplication& app){for(int i=0;i<5;++i){app.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}}
void edit(QTableWidget* table,int row){table->cellClicked(row,3);}
}
int main(int argc,char** argv){QApplication app(argc,argv);try{
#if defined(_WIN32)
    if(QGuiApplication::platformName()=="offscreen") {const auto font=QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR")+"/Fonts/segoeui.ttf");check(font>=0,"Missing test font");app.setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(),9));}
#endif
    QWidget owner;owner.resize(1600,1200);owner.show();QWidget view(&owner);view.setGeometry(owner.rect());view.show();
    document::CamDefinition definition;definition.stock_id="stock";definition.target_id="target";definition.machine.id="machine";definition.machine.name="Machine";
    cam::Tool tool{"tool","Exact tool",{{-20.123456789,5.123456789},{0,5}},{{0,5},{40,6}},300.123456789,6000.123456789};definition.tools.push_back(tool);
    cam::Facing facing{"facing","First","tool",1.123456789,2.123456789,5.123456789,.123456789};definition.operations.emplace_back(facing);
    document::CamManualOperation manual;manual.id="manual";manual.name="Second";manual.program.initial_tip={1.123456789,2.123456789,3.123456789};
    cam::Block block;block.id="block";block.operation_id=manual.id;block.end={4.123456789,5.123456789,6.123456789};manual.program.blocks.push_back(block);definition.operations.emplace_back(manual);
    cam::CompiledProgram preview;cam::Motion arc;arc.block_id="arc";arc.operation_id="op";arc.kind=cam::MotionKind::ArcCounterclockwise;arc.start=arc.end={10,0,2};arc.center={0,0,2};arc.sweep_radians=2*std::numbers::pi;preview.motions.push_back(arc);
    cam::Motion rapid;rapid.block_id="rapid";rapid.kind=cam::MotionKind::Rapid;rapid.start={0,0,10};rapid.end={20,10,10};preview.motions.push_back(rapid);
    const auto wire=app::cam_path_wire(preview);check(wire.size()==2&&wire[0].points.size()>32&&wire[0].reference.semantic_key==arc.block_id,"NC wire lost full arc or block identity");
    check(wire[1].preview_terminal_dashed&&wire[1].color=="#FF3030"&&wire[0].color=="#FFD530","NC path colors or rapid dashes differ");
    const auto tool_wire=app::cam_tool_wire(tool,{1,2,3});check(std::abs(tool_wire.front().points.front().z-3)<1e-12,"Tool preview replaced the authored cutting/shank origin with the tip");
    int commits{};auto tools=definition.tools;auto* tool_dialog=new app::CamToolsDialog(definition,[&](auto value){++commits;tools=std::move(value);},&owner);tool_dialog->show();flush(app);
    check(tool_dialog->pending_value()==tools,"Opening rounded tool geometry or conditions");
    check((tool_dialog->windowFlags()&Qt::SubWindow)&&!tool_dialog->buttons()->button(QDialogButtonBox::Apply),"Tool editor is not a shared properties window");
    check(field<QDoubleSpinBox>(tool_dialog,"camCuttingZ1")->isReadOnly()&&field<QDoubleSpinBox>(tool_dialog,"camShankZ0")->isReadOnly(),"Tool boundary is editable");
    field<QPushButton>(tool_dialog,"camRemoveTool")->click();check(tool_dialog->pending_value().size()==1,"Removed an operation's tool");
    tool_dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush(app);check(commits==1&&tools==definition.tools,"Unchanged tool OK modified authored data");
    tool_dialog=new app::CamToolsDialog(definition,[&](auto){++commits;},&owner);tool_dialog->show();flush(app);
    field<QDoubleSpinBox>(tool_dialog,"camToolFeed")->setValue(750);field<QPushButton>(tool_dialog,"camCuttingAddStation")->click();flush(app);
    check(tool_dialog->pending_value()[0].cutting.size()==3,"Could not insert exact profile station");
    tool_dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush(app);check(commits==1&&tools==definition.tools,"Cancel published tools");
    auto operations=definition.operations;auto make=[&]{return new app::CamOperationsDialog(definition,[&](auto value){++commits;operations=std::move(value);},&owner);};
    auto* dialog=make();dialog->show();flush(app);check(dialog->pending_value()==definition.operations,"Opening changed operation data");
    edit(field<QTableWidget>(dialog,"camOperations"),0);flush(app);check(dialog->pending_value()==definition.operations,"Opening facing rounded values");
    check(!field<QPushButton>(dialog,"camOperationUp")->isEnabled()&&!field<QPushButton>(dialog,"camOperationDown")->isEnabled(),"Opening an editor selected ordering");
    edit(field<QTableWidget>(dialog,"camOperations"),1);flush(app);edit(field<QTableWidget>(dialog,"camBlocks"),0);flush(app);
    check(dialog->pending_value()==definition.operations,"Opening block editor rounded coordinates");
    dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush(app);check(commits==2&&operations==definition.operations,"Unchanged operation OK modified definition");
    dialog=make();dialog->show();flush(app);field<QCheckBox>(dialog,"camOperationsOrder1")->click();flush(app);
    check(!dialog->findChild<QLineEdit*>("camOperationName"),"Ordering checkbox opened operation editor");
    field<QPushButton>(dialog,"camOperationUp")->click();flush(app);
    check(document::cam_operation_id(dialog->pending_value()[0])==manual.id&&field<QCheckBox>(dialog,"camOperationsOrder0")->isChecked(),"Operation order lost stable selection");
    check(!field<QPushButton>(dialog,"camOperationUp")->isEnabled(),"Ordering can move before first item");
    edit(field<QTableWidget>(dialog,"camOperations"),0);flush(app);field<QPushButton>(dialog,"camAddBlock")->click();flush(app);
    const auto added=std::get<document::CamManualOperation>(dialog->pending_value()[0]).program.blocks[1].id;
    field<QComboBox>(dialog,"camBlockMotion")->setCurrentIndex(2);flush(app);
    field<QDoubleSpinBox>(dialog,"camBlockEndX")->setValue(20);field<QDoubleSpinBox>(dialog,"camBlockCenterY")->setValue(8);
    field<QCheckBox>(dialog,"camBlocksOrder1")->click();flush(app);field<QPushButton>(dialog,"camBlockUp")->click();flush(app);
    const auto& program=std::get<document::CamManualOperation>(dialog->pending_value()[0]).program;
    check(program.blocks[0].id==added&&program.blocks[0].operation_id==manual.id&&program.blocks[0].motion==cam::MotionKind::ArcClockwise&&program.blocks[0].end.x==20&&program.blocks[0].center.y==8,"Block editing/order lost motion or identity");
    check(program.blocks[1]==block,"Editing new block changed existing exact coordinates");
    QMouseEvent single(QEvent::MouseButtonPress,QPointF(10,10),QPointF(10,10),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);QApplication::sendEvent(&view,&single);check(commits==2,"Short middle click committed operations");
    const auto expected=dialog->pending_value();QMouseEvent confirm(QEvent::MouseButtonDblClick,QPointF(10,10),QPointF(10,10),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);QApplication::sendEvent(&view,&confirm);flush(app);
    check(commits==3&&operations==expected,"Middle double click failed to confirm operation draft");
    dialog=make();dialog->show();flush(app);field<QPushButton>(dialog,"camAddFacing")->click();flush(app);field<QLineEdit>(dialog,"camOperationName")->setText("");dialog->buttons()->button(QDialogButtonBox::Ok)->click();check(commits==3&&dialog->isVisible(),"Invalid operation name was accepted");
    dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush(app);check(commits==3,"Cancel committed operation draft");
    std::cout<<"CAM editor checks: "<<checks<<" passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"CAM editor failure after "<<checks<<": "<<e.what()<<'\n';return 1;}}
