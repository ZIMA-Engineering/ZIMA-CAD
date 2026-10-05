#include "../app/boundary_surface_dialog.hpp"
#include "../app/surface_sewing_dialog.hpp"
#include "../app/general_surface_dialog.hpp"
#include "../app/surface_intersection_dialog.hpp"
#include "../app/surface_trim_dialog.hpp"
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
        auto* resized=new app::BoundarySurfaceDialog(committed,[&](auto pending){++commits;committed=std::move(pending);},&parent);
        resized->show();application.processEvents();
        auto* boundary_count=resized->findChild<QSpinBox*>("boundarySurfaceCount");
        auto* resized_table=resized->findChild<QTableWidget*>("boundarySurfaceReferences");
        check(boundary_count&&boundary_count->value()==4,"Boundary count control is missing");
        boundary_count->setValue(3);application.processEvents();
        check(resized_table->rowCount()==3&&resized->pending.boundary_surface.boundaries.size()==3&&
            resized->pending.boundary_surface.boundaries[0].owner_id=="a"&&resized->active_row()==-1&&commits==1,
            "Changing boundary count lost retained references or committed a draft");
        resized->end_entry();boundary_count->setValue(5);application.processEvents();
        check(resized_table->rowCount()==5&&resized->active_row()==3&&!resized->inspected(0),
            "Added boundaries did not arm entry or preserved inspection incorrectly");
        resized->set_boundary({"e",{}});resized->set_boundary({"f",{}});
        check(resized->active_row()==-1&&resized->inspected(4),"Expanded boundary entry failed");
        const auto viewport_height=resized_table->viewport()->height();
        const auto top=resized_table->mapTo(resized,QPoint{}).y();resized->resize(resized->width(),600);application.processEvents();
        check(resized_table->viewport()->height()>viewport_height&&resized_table->mapTo(resized,QPoint{}).y()==top&&resized_table->rowHeight(4)==34,
            "Extra height did not expand the boundary viewport with fixed rows");
        resized_table->cellWidget(4,1)->findChild<QPushButton*>()->click();
        check(resized->active_row()==4&&resized_table->rowCount()==5&&!resized->inspected(4),"Expanded row clear lost input or inspection state");
        resized->reject();application.processEvents();
        check(commits==1&&committed.boundary_surface.boundaries.size()==4,"Count Cancel changed stored definition");
        auto triangle=committed;triangle.boundary_surface.boundaries.resize(3);
        auto* three=new app::BoundarySurfaceDialog(triangle,[&](auto pending){++commits;committed=std::move(pending);},&parent);
        three->show();application.processEvents();three->buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();
        check(commits==2&&committed.boundary_surface.boundaries.size()==3,"OK did not preserve three-boundary definition");
        auto supported=committed;supported.boundary_surface.boundaries[0]={"surface","edge",document::BoundaryCurveSource::Kind::Edge};
        auto* continuity_dialog=new app::BoundarySurfaceDialog(supported,[&](auto pending){++commits;committed=std::move(pending);},&parent);
        continuity_dialog->show();application.processEvents();auto* supported_table=continuity_dialog->findChild<QTableWidget*>("boundarySurfaceReferences");
        continuity_dialog->findChild<QComboBox*>("boundaryContinuity0")->setCurrentIndex(2);
        check(continuity_dialog->active_row()==0&&continuity_dialog->support_entry(),"G2 did not arm its missing support field");
        check(!static_cast<ui::ReferenceCellItem*>(supported_table->item(0,2))->is_active_input()&&
            static_cast<ui::ReferenceCellItem*>(supported_table->item(0,6))->is_active_input(),"Boundary and support shared input ownership");
        const kernel::FaceReference support{"surface","face",{}};continuity_dialog->set_support(support);
        check(continuity_dialog->active_row()==-1&&continuity_dialog->support_inspected(0)&&continuity_dialog->inspected(0),"Support entry or independent inspection failed");
        supported_table->cellWidget(0,7)->findChild<QToolButton*>()->click();
        check(!continuity_dialog->support_inspected(0)&&continuity_dialog->inspected(0),"Support eye altered boundary inspection");
        supported_table->cellWidget(0,8)->findChild<QCheckBox*>()->setChecked(true);
        continuity_dialog->end_entry();
        check(!continuity_dialog->support_inspected(0)&&!continuity_dialog->inspected(0)&&continuity_dialog->pending.boundary_surface.boundaries[0].support==support,
            "Ending support entry deleted data or left inspection active");
        continuity_dialog->buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();
        check(commits==3&&committed.boundary_surface.boundaries[0].support_reversed&&committed.boundary_surface.boundaries[0].continuity==kernel::SurfaceContinuity::G2,
            "OK lost continuity or supporting-face side");
        auto* support_edit=new app::BoundarySurfaceDialog(committed,[&](auto){++commits;},&parent);support_edit->show();application.processEvents();
        auto* edit_table=support_edit->findChild<QTableWidget*>("boundarySurfaceReferences");
        edit_table->cellClicked(0,2);support_edit->set_boundary({"surface","edge",document::BoundaryCurveSource::Kind::Edge});
        check(support_edit->pending.boundary_surface==committed.boundary_surface,"Reselecting an unchanged edge lost support or side");
        edit_table->cellWidget(0,5)->findChild<QPushButton*>()->click();
        check(support_edit->active_row()==0&&support_edit->support_entry()&&!support_edit->pending.boundary_surface.boundaries[0].support&&
            support_edit->pending.boundary_surface.boundaries[0].curve_id=="edge"&&edit_table->rowCount()==3,"Support clear deleted the boundary or failed to arm replacement");
        support_edit->reject();application.processEvents();check(commits==3&&committed.boundary_surface.boundaries[0].support==support,"Support Cancel changed stored data");
        auto sewing=document::create_surface_sewing();int sewing_commits=0;document::HistoryContainer sewn;
        auto* sewing_dialog=new app::SurfaceSewingDialog(sewing,[&](auto pending){++sewing_commits;sewn=std::move(pending);},&parent);
        sewing_dialog->show();application.processEvents();auto* sewing_table=sewing_dialog->findChild<QTableWidget*>("surfaceSewingReferences");
        check(sewing_dialog->windowFlags().testFlag(Qt::SubWindow)&&sewing_table->rowCount()==1&&sewing_dialog->active_row()==0,
            "Sewing did not reuse internal reference-entry properties");
        const int sewing_height=sewing_table->height();
        check(sewing_height==sewing_table->horizontalHeader()->sizeHint().height()+2*sewing_table->frameWidth()+68&&
            sewing_table->horizontalHeaderItem(0)->text().isEmpty(),"Sewing does not use a fixed two-row viewport with uncaptioned numbering");
        const kernel::FaceReference first{"first","face",{}},second{"second","face",{}};
        sewing_dialog->set_face(first);sewing_dialog->set_face(first);
        check(sewing_table->rowCount()==2&&sewing_dialog->pending.surface_sewing.faces.size()==1,"Sewing duplicate input inserted a second row");
        sewing_dialog->set_face(second);check(sewing_table->rowCount()==3&&sewing_dialog->inspected(0)&&sewing_dialog->inspected(1),"Sewing did not inspect selected faces independently");
        const int row_height=sewing_table->rowHeight(0),sewing_top=sewing_table->pos().y();sewing_dialog->resize(800,550);application.processEvents();
        check(sewing_table->rowHeight(0)==row_height&&sewing_table->pos().y()==sewing_top&&sewing_table->height()==sewing_height,
            "Sewing resize moved upper fields or stretched its two-row viewport");
        check(!sewing_table->visualItemRect(sewing_table->item(2,2)).isEmpty()&&
            sewing_table->viewport()->rect().intersects(sewing_table->visualItemRect(sewing_table->item(2,2))),
            "Sewing did not scroll the trailing entry into its fixed viewport");
        const auto table_position=sewing_table->pos();sewing_dialog->move(40,60);application.processEvents();
        check(sewing_table->pos()==table_position&&parent.rect().contains(sewing_dialog->geometry()),
            "Moving Sewing properties changed its form layout or escaped the parent bounds");
        sewing_table->cellWidget(0,3)->findChild<QToolButton*>()->click();check(!sewing_dialog->inspected(0)&&sewing_dialog->inspected(1)&&sewing_dialog->active_row()==2,
            "Sewing inspection changed input ownership or another face");
        sewing_dialog->end_entry();check(sewing_dialog->active_row()==-1&&!sewing_dialog->inspected(1)&&sewing_dialog->pending.surface_sewing.faces.size()==2,"Ending sewing entry deleted data");
        sewing_table->cellWidget(0,1)->findChild<QPushButton*>()->click();
        check(sewing_table->rowCount()==2&&sewing_dialog->pending.surface_sewing.faces.front()==second&&sewing_dialog->active_row()==1,
            "Sewing remove did not delete its list item and arm the trailing row");
        sewing_dialog->set_face(first);sewing_dialog->buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();
        check(sewing_commits==1&&sewn.surface_sewing.faces==std::vector<kernel::FaceReference>{second,first},"Sewing OK did not commit its native reference list");
        auto* sewing_edit=new app::SurfaceSewingDialog(sewn,[&](auto){++sewing_commits;},&parent);sewing_edit->show();application.processEvents();
        sewing_edit->findChild<QTableWidget*>("surfaceSewingReferences")->cellWidget(0,1)->findChild<QPushButton*>()->click();sewing_edit->reject();application.processEvents();
        check(sewing_commits==1&&sewn.surface_sewing.faces.size()==2,"Sewing Cancel committed reference removal");
        // Leave room to exercise growth without the required parent-bounds clamp.
        parent.resize(1000,1100);application.processEvents();
        auto general=document::create_general_surface();int general_commits=0;document::HistoryContainer general_committed;
        auto* general_dialog=new app::GeneralSurfaceDialog(general,[&](auto value){++general_commits;general_committed=std::move(value);},&parent);
        general_dialog->show();application.processEvents();auto* general_table=general_dialog->findChild<QTableWidget*>("generalSurfaceBoundaries");
        check(general_dialog->windowFlags().testFlag(Qt::SubWindow)&&general_table->rowCount()==5&&general_table->columnCount()==5,
            "Owned surface did not use shared internal properties with a trailing row");
        check(general_table->verticalHeader()->isVisible()&&general_table->model()->headerData(0,Qt::Vertical).toString()=="1"&&
            general_table->horizontalHeaderItem(0)->text().isEmpty()&&!general_dialog->findChild<QPushButton*>("generalSurfaceAddSketch")&&
            !general_dialog->findChild<QPushButton*>("generalSurfaceAddCurve"),"Owned boundary table retained its custom number column or bottom add actions");
        const auto first_owned=sketcher::Sketch::from_serialized(general.general_surface.boundaries[0].sketch_serialized).id;
        general_table->cellWidget(0,0)->findChild<QCheckBox*>()->click();
        auto* down=general_dialog->findChild<QPushButton*>("generalSurfaceMoveDown");down->click();
        check(sketcher::Sketch::from_serialized(general_dialog->pending.general_surface.boundaries[1].sketch_serialized).id==first_owned&&
            general_table->cellWidget(1,0)->findChild<QCheckBox*>()->isChecked(),"Owned boundary reorder changed identity or ordering selection");
        general_table->cellWidget(1,4)->findChild<QToolButton*>()->click();
        check(general_dialog->inspected_boundaries().contains(first_owned)&&general_table->cellWidget(1,0)->findChild<QCheckBox*>()->isChecked(),
            "Owned inspection changed ordering selection");
        int sketch_edits=0;general_dialog->edit_sketch_properties=[&](unsigned row){check(row==1,"Owned field edited another boundary");++sketch_edits;};
        general_table->cellClicked(1,3);check(sketch_edits==1,"Owned editable field did not open Sketch with one click");
        auto edited_sketch=sketcher::Sketch::from_serialized(general_dialog->pending.general_surface.boundaries[1].sketch_serialized);
        edited_sketch.plane=sketcher::SketchPlane::XZ;edited_sketch.plane_auto=false;edited_sketch.plane_offset=12;
        static_cast<void>(edited_sketch.add_segment(0,0,10,0));general_dialog->set_sketch(1,edited_sketch);
        check(!qobject_cast<QComboBox*>(general_table->cellWidget(1,2))->isEnabled(),"Populated type selector can discard authored geometry");
        const auto moved_sketch=sketcher::Sketch::from_serialized(general_dialog->pending.general_surface.boundaries[1].sketch_serialized);
        check(moved_sketch.plane==sketcher::SketchPlane::XZ&&moved_sketch.plane_offset==12&&moved_sketch.resolved_origin.y==12,
            "Owned Sketch plane/offset did not preserve ordinary local geometry");
        const auto general_top=general_table->pos().y(),general_height=general_table->viewport()->height();
        general_dialog->resize(general_dialog->width(),general_dialog->height()+100);application.processEvents();
        check(general_table->pos().y()==general_top&&general_table->rowHeight(1)==34&&general_table->viewport()->height()>general_height,
            "Owned surface resizing redistributed upper content or stretched rows");
        int curve_edits=0;general_dialog->edit_curve=[&](unsigned row){check(row==4,"Trailing row opened another Curve");++curve_edits;};
        qobject_cast<QComboBox*>(general_table->cellWidget(4,2))->setCurrentIndex(1);general_table->cellClicked(4,3);application.processEvents();
        check(general_table->rowCount()==6&&curve_edits==1&&general_dialog->pending.general_surface.boundaries.back().curve&&
            general_dialog->pending.general_surface.boundaries.back().curve->parent_construction_id==general.id,"Owned 3D Curve was not inserted inside its feature");
        general_dialog->end_entry();check(general_dialog->inspected_boundaries().empty()&&general_table->rowCount()==6,"Owned end-entry deleted data");
        general_table->cellWidget(4,1)->findChild<QPushButton*>()->click();check(general_table->rowCount()==5,"Owned remove failed to delete its list item");
        const auto blank_id=sketcher::Sketch::from_serialized(general_dialog->pending.general_surface.boundaries[2].sketch_serialized).id;
        qobject_cast<QComboBox*>(general_table->cellWidget(2,2))->setCurrentIndex(1);
        check(general_dialog->pending.general_surface.boundaries[2].curve&&general_dialog->pending.general_surface.boundaries[2].curve->id!=blank_id,
            "Empty boundary type did not create a new correctly owned definition");
        general_dialog->buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();
        check(general_commits==1&&sketcher::Sketch::from_serialized(general_committed.general_surface.boundaries[1].sketch_serialized).id==first_owned,
            "Owned OK lost edited boundary identity");
        auto* general_edit=new app::GeneralSurfaceDialog(general_committed,[&](auto){++general_commits;},&parent);general_edit->show();application.processEvents();
        auto* empty_table=general_edit->findChild<QTableWidget*>("generalSurfaceBoundaries");
        while(!general_edit->pending.general_surface.boundaries.empty()) {
            auto* remove=empty_table->cellWidget(0,1)->findChild<QPushButton*>();
            check(remove->isEnabled(),"The last owned boundaries cannot be removed");remove->click();
        }
        check(empty_table->rowCount()==1&&general_edit->inspected_boundaries().empty()&&
            !general_edit->findChild<QPushButton*>("generalSurfaceMoveUp")->isEnabled()&&
            !general_edit->findChild<QPushButton*>("generalSurfaceMoveDown")->isEnabled(),"Empty draft retained stale rows or ordering controls");
        general_edit->buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();
        check(general_edit->isVisible()&&general_commits==1,"Empty surface OK bypassed boundary validation");
        int replacement_edits=0;general_edit->edit_sketch_properties=[&](unsigned row){check(row==0,"Empty draft added the wrong row");++replacement_edits;};
        empty_table->cellClicked(0,3);
        check(replacement_edits==1&&empty_table->rowCount()==2&&general_edit->pending.general_surface.boundaries.size()==1,
            "Empty surface draft cannot add a new owned boundary");
        general_edit->reject();application.processEvents();
        check(general_commits==1&&general_committed.general_surface.boundaries.size()==4,"Owned Cancel committed a draft removal");
        int intersection_commits=0;document::HistoryContainer intersection_committed;
        auto* intersection_dialog=new app::SurfaceIntersectionDialog(document::create_surface_intersection(),[&](auto value){++intersection_commits;intersection_committed=std::move(value);},&parent);
        intersection_dialog->show();application.processEvents();
        auto* intersection_table=intersection_dialog->findChild<QTableWidget*>("surfaceIntersectionReferences");
        check(intersection_table&&intersection_table->rowCount()==2&&intersection_dialog->windowType()==Qt::SubWindow,"Intersection did not use two native reference fields");
        intersection_dialog->set_face({"first-source","first-face",{}});intersection_dialog->set_face({"second-source","second-face",{}});
        check(intersection_dialog->active_row()==-1&&intersection_dialog->inspected(0)&&intersection_dialog->inspected(1),"Intersection did not advance required input independently of inspection");
        check(!intersection_table->cellWidget(0,1)->findChild<QPushButton*>()->isVisible()&&
            !intersection_table->cellWidget(0,1)->findChild<QLabel*>()->isVisible(),"Required intersection face exposed a clear or entry control");
        intersection_table->cellClicked(0,2);check(intersection_dialog->active_row()==0&&intersection_dialog->inspected(1),"Intersection replacement changed another inspection state");
        intersection_dialog->set_face({"second-source","second-face",{}});check(intersection_dialog->pending.surface_intersection.faces[0].owner_id=="first-source","Intersection accepted the same face twice");
        const auto intersection_top=intersection_table->pos().y();intersection_dialog->resize(intersection_dialog->width()+100,intersection_dialog->height()+150);application.processEvents();
        check(intersection_table->pos().y()==intersection_top&&intersection_table->rowHeight(0)==34,"Intersection resize redistributed its reference fields");
        intersection_dialog->end_entry();check(intersection_dialog->active_row()==-1&&!intersection_dialog->inspected(0)&&intersection_dialog->pending.surface_intersection.faces[0].valid(),"Intersection end-entry deleted a required face");
        intersection_dialog->buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();check(intersection_commits==1,"Intersection OK did not commit");
        auto* intersection_edit=new app::SurfaceIntersectionDialog(intersection_committed,[&](auto){++intersection_commits;},&parent);intersection_edit->show();application.processEvents();
        intersection_edit->findChild<QTableWidget*>("surfaceIntersectionReferences")->cellClicked(0,2);intersection_edit->set_face({"replacement","replacement-face",{}});intersection_edit->reject();application.processEvents();
        check(intersection_commits==1&&intersection_committed.surface_intersection.faces[0].owner_id=="first-source","Intersection Cancel committed reference replacement");
        int trim_commits=0;document::HistoryContainer trim_committed;
        auto* trim_dialog=new app::SurfaceTrimDialog(document::create_surface_trim(),[&](auto value){++trim_commits;trim_committed=std::move(value);},&parent);
        trim_dialog->show();application.processEvents();auto* trim_table=trim_dialog->findChild<QTableWidget*>("surfaceTrimReferences");
        check(trim_table&&trim_table->rowCount()==2&&trim_dialog->windowType()==Qt::SubWindow,"Trim did not use shared reference controls");
        trim_dialog->set_target({"target","surface",{}});trim_dialog->set_tool({{"tool","edge",{}},false});
        check(trim_table->rowCount()==3&&trim_dialog->inspected(0)&&trim_dialog->inspected(1),"Trim omitted its independent target/tool inspection");
        trim_table->cellClicked(1,2);trim_dialog->set_tool({{"tool","other",{}},false});
        check(trim_dialog->pending.surface_trim.tools.size()==1&&trim_dialog->pending.surface_trim.tools[0].reference.semantic_key=="other","Trim replacement inserted another tool");
        trim_dialog->set_region({25,40,0});trim_dialog->end_entry();
        check(trim_dialog->pending.surface_trim.seed_valid&&trim_dialog->active_row()==-1&&!trim_dialog->inspected(0)&&trim_dialog->pending.surface_trim.tools.size()==1,"Trim end-entry deleted its region or tool");
        const auto trim_top=trim_table->pos().y(),trim_height=trim_table->viewport()->height();trim_dialog->resize(trim_dialog->width()+100,trim_dialog->height()+100);application.processEvents();
        check(trim_table->pos().y()==trim_top&&trim_table->rowHeight(0)==34&&trim_table->viewport()->height()>trim_height,"Trim resize moved upper fields or stretched rows");
        trim_dialog->buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();check(trim_commits==1,"Trim OK did not commit");
        auto* trim_edit=new app::SurfaceTrimDialog(trim_committed,[&](auto){++trim_commits;},&parent);trim_edit->show();application.processEvents();
        trim_edit->findChild<QTableWidget*>("surfaceTrimReferences")->cellWidget(1,1)->findChild<QPushButton*>()->click();
        check(trim_edit->pending.surface_trim.tools.empty(),"Trim tool removal did not delete its list row");trim_edit->reject();application.processEvents();
        check(trim_commits==1&&trim_committed.surface_trim.tools.size()==1,"Trim Cancel committed tool removal");
        std::cout<<"Surface dialogs: entry, owned editors/order, independent inspection, clear/remove, resize, OK and Cancel passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
