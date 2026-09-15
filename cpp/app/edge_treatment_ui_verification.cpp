#include "assembly_workspace_window.hpp"
#include "primitive_properties_dialog.hpp"
#include <zima/viewer/mesh_view.hpp>
#include <QApplication>
#include <QAction>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVariantAnimation>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace zima::app {
int verify_edge_treatment_ui(QApplication& application, AssemblyWorkspaceWindow& window,
    const std::filesystem::path& directory) {
    using commands::Json;
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto run=[&](const char* command,Json args=Json::object()) {
        const auto result=window.execute_console_command(QString::fromStdString(Json{{"command",command},{"arguments",args}}.dump()));
        if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.message);
        flush();return result.data;
    };
    try {
        window.showMaximized();flush();
        const auto stem="edge-grips-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        auto* tree=window.findChild<QTreeWidget*>();
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));
        check(tree&&view,"Missing model view/tree");
        const auto mouse=[&](QEvent::Type type,QPointF point,Qt::MouseButton button,Qt::MouseButtons buttons) {
            QMouseEvent event(type,point,QPointF(view->mapToGlobal(point.toPoint())),button,buttons,Qt::NoModifier);
            QApplication::sendEvent(view,&event);flush();
        };
        const auto click=[&](QPointF point) {
            mouse(QEvent::MouseMove,point,Qt::NoButton,Qt::NoButton);
            mouse(QEvent::MouseButtonPress,point,Qt::LeftButton,Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease,point,Qt::LeftButton,Qt::NoButton);
        };
        const auto pick=[&](viewer::CandidateKind kind,const std::string& owner,const std::string& key) {
            for(int y=12;y<view->height()-12;y+=6)for(int x=12;x<view->width()-12;x+=6) {
                const auto offered=view->selection_candidates_at(QPointF(x,y));
                if(!offered.empty()&&offered.front().kind==kind&&offered.front().owner_id==owner&&
                    (key.empty()||offered.front().semantic_key==key)) {
                    const auto result=offered.front();click(QPointF(x,y));return result;
                }
            }
            throw std::runtime_error("Common picker did not offer "+owner+"/"+key);
        };
        const auto dialog=[&] {
            for(auto* item:window.findChildren<QDialog*>())
                if(auto* value=dynamic_cast<PrimitivePropertiesDialog*>(item);value&&value->isVisible()&&
                    value->findChild<QDoubleSpinBox*>("edgeTreatmentPrimary"))return value;
            throw std::runtime_error("Treatment Properties is missing");
        };
        const auto edit=[&](const std::string& owner) {
            QTreeWidgetItem* selected=nullptr;
            for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==owner&&(*it)->data(0,Qt::UserRole+3)=="part-container") {
                    selected=*it;break;
                }
            check(selected,"Missing treatment tree item");
            window.show_tree_item_properties(selected);flush();return dialog();
        };
        const auto finish=[&](bool commit) {
            dialog()->findChild<QDialogButtonBox*>()->button(commit?QDialogButtonBox::Ok:QDialogButtonBox::Cancel)->click();flush();
        };
        const auto drag=[&](const std::string& owner,const std::string& key,int handle,bool cancel=false) {
            std::cout << "Grip " << key << " / " << handle << " cancel=" << cancel << std::endl;
            const auto candidate=pick(viewer::CandidateKind::Dimension,owner,key);
            const auto selected=[&] {
                const auto current=view->confirmed_candidate();
                return current&&current->kind==candidate.kind&&current->owner_id==owner&&current->semantic_key==key;
            };
            check(selected(),"Click did not confirm the treatment dimension");
            mouse(QEvent::MouseMove,QPointF(20,view->height()-20),Qt::NoButton,Qt::NoButton);
            check(selected(),"Leaving the treatment dimension cleared its confirmed selection");
            check(view->dimension_layout_editable(candidate),"Treatment disables purple grip editing");
            const auto source=view->dimension_source(candidate);
            check(source.has_value(),"Missing source annotation");
            const int count=source->kind==kernel::ViewerDimensionKind::Radius?2:3;
            for(int i=0;i<count;++i) {
                const auto point=view->dimension_handle_position(candidate,i);
                check(point.has_value(),"Purple grip is missing");
                mouse(QEvent::MouseMove,*point,Qt::NoButton,Qt::NoButton);
                check(selected(),"Moving to a purple grip cleared its selection");
            }
            const auto from=*view->dimension_handle_position(candidate,handle),to=from+QPointF(38,-27);
            mouse(QEvent::MouseButtonPress,from,Qt::LeftButton,Qt::LeftButton);
            mouse(QEvent::MouseMove,to,Qt::NoButton,Qt::LeftButton);
            const auto moved=view->dimension_handle_position(candidate,handle);
            check(moved&&QLineF(from,*moved).length()>1,"Dragging the purple grip did not move it");
            if(handle==0) {
                mouse(QEvent::MouseButtonPress,to,Qt::RightButton,Qt::LeftButton|Qt::RightButton);
                mouse(QEvent::MouseButtonRelease,to,Qt::RightButton,Qt::LeftButton);
            }
            if(cancel) {QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);flush();}
            mouse(QEvent::MouseButtonRelease,to,Qt::LeftButton,Qt::NoButton);
            check(selected(),"Finishing a grip drag lost the selected dimension");
            check(view->dimension_source(candidate)==source,"Annotation drag changed its source geometry or value");
        };
        for(int mode=0;mode<5;++mode) {
            const bool fillet=mode<2;
            const std::string prefix=fillet?"fillet":"chamfer";
            const auto name=stem+"-"+std::to_string(mode);
            run("new",{{"type","part"},{"name",name}});
            const auto box=run("box.create",{{"length_mm","20"},{"width_mm","20"},{"height_mm","20"}}).at("container").get<std::string>();
            // Horizontal as well as vertical edges expose an incorrect default
            // XY annotation plane on Chamfer distance dimensions.
            const std::string edge=mode==3?"edge:x_max:y_max:z_max--x_min:y_max:z_max":"edge:x_max:y_min:z_max--x_max:y_min:z_min";
            Json args={{"routes",Json::array({Json{{"edges",Json::array({Json{{"owner",box},{"key",edge}}})}}})}};
            if(fillet) {args["radius_mm"]=2;if(mode==1){args["mode"]="linear";args["radius_end_mm"]=3;}}
            else {args["distance_a_mm"]=2;if(mode==3){args["mode"]="two_distances";args["distance_b_mm"]=3;}if(mode==4){args["mode"]="distance_angle";args["angle_degrees"]=35;}}
            if(mode==1) {
                const auto edges=run("edge_treatment.edges",{{"owner",box}}).at("items");
                const auto found=std::ranges::find_if(edges,[&](const auto& item){return item.at("key")==edge;});
                check(found!=edges.end(),"Missing input edge for explicit R1");
                auto start=found->at("segments")[0].at("endpoints")[0];start.erase("position_mm");
                args["routes"][0]["start"]=start;
            }
            const auto owner=run((prefix+".create").c_str(),args).at("container").get<std::string>();
            run("save");const auto file=directory/(name+".prtz");
            view->set_standard_view(viewer::StandardView::Isometric);
            for(auto* animation:view->findChildren<QVariantAnimation*>())animation->setCurrentTime(animation->duration());
            flush();
            auto* pending=edit(owner);
            std::vector<std::string> keys{"parameter:primary"};
            if(mode==1||mode==3)keys.push_back("parameter:secondary");
            if(mode==4)keys.push_back("parameter:treatment_angle");
            for(const auto& key:keys)for(int handle=0;handle<(fillet?2:3);++handle) {
                const auto before=pending->pending_dimension_layout({owner,key,{}});
                drag(owner,key,handle);
                check(pending->pending_dimension_layout({owner,key,{}})!=before,"Grip did not update pending annotation layout");
            }
            check(pending->pending_value().edge_treatment.primary_size==2,"Dragging a label changed its model parameter");
            click(QPointF(20,view->height()-20));check(!view->confirmed_candidate(),"Empty View click retained the treatment dimension");
            finish(false);run("save");
            check(document::PartDocument::load(file).dimension_layouts.empty(),"Cancel committed pending annotation layouts");
            pending=edit(owner);drag(owner,"parameter:primary",0);
            const auto committed=pending->dimension_layouts();
            drag(owner,"parameter:primary",1,true);
            check(pending->dimension_layouts()==committed,"Escape kept a pending grip drag");
            pending->findChild<QDoubleSpinBox*>("edgeTreatmentPrimary")->setValue(2.25);flush();
            finish(true);run("save");
            auto saved=document::PartDocument::load(file);
            check(saved.dimension_layouts==committed&&saved.find_container(owner)->edge_treatment.primary_size==2.25,
                "OK failed to commit parameter and annotation together");
            run("undo");run("save");saved=document::PartDocument::load(file);
            check(saved.dimension_layouts.empty()&&saved.find_container(owner)->edge_treatment.primary_size==2,
                "Treatment parameter and layout did not undo together");
            run("redo");pending=edit(owner);
            check(pending->dimension_layouts()==committed,"Reopened Properties lost annotation placement");
            drag(owner,"parameter:primary",0);const auto layout_only=pending->dimension_layouts();
            finish(true);run("save");saved=document::PartDocument::load(file);
            check(saved.dimension_layouts==layout_only,"OK ignored an annotation-only edit");
            run("undo");run("save");check(document::PartDocument::load(file).dimension_layouts==committed,"Annotation-only OK did not undo in one step");
            // Ordinary double-click mode: select and drag with no dialog.
            view->confirm_container(owner);
            mouse(QEvent::MouseButtonDblClick,QPointF(view->rect().center()),Qt::LeftButton,Qt::LeftButton);
            for(const auto& key:keys)for(int handle=0;handle<(fillet?2:3);++handle) {
                run("save");const auto before=document::PartDocument::load(file);
                drag(owner,key,handle);run("save");const auto after=document::PartDocument::load(file);
                check(after.dimension_layouts!=before.dimension_layouts&&*after.find_container(owner)==*before.find_container(owner),
                    "View grip failed to persist or changed feature parameters");
                run("undo");run("save");check(document::PartDocument::load(file).dimension_layouts==before.dimension_layouts,
                    "View grip was not one Undo step");
            }
            // Value editing must remain available alongside the purple grips.
            const auto candidate=pick(viewer::CandidateKind::Dimension,owner,"parameter:primary");
            const auto label=view->candidate_dimension_label_position(candidate);check(label.has_value(),"Missing dimension label");
            mouse(QEvent::MouseButtonDblClick,*label,Qt::LeftButton,Qt::LeftButton);
            auto* input=view->findChild<QLineEdit*>("inlineDimensionValueEdit");check(input&&input->isVisible(),"Double-click did not open the dimension editor");
            input->setText("2.5");QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(input,&enter);flush();
            run("save");check(document::PartDocument::load(file).find_container(owner)->edge_treatment.primary_size==2.5,"Direct dimension value reverted");
            run("undo");
            if(mode==0||mode==3) {
                pick(viewer::CandidateKind::Dimension,owner,"parameter:primary");
                check(window.grab().save(QString::fromStdString((directory/(mode==0?"fillet-purple-grips.png":"chamfer-purple-grips.png")).string())),"Grip capture failed");
            }
            run("close",{{"discard",true}});
            std::cout << prefix << " mode " << mode << ": picker, every grip, Cancel/OK, Escape, Undo and direct View passed\n";
        }
        // Creation uses the same pending layout and OK/Cancel transaction.
        for(const bool fillet:{true,false})for(const bool commit:{false,true}) {
            const auto name=stem+"-create-"+std::to_string(fillet)+"-"+std::to_string(commit);
            run("new",{{"type","part"},{"name",name}});
            const auto box=run("box.create",{{"length_mm","20"},{"width_mm","20"},{"height_mm","20"}}).at("container").get<std::string>();
            window.findChild<QAction*>(fillet?"filletAction":"chamferAction")->trigger();flush();
            auto* pending=dialog();const auto owner=pending->container_id();
            pick(viewer::CandidateKind::Edge,box,{});
            check(pending->pending_value().edge_treatment.routes.size()==1,"Mouse did not select a creation edge");
            drag(owner,"parameter:primary",0);const auto layout=pending->dimension_layouts();
            finish(commit);run("save");const auto file=directory/(name+".prtz");
            const auto saved=document::PartDocument::load(file);
            check(saved.history.size()==(commit?2:1)&&saved.dimension_layouts==(commit?layout:std::vector<kernel::DimensionLayoutEntry>{}),
                "Creation did not commit/cancel geometry and annotation together");
            if(commit) {run("undo");run("save");check(document::PartDocument::load(file).history.size()==1&&document::PartDocument::load(file).dimension_layouts.empty(),"Creation Undo left its annotation behind");}
            run("close",{{"discard",true}});
        }
        std::cout << "Treatment creation grips and transactions passed\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<std::endl;return 1;}
}
}
