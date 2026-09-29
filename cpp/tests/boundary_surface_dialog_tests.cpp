#include "../app/boundary_surface_dialog.hpp"
#include <QApplication>
#include <QDialogButtonBox>
#include <QPushButton>
#include <iostream>
using namespace zima;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv) {
    QApplication application(argc,argv);QWidget parent;parent.resize(1000,700);parent.show();
    try {
        int commits=0;document::HistoryContainer committed;
        auto feature=document::create_boundary_surface();
        auto* dialog=new app::BoundarySurfaceDialog(feature,[&](auto pending){++commits;committed=std::move(pending);},&parent);
        dialog->show();application.processEvents();
        auto* table=dialog->findChild<QTableWidget*>("boundarySurfaceReferences");
        check(table&&table->rowCount()==4&&dialog->windowFlags().testFlag(Qt::SubWindow),"Missing four-row internal properties dialog");
        check(dialog->active_row()==0,"First boundary entry is not active");
        dialog->set_boundary({"a","curve"});dialog->set_boundary({"a","curve"});
        check(dialog->active_row()==1&&dialog->pending.boundary_surface.boundaries[1].owner_id.empty(),"Duplicate selection filled another row");
        for(const auto* owner:{"b","c","d"})dialog->set_boundary({owner,{}});
        check(dialog->active_row()==-1,"Complete boundary input remained armed");
        for(int row=0;row<4;++row)check(dialog->inspected(row),"New boundary was not inspected by default");
        auto* eye=table->cellWidget(0,3)->findChild<QToolButton*>();eye->click();
        check(!dialog->inspected(0)&&dialog->inspected(1)&&dialog->active_row()==-1,"Inspection could not be disabled independently");
        eye->click();
        check(dialog->inspected(0)&&dialog->active_row()==-1,"Inspection changed input ownership");
        dialog->end_entry();check(!dialog->inspected(0)&&dialog->pending.boundary_surface.boundaries[0].owner_id=="a","Ending input erased reference or kept inspection");
        const int before=table->mapTo(dialog,QPoint{}).y();dialog->resize(dialog->width(),550);application.processEvents();
        check(table->mapTo(dialog,QPoint{}).y()==before&&table->rowHeight(0)==34,"Resize redistributed reference rows");
        table->cellWidget(1,1)->findChild<QPushButton*>()->click();
        check(table->rowCount()==4&&dialog->pending.boundary_surface.boundaries[1].owner_id.empty()&&dialog->active_row()==1,"Clear deleted structural row or did not reactivate entry");
        dialog->set_boundary({"b",{}});dialog->buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();
        check(commits==1&&committed.boundary_surface.boundaries[1].owner_id=="b","OK lost boundary references");
        auto* edited=new app::BoundarySurfaceDialog(committed,[&](auto){++commits;},&parent);
        for(int row=0;row<4;++row)check(edited->inspected(row),"Reopened boundary was not inspected by default");
        edited->show();application.processEvents();edited->findChild<QTableWidget*>("boundarySurfaceReferences")->cellClicked(0,2);
        edited->set_boundary({"replacement",{}});check(edited->pending.boundary_surface.boundaries[0].owner_id=="replacement","Editing did not replace armed reference");edited->reject();application.processEvents();
        check(commits==1&&committed.boundary_surface.boundaries[0].owner_id=="a","Cancel committed edited source");
        std::cout<<"Boundary dialog: entry, duplicate picks, independent inspection, clear, resize, OK and Cancel passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
