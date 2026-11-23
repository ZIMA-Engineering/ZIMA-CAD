#include "../tests/gui_profile_fixture.hpp"
#include "authored_feature_wire.hpp"
#include <zima/kernel/calculation_diagnostics.hpp>
#include "../tests/profile_solid_fixture.hpp"
#include "../tests/drill_point_test_support.hpp"
#include <QTableWidget>
#include <QEventLoop>
#include <QTimer>
#include "assembly_workspace_window.hpp"
#include "primitive_properties_dialog.hpp"
#include "sheet_from_body_dialog.hpp"
#include "drawing_depth_view.hpp"
#include "workspace/workspace_internal.hpp"
#include <zima/viewer/mesh_view.hpp>
#include <zima/viewer/shading.hpp>
#include <QApplication>
#include <QCursor>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/document/edge_treatment_selection.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/workspace/edge_treatment_operations.hpp>
#include <zima/workspace/operation_input.hpp>
#include <numbers>
#include <QAction>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QPointer>
#include <QPainter>
#include <QQuaternion>
#include <zima/kernel/dimension_layout.hpp>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVariantAnimation>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <numeric>

namespace zima::app {
int verify_edge_treatment_ui(QApplication& application, AssemblyWorkspaceWindow& window,
    const std::filesystem::path& directory) {
    using commands::Json;
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto wire_contains=[](const kernel::ViewerEdge& wire,
            const kernel::Vec3& point,double tolerance=1.0e-6) {
        for(std::size_t index=1;index<wire.points.size();++index) {
            const auto& first=wire.points[index-1];
            const auto& second=wire.points[index];
            const kernel::Vec3 direction{second.x-first.x,
                second.y-first.y,second.z-first.z};
            const kernel::Vec3 delta{point.x-first.x,
                point.y-first.y,point.z-first.z};
            const double length2=direction.x*direction.x+
                direction.y*direction.y+direction.z*direction.z;
            const double parameter=length2<=1.0e-24?0.:
                std::clamp((delta.x*direction.x+delta.y*direction.y+
                    delta.z*direction.z)/length2,0.,1.);
            const kernel::Vec3 closest{first.x+parameter*direction.x,
                first.y+parameter*direction.y,
                first.z+parameter*direction.z};
            if(std::hypot(point.x-closest.x,point.y-closest.y,
                    point.z-closest.z)<=tolerance)
                return true;
        }
        return false;
    };
    const auto verify_terminal_contacts=[&](
            const std::vector<kernel::ViewerEdge>& wires,
            const std::vector<kernel::Vec3>& contacts) {
        for(const auto& point:contacts) {
            const auto contact_count=std::ranges::count_if(wires,
                [&](const auto& wire){return wire_contains(wire,point);});
            check(contact_count>=2,
                "A Fillet/Chamfer preview rail extends beyond its terminal section");
        }
    };
    const auto route_node_counts=[&](
            const std::vector<std::vector<kernel::ViewerEdge>>& groups) {
        std::vector<std::size_t> counts;
        for(const auto& group:groups)
            for(const auto& path:workspace_detail::
                    ordered_edge_treatment_preview_paths(
                        document::actual_edge_treatment_intervals(group)))
                counts.push_back(workspace_detail::
                    edge_treatment_preview_path_nodes(path).size());
        return counts;
    };
    const auto verify_route_labels=[&](
            const std::vector<std::pair<kernel::Vec3,std::string>>& labels,
            const std::vector<viewer::InteractiveLabel>& terminal_labels,
            const std::vector<std::size_t>& counts) {
        check(!counts.empty()&&std::ranges::all_of(counts,
                [](std::size_t count){return count>0;}),
            "Fillet/Chamfer routes have no source nodes");
        std::multiset<std::string> actual;
        for(const auto& label:labels)actual.insert(label.second);
        std::size_t terminal_token_count{};
        for(const auto& label:terminal_labels) {
            std::size_t begin{};
            while(begin<label.text.size()) {
                const auto separator=label.text.find('-',begin);
                actual.insert(label.text.substr(begin,separator-begin));
                ++terminal_token_count;
                if(separator==std::string::npos)break;
                begin=separator+1;
            }
        }
        check(labels.size()+terminal_token_count==
                std::accumulate(counts.begin(),counts.end(),std::size_t{}),
            "Fillet/Chamfer routes omitted or duplicated source-node labels");
        for(std::size_t path=0;path<counts.size();++path) {
            const auto prefix=std::to_string(path+1)+".";
            for(std::size_t node=0;node<counts[path];++node)
                check(actual.erase(prefix+std::to_string(node+1))==1,
                    "Fillet/Chamfer source-node labels are not sequential");
        }
        check(actual.empty(),
            "Fillet/Chamfer route labels contain an unexpected node");
    };
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto run=[&](const char* command,Json args=Json::object()) {
        const auto result=window.execute_console_command(QString::fromStdString(Json{{"command",command},{"arguments",args}}.dump()));
        if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.message);
        flush();return result.data;
    };
    try {
        using TerminalLabel=workspace_detail::EdgeTreatmentPreviewGeometry::TerminalLabel;
        const kernel::VertexReference joined_vertex{"feature-a","vertex:7",{}};
        const kernel::Vec3 joined_point{3.,4.,5.};
        const auto joined_nodes=workspace_detail::edge_treatment_terminal_nodes({
            TerminalLabel{joined_point,"1.2",0,false,joined_vertex},
            TerminalLabel{joined_point,"2.1",1,true,joined_vertex},
            TerminalLabel{joined_point,"3.1",2,true,joined_vertex}});
        check(joined_nodes.size()==1&&
                joined_nodes.front().text=="1.2-2.1-3.1"&&
                joined_nodes.front().route_ends==
                    std::vector<std::pair<std::size_t,bool>>{
                        {0,false},{1,true},{2,true}},
            "Connected treatment route ends do not share one compound node label");
        const auto distinct_nodes=workspace_detail::edge_treatment_terminal_nodes({
            TerminalLabel{joined_point,"1.2",0,false,joined_vertex},
            TerminalLabel{joined_point,"2.1",1,true,
                kernel::VertexReference{"feature-b","vertex:7",{}}}});
        check(distinct_nodes.size()==2,
            "Coincident but topologically separate treatment ends were grouped");
        // Keep the GUI fixture independent of the desktop resolution.
        window.showNormal();window.resize(1500,950);flush();
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
            const auto step=qEnvironmentVariableIsSet("ZIMA_VERIFY_PICK_MODEL")?2:6;
            for(int y=12;y<view->height()-12;y+=step)for(int x=12;x<view->width()-12;x+=step) {
                const auto offered=view->selection_candidates_at(QPointF(x,y));
                if(!offered.empty()&&offered.front().kind==kind&&offered.front().owner_id==owner&&
                    (key.empty()||offered.front().semantic_key==key)) {
                    const auto result=offered.front();click(QPointF(x,y));return result;
                }
            }
            window.grab().save(QString::fromStdString((directory/"edge-treatment-pick-failure.png").string()));
            throw std::runtime_error("Common picker did not offer "+owner+"/"+key);
        };
        const auto dialog=[&] {
            for(auto* item:window.findChildren<QDialog*>())
                if(auto* value=dynamic_cast<PrimitivePropertiesDialog*>(item);value&&value->isVisible()&&
                    (value->findChild<QDoubleSpinBox*>("edgeTreatmentPrimary")||
                     (qEnvironmentVariableIsSet("ZIMA_VERIFY_FORM_TOPOLOGY")&&value->findChild<QTableWidget*>("shellFaces"))||
                     (qEnvironmentVariableIsSet("ZIMA_VERIFY_FORM_TOPOLOGY")&&value->findChild<QTableWidget*>("primitiveReferenceTable"))||
                     ((qEnvironmentVariableIsSet("ZIMA_VERIFY_SHELL_OPENING")||qEnvironmentVariableIsSet("ZIMA_VERIFY_TREATMENT_SELECTION_ONLY"))&&value->findChild<QDoubleSpinBox*>("shellThickness"))||
                     (qEnvironmentVariableIsSet("ZIMA_VERIFY_DRILL_OPENING")&&value->findChild<QDoubleSpinBox*>("drillPointIncludedAngle"))))return value;
            throw std::runtime_error("Treatment Properties is missing");
        };
        const auto edit=[&](const std::string& owner) {
            QTreeWidgetItem* selected=nullptr;
            for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==owner&&(*it)->data(0,Qt::UserRole+3)=="part-container") {
                    selected=*it;break;
                }
            check(selected,"Missing treatment tree item");
            window.show_tree_item_properties(selected);flush();
            // Frame after Properties has installed its rollback scene; leave
            // space for the horizontal-edge dimensions outside the solid.
            view->fit_all();auto camera=view->camera_state();camera[4]*=2;view->set_camera_state(camera);flush();
            return dialog();
        };
        const auto finish=[&](bool commit) {
            dialog()->findChild<QDialogButtonBox*>()->button(commit?QDialogButtonBox::Ok:QDialogButtonBox::Cancel)->click();flush();
        };
        if(const auto model=qEnvironmentVariable("ZIMA_VERIFY_TREATMENT_ROLLBACK_MODEL");!model.isEmpty()) {
            const auto file=directory/"treatment-rollback.prtz";
            std::filesystem::copy_file(std::filesystem::u8path(model.toStdString()),file);
            std::vector<kernel::BodyResult> cached;
            const auto source=document::PartDocument::load(file,&cached);
            const auto& feature=source.history.back();
            document::DocumentSession session(source,cached);
            const auto boundary=session.rollback_boundary(feature.id);
            check(boundary&&boundary->input_body,"Missing treatment rollback input");
            const auto& expected=boundary->input_body->mesh;
            check(expected.triangle_references.size()>1000,
                "Rollback fixture retained only a narrow dependency packet");
            run("open",{{"path",document::path_to_utf8(file)}});
            view->set_standard_view(viewer::StandardView::Isometric);flush();
            const auto before=run("documents");
            const auto triangle_owners=[](const auto& mesh) {
                std::map<std::string,std::size_t> result;
                for(const auto& ref:mesh.triangle_references)++result[ref.owner_id];
                return result;
            };
            const auto complete=triangle_owners(expected);
            check(complete.size()>=3,"Rollback fixture must contain both features and Hole");
            const auto calculations=kernel::body_calculation_count.load();
            for(const bool accept:{false,true,false,true}) {
                edit(feature.id);
                check(view->display_mode()==viewer::DisplayMode::ShadedWithEdges,
                    "Treatment Properties changed the display mode");
                check(triangle_owners(view->mesh())==complete,
                    "Treatment Properties omitted shaded input faces");
                window.grab().save(QString::fromStdString(document::path_to_utf8(directory/"treatment-rollback.png")));
                finish(accept);
                check(run("documents")==before,"Unchanged treatment changed document revision");
            }
            check(kernel::body_calculation_count.load()==calculations,
                "Opening unchanged treatment recalculated its Body");
            double previous=feature.edge_treatment.primary_size;
            for(const double radius:{.75*feature.edge_treatment.primary_size,
                    1.25*feature.edge_treatment.primary_size,feature.edge_treatment.primary_size}) {
                auto* pending=edit(feature.id);
                pending->findChild<QDoubleSpinBox*>("edgeTreatmentPrimary")->setValue(radius);
                flush();finish(true);
                check(run("fillet.get",{{"container",feature.id}}).at("radius_mm")==radius,
                    "Treatment radius edit was not committed");
                run("undo");
                check(run("fillet.get",{{"container",feature.id}}).at("radius_mm")==previous,
                    "Treatment after Hole lost its previous radius on Undo");
                run("redo");run("save");run("close");
                std::vector<kernel::BodyResult> saved_cache;
                const auto saved=document::PartDocument::load(file,&saved_cache);
                check(saved.find_container(feature.id)->edge_treatment.routes==feature.edge_treatment.routes&&
                    saved.find_container(feature.id)->edge_treatment.route_end_faces==feature.edge_treatment.route_end_faces,
                    "Treatment edit changed its route or endpoint identities");
                check(!saved_cache.empty()&&saved_cache.back().calculation_errors.empty(),
                    "Treatment after Hole saved an invalid calculation");
                run("open",{{"path",document::path_to_utf8(file)}});
                check(run("fillet.get",{{"container",feature.id}}).at("radius_mm")==radius,
                    "Treatment radius was lost on native reopening");
                const auto count=kernel::body_calculation_count.load();
                edit(feature.id);
                check(triangle_owners(view->mesh())==complete,
                    "Reopened treatment lost its complete shaded Hole input");
                finish(false);
                check(kernel::body_calculation_count.load()==count,
                    "Reopened treatment Properties recalculated its Body");
                previous=radius;
            }
            std::cout<<"Treatment rollback: complete shaded Hole input, repeated Cancel/OK, zero opening calculations, radius edits, Undo/Redo and native reopening passed\n";
            return 0;
        }
        if(const auto model=qEnvironmentVariable("ZIMA_VERIFY_CONNECTED_FILLET_SOURCE");!model.isEmpty()) {
            const auto file=directory/"connected-fillet-input.prtz";
            std::filesystem::copy_file(std::filesystem::u8path(model.toStdString()),file);
            const auto original=document::PartDocument::load(file);
            run("open",{{"path",document::path_to_utf8(file)}});
            view->set_standard_view(viewer::StandardView::Isometric);
            for(auto* animation:view->findChildren<QVariantAnimation*>())animation->setCurrentTime(animation->duration());
            view->fit_all();flush();
            const auto available=run("edge_treatment.edges").at("items");
            const auto seed=std::ranges::find_if(available,[](const auto& row){return row.at("segments").size()>1&&!row.at("ambiguous").template get<bool>();});
            check(seed!=available.end(),"Connected-curve fixture has no proven split input edge");
            const auto revision=run("documents").at(0).at("revision");
            window.findChild<QAction*>("filletAction")->trigger();flush();
            static_cast<void>(pick(viewer::CandidateKind::Edge,seed->at("owner"),seed->at("key")));
            auto* pending=dialog();const auto owner=pending->container_id();
            const auto routes=pending->pending_value().edge_treatment.routes;
            check(!routes.empty(),"Mouse did not retain the connected curve");
            for(const auto& route:routes)for(std::size_t i=0;i<route.size();++i)
                check(std::find(route.begin(),route.begin()+i,route[i])==route.begin()+i,"Mouse duplicated a persisted curve in its route");
            pending->findChild<QDoubleSpinBox*>("edgeTreatmentPrimary")->setValue(1);flush();
            const auto& node_labels=view->transient_labels();
            std::vector<std::vector<kernel::ViewerEdge>> source_groups;
            for(std::size_t route=0;route<routes.size();++route)
                source_groups.push_back(document::current_treatment_selection_wire(
                    pending->pending_value().edge_treatment,route,
                    std::nullopt,view->mesh()));
            verify_route_labels(node_labels,view->interactive_labels(),
                route_node_counts(source_groups));
            const auto source_preview=workspace_detail::
                edge_treatment_preview_wire(source_groups,
                    pending->pending_value().edge_treatment,
                    pending->feature_kind(),owner,&view->mesh());
            check(node_labels==source_preview.labels,
                "Connected Fillet did not label every source node in its route");
            verify_terminal_contacts(source_preview.edges,
                source_preview.terminal_contacts);
            check(run("documents").at(0).at("revision")==revision,"Hover/reference draft committed a model transaction");
            finish(true);run("save");
            check(view->transient_labels().empty(),
                "Fillet terminal labels survived closing Properties");
            const auto volume=[&] {
                std::vector<kernel::BodyResult> bodies;const auto saved=document::PartDocument::load(file,&bodies);
                check(!bodies.empty(),"Saved connected Fillet has no calculated body");
                check(saved.history.size()==original.history.size()+1&&
                    std::equal(original.history.begin(),original.history.end(),saved.history.begin()),"Fillet changed its source definitions");
                auto result=bodies.back();kernel::OcctKernel exact;exact.ensure_body_properties(result);return std::abs(result.volume);
            };
            const double first_volume=volume();
            const auto created=run("fillet.get",{{"container",owner}});
            check(created.at("radius_mm")==1,"GUI Fillet did not commit its radius");
            auto before=run("documents").at(0).at("revision");
            pending=edit(owner);pending->findChild<QDoubleSpinBox*>("edgeTreatmentPrimary")->setValue(1.5);flush();finish(false);
            check(run("documents").at(0).at("revision")==before,"Cancel changed the connected-curve model");
            pending=edit(owner);pending->findChild<QDoubleSpinBox*>("edgeTreatmentPrimary")->setValue(1.5);flush();finish(true);run("save");
            const double second_volume=volume();check(std::abs(first_volume-second_volume)>1e-6,"Radius edit did not change actual solid volume");
            run("undo");check(run("fillet.get",{{"container",owner}}).at("radius_mm")==1,"Connected Fillet edit Undo failed");
            run("redo");check(run("fillet.get",{{"container",owner}}).at("radius_mm")==1.5,"Connected Fillet edit Redo failed");
            check(run("fillet.get",{{"container",owner}}).at("routes")==created.at("routes"),"GUI edits changed original route identities");
            before=run("documents").at(0).at("revision");edit(owner);finish(true);
            check(run("documents").at(0).at("revision")==before,"Unchanged connected Fillet OK created a transaction");
            window.grab().save(QString::fromStdString(document::path_to_utf8(directory/"connected-fillet-result.png")));
            std::cout<<"Connected Fillet: actual hover/LMB, deduplicated route, OK/Cancel, real volume change, Undo/Redo and unchanged OK passed\n";
            return 0;
        }
        if(const auto model=qEnvironmentVariable("ZIMA_VERIFY_MERGED_FACE_MODEL");!model.isEmpty()) {
            std::filesystem::create_directories(directory);
            const auto file=directory/"merged-face-input.prtz";
            std::filesystem::copy_file(std::filesystem::u8path(model.toStdString()),file,
                std::filesystem::copy_options::overwrite_existing);
            run("open",{{"path",document::path_to_utf8(file)}});
            run("regenerate");run("save");run("close");
            run("open",{{"path",document::path_to_utf8(file)}});
            std::vector<kernel::BodyResult> cache;
            const auto source=document::PartDocument::load(file,&cache);
            document::DocumentSession session(source,cache);
            const auto* body=workspace::calculated_operation_input(session);
            check(body,"Merged-face fixture has no current Body");
            std::map<std::string,std::vector<kernel::FaceReference>> aliases;
            for(const auto& binding:body->kernel_faces)aliases[binding.locator].push_back(binding.reference);
            std::vector<kernel::FaceReference> faces;
            for(const auto& [locator,group]:aliases)if(group.size()==2) {
                const auto& references=body->mesh.original_references.triangle_references;
                const auto face=std::ranges::find_if(references,[&](const auto& item) {
                    return item.surface&&item.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&
                        std::ranges::find(group,item)!=group.end()&&
                        std::ranges::find(body->mesh.triangle_references,item)!=body->mesh.triangle_references.end();
                });
                if(face!=references.end())faces.push_back(*face);
            }
            check(!faces.empty(),"Merged-face fixture has no selectable two-sided wall");
            const auto revision=run("documents").at(0).at("revision");
            const auto calculations=kernel::body_calculation_count.load();
            const auto select_face=[&](const kernel::FaceReference& face) {
                for(const double sign:{1.,-1.}) {
                    const auto normal=face.surface->axis;
                    view->set_view_direction({sign*normal.x,sign*normal.y,sign*normal.z});
                    for(auto* animation:view->findChildren<QVariantAnimation*>())animation->setCurrentTime(animation->duration());
                    view->fit_all();flush();
                    for(int y=20;y<view->height()-20;y+=12)for(int x=20;x<view->width()-20;x+=12) {
                        const auto candidates=view->selection_candidates_at({double(x),double(y)});
                        if(!candidates.empty()&&candidates.front().kind==viewer::CandidateKind::Face&&
                                candidates.front().owner_id==face.owner_id&&candidates.front().semantic_key==face.semantic_key) {
                            click({double(x),double(y)});return;
                        }
                    }
                }
                throw std::runtime_error("Merged wall is not selectable: "+face.semantic_key);
            };
            for(const auto& face:faces) {
                window.findChild<QAction*>("featureShortcut1Action")->trigger();flush();
                auto* feature=dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("featurePropertiesDialog"));
                check(feature,"Merged-face Extrusion Properties missing");
                feature->findChild<QTableWidget*>("primitiveReferenceTable")->cellClicked(0,1);flush();
                select_face(face);
                const auto refs=feature->pending_value().placement.references;
                check(!refs.empty()&&refs.front().owner_id==face.owner_id&&refs.front().semantic_key==face.semantic_key,
                    "Extrusion mouse click did not accept the merged wall");
                feature->reject();flush();
                window.show_sheet_from_body();flush();
                auto* sheet=dynamic_cast<SheetFromBodyDialog*>(window.findChild<QWidget*>("sheetFromBodyDialog"));
                check(sheet,"Merged-face Sheet from Body Properties missing");
                select_face(face);
                check(sheet->selected()&&*sheet->selected()==face,
                    "Sheet from Body mouse click did not accept the merged wall");
                sheet->reject();flush();
                check(run("documents").at(0).at("revision")==revision,
                    "Merged-face selection/Cancel changed the document");
            }
            check(kernel::body_calculation_count.load()==calculations,
                "Merged-face opening, selection or Cancel calculated geometry");
            std::cout<<"Merged faces: "<<faces.size()<<" walls selected by actual mouse in Extrusion and Sheet from Body after save/reopen; Cancel and zero calculations passed\n";
            if(qEnvironmentVariableIsSet("ZIMA_VERIFY_MERGED_FACE_CONVERT"))for(const auto& face:faces) {
                window.show_sheet_from_body();flush();
                auto* sheet=dynamic_cast<SheetFromBodyDialog*>(window.findChild<QWidget*>("sheetFromBodyDialog"));
                check(sheet,"Merged-face conversion dialog missing");select_face(face);
                QPointer<SheetFromBodyDialog> guard(sheet);
                sheet->buttons()->button(QDialogButtonBox::Ok)->click();flush();
                if(guard&&guard->isVisible()) {
                    std::string error="Merged-face conversion did not close:";
                    for(auto* label:guard->findChildren<QLabel*>())if(label->isVisible())error+=" "+label->text().toStdString();
                    throw std::runtime_error(error);
                }
                run("save");
                std::vector<kernel::BodyResult> converted_cache;
                const auto converted=document::PartDocument::load(file,&converted_cache);
                check(converted.history.size()>source.history.size()&&
                    std::equal(source.history.begin(),source.history.end(),converted.history.begin()),
                    "Merged-face conversion changed source history or created no sheet features");
                check(!converted_cache.empty()&&converted_cache.back().calculation_errors.empty(),
                    "Merged-face conversion saved an unresolved result");
                const auto converted_history=converted.history;
                run("undo");run("save");
                check(document::PartDocument::load(file).history==source.history,"Merged-face conversion Undo changed source history");
                run("redo");run("save");
                check(document::PartDocument::load(file).history==converted_history,"Merged-face conversion Redo lost its definitions");
                run("close");run("open",{{"path",document::path_to_utf8(file)}});run("regenerate");run("save");
                converted_cache.clear();static_cast<void>(document::PartDocument::load(file,&converted_cache));
                check(!converted_cache.empty()&&converted_cache.back().calculation_errors.empty(),
                    "Merged-face conversion failed after cold reopening");
                run("close");source.save(file,cache);run("open",{{"path",document::path_to_utf8(file)}});
                std::cout<<"Merged-face full conversion, source preservation, native reopen, cold regeneration and Undo/Redo passed\n";
            }
            return 0;
        }
        if(const auto model=qEnvironmentVariable("ZIMA_VERIFY_PICK_MODEL");!model.isEmpty()) {
            const auto file=directory/"pick-input.prtz";
            std::filesystem::copy_file(std::filesystem::u8path(model.toStdString()),file,
                std::filesystem::copy_options::overwrite_existing);
            run("open",{{"path",document::path_to_utf8(file)}});
            if(qEnvironmentVariableIsSet("ZIMA_VERIFY_PICK_REGENERATE"))run("regenerate");
            view->set_standard_view(viewer::StandardView::Isometric);
            view->fit_all();flush();
            const auto pick_owner=qEnvironmentVariable("ZIMA_VERIFY_PICK_OWNER").toStdString();
            const bool chamfer=qEnvironmentVariableIsSet("ZIMA_VERIFY_PICK_CHAMFER");
            const auto revision=run("documents").at(0).at("revision");
            const auto draft_calculations=kernel::body_calculation_count.load();
            const auto available=pick_owner.empty()?run("edge_treatment.edges").at("items"):run("edge_treatment.edges",{{"container",pick_owner}}).at("items");
            std::cerr<<"PICK items="<<available.size()<<std::endl;
            if(pick_owner.empty())window.findChild<QAction*>(chamfer?"chamferAction":"filletAction")->trigger();else static_cast<void>(edit(pick_owner));
            flush();
            std::cerr<<"PICK dialog open"<<std::endl;
            auto* method_zima=dialog()->findChild<QCheckBox*>("edgeTreatmentMethodZima");
            auto* method_occt=dialog()->findChild<QCheckBox*>("edgeTreatmentMethodOcct");
            const bool occt_first=qEnvironmentVariableIsSet("ZIMA_VERIFY_PICK_OCCT_FIRST");
            if(occt_first){method_occt->click();flush();std::cerr<<"PICK occt on"<<std::endl;}
            const auto specs=qEnvironmentVariable("ZIMA_VERIFY_PICK_EDGES").split(';',Qt::SkipEmptyParts);
            std::vector<Json> picked;
            for(const auto& spec:specs) {
                const auto values=spec.split(',');
                check(values.size()==6,"Bad pick spec");
                const auto near=[&](const Json& point,int offset) {
                    for(int axis=0;axis<3;++axis)
                        if(std::abs(point.at(axis).get<double>()-values[offset+axis].toDouble())>0.01)return false;
                    return true;
                };
                const Json* found=nullptr;
                for(const auto& row:available) {
                    if(!row.contains("segments")||!row.at("segments").is_array()||row.at("segments").empty())continue;
                    const auto& ends=row.at("segments")[0].at("endpoints");
                    if(ends.size()==2&&((near(ends[0].at("position_mm"),0)&&near(ends[1].at("position_mm"),3))||
                            (near(ends[0].at("position_mm"),3)&&near(ends[1].at("position_mm"),0)))) {
                        found=&row;break;
                    }
                }
                if(!found)for(const auto& row:available) {
                    if(!row.contains("segments")||!row.at("segments").is_array()||row.at("segments").empty())continue;
                    const auto& ends=row.at("segments")[0].at("endpoints");
                    if(ends.size()==2)
                        std::cerr<<"PICK cand "<<ends[0].at("position_mm").dump()<<' '<<ends[1].at("position_mm").dump()<<std::endl;
                }
                check(found,"Pick spec edge not found");
                picked.push_back(*found);
                std::cerr<<"PICK edge found "<<found->at("key").get<std::string>()<<std::endl;
                static_cast<void>(pick(viewer::CandidateKind::Edge,found->at("owner"),found->at("key")));
            }
            flush();
            if(occt_first) {
                std::cout<<"OCCT routes="<<dialog()->pending_value().edge_treatment.routes.size()
                    <<" OK="<<dialog()->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->isEnabled()<<'\n';
                method_zima->click();flush();std::cerr<<"PICK zima on"<<std::endl;
            }
            auto* pending=dialog();
            const auto requested_radius=qEnvironmentVariable("ZIMA_VERIFY_PICK_RADIUS");
            pending->findChild<QDoubleSpinBox*>("edgeTreatmentPrimary")->setValue(
                requested_radius.isEmpty()?1.:requested_radius.toDouble());flush();
            const auto picked_definition=pending->pending_value().edge_treatment;
            for(std::size_t route=0;route<picked_definition.routes.size();++route) {
                const auto terminals=workspace::edge_treatment_route_endpoints(
                    view->mesh().edges,picked_definition.routes[route]);
                if(terminals.size()!=2)continue;
                check(route<picked_definition.route_end_faces.size(),
                    "Picked route did not retain its physical terminals");
                const auto& ends=picked_definition.route_end_faces[route];
                check(ends.first_vertex!=ends.last_vertex&&
                    std::ranges::find(terminals,ends.first_vertex)!=terminals.end()&&
                    std::ranges::find(terminals,ends.last_vertex)!=terminals.end(),
                    "Picked route learned an internal preview junction as a terminal");
            }
            if(qEnvironmentVariableIsSet("ZIMA_VERIFY_PICK_STALE_ENDPOINT")) {
                const auto value=pending->pending_value();
                check(!value.edge_treatment.route_end_faces.empty(),"Missing endpoint test route");
                auto ends=value.edge_treatment.route_end_faces.front();
                ends.first_vertex.semantic_key+=":missing";
                pending->set_edge_route_end_vertices(0,ends.first_vertex,ends.last_vertex);
            }
            std::cout<<"PICK routes="<<pending->pending_value().edge_treatment.routes.size()<<'\n';
            for(auto* label:pending->findChildren<QLabel*>())
                if(label->isVisible()&&!label->text().isEmpty()&&
                   (label->text().contains("ZIMA",Qt::CaseInsensitive)||label->text().contains("invalid",Qt::CaseInsensitive)||label->text().contains("rejected",Qt::CaseInsensitive)))
                    std::cout<<"DIALOG-LABEL: "<<label->text().toStdString()<<'\n';
            auto* ok=pending->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
            std::cout<<"OK enabled="<<ok->isEnabled()<<'\n';
            window.grab().save(QString::fromStdString((directory/"pick-dialog.png").string()));
            std::cout<<"PICK-DIR "<<directory.string()<<'\n';
            if(qEnvironmentVariableIsSet("ZIMA_VERIFY_PICK_COMMIT_ONLY")) {
                check(ok->isEnabled()&&kernel::body_calculation_count.load()==draft_calculations,
                    "Picked treatment calculated a Body before OK");
                const auto owner=pending->container_id();
                if(qEnvironmentVariableIsSet("ZIMA_VERIFY_PICK_STALE_ENDPOINT")) {
                    QPointer<PrimitivePropertiesDialog> held=pending;
                    ok->click();flush();
                    auto* notice=held?held->findChild<QLabel*>("propertiesSubmitError"):nullptr;
                    check(held&&notice&&notice->isVisible()&&
                        run("documents").at(0).at("revision")==revision,
                        "First invalid endpoint OK did not retain the unchanged draft and retry notice");
                    const auto count=kernel::body_calculation_count.load();
                    ok->click();flush();
                    check(!held&&kernel::body_calculation_count.load()==count,
                        "Second invalid endpoint OK did not close without calculation");
                    run("save");
                    std::vector<kernel::BodyResult> saved;
                    const auto reopened=document::PartDocument::load(file,&saved);
                    check(reopened.find_container(owner)&&!saved.empty()&&
                        saved.back().calculation_errors.contains(owner),
                        "Invalid endpoint definition was not saved as unresolved");
                    run("undo");run("redo");run("save");
                    check(document::PartDocument::load(file).find_container(owner),
                        "Invalid endpoint definition was lost through Undo/Redo");
                    std::cout<<"Invalid endpoint GUI: first/second OK, native reopen, Undo/Redo passed\n";
                    return 0;
                }
                QPointer<PrimitivePropertiesDialog> submitted=pending;
                finish(true);
                if(submitted) {
                    for(auto* label:submitted->findChildren<QLabel*>())
                        if(label->isVisible()&&!label->text().isEmpty())
                            std::cerr<<"FIRST-OK: "<<label->text().toStdString()<<'\n';
                    check(false,"Valid picked treatment did not close on its first OK");
                }
                check(kernel::body_calculation_count.load()>draft_calculations,
                    "Picked treatment did not calculate on OK");
                const auto get=chamfer?"chamfer.get":"fillet.get";
                const auto created=run(get,{{"container",owner}});
                check(created.at("calculation_method")=="zima",
                    "Picked treatment silently changed its calculation method");
                run("save");
                std::vector<kernel::BodyResult> saved;
                const auto reopened=document::PartDocument::load(file,&saved);
                check(reopened.find_container(owner)&&!saved.empty()&&saved.back().calculation_errors.empty(),
                    "Picked treatment did not reopen its calculated Body");
                run("undo");run("redo");
                check(run(get,{{"container",owner}})==created,"Picked treatment lost its definition through Undo/Redo");
                const auto unchanged=run("documents");
                edit(owner);
                // A cold compact history can materialize the real rollback
                // input on entry. Unchanged OK itself must do no work.
                const auto calculations=kernel::body_calculation_count.load();
                finish(true);
                check(run("documents")==unchanged&&kernel::body_calculation_count.load()==calculations,
                    "Unchanged picked treatment calculated or changed its document");
                window.grab().save(QString::fromStdString((directory/"pick-committed.png").string()));
                std::cout<<"Picked treatment: analytic draft, ZIMA OK, native reopening, Undo/Redo and unchanged OK passed\n";
                return 0;
            }
            if(qEnvironmentVariableIsSet("ZIMA_VERIFY_PICK_RECOVERY")) {
                check(kernel::body_calculation_count.load()==draft_calculations,
                    "Opening/picking a treatment calculated a Body");
                check(ok->isEnabled(),"Valid pocket preview failed");
                check(pending->pending_value().edge_treatment.routes.size()==picked.size(),
                    "Pocket selection dropped an edge");
                for(const auto& label:view->interactive_labels())
                    if(std::ranges::count(label.text,'-')==1)
                        check(label.choice,"Pocket corner omitted its spike/rounding solution menu");
                auto* size=pending->findChild<QDoubleSpinBox*>("edgeTreatmentPrimary");
                QElapsedTimer wire_timer;wire_timer.start();
                for(const double radius:{1.25,1.5,2.,1.})size->setValue(radius);
                flush();
                std::cout<<"Four wire edits ms="<<wire_timer.nsecsElapsed()/1.0e6<<'\n';
                size->setValue(10000);flush();
                check(ok->isEnabled()&&kernel::body_calculation_count.load()==draft_calculations,
                    "Draft edit built or prevalidated a Body before OK");
                const auto rejected_owner=pending->container_id();
                QPointer<PrimitivePropertiesDialog> rejected=pending;
                ok->click();flush();
                check(rejected&&rejected->isVisible()&&kernel::body_calculation_count.load()>draft_calculations,
                    "First failed ZIMA OK did not retain Properties");
                check(run("documents").at(0).at("revision")==revision,
                    "Failed construction changed the document revision");
                check(!window.execute_console_command(QString::fromStdString(Json{
                    {"command",chamfer?"chamfer.get":"fillet.get"},
                    {"arguments",{{"container",rejected_owner}}}}.dump())).ok,
                    "Failed construction inserted a treatment definition");
                auto* notice=rejected->findChild<QLabel*>("propertiesSubmitError");
                check(notice&&notice->isVisible()&&!notice->text().isEmpty(),"First failed OK omitted the unresolved-definition notice");
                const auto failed_count=kernel::body_calculation_count.load();
                size->setValue(9000);ok->click();flush();
                check(rejected&&run("documents").at(0).at("revision")==revision&&
                    kernel::body_calculation_count.load()>failed_count,"Changed draft reused a previous failure acknowledgement");
                const auto retry_count=kernel::body_calculation_count.load();
                ok->click();flush();
                check(!rejected&&kernel::body_calculation_count.load()==retry_count,
                    "Second unchanged OK did not close using the already calculated failure");
                run("save");
                std::vector<kernel::BodyResult> unresolved_cache;
                const auto unresolved=document::PartDocument::load(file,&unresolved_cache);
                check(unresolved.find_container(rejected_owner)&&!unresolved_cache.empty()&&
                    unresolved_cache.back().calculation_errors.contains(rejected_owner),"Second OK lost the unresolved native feature");
                run("undo");run("redo");run("undo");
                for(const bool next_chamfer:{chamfer,!chamfer}) {
                    const auto before_draft=kernel::body_calculation_count.load();
                    window.findChild<QAction*>(next_chamfer?"chamferAction":"filletAction")->trigger();flush();
                    for(const auto& edge:picked)
                        static_cast<void>(pick(viewer::CandidateKind::Edge,edge.at("owner"),edge.at("key")));
                    auto* next=dialog();
                    next->findChild<QDoubleSpinBox*>("edgeTreatmentPrimary")->setValue(1);flush();
                    check(next->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->isEnabled(),
                        "A failed treatment poisoned the next command");
                    const auto owner=next->container_id();
                    check(kernel::body_calculation_count.load()==before_draft,
                        "A new treatment calculated a Body before OK");
                    finish(true);
                    check(kernel::body_calculation_count.load()>before_draft,
                        "Changed OK did not calculate a Body");
                    const auto query=next_chamfer?"chamfer.get":"fillet.get";
                    const auto created=run(query,{{"container",owner}});
                    run("save");
                    std::vector<kernel::BodyResult> saved;
                    const auto reopened=document::PartDocument::load(file,&saved);
                    check(reopened.find_container(owner)&&!saved.empty()&&
                            saved.back().calculation_errors.empty(),
                        "Recovered treatment did not save/reopen a valid Body");
                    const auto before=run("documents").at(0).at("revision");
                    const auto before_unchanged=kernel::body_calculation_count.load();
                    edit(owner);finish(true);
                    check(run("documents").at(0).at("revision")==before,
                        "Unchanged recovered treatment OK created an Undo transaction");
                    check(kernel::body_calculation_count.load()==before_unchanged,
                        "Opening/unchanged OK calculated a Body");
                    QPointer<PrimitivePropertiesDialog> failed_edit=edit(owner);
                    failed_edit->findChild<QDoubleSpinBox*>("edgeTreatmentPrimary")->setValue(10000);flush();
                    const QPointF confirmation(view->width()*.5,view->height()*.5);
                    mouse(QEvent::MouseButtonDblClick,confirmation,Qt::MiddleButton,Qt::MiddleButton);
                    mouse(QEvent::MouseButtonRelease,confirmation,Qt::MiddleButton,Qt::NoButton);
                    check(failed_edit&&run("documents").at(0).at("revision")==before&&
                            run(query,{{"container",owner}})==created,
                        "First failed middle-button confirmation changed the previous treatment");
                    const auto edit_retry_count=kernel::body_calculation_count.load();
                    mouse(QEvent::MouseButtonDblClick,confirmation,Qt::MiddleButton,Qt::MiddleButton);
                    mouse(QEvent::MouseButtonRelease,confirmation,Qt::MiddleButton,Qt::NoButton);
                    check(!failed_edit&&kernel::body_calculation_count.load()==edit_retry_count,
                        "Second middle-button confirmation did not retain the failed definition without recalculation");
                    run("undo");run("redo");run("undo");
                    check(run(query,{{"container",owner}})==created,
                        "Recovered treatment lost its references through Undo/Redo");
                    run("undo");
                }
                std::cout<<"Pocket recovery: two-OK unresolved definitions, changed draft, new/edit Fillet and Chamfer, middle button, native reopen, Undo/Redo and unchanged OK passed\n";
                return 0;
            }
            finish(false);
            return 0;
        }
        if(const auto model=qEnvironmentVariable("ZIMA_VERIFY_EDGE_TREATMENT_MODEL");!model.isEmpty()) {
            const bool node_inspection=qEnvironmentVariableIsSet("ZIMA_VERIFY_EDGE_TREATMENT_NODE_INSPECTION");
            const auto model_path=node_inspection?directory/("node-inspection-"+document::PartDocument::create_default().document_id+".prtz"):std::filesystem::u8path(model.toStdString());
            if(node_inspection)std::filesystem::copy_file(std::filesystem::u8path(model.toStdString()),model_path);
            run("open",{{"path",document::path_to_utf8(model_path)}});
            if(qEnvironmentVariableIsSet(
                    "ZIMA_VERIFY_EDGE_TREATMENT_REGENERATE"))
                run("regenerate");
            view->set_projection_mode(viewer::ProjectionMode::Orthographic);view->fit_all();flush();
            const auto revision=run("documents").at(0).at("revision");
            const auto owner=qEnvironmentVariable("ZIMA_VERIFY_EDGE_TREATMENT_OWNER").toStdString();
            check(!owner.empty(),"User-model treatment is missing");
            if(node_inspection) {
                const auto original=document::PartDocument::load(model_path);
                const auto* original_feature=original.find_container(owner);
                check(original_feature,"Node inspection fixture has no treatment");
                const auto get=original_feature->feature_kind==document::FeatureKind::Fillet?"fillet.get":"chamfer.get";
                const auto read_definition=[&] {
                    auto value=run(get,{{"container",owner}});
                    value.erase("revision");
                    return value;
                };
                const auto definition=read_definition();
                for(int pass=0;pass<7;++pass) {
                    const auto before=run("documents");
                    const auto previous=read_definition();
                    const auto calculations=kernel::body_calculation_count.load();
                    QTreeWidgetItem* item{};
                    for(QTreeWidgetItemIterator it(tree);*it;++it)
                        if((*it)->data(0,Qt::UserRole).toString().toStdString()==owner&&
                                (*it)->data(0,Qt::UserRole+3)=="part-container"){item=*it;break;}
                    check(item,"Node inspector has no Tree row");
                    for(auto* parent=item->parent();parent;parent=parent->parent())parent->setExpanded(true);
                    tree->scrollToItem(item);flush();
                    const QPointF position=tree->visualItemRect(item).center();
                    for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease,QEvent::MouseButtonDblClick,QEvent::MouseButtonRelease}) {
                        QMouseEvent event(type,position,QPointF(tree->viewport()->mapToGlobal(position.toPoint())),
                            Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);
                        QApplication::sendEvent(tree->viewport(),&event);flush();
                    }
                    const auto labels=view->interactive_labels();
                    const auto joined=std::ranges::find_if(labels,[&](const auto& label) {
                        return label.choice&&((label.text.find('-')!=std::string::npos)==(pass<4));
                    });
                    check(joined!=labels.end(),"Double-click inspection omitted the selectable numbered node");
                    check(std::ranges::any_of(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==owner;}),
                        "Node inspection omitted the treatment dimension");
                    check(run("documents")==before&&kernel::body_calculation_count.load()==calculations,
                        "Node inspection calculated or changed the document");
                    window.grab().save(QString::fromStdString((directory/("inspection-overlay-"+std::to_string(pass)+".png")).string()));
                    const auto node_position=view->interactive_label_position(joined->key);
                    check(node_position.has_value(),"Inspection node has no hit position");
                    bool opened{},selected{},properties_seen{};QTimer selection,timeout;
                    const auto camera=view->camera_state();
                    const auto has_properties=[&] {
                        for(auto* candidate:window.findChildren<QDialog*>())
                            if(dynamic_cast<PrimitivePropertiesDialog*>(candidate))return true;
                        return false;
                    };
                    selection.setInterval(10);timeout.setSingleShot(true);
                    QObject::connect(&selection,&QTimer::timeout,&window,[&] {
                        for(auto* menu:window.findChildren<QMenu*>()) {
                            if(!menu->isVisible()||menu->objectName()!="edgeTreatmentEndMenu")continue;
                            opened=true;properties_seen=has_properties();
                            check(kernel::body_calculation_count.load()==calculations&&run("documents")==before,
                                "Opening the direct node menu calculated or changed the model");
                            window.grab().save(QString::fromStdString((directory/("inspection-menu-"+std::to_string(pass)+".png")).string()));
                            if(pass==0) {selection.stop();menu->close();return;}
                            const auto text=pass>=5?QCoreApplication::translate("AssemblyWorkspaceWindow","Řez %1").arg(1):
                                QCoreApplication::translate("AssemblyWorkspaceWindow",pass==4?"Bez řezu":pass==3?"Zaoblení":"Špička");
                            for(auto* action:menu->actions())if(action->text()==text) {
                                if(pass==2||pass==6)check(action->isChecked(),"Current node solution was not checked");
                                selected=true;selection.stop();menu->setActiveAction(action);
                                QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
                                QApplication::sendEvent(menu,&enter);return;
                            }
                        }
                    });
                    QObject::connect(&timeout,&QTimer::timeout,&window,[&] {
                        for(auto* menu:window.findChildren<QMenu*>())if(menu->isVisible())menu->close();
                    });
                    selection.start();timeout.start(5000);
                    mouse(QEvent::MouseButtonDblClick,*node_position+QPointF(14.,-14.),Qt::LeftButton,Qt::LeftButton);
                    selection.stop();timeout.stop();
                    check(opened&&(pass==0||selected),"Inspection node did not open its direct solution menu");
                    check(!properties_seen&&!has_properties(),"Direct node menu constructed Fillet/Chamfer Properties");
                    check(view->camera_state()==camera,"Direct node edit changed the camera");
                    check(!view->interactive_labels().empty(),"Direct node edit lost the inspector");
                    check(tree->currentItem()&&tree->currentItem()->data(0,Qt::UserRole).toString().toStdString()==owner,
                        "Direct node edit lost the selected Tree feature");
                    if(pass==0||pass==2||pass==6)check(run("documents")==before&&kernel::body_calculation_count.load()==calculations,
                        "Dismissed or current node solution created a calculation/transaction");
                    else {
                        check(kernel::body_calculation_count.load()>calculations,"Changed node action did not calculate the Body");
                        const auto changed=read_definition();
                        if(pass<4)check((changed==definition)==(pass==3),"Direct node choice did not preserve the expected definition");
                        else check(changed!=previous,"Direct terminal choice did not change its definition");
                        run("undo");check(read_definition()==previous,"Direct node edit Undo failed");
                        run("redo");check(read_definition()==changed,"Direct node edit Redo failed");
                        run("save");
                        const auto reopened=document::PartDocument::load(model_path);
                        std::size_t spikes{};
                        for(const auto& end:reopened.find_container(owner)->edge_treatment.route_end_faces) {
                            spikes+=end.first_mode==document::EdgeTreatmentParameters::RouteEndMode::RoundedJoin;
                            spikes+=end.last_mode==document::EdgeTreatmentParameters::RouteEndMode::RoundedJoin;
                        }
                        check(spikes==(pass==1?2:0),"Direct node edit changed the wrong persisted endpoints");
                    }
                }
                std::cout<<"Direct node inspection: cold open, dimension, numbered node, popup without Properties, spike/round/no-cut/cut, dismiss, current no-op, repeated changes, Undo/Redo, native reopening and selection passed\n";
                return 0;
            }
            window.grab().save(QString::fromStdString(
                (directory/("user-model-treatment-final-"+owner+".png")).string()));
            const auto overview_camera=view->camera_state();
            auto focused_camera=overview_camera;
            focused_camera[4]/=3.F;
            focused_camera[7]/=3.F;
            view->set_camera_state(focused_camera);flush();
            window.grab().save(QString::fromStdString(
                (directory/("user-model-treatment-final-zoom-"+owner+".png")).string()));
            view->set_camera_state(overview_camera);flush();
            if(qEnvironmentVariableIsSet(
                    "ZIMA_VERIFY_EDGE_TREATMENT_CAPTURE_STANDARD_VIEWS")) {
                const std::pair<viewer::StandardView,const char*> views[]{
                    {viewer::StandardView::Isometric,"isometric"},
                    {viewer::StandardView::Front,"front"},
                    {viewer::StandardView::Back,"back"},
                    {viewer::StandardView::Left,"left"},
                    {viewer::StandardView::Right,"right"},
                    {viewer::StandardView::Top,"top"},
                    {viewer::StandardView::Bottom,"bottom"}};
                for(const auto& [orientation,name]:views) {
                    view->set_standard_view(orientation);
                    view->fit_all();
                    QEventLoop settle;
                    QTimer::singleShot(500,&settle,&QEventLoop::quit);
                    settle.exec();
                    flush();
                    window.grab().save(QString::fromStdString((directory/
                        ("user-model-treatment-final-"+owner+"-"+name+
                            ".png")).string()));
                }
                view->set_camera_state(overview_camera);flush();
            }
            if(qEnvironmentVariableIsSet("ZIMA_CPP_PROFILE_FEATURE_PREVIEW")) {
                auto* pending=edit(owner);
                const auto parameters=pending->pending_value().edge_treatment;
                std::vector<std::vector<kernel::ViewerEdge>> groups;
                for(std::size_t route=0;route<parameters.routes.size();++route)
                    groups.push_back(document::current_treatment_selection_wire(
                        parameters,route,std::nullopt,view->mesh()));
                const auto preview=workspace_detail::edge_treatment_preview_wire(
                    groups,parameters,pending->feature_kind(),owner,&view->mesh());
                std::cout<<"User-model preview diagnostic: input="<<view->mesh().edges.size()
                    <<" routes="<<parameters.routes.size()<<" groups=";
                for(const auto& group:groups)std::cout<<group.size()<<',';
                std::cout<<" preview="<<preview.edges.size()
                    <<" published="<<view->transient_edges().size()<<'\n';
                window.grab().save(QString::fromStdString(
                    (directory/"user-model-fillet-preview.png").string()));
                finish(false);
            }
            if(qEnvironmentVariableIsSet(
                    "ZIMA_VERIFY_EDGE_TREATMENT_PREVIEW_ONLY")) {
                auto* pending=edit(owner);
                // Opening a cold history may materialize its real rollback
                // input. Draft generation and node choices must reuse it.
                const auto preview_calculations=kernel::body_calculation_count.load();
                const auto parameters=pending->pending_value().edge_treatment;
                std::vector<std::vector<kernel::ViewerEdge>> groups;
                for(std::size_t route=0;route<parameters.routes.size();
                        ++route)
                    groups.push_back(document::current_treatment_selection_wire(
                        parameters,route,std::nullopt,view->mesh()));
                check(!groups.empty()&&std::ranges::all_of(groups,
                        [](const auto& group){return !group.empty();}),
                    "A persisted treatment route has no analytical preview input");
                const auto expected=workspace_detail::
                    edge_treatment_preview_wire(groups,parameters,
                        pending->feature_kind(),owner,&view->mesh());
                const auto& published=view->transient_edges();
                const auto published_labels=view->transient_labels();
                const auto source_node_counts=route_node_counts(groups);
                verify_route_labels(published_labels,view->interactive_labels(),
                    source_node_counts);
                bool expected_node_count_valid{};
                const int expected_node_count=qEnvironmentVariableIntValue(
                    "ZIMA_VERIFY_EDGE_TREATMENT_EXPECTED_NODE_LABELS",
                    &expected_node_count_valid);
                if(expected_node_count_valid)
                    check(expected_node_count>=0&&published_labels.size()==
                            static_cast<std::size_t>(expected_node_count),
                        "Treatment preview has an unexpected number of source-node labels");
                std::cout<<"Treatment wire: published="<<published.size()<<" expected="<<expected.edges.size()
                    <<" labels="<<published_labels.size()<<'/'<<expected.labels.size()<<std::endl;
                double wire_difference{};bool matching_sample_counts=published.size()==expected.edges.size();
                for(std::size_t e=0;e<std::min(published.size(),expected.edges.size());++e) {
                    const auto& a=published[e].points;const auto& b=expected.edges[e].points;
                    matching_sample_counts=matching_sample_counts&&a.size()==b.size();
                    for(std::size_t p=0;p<std::min(a.size(),b.size());++p)
                        wire_difference=std::max(wire_difference,std::hypot(a[p].x-b[p].x,a[p].y-b[p].y,a[p].z-b[p].z));
                }
                std::cout<<"Treatment wire maximum difference="<<wire_difference<<std::endl;
                window.grab().save(QString::fromStdString(
                    (directory/("user-model-treatment-preview-"+owner+".png")).string()));
                // Scene-normalized descriptors can differ by machine rounding
                // from the same Part-local input. Compare geometry, not bits.
                check(!published.empty()&&matching_sample_counts&&wire_difference<1.e-9&&
                        published_labels.size()==expected.labels.size()&&
                        std::equal(expected.labels.begin(),expected.labels.end(),
                            published_labels.begin(),[](const auto& first,
                                const auto& second) {
                                return first.first==second.first&&
                                    first.second==second.second;
                            }),
                    "Treatment Properties did not show its shared lightweight wire and labels");
                if(qEnvironmentVariableIsSet(
                        "ZIMA_VERIFY_EDGE_TREATMENT_SHARED_NODE_MENU")) {
                    const auto labels=view->interactive_labels();
                    const auto compound=std::ranges::find_if(labels,
                        [](const auto& label) {
                            return label.text.find('-')!=std::string::npos;
                        });
                    check(compound!=labels.end(),
                        "Treatment preview has no compound endpoint node");
                    using EndMode=document::EdgeTreatmentParameters::RouteEndMode;
                    check(compound->choice,"Selectable rounded node was presented as passive");
                    for(const auto mode:{EndMode::RoundedArc,EndMode::RoundedJoin,EndMode::RoundedArc}) {
                        const auto position=view->interactive_label_position(compound->key);
                        check(position.has_value(),"Compound node has no double-click position");
                        bool selected{};
                        QTimer selection,timeout;
                        selection.setInterval(10);timeout.setSingleShot(true);
                        const auto menus=[&] {
                            auto result=window.findChildren<QMenu*>();
                            for(auto* widget:QApplication::topLevelWidgets())
                                if(auto* menu=qobject_cast<QMenu*>(widget))result.push_back(menu);
                            return result;
                        };
                        QObject::connect(&timeout,&QTimer::timeout,&window,[&] {
                            for(auto* menu:menus())if(menu->isVisible())menu->close();
                        });
                        QObject::connect(&selection,&QTimer::timeout,&window,[&] {
                            for(auto* menu:menus()) {
                                if(!menu->isVisible()||menu->objectName()!="edgeTreatmentEndMenu")continue;
                                const auto text=QCoreApplication::translate("AssemblyWorkspaceWindow",
                                    mode==EndMode::RoundedArc?"Zaoblení":"Špička");
                                for(auto* action:menu->actions())if(action->text()==text) {
                                    selected=true;selection.stop();menu->setActiveAction(action);
                                    QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
                                    QApplication::sendEvent(menu,&enter);return;
                                }
                            }
                        });
                        selection.start();timeout.start(5000);
                        mouse(QEvent::MouseButtonDblClick,*position+QPointF(14.,-14.),Qt::LeftButton,Qt::LeftButton);
                        selection.stop();timeout.stop();
                        std::size_t changed_ends{};
                        for(const auto& ends:pending->pending_value().edge_treatment.route_end_faces) {
                            changed_ends+=ends.first_mode==mode;
                            changed_ends+=ends.last_mode==mode;
                        }
                        check(selected&&changed_ends==2,"Node menu did not update both ends to its selected solution");
                    }
                }
                check(kernel::body_calculation_count.load()==preview_calculations,
                    "Properties or node choices calculated a Body before OK");
                window.grab().save(QString::fromStdString(
                    (directory/("user-model-treatment-preview-"+owner+
                        ".png")).string()));
                finish(false);
                check(run("documents").at(0).at("revision")==revision,
                    "Treatment preview inspection changed the user model");
                std::cout<<"User-model treatment preview source nodes:";
                for(const auto& label:published_labels)
                    std::cout<<' '<<label.second;
                std::cout<<"\nUser-model treatment preview: every persisted route, every source-node label, published geometry and Cancel passed\n";
                return 0;
            }
            QPointF surface;
            bool found=false;
            for(const auto orientation:{viewer::StandardView::Isometric,viewer::StandardView::Bottom,
                viewer::StandardView::Back,viewer::StandardView::Front,viewer::StandardView::Left,
                viewer::StandardView::Right,viewer::StandardView::Top}) {
            view->set_standard_view(orientation);
            QEventLoop settle;QTimer::singleShot(1000,&settle,&QEventLoop::quit);settle.exec();flush();
            const QPointF center(view->width()/2.,view->height()/2.);
            const auto ray0=view->ray_at(center),rayx=view->ray_at(center+QPointF(100,0)),rayy=view->ray_at(center+QPointF(0,100));
            check(ray0&&rayx&&rayy,"Missing orthographic test rays");
            const auto delta=[](auto a,auto b){return kernel::Vec3{a.x-b.x,a.y-b.y,a.z-b.z};};
            const auto dot=[](auto a,auto b){return a.x*b.x+a.y*b.y+a.z*b.z;};
            const auto u=delta(rayx->first,ray0->first),v=delta(rayy->first,ray0->first);
            const auto& mesh=view->mesh();
            for(std::size_t triangle=0;triangle<mesh.triangle_references.size()&&!found;++triangle) {
                const auto& reference=mesh.triangle_references[triangle];
                if(reference.owner_id!=owner&&reference.display_owner_id!=owner)continue;
                const auto a=mesh.vertices[mesh.triangles[3*triangle]],b=mesh.vertices[mesh.triangles[3*triangle+1]],c=mesh.vertices[mesh.triangles[3*triangle+2]];
                const auto offset=delta(kernel::Vec3{(a.x+b.x+c.x)/3,(a.y+b.y+c.y)/3,(a.z+b.z+c.z)/3},ray0->first);
                const QPointF point=center+QPointF(100*dot(offset,u)/dot(u,u),100*dot(offset,v)/dot(v,v));
                const auto candidates=view->selection_candidates_at(point);
                const auto ray=view->ray_at(point);if(!ray)continue;
                const auto faces=viewer::ordered_ray_candidates(view->mesh(),ray->first,ray->second);
                if(faces.empty()||(faces.front().reference.owner_id!=owner&&faces.front().reference.display_owner_id!=owner))continue;
                if(candidates.empty()||candidates.front().kind!=viewer::CandidateKind::Container||candidates.front().owner_id!=owner)continue;
                surface=point;found=true;
            }
            if(found)break;
            }
            if(!found)window.grab().save(QString::fromStdString((directory/"user-model-fillet-pick-failure.png").string()));
            check(found,"Rounded surface did not offer its own Fillet through the common picker");click(surface);
            const auto radius_visible=[&]{return std::ranges::any_of(view->mesh().dimensions,[&](const auto& dimension){return dimension.reference.owner_id==owner&&dimension.kind==kernel::ViewerDimensionKind::Radius;});};
            check(!radius_visible(),"Single-clicking the rounded surface exposed its radius annotation");
            const auto selected_feature=[&] {
                const auto* item=tree->currentItem();
                return item&&item->isSelected()&&item->data(0,Qt::UserRole).toString().toStdString()==owner&&
                    item->data(0,Qt::UserRole+3)=="part-container";
            };
            check(selected_feature(),"Single-clicking the rounded surface did not select its feature in the Tree");
            check(view->property("gpuSourceWireVertexCount").toULongLong()>0,
                "Single-clicking the rounded surface omitted its authored azure wire");
            check(view->property("selectedFeatureFaceTriangleCount").toULongLong()==0,
                "Single-clicking the rounded surface filled the selected feature");
            check(run("documents").at(0).at("revision")==revision,"Surface selection created a transaction");
            mouse(QEvent::MouseButtonDblClick,surface,Qt::LeftButton,Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease,surface,Qt::LeftButton,Qt::NoButton);
            check(radius_visible(),"Double-clicking the rounded surface omitted its radius annotation");
            check(selected_feature(),"Double-clicking the rounded surface lost its selected Tree feature");
            check(run("documents").at(0).at("revision")==revision,"Surface inspection created a transaction");
            auto* opened_properties=edit(owner);
            check(opened_properties->container_id()==owner,
                "Tree Properties opened a different treatment");
            check(!view->mesh().vertices.empty()&&
                    !view->mesh().triangles.empty(),
                "Opening treatment Properties hid the complete rollback input");
            finish(false);
            check(run("documents").at(0).at("revision")==revision,
                "Treatment Properties Cancel changed the user model");
            if(qEnvironmentVariableIsSet(
                    "ZIMA_VERIFY_EDGE_TREATMENT_FRAGMENT_PRESENTATION")) {
                auto* pending=edit(owner);
                const auto parameters=pending->pending_value().edge_treatment;
                auto* rows=pending->findChild<QTreeWidget*>(
                    "edgeTreatmentEdges");
                std::vector<std::vector<kernel::ViewerEdge>> groups;
                std::size_t persisted_count{},resolved_count{};
                std::size_t presented_item{};
                bool reconnected_compact_route{};
                for(std::size_t route=0;route<parameters.routes.size();++route) {
                    const auto active=document::actual_edge_treatment_intervals(
                        parameters.routes[route]);
                    persisted_count+=active.size();
                    auto fragments=document::current_treatment_selection_wire(
                        parameters,route,std::nullopt,view->mesh());
                    resolved_count+=fragments.size();
                    for(const auto& stored:active) {
                        auto* row=rows&&presented_item<
                                static_cast<std::size_t>(rows->topLevelItemCount())
                            ?rows->topLevelItem(
                                static_cast<int>(presented_item)):nullptr;
                        check(row&&row->childCount()==0&&
                                rows->itemWidget(row,0)&&
                                row->text(1).toStdString()==stored.owner_id+
                                    " / "+stored.semantic_key,
                            "Treatment Properties did not present a direct removable row for every authored segment");
                        ++presented_item;
                        const auto retained=std::ranges::find_if(
                            view->mesh().original_references.edges,
                            [&](const auto& edge) {
                                return edge.reference==stored&&
                                    edge.edge_treatment_side_directions.empty();
                            });
                        const bool tagged_current=std::ranges::any_of(
                            view->mesh().edges,[&](const auto& edge) {
                                return edge.reference==stored;
                            });
                        if(retained!=view->mesh().original_references.edges.end()&&
                                !tagged_current&&std::ranges::any_of(
                                    fragments,[&](const auto& edge) {
                                        return edge.reference==stored&&
                                            edge.edge_treatment_side_directions.size()==2;
                                    }))
                            reconnected_compact_route=true;
                    }
                    for(const auto& fragment:fragments)
                        check(fragment.edge_treatment_side_directions.size()==2&&
                            std::ranges::all_of(
                                fragment.edge_treatment_side_directions,
                                [&](const auto& directions) {
                                    return directions.size()==
                                        fragment.points.size();
                                }),
                            "A displayed route fragment lacks preview face guides");
                    groups.push_back(std::move(fragments));
                }
                check(resolved_count>=persisted_count&&
                        reconnected_compact_route,
                    "User-model fixture did not reconnect a compact two-sided Feature edge to its current face guides");
                check(rows&&presented_item==persisted_count&&
                        rows->topLevelItemCount()==
                            static_cast<int>(persisted_count+1)&&
                        rows->topLevelItem(
                            static_cast<int>(persisted_count))->childCount()==0,
                    "Treatment Properties retained a route branch or omitted its offered-input row");
                const auto expected=workspace_detail::
                    edge_treatment_preview_wire(groups,parameters,
                        pending->feature_kind(),owner,&view->mesh());
                const auto& published=view->transient_edges();
                check(!expected.edges.empty()&&
                    published.size()==expected.edges.size()&&
                    std::equal(expected.edges.begin(),expected.edges.end(),
                        published.begin(),[](const auto& first,
                            const auto& second) {
                            return first.points==second.points;
                        }),
                    "Properties did not publish the complete analytical preview for every resolved fragment");
                finish(false);
                check(run("documents").at(0).at("revision")==revision,
                    "Fragment presentation inspection changed the document");
                std::cout<<"User-model treatment fragments: stable parent, direct segment rows, face guides, complete preview and Cancel passed\n";
            }
            window.grab().save(QString::fromStdString((directory/"user-model-fillet-inspection.png").string()));
            std::cout<<"User-model rounded surface: single-click Tree selection, double-click radius and unchanged revision passed\n";return 0;
        }
