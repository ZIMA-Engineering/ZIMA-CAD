#include <QComboBox>
#include "primitive_properties_dialog.hpp"
#include "assembly_workspace_window.hpp"
#include "sketch_properties_dialog.hpp"
#include <zima/document/part_document.hpp>
#include <zima/viewer/mesh_view.hpp>
#include <QApplication>
#include <QAction>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QMouseEvent>
#include <QMenu>
#include <QTimer>
#include <QKeyEvent>
#include <QEventLoop>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QPointer>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>

namespace zima::app {
int verify_holes_ui(QApplication& application, AssemblyWorkspaceWindow& window,
    const std::filesystem::path& directory) {
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto run=[&](const char* command,commands::Json args=commands::Json::object()) {
        const auto json=commands::Json{{"command",command},{"arguments",std::move(args)}};
        auto result=window.execute_console_command(QString::fromStdString(json.dump()));
        if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.message);flush();return result.data;
    };
    try {
        window.showMaximized();flush();
        const auto name="holes-ui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        run("new",{{"type","part"},{"name",name}});
        const auto box=run("box.create",{{"length_mm","40"},{"width_mm","40"},{"height_mm","40"}})
            .at("container").get<std::string>();
        const auto source=run("sketch.create",{{"name","Channels"},{"plane","XY"}});
        const auto sketch=source.at("sketch").get<std::string>(),owner=source.at("owner").get<std::string>();
        run("sketch.segment.create",{{"sketch",sketch},{"first",{-20,0}},{"second",{20,0}}});
        run("save");
        const auto file=directory/(name+".prtz");
        auto* tree=window.findChild<QTreeWidget*>();auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));
        auto* action=window.findChild<QAction*>("holesAction");
        check(tree&&view&&action&&action->isEnabled()&&!action->icon().isNull(),"Holes action/icon missing");
        const auto select=[&] {
            for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==owner &&
                   (*it)->data(0,Qt::UserRole+3)=="part-container") {
                    tree->setCurrentItem(*it);(*it)->setSelected(true);return *it;
                }
            throw std::runtime_error("Holes container missing from Tree");
        };
        const auto edit=[&] {
            auto* row=select();
            for(auto* parent=row->parent();parent;parent=parent->parent())tree->expandItem(parent);
            tree->scrollToItem(row);
            bool invoked=false;
            QTimer::singleShot(0,&window,[&] {
                auto* menu=window.findChild<QMenu*>("partHistoryMenu");if(!menu)return;
                for(auto* action:menu->actions())if(action->text().startsWith(QString::fromUtf8("Vlastnosti"))) {
                    invoked=true;menu->setActiveAction(action);
                    QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
                    QApplication::sendEvent(menu,&enter);return;
                }
                menu->close();
            });
            tree->customContextMenuRequested(tree->visualItemRect(row).center());flush();
            check(invoked,"Holes Properties menu action missing");
        };
        const auto dialog=[&] {
            auto* result=dynamic_cast<SketchPropertiesDialog*>(window.findChild<QDialog*>("holesPropertiesDialog"));
            check(result&&result->isVisible(),"Holes properties did not open");
            check(result->parentWidget()==&window&&(result->windowFlags()&Qt::WindowType_Mask)==Qt::SubWindow,"Holes must be an internal properties window");
            check(!result->buttons()->button(QDialogButtonBox::Apply),"Holes exposed Apply");
            check(result->findChild<QDoubleSpinBox*>("holesDiameter")->mapTo(result,QPoint()).y() >
                  result->findChild<QDoubleSpinBox*>("sketchPlaneOffset")->mapTo(result,QPoint()).y(),
                  "Drilling diameter must follow the plane offset");
            return result;
        };
        const auto click=[&](const QPointF& position) {
            const auto global=QPointF(view->mapToGlobal(position.toPoint()));
            QMouseEvent move(QEvent::MouseMove,position,global,Qt::NoButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view,&move);
            for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                QMouseEvent event(type,position,global,Qt::LeftButton,
                    type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(view,&event);
            }
            flush();
        };
        const auto hit=[&](const auto& matches) -> QPointF {
            for(int y=12;y<view->height()-12;y+=6) for(int x=12;x<view->width()-12;x+=6) {
                const auto offered=view->selection_candidates_at(QPointF(x,y));
                if(!offered.empty()&&matches(offered.front()))return QPointF(x,y);
            }
            throw std::runtime_error("Sketcher did not offer the requested common-picker candidate");
        };
        const auto exercise_sketch=[&](SketchPropertiesDialog* pending,const char* capture) {
            const auto before=pending->pending_value().first;
            pending->findChild<QPushButton*>("sketchOpenButton")->click();flush();
            // Let the Sketch-normal camera animation finish before locating
            // a candidate and clicking the same screen position.
            QEventLoop alignment;QTimer::singleShot(950,&alignment,&QEventLoop::quit);alignment.exec();flush();
            auto* segment=window.findChild<QAction*>("sketchSegmentAction");
            auto* external=window.findChild<QAction*>("sketchExternalReferenceAction");
            auto* profile=window.findChild<QAction*>("sketchExternalProfileAction");
            auto* finish=window.findChild<QAction*>("finishSketchAction");
            check(segment&&external&&profile&&finish&&external->isEnabled()&&profile->isEnabled(),
                  "Owned Sketch reference tools unavailable");
            segment->trigger();flush();
            const auto origin=hit([&](const auto& value){return value.owner_id==before.id &&
                value.semantic_key=="external_point:sketch_origin";});
            check(window.grab().save(QString::fromStdString((directory/capture).string())),"Sketch screenshot failed");
            click(origin);click(origin+QPointF(85,-55));
            external->trigger();flush();
            check(external->isChecked(),"External reference mode did not start");
            const auto projectable=[&](const auto& value) {
                if(value.owner_id!=box || value.kind!=viewer::CandidateKind::Edge ||
                   value.geometry!=viewer::CandidateGeometry::OriginalReference)return false;
                const auto edge=view->candidate_edge(value);
                if(!edge || edge->points.size()<2)return false;
                const auto a=before.local_point(edge->points.front()),b=before.local_point(edge->points.back());
                return std::hypot(a[0]-b[0],a[1]-b[1])>1;
            };
            const auto source_hit=hit(projectable);
            const auto source_key=view->selection_candidates_at(source_hit).front().semantic_key;
            click(source_hit);
            const auto reference_status=window.findChild<QLabel*>("workspaceState")->text();
            profile->trigger();flush();
            check(profile->isChecked(),"Reference to outline mode did not start");
            click(hit([&](const auto& value){return projectable(value)&&value.semantic_key!=source_key;}));
            const auto profile_status=window.findChild<QLabel*>("workspaceState")->text();
            finish->trigger();flush();
            check(dialog()==pending,"Sketcher did not return to its owning Holes dialog");
            const auto after=pending->pending_value().first;
            if(after.external_references.size()!=before.external_references.size()+2)
                std::cerr<<"Reference click: "<<reference_status.toStdString()<<"; outline click: "<<profile_status.toStdString()<<'\n';
            check(after.external_references.size()==before.external_references.size()+2,
                  "Holes Sketch lost externally picked references");
            check(after.segments.size()==before.segments.size()+2,
                  "Holes Sketch lost the mouse-drawn segment or projected outline");
            check(std::ranges::any_of(after.constraints,[](const auto& constraint){
                return constraint.kind==sketcher::ConstraintKind::PointReference && constraint.second_point_id=="sketch_origin";
            }),"Clicking the Sketch origin failed to anchor the segment");
        };
        select();action->trigger();flush();
        auto* pending=dialog();pending->findChild<QDoubleSpinBox*>("holesDiameter")->setValue(6);
        pending->buttons()->button(QDialogButtonBox::Cancel)->click();flush();run("save");
        check(document::PartDocument::load(file).find_container(owner)->feature_kind==document::FeatureKind::Sketch,"Cancel converted the source Sketch");
        select();action->trigger();flush();pending=dialog();
        pending->findChild<QDoubleSpinBox*>("holesDiameter")->setValue(6);
        pending->findChild<QPushButton*>("sketchOpenButton")->click();flush();
        check(!pending->isVisible(),"Owned Sketch did not open");
        auto* finish=window.findChild<QAction*>("finishSketchAction");check(finish&&finish->isEnabled(),"Sketcher finish missing");
        finish->trigger();flush();check(dialog()==pending,"Sketcher returned to a different properties dialog");
        // Short MMB must not commit. Double MMB over the owning View must.
        const QPointF local(view->rect().center()),global(view->mapToGlobal(local.toPoint()));
        for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
            QMouseEvent event(type,local,global,Qt::MiddleButton,type==QEvent::MouseButtonPress?Qt::MiddleButton:Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view,&event);
        }
        flush();check(pending->isVisible(),"Short MMB committed Holes");
        QMouseEvent dbl(QEvent::MouseButtonDblClick,local,global,Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
        QApplication::sendEvent(view,&dbl);flush();
        check(!window.findChild<QDialog*>("holesPropertiesDialog"),"Middle double-click did not commit Holes");
        check(run("holes.get",{{"container",owner}}).at("diameter_mm")==6,"GUI lost drilling diameter");
        run("save");std::vector<kernel::BodyResult> cached;
        const auto native=document::PartDocument::load(file,&cached);
        check(std::abs(cached.back().volume-(64000-std::numbers::pi*9*40))<1e-5,"GUI drilled the wrong geometry");
        edit();pending=dialog();
        exercise_sketch(pending,"holes-edit-sketch.png");
        pending->findChild<QDoubleSpinBox*>("holesDiameter")->setValue(8);
        check(pending->mutate_sketch(sketch,[](auto& s){static_cast<void>(s.add_segment(0,-20,0,20));}),"Pending sketch mutation failed");
        check(window.grab().save(QString::fromStdString((directory/"holes-properties.png").string())),"Holes screenshot failed");
        pending->buttons()->button(QDialogButtonBox::Cancel)->click();flush();run("save");
        const auto cancelled=document::PartDocument::load(file);
        check(cancelled.find_container(owner)->holes.diameter==6 && cancelled.sketches.front().segments.size()==1,"Cancel retained a diameter or Sketch edit");
        edit();pending=dialog();
        pending->findChild<QDoubleSpinBox*>("holesDiameter")->setValue(8);
        pending->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(run("holes.get",{{"container",owner}}).at("diameter_mm")==8,"Edit OK lost diameter");
        run("undo");check(run("holes.get",{{"container",owner}}).at("diameter_mm")==6,"Edit Undo failed");
        // New feature may enter Sketcher without inserting an empty history item.
        tree->clearSelection();tree->setCurrentItem(nullptr);action->trigger();flush();pending=dialog();
        const auto new_sketch=pending->pending_value().first.id;
        exercise_sketch(pending,"holes-new-sketch.png");
        pending->mutate_sketch(new_sketch,[](auto& s){static_cast<void>(s.add_segment(-20,10,20,10));});
        pending->buttons()->button(QDialogButtonBox::Cancel)->click();flush();run("save");
        check(document::PartDocument::load(file).history.size()==native.history.size(),"New Holes Cancel inserted history");
        tree->clearSelection();tree->setCurrentItem(nullptr);action->trigger();flush();pending=dialog();
        const auto created_sketch=pending->pending_value().first.id;
        pending->findChild<QDoubleSpinBox*>("holesDiameter")->setValue(4);
        check(pending->mutate_sketch(created_sketch,[](auto& s){static_cast<void>(s.add_segment(-20,10,20,10));}),"New drilling sketch mutation failed");
        pending->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(!window.findChild<QDialog*>("holesPropertiesDialog"),"New Holes OK failed");
        run("save");cached.clear();const auto created=document::PartDocument::load(file,&cached);
        check(created.history.size()==native.history.size()+1,"New Holes OK did not insert history");
        check(std::abs(cached.back().volume-(64000-std::numbers::pi*(9+4)*40))<1e-5,"New Holes drilled wrong geometry");
        run("undo");run("save");
        check(document::PartDocument::load(file).history.size()==native.history.size(),"New Holes Undo did not remove history");
        std::cout<<"Holes GUI creation, conversion, draft, Cancel, OK, MMB, persistence and Undo passed\n";
        return 0;
    } catch(const std::exception& error) {
        window.grab().save(QString::fromStdString((directory/"holes-ui-failure.png").string()));
        std::cerr<<"Holes GUI: "<<error.what()<<'\n';return 1;
    }
}
}

