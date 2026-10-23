int verify_sketch_curve_plane_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool value,const char* text){if(!value)throw std::runtime_error(text);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        for(const auto& theme:QStringList{"light","dark"})for(const auto& language:QStringList{"cs","en","de","fr","ru"})for(bool curve3d:{false,true})for(bool contour:{false,true}) {
            QSettings preferences(QString::fromStdString((directory/"config.ini").string()),QSettings::IniFormat);
            preferences.setValue("Application/Language",language);preferences.setValue("Application/Theme",theme);preferences.sync();
            auto part=document::PartDocument::create_default();auto stock=sketcher::Sketch::create_default();
            static_cast<void>(stock.add_rectangle(-50,-15,-30,15));auto solid=document::PartDocument::create_feature_container(stock.id);
            solid.feature.sides[0].length=2;stock.owner_container_id=solid.id;
            auto source=sketcher::Sketch::create_default(),target=sketcher::Sketch::create_default();
            auto source_owner=document::PartDocument::create_sketch_container(),owner=document::PartDocument::create_sketch_container();
            source.owner_container_id=source_owner.id;source_owner.feature.sketch_id=source.id;target.owner_container_id=owner.id;owner.feature.sketch_id=target.id;
            target.plane_auto=false;target.plane=curve3d?sketcher::SketchPlane::XY:sketcher::SketchPlane::XZ;
            document::BodyHistoryGraph graph;const auto body=graph.create_body("Curve-plane GUI");graph.insert({document::PartHistoryKind::Feature,solid.id});
            std::string source_id,key;
            part.history={solid};part.sketches={stock};
            if(curve3d) {
                auto object=document::PartDocument::create_construction(document::ConstructionKind::Curve3D);object.curve_type=document::Curve3DType::InterpolatingSpline;
                for(int i=0;i<4;++i){auto point=document::PartDocument::create_construction(document::ConstructionKind::Point);point.parent_construction_id=object.id;point.origin={30.*i,0,i%2?8.:-8.};object.curve_points.push_back(point);}
                source_id=object.entity_id;key=document::curve3d_route(object).segments[1].source_id;part.constructions.push_back(object);
                graph.insert({document::PartHistoryKind::Construction,object.id});
            } else {
                key="bspline:"+source.add_bspline({{0,-8},{100./3,14},{200./3,-14},{100,8}},3,false,false,1e-6,false);
                source_id=source.id;part.history.push_back(source_owner);part.sketches.push_back(source);graph.insert({document::PartHistoryKind::Feature,source_owner.id});
            }
            part.history.push_back(owner);part.sketches.push_back(target);graph.insert({document::PartHistoryKind::Feature,owner.id});part.set_body_history(graph);part.resolve_constructions();
            const kernel::OcctKernel kernel;const auto bodies=kernel.evaluate_history(part.kernel_operations());
            check(!bodies.empty()&&bodies.back().calculation_errors.empty(),"Curve-plane GUI fixture failed to calculate");
            const auto stem="curve-plane-"+theme.toStdString()+"-"+language.toStdString()+"-"+std::to_string(curve3d)+std::to_string(contour);
            const auto file=directory/(stem+".prtz");part.save(file,bodies);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(document::path_to_utf8(directory)));window.resize(1200,900);window.show();
            check(window.open_document_path(QString::fromStdString(document::path_to_utf8(file))),"Cannot open curve-plane GUI fixture");flush();
            check(activate_test_body(application,window,body),"Cannot activate curve-plane target Body");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==target.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"Curve-plane target Sketch row missing");window.show_tree_item_properties(row);flush();
            window.findChild<QPushButton*>("sketchOpenButton")->click();flush();
            QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Curve-plane View missing");
            // Localized toolbars settle after Sketch entry. Fit the actual
            // source scene once they have their final width, as a user can.
            window.findChild<QAction*>("fitViewAction")->trigger();flush();
            auto* action=window.findChild<QAction*>(contour?"sketchIntersectionProfileAction":"sketchIntersectionReferenceAction");check(action&&action->isEnabled(),"Curve-plane command unavailable");
            const auto settings=app::ApplicationSettings::load(QString::fromStdString(document::path_to_utf8(directory)));const auto label=QString::fromUtf8(contour?"Intersection → contour":"Intersection → reference");
            check(!settings.qt_translations.value(label).isEmpty()&&action->text()==settings.qt_translations.value(label),"Curve-plane action translation is missing");
            action->trigger();flush();
            const auto hit=[&](bool branch)->QPointF {
                for(int y=8;y<view->height()-8;y+=2)for(int x=8;x<view->width()-8;x+=2) {
                    const auto values=view->selection_candidates_at(QPointF(x,y));if(values.empty())continue;const auto& value=values.front();
                    if(branch?(value.kind==viewer::CandidateKind::SketchExternalReference&&value.owner_id==target.id&&value.semantic_key.starts_with("external_point:")):
                            (value.owner_id==source_id&&value.semantic_key==key&&value.geometry==viewer::CandidateGeometry::OriginalReference))return {double(x),double(y)};
                }
                window.grab().save(QString::fromStdString(document::path_to_utf8(directory/(stem+"-failed.png"))));
                std::cerr<<"Curve-plane expected "<<source_id<<" "<<key<<"; original edges="<<view->mesh().original_references.edges.size()<<'\n';
                for(const auto& e:view->mesh().original_references.edges)if(e.reference.owner_id==source_id)
                    std::cerr<<"Original source edge "<<e.reference.semantic_key<<" samples="<<e.points.size()<<'\n';
                throw std::runtime_error(branch?"Curve-plane point branch cannot be picked":"Exact source curve cannot be picked");
            };
            const auto click=[&](QPointF p,Qt::MouseButton button=Qt::LeftButton) {
                for(auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                    QMouseEvent event(type,p,QPointF(view->mapToGlobal(p.toPoint())),type==QEvent::MouseMove?Qt::NoButton:button,
                        type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();
                }
            };
            click(hit(false));
            if(!curve3d){click(hit(true),Qt::MiddleButton);check(action->isChecked(),"Short middle click ended curve command");click(hit(false));for(int i=0;i<3;++i)click(hit(true));}
            const auto snapshot=[&]{window.findChild<QAction*>("saveDocumentAction")->trigger();flush();return document::PartDocument::load(file);};
            const auto saved=snapshot();const auto& result=saved.sketches.back();const std::size_t count=curve3d?1:3;
            check(result.external_references.size()==count&&result.points.size()==(contour?count:0)&&result.bsplines.empty()&&result.segments.empty(),"Curve-plane GUI created sampled geometry or wrong point count");
            for(const auto& reference:result.external_references)check(reference.curve_intersection&&!reference.broken&&reference.cached_points.size()==1,"Saved curve-plane point binding missing");
            if(!curve3d)check(saved.sketches[1].serialized()==part.sketches[1].serialized(),"Curve-plane GUI modified source Sketch");
            else check(saved.constructions==part.constructions,"Curve-plane GUI modified source 3D curve");
            for(std::size_t i=0;i<count;++i){window.findChild<QAction*>("undoAction")->trigger();flush();}
            check(snapshot().sketches.back().external_references.empty(),"Curve-plane GUI Undo left point bindings");
            for(std::size_t i=0;i<count;++i){window.findChild<QAction*>("redoAction")->trigger();flush();}
            check(snapshot().sketches.back().serialized()==result.serialized(),"Curve-plane GUI Redo/save/reopen changed roots");
            view->grabFramebuffer().save(QString::fromStdString(document::path_to_utf8(directory/(stem+".png"))));
            std::cout<<"Curve-plane GUI "<<theme.toStdString()<<" "<<language.toStdString()<<" 3D="<<curve3d<<" contour="<<contour<<": exact source/branch picking, middle return, source isolation, native reopen and Undo/Redo passed\n";
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<"Curve-plane GUI: "<<e.what()<<'\n';return 1;}
}
