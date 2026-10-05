int verify_surface_trim_ui(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        auto part=document::PartDocument::create_default();document::BodyHistoryGraph graph;
        const auto body=graph.create_body("Trim GUI");part.set_body_history(graph);
        const std::array<kernel::Vec3,4> corners{{{0,0,0},{100,0,0},{100,80,0},{0,80,0}}};
        std::array<document::HistoryContainer,2> surfaces;
        for(unsigned side=0;side<2;++side) {
            auto feature=document::create_general_surface();feature.name=side?"Crossing surface":"Base surface";
            for(unsigned i=0;i<4;++i) {
                auto sketch=sketcher::Sketch::from_serialized(feature.general_surface.boundaries[i].sketch_serialized);
                static_cast<void>(sketch.add_segment(corners[i].x,corners[i].y,corners[(i+1)%4].x,corners[(i+1)%4].y));
                feature.general_surface.boundaries[i].sketch_serialized=sketch.serialized();
            }
            if(side){feature.placement.x=50;feature.placement.z=50;feature.placement.rotation_y=90;feature.placement.absolute_rotation_y=90;}
            part.insert_history_entry(document::PartHistoryKind::Feature,feature.id);part.history.push_back(feature);surfaces[side]=std::move(feature);
        }
        kernel::OcctKernel kernel;const auto input=workspace::calculate_part_with_resolved_references(kernel,part);
        const auto path=directory/"surface-trim-ui.prtz";part.save(path,input);
        window.resize(1280,900);window.show();check(window.open_document_path(QString::fromStdString(path.string())),"Cannot open Trim fixture");flush();
        check(window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",body}}}}.dump())).ok,"Cannot activate Trim Body");flush();
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));check(view,"Trim View missing");
        auto* action=window.findChild<QAction*>("surfaceTrimAction");check(action&&action->isEnabled(),"Trim action unavailable");
        const auto editor=[&]{return dynamic_cast<SurfaceTrimDialog*>(window.findChild<QWidget*>("surfaceTrimDialog"));};
        const auto documents=window.execute_console_command("documents").data;
        const auto click=[&](QWidget* widget,QPointF point,Qt::MouseButton button) {
            for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                QMouseEvent event(type,point,QPointF(widget->mapToGlobal(point.toPoint())),button,type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(widget,&event);flush();
            }
        };
        action->trigger();flush();check(editor()&&editor()->windowType()==Qt::SubWindow,"Trim did not open common internal Properties");
        check(!editor()->findChild<QPushButton*>("containerOriginSelectionButton"),"Source-face command exposed unused placement");
        const auto id=editor()->pending.id;
        for(unsigned side=0;side<2;++side) {
            auto* table=editor()->findChild<QTableWidget*>("surfaceTrimReferences");check(table,"Trim reference table missing");
            table->cellClicked(side,2);flush();view->fit_all();flush();
            const auto source=surfaces[side].id;const auto key="surface:from:"+surfaces[side].feature_id;
            std::optional<QPointF> position;
            for(int y=20;y<view->height()-20&&!position;y+=6)for(int x=20;x<view->width()-20&&!position;x+=6)
                if(std::ranges::any_of(view->selection_candidates_at(QPointF(x,y)),[&](const auto& candidate){return candidate.kind==viewer::CandidateKind::Face&&candidate.owner_id==source&&candidate.semantic_key==key;}))position=QPointF(x,y);
            check(position.has_value(),"Original trim face absent from common picker");
            QMouseEvent move(QEvent::MouseMove,*position,QPointF(view->mapToGlobal(position->toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view,&move);flush();
            for(std::size_t i=0;i<view->selection_candidates_at(*position).size();++i) {
                const auto offered=view->offered_candidate();if(offered&&offered->owner_id==source&&offered->semantic_key==key)break;
                click(view,*position,Qt::RightButton);
            }
            const auto offered=view->offered_candidate();check(offered&&offered->owner_id==source&&offered->semantic_key==key,"Trim cycling changed the candidate list");
            click(view,*position,Qt::LeftButton);check(side?editor()->pending.surface_trim.tools.size()==1&&editor()->pending.surface_trim.tools[0].reference==kernel::EdgeReference{source,key,{}}:
                editor()->pending.surface_trim.target==kernel::FaceReference{source,key,{}},"Trim lost selected native face");
        }
        check(window.execute_console_command("documents").data==documents,"Trim picking committed history before OK");
        for(int row=0;row<1;++row) {
            auto* indicator=editor()->findChild<QTableWidget*>("surfaceTrimReferences")->cellWidget(row,1);
            check(!indicator->findChild<QLabel*>()->isVisible()&&!indicator->findChild<QPushButton*>()->isVisible(),
                "Populated required face exposed an unused entry or remove control");
        }
        editor()->findChild<QPushButton*>("surfaceTrimRegion")->click();flush();
        std::optional<QPointF> region_position;
        for(int y=20;y<view->height()-20&&!region_position;y+=6)for(int x=20;x<view->width()-20&&!region_position;x+=6) {
            const QPointF position(x,y);const auto ray=view->ray_at(position);if(!ray)continue;
            for(const auto& candidate:view->selection_candidates_at(position))if(candidate.owner_id==surfaces[0].id) {
                const auto hit=confirmed_face_hit(view->candidate_face_triangles(candidate),ray->first,ray->second);
                if(hit&&hit->x>10&&hit->x<40&&hit->y>20&&hit->y<60){region_position=position;break;}
            }
        }
        check(region_position.has_value(),"Trim retained region absent from common picker");
        QMouseEvent region_move(QEvent::MouseMove,*region_position,QPointF(view->mapToGlobal(region_position->toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(view,&region_move);flush();click(view,*region_position,Qt::LeftButton);
        check(editor()->pending.surface_trim.seed_valid&&editor()->pending.surface_trim.seed.x<40,"Trim lost clicked retained region");
        window.grab().save(QString::fromStdString((directory/"surface-trim-properties.png").string()));
        editor()->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!editor(),"Trim failed on OK");
        check(std::ranges::any_of(view->mesh().triangle_references,[&](const auto& face){return face.owner_id==id&&face.surface_result;}),"Trim surface is missing from the result");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        std::vector<kernel::BodyResult> cache;const auto saved=document::PartDocument::load(path,&cache);
        check(saved.find_container(id)&&!cache.empty()&&cache.back().calculation_errors.empty()&&std::abs(cache.back().surface_area-12000)<.02,"Trim native result changed its surfaces");
        const auto before=window.execute_console_command("documents").data;
        const auto row=[&]()->QTreeWidgetItem* {auto* tree=window.findChild<QTreeWidget*>("documentTree");for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==id&&(*it)->data(0,Qt::UserRole+3)=="part-container")return *it;return nullptr;};
        check(row(),"Trim Tree row missing");const auto revision=view->base_mesh_revision();window.show_tree_item_properties(row());flush();
        check(editor()&&view->base_mesh_revision()==revision+1,"Trim opening published an unused result scene");
        check(std::ranges::none_of(view->mesh().original_references.triangle_references,[&](const auto& face){return face.owner_id==id;}),"Trim rollback offered its own result");
        editor()->end_entry();check(editor()->active_row()==-1&&!editor()->inspected(0)&&!editor()->inspected(1),"Trim end-entry retained input or inspection");
        editor()->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!editor()&&window.execute_console_command("documents").data==before,"Trim unchanged OK created a transaction");
        window.show_tree_item_properties(row());flush();editor()->findChild<QLineEdit*>("surfaceTrimName")->setText("Canceled name");
        editor()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();check(window.execute_console_command("documents").data==before,"Trim Cancel changed history");
        window.findChild<QAction*>("undoAction")->trigger();flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();check(!document::PartDocument::load(path).find_container(id),"Trim Undo failed");
        window.findChild<QAction*>("redoAction")->trigger();flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();check(document::PartDocument::load(path).find_container(id),"Trim Redo failed");
        // Consume a supported native Intersection wire. Separately verify
        // ordinary Curve presentation without extending Trim's tool contract.
        for(const bool intersection_tool:{true,false}) {
            auto fixture=part;fixture.document_id=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
            std::string source_id,packet_owner;
            if(intersection_tool) {
                auto source=document::create_surface_intersection();source.surface_intersection.faces={
                    kernel::FaceReference{surfaces[0].id,"surface:from:"+surfaces[0].feature_id,{}},
                    kernel::FaceReference{surfaces[1].id,"surface:from:"+surfaces[1].feature_id,{}}};
                source_id=packet_owner=source.id;fixture.insert_history_entry(document::PartHistoryKind::Feature,source.id);fixture.history.push_back(std::move(source));
            }else {
                auto source=document::PartDocument::create_construction(document::ConstructionKind::Curve3D);source.curve_points.clear();
                for(const kernel::Vec3 position: {kernel::Vec3{50,0,0},kernel::Vec3{50,80,0}}) {
                    auto point=document::create_owned_point(source.id);point.origin=position;source.curve_points.push_back(std::move(point));
                }
                source_id=source.id;packet_owner=source.entity_id;fixture.insert_history_entry(document::PartHistoryKind::Construction,source.id);fixture.constructions.push_back(std::move(source));
            }
            const auto calculated=workspace::calculate_part_with_resolved_references(kernel,fixture);
            const auto wire_mesh=intersection_tool?calculated.back().mesh:fixture.construction_viewer_mesh();
            const auto edge=std::ranges::find_if(wire_mesh.original_references.edges,[&](const auto& value){return value.reference.owner_id==packet_owner;});
            check(edge!=wire_mesh.original_references.edges.end(),"Trim wire source has no calculated native edge");
            const auto tool=edge->reference;
            const auto wire_path=directory/(intersection_tool?"trim-intersection-visibility.prtz":"trim-curve-visibility.prtz");fixture.save(wire_path,calculated);
            std::cout<<(intersection_tool?"Intersection":"Curve")<<" Trim wire fixture: open, commit and presentation"<<std::endl;
            check(window.open_document_path(QString::fromStdString(wire_path.string())),"Trim wire fixture cannot open");flush();
            check(window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",body}}}}.dump())).ok,"Trim wire Body cannot activate");flush();
            const auto displayed=[&]{return std::ranges::any_of(view->mesh().edges,[&](const auto& value){return value.reference.owner_id==packet_owner;});};
            check(displayed(),"Unused wire starts hidden");
            const auto find_row=[&](const std::string& owner,const char* role)->QTreeWidgetItem* {
                auto* tree=window.findChild<QTreeWidget*>("documentTree");
                for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==owner&&(*it)->data(0,Qt::UserRole+3)==role)return *it;
                return nullptr;
            };
            auto* tree=window.findChild<QTreeWidget*>("documentTree");
            const auto hidden_owners=surface_wire_packet_owners(fixture,{source_id});
            if(!intersection_tool) {
                const auto body_visibility=[&](bool visible) {
                    auto* row=find_row(body,"part-body");check(row,"Body visibility Tree row is missing");
                    for(auto* parent=row->parent();parent;parent=parent->parent())parent->setExpanded(true);
                    tree->scrollToItem(row);flush();bool selected=false;
                    QTimer::singleShot(0,tree,[&] {
                        auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());if(!menu)return;
                        for(auto* item:menu->actions())if(item->objectName()=="bodyVisibilityAction") {
                            selected=item->text()==QObject::tr(visible?"Zobrazit":"Skrýt");
                            menu->setActiveAction(item);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
                            QApplication::sendEvent(menu,&enter);return;
                        }menu->close();
                    });
                    tree->customContextMenuRequested(tree->visualItemRect(row).center());flush();
                    check(selected,"Body Show/Hide action is unavailable or untranslated");
                    check((find_row(body,"part-body")->foreground(0).color()==QColor(125,125,125))==!visible&&
                        (find_row(source_id,"part-construction")->foreground(0).color()==QColor(125,125,125))==!visible,
                        "Body visibility did not shade its own Tree row and construction descendants");
                };
                const auto triangles=view->mesh().triangles;
                body_visibility(false);
                check(!displayed()&&view->mesh().triangles.empty()&&
                    std::ranges::none_of(view->mesh().points,[&](const auto& point){return hidden_owners.contains(point.reference.owner_id);}),
                    "Hidden Body retained its solid, construction wire or child points");
                window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
                std::vector<kernel::BodyResult> body_cache;const auto hidden_body=document::PartDocument::load(wire_path,&body_cache);
                check(!hidden_body.body_history.find(body)->visible&&body_cache.back().source_fingerprint==calculated.back().source_fingerprint,
                    "Body Hide lost persisted visibility or recalculated geometry");
                verify_surface_wire_reopened_ui(application,wire_path,packet_owner,false);flush();
                window.findChild<QAction*>("undoAction")->trigger();flush();check(displayed(),"Body Hide Undo failed");
                window.findChild<QAction*>("redoAction")->trigger();flush();check(!displayed(),"Body Hide Redo failed");
                body_visibility(true);check(displayed()&&view->mesh().triangles==triangles,"Body Show changed its calculated geometry");
                toggle_surface_wire_tree_ui(application,tree,find_row(source_id,"part-construction"),true);flush();
                check(find_row(source_id,"part-construction")->foreground(0).color()==QColor(125,125,125),"Hidden Curve is not gray in Tree");
                check(!displayed()&&std::ranges::none_of(view->mesh().points,[&](const auto& point){return hidden_owners.contains(point.reference.owner_id);}),
                    "Manual Curve Hide retained its wire or child points");
                window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
                std::vector<kernel::BodyResult> hidden_cache;const auto hidden=document::PartDocument::load(wire_path,&hidden_cache);
                check(!hidden.surface_wire_visibility.at(source_id)&&hidden_cache.back().source_fingerprint==calculated.back().source_fingerprint,
                    "Manual Curve Hide lost native visibility or changed geometry");
                verify_surface_wire_reopened_ui(application,wire_path,packet_owner,false);flush();
                window.findChild<QAction*>("undoAction")->trigger();flush();check(displayed(),"Manual Curve Hide Undo failed");
                window.findChild<QAction*>("redoAction")->trigger();flush();check(!displayed(),"Manual Curve Hide Redo failed");
                toggle_surface_wire_tree_ui(application,tree,find_row(source_id,"part-construction"),false);flush();check(displayed(),"Manual Curve Show failed");
                check(find_row(source_id,"part-construction")->foreground(0).color()!=QColor(125,125,125),"Shown Curve remains gray in Tree");
                window.findChild<QAction*>("saveDocumentAction")->trigger();flush();verify_surface_wire_reopened_ui(application,wire_path,packet_owner,true);flush();
                continue;
            }
            action->trigger();flush();check(editor(),"Trim wire creation dialog is unavailable");
            const auto wire_trim=editor()->pending.id;
            editor()->set_target({surfaces[0].id,"surface:from:"+surfaces[0].feature_id,{}});
            editor()->set_tool({{tool.owner_id,tool.semantic_key,{}},false});editor()->set_region({25,40,0});flush();
            check(displayed(),"Pending Trim hid its cutting wire before OK");
            editor()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            if(editor()) {
                QStringList diagnostics;for(auto* label:editor()->findChildren<QLabel*>())if(!label->text().isEmpty())diagnostics.push_back(label->text());
                throw std::runtime_error("Trim wire OK did not commit: "+diagnostics.join(" | ").toStdString());
            }
            check(!displayed(),"Trim OK did not hide its consumed cutting wire");
            check(find_row(source_id,"part-container")->foreground(0).color()==QColor(125,125,125),"Consumed Intersection is not gray in Tree");
            check(std::ranges::none_of(view->mesh().points,[&](const auto& point){return hidden_owners.contains(point.reference.owner_id);}),"Hidden Trim wire retained its points");
            window.findChild<QAction*>("undoAction")->trigger();flush();check(displayed(),"Undo Trim did not restore its previously visible wire");
            window.findChild<QAction*>("redoAction")->trigger();flush();check(!displayed(),"Redo Trim did not hide its wire again");
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            std::vector<kernel::BodyResult> trim_results;const auto committed=document::PartDocument::load(wire_path,&trim_results);
            check(committed.find_container(wire_trim)&&trim_results.back().calculation_errors.empty()&&std::abs(trim_results.back().surface_area-12000)<.02,
                "Trim wire visibility changed the calculated result");
            const auto unchanged=window.execute_console_command("documents").data;
            window.show_tree_item_properties(find_row(wire_trim,"part-container"));flush();
            check(editor()&&displayed(),"Editing Trim does not temporarily restore its cutting wire");
            editor()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
            check(!displayed()&&window.execute_console_command("documents").data==unchanged,"Trim Cancel did not restore hidden wire/history");
            toggle_surface_wire_tree_ui(application,tree,find_row(source_id,intersection_tool?"part-container":"part-construction"),false);flush();check(displayed(),"Tree Show cannot display a consumed Trim wire");
            check(find_row(source_id,"part-container")->foreground(0).color()!=QColor(125,125,125),"Shown Intersection remains gray in Tree");
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            std::vector<kernel::BodyResult> shown_results;const auto shown=document::PartDocument::load(wire_path,&shown_results);
            check(shown.surface_wire_visibility.at(source_id)&&shown_results.back().source_fingerprint==trim_results.back().source_fingerprint,
                "Trim wire Show lost its native presentation or recalculated geometry");
            verify_surface_wire_reopened_ui(application,wire_path,packet_owner,true);flush();
            toggle_surface_wire_tree_ui(application,tree,find_row(source_id,intersection_tool?"part-container":"part-construction"),true);flush();check(!displayed(),"Tree Hide cannot hide a consumed Trim wire");
            window.findChild<QAction*>("undoAction")->trigger();flush();check(displayed(),"Trim wire Hide Undo failed");
            window.findChild<QAction*>("redoAction")->trigger();flush();check(!displayed(),"Trim wire Hide Redo failed");
            window.show_tree_item_properties(find_row(wire_trim,"part-container"));flush();check(editor()&&displayed(),"Trim edit did not override manual wire hiding temporarily");
            const auto no_op=window.execute_console_command("documents").data;editor()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            check(!editor()&&!displayed()&&window.execute_console_command("documents").data==no_op,"Unchanged Trim OK changed visibility/history");
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            verify_surface_wire_reopened_ui(application,wire_path,packet_owner,false);flush();
        }
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}
}