#include "form_topology_ui_verification.inc"
#include "treatment_selection_ui_verification.inc"
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
            QPointF delta(38,-27);
            // Pull line grips outward: an inward drag may correctly stop at
            // the model envelope and is not evidence of a broken grip.
            if(count==3) {
                const auto outward=kernel::dimension_unit(kernel::dimension_cross(source->plane_normal,
                    kernel::dimension_measurement_direction(*source)));
                const auto camera=view->camera_state();
                const auto projected=QQuaternion(camera[0],camera[1],camera[2],camera[3]).rotatedVector(
                    QVector3D(outward.x,outward.y,outward.z));
                delta=QPointF(projected.x(),-projected.y());
                check(QLineF(QPointF{},delta).length()>1e-6,"Dimension plane is edge-on to the test camera");
                delta*=45/QLineF(QPointF{},delta).length();
            }
            const auto from=*view->dimension_handle_position(candidate,handle),to=from+delta;
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
#include "drill_point_opening_verification.inc"
        const bool shell_probe=qEnvironmentVariableIsSet("ZIMA_VERIFY_SHELL_OPENING");
        if(qEnvironmentVariableIsSet("ZIMA_VERIFY_TREATMENT_OPENING")||shell_probe) {
            for(const int sides:{4,128})for(const bool fillet:{true,false}) {
                if(shell_probe&&fillet)continue;
                auto source=document::PartDocument::create_default();
                source.history.clear();source.sketches.clear();source.history_order.clear();
                document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Treatment benchmark"));
                auto sketch=sketcher::Sketch::create_default();
                std::vector<kernel::Vec3> points;
                for(int i=0;i<sides;++i) {
                    const auto angle=2*std::numbers::pi*i/sides;
                    points.push_back({100*std::cos(angle),100*std::sin(angle),0});
                }
                for(int i=0;i<sides;++i) {
                    const auto a=points[i],b=points[(i+1)%sides];
                    static_cast<void>(sketch.add_segment(a.x,a.y,b.x,b.y));
                }
                auto extrusion=document::PartDocument::create_extrusion_container(sketch.id);
                extrusion.extrusion.length_forward=40;sketch.owner_container_id=extrusion.id;
                source.sketches.push_back(sketch);source.history.push_back(extrusion);
                graph.insert({document::PartHistoryKind::Feature,extrusion.id});source.set_body_history(graph);
                kernel::OcctKernel kernel;
                const auto calculated=kernel.evaluate_history(source.kernel_operations());
                const auto& edges=calculated.back().mesh.edges;
                const auto selected=std::ranges::find_if(edges,[](const auto& edge) {
                    return edge.points.size()>=2&&std::abs(edge.points.front().z-40)<1e-6&&std::abs(edge.points.back().z-40)<1e-6;
                });
                check(selected!=edges.end(),"Polygon fixture has no top rim edge");
                const auto file=directory/("treatment-opening-"+std::to_string(sides)+(shell_probe?"-shell.prtz":fillet?"-fillet.prtz":"-chamfer.prtz"));
                source.save(file,calculated);
                check(window.open_document_path(QString::fromStdWString(file.wstring())),"Cannot open treatment benchmark");flush();
                Json args={{"routes",Json::array({Json{{"edges",Json::array({Json{{"owner",selected->reference.owner_id},{"key",selected->reference.semantic_key}}})}}})}};
                args[fillet?"radius_mm":"distance_a_mm"]=0.5;
                if(shell_probe)args={{"thickness_mm",0.5},{"faces",Json::array()}};
                const auto owner=run(shell_probe?"shell.create":fillet?"fillet.create":"chamfer.create",args).at("container").get<std::string>();
                run("save");view->set_standard_view(viewer::StandardView::Isometric);
                for(auto* animation:view->findChildren<QVariantAnimation*>())animation->setCurrentTime(animation->duration());flush();
                if(shell_probe) {
                    // The first Shell opening establishes its existing camera
                    // framing. Measure subsequent openings from that same state.
                    QTreeWidgetItem* item=nullptr;
                    for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==owner&&(*it)->data(0,Qt::UserRole+3)=="part-container"){item=*it;break;}
                    check(item,"Missing Shell warmup row");window.show_tree_item_properties(item);flush();finish(false);
                }
                const auto state=run("documents");const auto camera=view->camera_state();
                auto* undo=window.findChild<QAction*>("undoAction");auto* redo=window.findChild<QAction*>("redoAction");
                check(undo&&redo,"Missing Undo/Redo");const bool undo_before=undo->isEnabled(),redo_before=redo->isEnabled();
                const auto packet=[&] {kernel::BodyResult body;body.mesh=view->mesh();return document::serialize_body_result(body,false);};
                const auto original=packet();
                const auto hash=[&] {const auto image=view->grabFramebuffer().convertToFormat(QImage::Format_RGBA8888);
                    return QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char*>(image.constBits()),image.sizeInBytes()),QCryptographicHash::Sha256).toHex();};
                QByteArray first;
                for(int trial=0;trial<6;++trial) {
                    QTreeWidgetItem* item=nullptr;
                    for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==owner&&(*it)->data(0,Qt::UserRole+3)=="part-container"){item=*it;break;}
                    check(item,"Missing benchmark row");
                    if(shell_probe)QCursor::setPos(window.mapToGlobal(QPoint(5,5)));
                    const auto revision=view->base_mesh_revision();
                    QElapsedTimer timer;timer.start();window.show_tree_item_properties(item);flush();const auto elapsed=timer.nsecsElapsed()/1e6;
                    const auto publications=view->base_mesh_revision()-revision;
                    check(publications==1,"Treatment opening published an unused scene");
                    const auto preview=hash();if(trial==0)first=preview;
                    check(first==preview,"Repeated treatment preview changed");
                    finish(trial<3);
                    check(run("documents")==state&&undo->isEnabled()==undo_before&&redo->isEnabled()==redo_before,"Unchanged treatment changed document or Undo/Redo");
                    check(packet()==original,"Treatment close failed to restore geometry");
                    check(view->camera_state()==camera,"Treatment close failed to restore camera");
                    std::cout<<(shell_probe?"Shell opening sides=":"Treatment opening sides=")<<sides<<" fillet="<<fillet<<" trial="<<trial<<" ms="<<elapsed
                        <<" publications="<<publications<<" preview="<<preview.constData()<<" restored="<<hash().constData()<<std::endl;
                }
                if(shell_probe) {
                    const auto saved=run("shell.get",{{"container",owner}});
                    auto* pending=edit(owner);
                    pick(viewer::CandidateKind::Face,extrusion.id,{});
                    check(pending->pending_value().shell.removed_faces.size()==1,"Shell View selection did not retain one face");
                    pending->findChild<QDoubleSpinBox*>("shellThickness")->setValue(1);finish(false);
                    check(run("shell.get",{{"container",owner}})==saved&&packet()==original,"Shell Cancel changed faces or geometry");
                    pending=edit(owner);pending->findChild<QDoubleSpinBox*>("shellThickness")->setValue(1);finish(true);
                    check(run("shell.get",{{"container",owner}}).at("thickness_mm")==1,"Shell OK did not commit thickness");
                    run("undo");check(run("shell.get",{{"container",owner}})==saved,"Shell Undo failed");
                    run("redo");check(run("shell.get",{{"container",owner}}).at("thickness_mm")==1,"Shell Redo failed");
                    run("save");check(document::PartDocument::load(file).find_container(owner)->shell.thickness==1,"Shell native save lost thickness");
                }
                run("close",{{"discard",true}});
            }
            return 0;
        }
        const bool symmetric_fixture=qEnvironmentVariableIsSet("ZIMA_VERIFY_SYMMETRIC_ENDPOINTS");
        const bool creation_only=qEnvironmentVariableIsSet(
            "ZIMA_VERIFY_EDGE_TREATMENT_CREATION_ONLY");
        if(!creation_only)for(int mode=0;mode<5;++mode) {
            const bool fillet=mode<2;
            const std::string prefix=fillet?"fillet":"chamfer";
            const auto name=stem+"-"+std::to_string(mode);
            run("new",{{"type","part"},{"name",name}});
            const auto box=zima::test::gui_rectangular_profile(window,20,20,20,!symmetric_fixture).data.at("container").get<std::string>();
            // Horizontal as well as vertical edges expose an incorrect default
            // XY annotation plane on Chamfer distance dimensions.
            const auto file=directory/(name+".prtz");run("save");
            const auto source=document::PartDocument::load(file);
            const auto edge=test::profile_key(source,box,mode==3?
                "edge:x_max:y_max:z_max--x_min:y_max:z_max":"edge:x_max:y_min:z_max--x_max:y_min:z_min");
            Json args={{"routes",Json::array({Json{{"edges",Json::array({Json{{"owner",box},{"key",edge}}})}}})}};
            if(fillet) {args["radius_mm"]=2;if(mode==1){args["mode"]="linear";args["radius_end_mm"]=3;}}
            else {args["distance_a_mm"]=2;if(mode==3){args["mode"]="two_distances";args["distance_b_mm"]=3;}if(mode==4){args["mode"]="distance_angle";args["angle_degrees"]=35;}}
            if(mode==1) {
                const auto route=run("edge_treatment.route",{{"seed",{{"owner",box},{"key",edge}}}});
                check(route.at("endpoints_complete")==true&&route.at("endpoints").size()==2,"Variable Fillet fixture has no two persisted R1 endpoints");
                check(route.at("edges").size()==(symmetric_fixture?2:1),"Variable Fillet did not traverse its physical rail");
                args["routes"][0]["edges"]=route.at("edges");
                auto start=route.at("endpoints")[0];start.erase("position_mm");
                args["routes"][0]["start"]=start;
            }
            const auto owner=run((prefix+".create").c_str(),args).at("container").get<std::string>();
            run("save");
            view->set_standard_view(viewer::StandardView::Isometric);
            for(auto* animation:view->findChildren<QVariantAnimation*>())animation->setCurrentTime(animation->duration());
            flush();
            auto* pending=edit(owner);
            if(mode==3)check(
                view->display_mode()==viewer::DisplayMode::ShadedWithEdges&&
                    !view->mesh().triangles.empty(),
                "Opening A x B Chamfer Properties replaced the shaded rollback body with wire display");
            std::vector<std::string> keys{"parameter:primary"};
            if(mode==1||mode==3)keys.push_back("parameter:secondary");
            if(mode==4)keys.push_back("parameter:treatment_angle");
            const auto verify_chamfer_envelope=[&] { if(!fillet)for(const auto& key:keys) {
                const auto candidate=pick(viewer::CandidateKind::Dimension,owner,key);
                const auto dimension=view->dimension_source(candidate);
                check(dimension.has_value(),"Missing Chamfer annotation");
                if(dimension->kind!=kernel::ViewerDimensionKind::Linear)continue;
                const auto outward=kernel::dimension_unit(kernel::dimension_cross(dimension->plane_normal,
                    kernel::dimension_measurement_direction(*dimension)));
                check(std::abs(mode==3?outward.x:outward.z)>.999,
                    "Chamfer distance is not offset along its selected edge");
                const auto bounds=view->dimension_envelope();
                const auto shown=kernel::layout_dimension(*dimension,bounds,*pending->pending_dimension_layout({owner,key,{}}));
                for(const auto corner:bounds.corners())check(
                    kernel::dimension_dot(kernel::dimension_sub(shown.line_first,corner),outward)>=8-1e-6&&
                    kernel::dimension_dot(kernel::dimension_sub(shown.line_second,corner),outward)>=8-1e-6,
                    "Chamfer distance does not clear the dimension envelope");
            }};
            verify_chamfer_envelope();
            if(mode==3) {
                auto* button=pending->findChild<QPushButton*>("edgeTreatmentFlip");
                check(button&&button->isVisible(),"Missing Chamfer orientation control");
                button->click();flush();verify_chamfer_envelope();
                button->click();flush();verify_chamfer_envelope();
            }
            if(mode==3)window.grab().save(QString::fromStdString((directory/"chamfer-default-envelope.png").string()));
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
            if(mode==3)check(
                view->display_mode()==viewer::DisplayMode::ShadedWithEdges&&
                    !view->mesh().triangles.empty(),
                "Reopening A x B Chamfer Properties replaced the shaded rollback body with wire display");
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
        if(!creation_only) {
            std::cout << "Treatment edit grips and transactions passed\n";
            return 0;
        }
        // Creation uses the same pending layout and OK/Cancel transaction.
        for(const bool fillet:{true,false})for(const bool commit:{false,true}) {
            const auto name=stem+"-create-"+std::to_string(fillet)+"-"+std::to_string(commit);
            run("new",{{"type","part"},{"name",name}});
            const auto box=zima::test::gui_rectangular_profile(window,20,20,20,!symmetric_fixture).data.at("container").get<std::string>();
            std::string rail;
            if(symmetric_fixture) {
                const auto rows=run("edge_treatment.edges").at("items");
                const auto found=std::ranges::find_if(rows,[](const auto& row){return row.at("key").template get<std::string>().starts_with("generated:");});
                check(found!=rows.end(),"Symmetric creation fixture has no authored rail");rail=found->at("key");
            }
            window.findChild<QAction*>(fillet?"filletAction":"chamferAction")->trigger();flush();
            auto* pending=dialog();const auto owner=pending->container_id();
            const auto selected=pick(viewer::CandidateKind::Edge,box,rail);
            auto historical=selected;
            historical.geometry=viewer::CandidateGeometry::OriginalReference;
            check(view->candidate_filter()&&
                    !view->candidate_filter()(historical),
                "Fillet/Chamfer creation offered a historical edge instead of a current Body edge");
            check(pending->pending_value().edge_treatment.routes.size()==1,"Mouse did not select a creation edge");
            if(symmetric_fixture)check(pending->pending_value().edge_treatment.routes.front().size()==2,"Mouse did not offer both halves of the symmetric rail");
            const auto& labels=view->transient_labels();
            std::vector<std::vector<kernel::ViewerEdge>> label_groups;
            for(std::size_t route=0;route<pending->pending_value()
                    .edge_treatment.routes.size();++route) {
                label_groups.push_back(document::current_treatment_selection_wire(
                    pending->pending_value()
                        .edge_treatment,route,std::nullopt,view->mesh()));
            }
            verify_route_labels(labels,view->interactive_labels(),
                route_node_counts(label_groups));
            const auto terminal_labels=view->interactive_labels();
            check(terminal_labels.size()==2,
                "Open Fillet/Chamfer route did not expose two purple endpoint labels");
            const bool chooses_first=terminal_labels.front().key.ends_with(":first");
            const auto initial_ends=pending->pending_value().edge_treatment.
                route_end_faces.front();
            using EndMode=document::EdgeTreatmentParameters::RouteEndMode;
            check(initial_ends.first_vertex.valid()&&
                    initial_ends.last_vertex.valid()&&
                    initial_ends.first_vertex!=initial_ends.last_vertex&&
                    initial_ends.first_mode==EndMode::Cut&&
                    initial_ends.last_mode==EndMode::Cut,
                "New treatment endpoints do not preserve automatic local-boundary trimming");
            if(commit) {
                const auto position=view->interactive_label_position(
                    terminal_labels.front().key);
                check(position.has_value(),
                    "Purple treatment endpoint has no double-click position");
                flush();
                const auto label_image=view->grab().toImage();
                const double pixel_ratio=label_image.devicePixelRatio();
                const QColor purple("#D05CFF");
                const auto purple_at=[&](QPointF logical) {
                    const int x=static_cast<int>(std::lround(
                        logical.x()*pixel_ratio));
                    const int y=static_cast<int>(std::lround(
                        logical.y()*pixel_ratio));
                    if(x<0||y<0||x>=label_image.width()||
                            y>=label_image.height())return false;
                    const auto actual=label_image.pixelColor(x,y);
                    return std::abs(actual.red()-purple.red())<35&&
                        std::abs(actual.green()-purple.green())<35&&
                        std::abs(actual.blue()-purple.blue())<35;
                };
                int text_pixels{};
                for(int y=-28;y<=2;++y)
                    for(int x=6;x<=48;++x)
                        if(purple_at(*position+QPointF(x,y)))++text_pixels;
                check(text_pixels>2,
                    "Endpoint number is not visibly painted in purple");
                const auto text_position=*position+QPointF(14.,-14.);
                mouse(QEvent::MouseMove,text_position,
                    Qt::NoButton,Qt::NoButton);
                flush();
                const auto hover_image=view->grab().toImage();
                const QColor azure("#00D1FF");
                int hover_pixels{};
                for(int y=-16;y<=6;++y)
                    for(int x=-8;x<=30;++x) {
                        const auto logical=text_position+QPointF(x,y);
                        const int px=static_cast<int>(std::lround(
                            logical.x()*pixel_ratio));
                        const int py=static_cast<int>(std::lround(
                            logical.y()*pixel_ratio));
                        if(px<0||py<0||px>=hover_image.width()||
                                py>=hover_image.height())continue;
                        const auto actual=hover_image.pixelColor(px,py);
                        if(std::abs(actual.red()-azure.red())<35&&
                           std::abs(actual.green()-azure.green())<35&&
                           std::abs(actual.blue()-azure.blue())<35)
                            ++hover_pixels;
                    }
                check(hover_pixels>2,
                    "Hovering an endpoint number did not paint it azure");
                click(text_position);
                check(pending->pending_value().edge_treatment.routes.size()==1&&
                        pending->pending_value().edge_treatment.
                            route_end_faces.front().first_mode==EndMode::Cut,
                    "Clicking an endpoint number changed the selected route");
                int point_pixels{};
                for(int y=-5;y<=5;++y)
                    for(int x=-5;x<=5;++x)
                        if(x*x+y*y<=25&&
                           purple_at(*position+QPointF(x,y)))++point_pixels;
                check(point_pixels==0,
                    "Endpoint point itself is purple instead of only its number");
                bool point_opened_menu{};
                QTimer point_probe;
                point_probe.setInterval(10);
                QObject::connect(&point_probe,&QTimer::timeout,&window,[&] {
                    for(auto* widget:QApplication::topLevelWidgets())
                        if(auto* menu=qobject_cast<QMenu*>(widget);
                           menu&&menu->isVisible()&&
                           menu->objectName()=="edgeTreatmentEndMenu") {
                            point_opened_menu=true;
                            menu->close();
                        }
                });
                point_probe.start();
                mouse(QEvent::MouseButtonDblClick,*position,
                    Qt::LeftButton,Qt::LeftButton);
                point_probe.stop();
                check(!point_opened_menu&&
                        pending->pending_value().edge_treatment.
                            route_end_faces.front().first_mode==EndMode::Cut,
                    "Double-clicking the endpoint point invoked its text action");
                bool selected_cut{};
                const auto before_menu_calculations=kernel::body_calculation_count.load();
                // The double-click handler opens a synchronous popup. Poll
                // from its nested event loop because the mouse helper may
                // process a zero-delay callback before dispatching the click.
                QTimer menu_selection;
                menu_selection.setInterval(10);
                QObject::connect(&menu_selection,&QTimer::timeout,&window,[&] {
                    auto menus=window.findChildren<QMenu*>();
                    for(auto* widget:QApplication::topLevelWidgets())
                        if(auto* menu=qobject_cast<QMenu*>(widget))
                            menus.push_back(menu);
                    for(auto* menu:menus) {
                        if(!menu->isVisible()||
                           menu->objectName()!="edgeTreatmentEndMenu"||
                           menu->actions().empty())
                            continue;
                        selected_cut=true;
                        menu_selection.stop();
                        menu->setActiveAction(menu->actions().front());
                        QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,
                            Qt::NoModifier);
                        QApplication::sendEvent(menu,&enter);
                        return;
                    }
                });
                menu_selection.start();
                mouse(QEvent::MouseButtonDblClick,
                    text_position,
                    Qt::LeftButton,Qt::LeftButton);
                menu_selection.stop();
                const auto selected_end=pending->pending_value().edge_treatment.
                    route_end_faces.front();
                check(selected_cut&&(chooses_first?
                        selected_end.first_mode:selected_end.last_mode)==EndMode::NoCut,
                    "Double-clicking a purple endpoint did not select No cut");
                check(kernel::body_calculation_count.load()==before_menu_calculations,
                    "Opening or choosing a free-end solution calculated a Body");
            }
            drag(owner,"parameter:primary",0);const auto layout=pending->dimension_layouts();
            const auto exact_wire=view->transient_edges();
            finish(commit);
            check(view->transient_labels().empty()&&
                    view->interactive_labels().empty(),
                "Fillet/Chamfer terminal labels survived closing Properties");
            run("save");const auto file=directory/(name+".prtz");
            const auto saved=document::PartDocument::load(file);
            check(saved.history.size()==(commit?2:1)&&saved.dimension_layouts==(commit?layout:std::vector<kernel::DimensionLayoutEntry>{}),
                "Creation did not commit/cancel geometry and annotation together");
            if(commit) {
                std::vector<kernel::BodyResult> saved_boundaries;
                auto reopened=document::PartDocument::load(file,&saved_boundaries);
                document::DocumentSession snapshot(std::move(reopened),std::move(saved_boundaries));
                const auto selection_wire=authored_feature_wire(snapshot,owner);
                check(!exact_wire.empty()&&selection_wire.size()==exact_wire.size()&&
                    std::equal(exact_wire.begin(),exact_wire.end(),selection_wire.begin(),
                        [](const auto& a,const auto& b){return a.points==b.points;}),
                    "Creation, saved Properties and ordinary selection use different authored wires");
                const auto& stored=saved.history.back().edge_treatment.
                    route_end_faces.front();
                check(stored.first_vertex==initial_ends.first_vertex&&
                        stored.last_vertex==initial_ends.last_vertex&&
                        stored.first_mode==(chooses_first?EndMode::NoCut:EndMode::Cut)&&
                        stored.last_mode==(chooses_first?EndMode::Cut:EndMode::NoCut),
                    "Saved treatment lost its per-endpoint Cut/No-cut choice");
            }
            if(commit) {run("undo");run("save");check(document::PartDocument::load(file).history.size()==1&&document::PartDocument::load(file).dimension_layouts.empty(),"Creation Undo left its annotation behind");}
            run("close",{{"discard",true}});
        }
        std::cout << "Treatment creation grips and transactions passed\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<std::endl;return 1;}
}
}