namespace zima::app {
int verify_work_plane_ui(QApplication& application, AssemblyWorkspaceWindow& window, const std::filesystem::path& directory) {
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto run=[&](const char* name,commands::Json args=commands::Json::object()) {
        auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",name},{"arguments",args}}.dump()));
        if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.message);flush();return result.data;
    };
    std::string stage;
    try {
        window.showMaximized();flush();
        for(const std::string kind:{"sketch","holes","extrusion","revolution","plane"}) {
            stage=kind;
            auto doc=document::PartDocument::create_default();doc.name="Plane "+kind;
            auto box=document::PartDocument::create_box_container();box.box={40,40,40};doc.history.push_back(box);
            auto sketch=sketcher::Sketch::create_default();
            auto feature=kind=="extrusion"?document::PartDocument::create_extrusion_container(sketch.id):kind=="revolution"?document::PartDocument::create_revolution_container(sketch.id):document::PartDocument::create_sketch_container();
            feature.name=kind;sketch.owner_container_id=feature.id;
            document::ConstructionReference reference;reference.owner_id=doc.document_id+":origin";reference.semantic_key="origin:plane:xy";
            reference.supports_offset=true;reference.orientation_drives_rotation=true;reference.orientation_role="front";
            feature.placement.references={reference};
            sketch.plane_offset=3;
            feature.extrusion.profile_plane_offset=3;
            feature.revolution.profile_plane_offset=3;
            if(kind=="holes") {feature.feature_kind=document::FeatureKind::Holes;feature.combine_mode=document::CombineMode::Subtract;feature.holes.sketch_id=sketch.id;static_cast<void>(sketch.add_segment(-20,0,20,0));}
            else if(kind=="revolution") {
                static_cast<void>(sketch.add_rectangle(2,-3,4,3));feature.revolution.axis_segment_id=sketch.add_segment(0,-5,0,5);
                sketch.segments.back().construction=true;sketch.segments.back().centerline=true;
            } else static_cast<void>(sketch.add_circle(0,0,2));
            std::string owner=feature.id;
            if(kind=="plane") {auto plane=document::PartDocument::create_construction(document::ConstructionKind::Plane);plane.definition=document::ConstructionDefinition::PointReference;plane.references={reference};plane.offset=3;owner=plane.id;doc.constructions.push_back(plane);}
            else {doc.history.push_back(feature);doc.sketches.push_back(sketch);}
            doc.resolve_constructions(doc.origin_viewer_mesh().original_references);
            kernel::OcctKernel kernel;const auto bodies=kernel.evaluate_history(doc.kernel_operations());
            const auto file=directory/("work-plane-"+kind+"-"+doc.document_id+".prtz");doc.save(file,bodies);
            run("open",{{"path",file.string()}});
            auto* tree=window.findChild<QTreeWidget*>("documentTree");check(tree,"Tree missing");
            const auto edit=[&] {
                QTreeWidgetItem* row=nullptr;
                for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==owner&&(*it)->data(0,Qt::UserRole+3).toString()==(kind=="plane"?"part-construction":"part-container")){row=*it;break;}
                check(row,"Work-plane owner missing from Tree");
                for(auto* parent=row->parent();parent;parent=parent->parent())tree->expandItem(parent);
                tree->clearSelection();tree->setCurrentItem(row);row->setSelected(true);tree->scrollToItem(row);
                bool invoked=false;
                QTimer::singleShot(0,&window,[&] {
                    auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());if(!menu)return;
                    for(auto* action:menu->actions())if(action->text().startsWith(QString::fromUtf8("Vlastnosti"))) {
                        invoked=true;menu->setActiveAction(action);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&enter);return;
                    }menu->close();
                });
                tree->customContextMenuRequested(tree->visualItemRect(row).center());flush();check(invoked,"Properties menu not invoked");
            };
            const char* combo_name=kind=="plane"?"constructionBasePlane":kind=="extrusion"||kind=="revolution"?"profilePlane":"sketchPlane";
            const auto dialog=[&] {
                for(auto* candidate:window.findChildren<QDialog*>())if(candidate->isVisible()&&candidate->findChild<QComboBox*>(combo_name))return candidate;
                throw std::runtime_error("Work-plane Properties missing");
            };
            edit();auto* pending=dialog();auto* combo=pending->findChild<QComboBox*>(combo_name);
            check(combo->currentData()==QVariant("auto"),"First reference was not automatic");
            const auto plane_border=[&] {
                auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));check(view,"View missing");
                for(const auto& edge:view->mesh().edges)
                    if(edge.reference.semantic_key=="border" && edge.points.size()==5)return edge.points;
                throw std::runtime_error("Live offset-plane border missing");
            };
            const auto automatic_border=plane_border();
            const QVariant xy=kind=="plane"?QVariant("xy"):QVariant(static_cast<int>(sketcher::SketchPlane::XY));
            combo->setCurrentIndex(combo->findData(xy));flush();
            const auto manual_border=plane_border();
            const auto center=[](const auto& border) {return kernel::Vec3{(border[0].x+border[2].x)/2,(border[0].y+border[2].y)/2,(border[0].z+border[2].z)/2};};
            const auto a=center(automatic_border),b=center(manual_border);
            check(std::abs(a.x-b.x)+std::abs(a.y-b.y)+std::abs(a.z-b.z)>1,
                "Changing work plane did not move the live offset-plane border");
            const auto dot=[](const auto& a,const auto& b){return a.x*b.x+a.y*b.y+a.z*b.z;};
            const auto difference=[](const auto& a,const auto& b){return kernel::Vec3{a.x-b.x,a.y-b.y,a.z-b.z};};
            std::vector<kernel::Vec3> centers;
            for(const auto plane:{sketcher::SketchPlane::XY,sketcher::SketchPlane::XZ,sketcher::SketchPlane::YZ}) {
                const QVariant key=kind=="plane"?QVariant(plane==sketcher::SketchPlane::XY?"xy":plane==sketcher::SketchPlane::XZ?"xz":"yz"):QVariant(static_cast<int>(plane));
                combo->setCurrentIndex(combo->findData(key));flush();
                const auto border=plane_border();const auto c=center(border);
                check(std::abs(dot(c,c)-9)<1e-8,"Live plane lost its 3 mm offset");
                check(std::abs(dot(c,difference(border[1],border[0])))<1e-8 &&
                    std::abs(dot(c,difference(border[3],border[0])))<1e-8,
                    "Live plane offset is not perpendicular to its border");
                for(const auto& previous:centers)check(std::abs(dot(c,previous))<1e-8,
                    "Live border did not rotate with the selected local plane");
                centers.push_back(c);
            }
            combo->setCurrentIndex(combo->findData("auto"));flush();
            const auto restored=plane_border();
            for(std::size_t i=0;i<restored.size();++i) {
                const auto delta=difference(restored[i],automatic_border[i]);
                check(dot(delta,delta)<1e-8,"AUTO did not restore the live plane border");
            }
            combo->setCurrentIndex(combo->findData(xy));flush();
            pending->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();run("save");
            auto unchanged=document::PartDocument::load(file);
            check(kind=="plane"?unchanged.find_construction(owner)->base_plane_auto:unchanged.sketches.front().plane_auto,"Cancel changed plane mode");
            edit();pending=dialog();combo=pending->findChild<QComboBox*>(combo_name);combo->setCurrentIndex(combo->findData(xy));flush();
            if(kind=="holes"||kind=="extrusion"||kind=="revolution") {
                auto* button=pending->findChild<QPushButton*>(kind=="holes"?"sketchOpenButton":"primitiveOwnSketchButton");check(button,"Owned Sketch button missing");button->click();flush();
                auto* finish=window.findChild<QAction*>("finishSketchAction");check(finish&&finish->isEnabled(),"Sketcher did not open");finish->trigger();flush();
                pending=dialog();check(pending->findChild<QComboBox*>(combo_name)->currentData()==xy,"Sketcher return lost manual plane");
            }
            pending->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();run("save");
            auto saved=document::PartDocument::load(file);
            check(kind=="plane"?!saved.find_construction(owner)->base_plane_auto:!saved.sketches.front().plane_auto,"OK lost manual choice");
            if(kind=="plane")check(!saved.find_construction(owner)->references.empty(),"Plane OK lost first reference");
            edit();pending=dialog();combo=pending->findChild<QComboBox*>(combo_name);check(combo->currentData()==xy,"Reopened Properties lost plane");
            window.grab().save(QString::fromStdString((directory/("work-plane-"+kind+".png")).string()));
            combo->setCurrentIndex(combo->findData("auto"));flush();pending->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();run("save");
            auto automatic=document::PartDocument::load(file);
            check(kind=="plane"?automatic.find_construction(owner)->base_plane_auto:automatic.sketches.front().plane_auto,"GUI did not restore AUTO");
        }
        std::cout<<"Work-plane GUI: all five live offset-plane borders, XY/XZ/YZ/AUTO, perpendicular 3 mm offsets, Cancel, OK, reopen and owned Sketch return passed\n";return 0;
    }catch(const std::exception& error){window.grab().save(QString::fromStdString((directory/"work-plane-ui-failure.png").string()));std::cerr<<"Work-plane GUI ("<<stage<<"): "<<error.what()<<'\n';return 1;}
}
}
