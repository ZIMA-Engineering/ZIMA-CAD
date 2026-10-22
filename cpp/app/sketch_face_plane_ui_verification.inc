int verify_sketch_face_plane_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        const bool spline_case=qEnvironmentVariableIsSet("ZIMA_VERIFY_FACE_PLANE_SPLINE");
        const bool trimmed_case=qEnvironmentVariableIsSet("ZIMA_VERIFY_FACE_PLANE_TRIMMED");
        for(bool assembly_mode:{false,true})for(bool multiple:{false,true})for(bool contour:{false,true}) {
            if(qEnvironmentVariableIsSet("ZIMA_VERIFY_FACE_PLANE_SINGLE")&&(assembly_mode||multiple||!contour))continue;
            std::cout<<"Face-plane GUI "<<assembly_mode<<multiple<<contour<<std::endl;
            auto part=document::PartDocument::create_default();auto source=sketcher::Sketch::create_default();
            if(spline_case) {
                static_cast<void>(source.add_bspline({{-5,0},{-3,5},{3,5},{5,0}}));static_cast<void>(source.add_segment(5,0,-5,0));
            } else if(!multiple)static_cast<void>(source.add_circle(0,0,5));
            else {
                const std::array<std::array<double,2>,8> points{{{0,0},{100,0},{100,20},{20,20},{20,60},{100,60},{100,80},{0,80}}};
                for(std::size_t i=0;i<points.size();++i)static_cast<void>(source.add_segment(points[i][0],points[i][1],points[(i+1)%points.size()][0],points[(i+1)%points.size()][1]));
            }
            auto feature=document::PartDocument::create_extrusion_container(source.id);feature.extrusion.length_forward=10;source.owner_container_id=feature.id;
            auto target=sketcher::Sketch::create_default();auto owner=document::PartDocument::create_sketch_container();target.owner_container_id=owner.id;owner.feature.sketch_id=target.id;
            if(multiple){target.plane=sketcher::SketchPlane::YZ;target.plane_auto=false;owner.placement.x=50;}else owner.placement.z=5;
            part.history={feature,owner};part.sketches={source,target};
            document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Source"));graph.insert({document::PartHistoryKind::Feature,feature.id});
            const auto body=graph.create_body("Target");graph.insert({document::PartHistoryKind::Feature,owner.id});part.set_body_history(graph);part.resolve_constructions();
            if(trimmed_case) {
                auto tool=sketcher::Sketch::create_default();static_cast<void>(tool.add_rectangle(0,-10,10,10));
                auto cut=document::PartDocument::create_extrusion_container(tool.id);cut.feature.sides[0].length=10;cut.combine_mode=document::CombineMode::Subtract;tool.owner_container_id=cut.id;
                part.history.insert(part.history.end()-1,cut);part.sketches.insert(part.sketches.end()-1,tool);
                auto history=part.body_history;history.activate(history.bodies().front().scope.id);history.insert({document::PartHistoryKind::Feature,cut.id});history.activate(body);part.set_body_history(history);part.resolve_constructions();
            }
            kernel::OcctKernel kernel;const auto calculated=kernel.evaluate_history(part.kernel_operations());
            check(!calculated.empty()&&calculated.back().calculation_errors.empty(),"Face-plane GUI source calculation failed");
            kernel::FaceReference face;const auto& mesh=calculated.back().mesh;
            for(std::size_t i=0;i<mesh.triangle_references.size();++i) {
                const auto& ref=mesh.triangle_references[i];if(!trimmed_case&&ref.owner_id!=feature.id)continue;
                if(spline_case){if(!ref.surface||ref.surface->kind!=kernel::SurfaceGeometry::Kind::Plane){face=ref;break;}}
                else if(!multiple){if(ref.surface&&ref.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder){face=ref;break;}}
                else {bool cap=true;for(unsigned j=0;j<3;++j)cap&=std::abs(mesh.vertices.at(mesh.triangles.at(i*3+j)).z-10)<1e-8;if(cap){face=ref;break;}}
            }
            check(face.valid(),"Face-plane GUI source face missing");
            const auto stem=std::string(spline_case?"face-plane-spline-":trimmed_case?"face-plane-trimmed-":"face-plane-")+(assembly_mode?"assembly-":"")+char('0'+multiple)+char('0'+contour);
            const auto file=directory/(stem+(assembly_mode?".asmz":".prtz"));std::filesystem::path source_file;
            if(!assembly_mode)part.save(file,calculated);
            else {
                auto source_part=document::PartDocument::create_default();source_part.history={feature};source_part.sketches={source};
                document::BodyHistoryGraph sources;static_cast<void>(sources.create_body("Source"));sources.insert({document::PartHistoryKind::Feature,feature.id});source_part.set_body_history(sources);source_part.resolve_constructions();
                const auto native=kernel.evaluate_history(source_part.kernel_operations());source_file=directory/(stem+"-source.prtz");source_part.save(source_file,native);
                auto assembly=assembly::AssemblyDocument::create_default();
                const auto component=assembly::AssemblyDocument::create_part_occurrence("Source",source_part.document_id,source_file,native.back());
                assembly.components.push_back(component);assembly.insert_history_entry(assembly::AssemblyDocument::HistoryKind::Component,component.occurrence_id);
                assembly.insert_sketch(target,owner);assembly.resolve_constructions();assembly.save(file);
            }
            app::AssemblyWorkspaceWindow window(QString::fromStdString(document::path_to_utf8(directory)));window.resize(1200,900);window.show();
            check(window.open_document_path(QString::fromStdString(document::path_to_utf8(file))),"Cannot open face-plane GUI fixture");flush();
            if(!assembly_mode)check(activate_test_body(application,window,body),"Cannot activate face-plane target Body");
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==(assembly_mode?owner.id:target.id)&&
                (*i)->data(0,Qt::UserRole+3).toString()==(assembly_mode?"assembly-sketch-container":"part-sketch")){row=*i;break;}
            check(row,"Face-plane target Sketch missing");window.show_tree_item_properties(row);flush();
            auto* open=window.findChild<QPushButton*>("sketchOpenButton");check(open,"Face-plane Sketch editor missing");open->click();flush();
            QEventLoop settled;QTimer::singleShot(950,&settled,&QEventLoop::quit);settled.exec();flush();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Face-plane view missing");
            view->set_view_direction({1,-1,-1},0);view->fit_all();flush();
            QEventLoop camera;QTimer::singleShot(950,&camera,&QEventLoop::quit);camera.exec();flush();
            auto* action=window.findChild<QAction*>(contour?"sketchIntersectionProfileAction":"sketchIntersectionReferenceAction");
            check(action&&action->isEnabled()&&!action->icon().isNull(),"Face-plane action/icon unavailable");
            std::cerr<<"Face-plane command begin"<<std::endl;action->trigger();flush();std::cerr<<"Face-plane command end"<<std::endl;
            const auto find_hit=[&](bool branch)->QPointF {
                for(int y=12;y<view->height()-12;y+=3)for(int x=12;x<view->width()-12;x+=3) {
                    const QPointF p(x,y);const auto candidates=view->selection_candidates_at(p);if(candidates.empty())continue;
                    const auto& c=candidates.front();
                    if(branch?(c.kind==viewer::CandidateKind::SketchExternalReference&&c.owner_id==target.id):
                        (c.kind==viewer::CandidateKind::Face&&c.owner_id==face.owner_id&&c.semantic_key==face.semantic_key))return p;
                }
                std::cerr<<"Expected face "<<face.owner_id<<" "<<face.semantic_key<<"; triangles="<<view->mesh().triangle_references.size()<<std::endl;
                std::set<std::pair<std::string,std::string>> seen;
                for(int y=12;y<view->height()-12;y+=6)for(int x=12;x<view->width()-12;x+=6)
                    for(const auto& c:view->selection_candidates_at(QPointF(x,y)))seen.emplace(c.owner_id,c.semantic_key);
                for(const auto& [owner,key]:seen)std::cerr<<"Offered "<<owner<<" "<<key<<std::endl;
                window.grab().save(QString::fromStdString(document::path_to_utf8(directory/"face-plane-failure.png")));
                throw std::runtime_error(branch?"No individual intersection branch can be picked":"No exact source face can be picked");
            };
            const auto click=[&](QPointF p,Qt::MouseButton button=Qt::LeftButton) {
                for(const auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                    QMouseEvent event(type,p,QPointF(view->mapToGlobal(p.toPoint())),type==QEvent::MouseMove?Qt::NoButton:button,
                        type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();
                }
            };
            const auto snapshot=[&] {window.findChild<QAction*>("saveDocumentAction")->trigger();flush();return assembly_mode?
                assembly::AssemblyDocument::load(file).sketches:document::PartDocument::load(file).sketches;};
            const auto read=[&](const auto& sketches)->const sketcher::Sketch& {const auto found=std::ranges::find(sketches,target.id,&sketcher::Sketch::id);check(found!=sketches.end(),"Saved target missing");return *found;};
            const auto choose_face=[&]{std::cerr<<"Face-plane source begin"<<std::endl;click(find_hit(false));std::cerr<<"Face-plane source end"<<std::endl;};
            choose_face();
            if(multiple) {
                const auto untouched=window.execute_console_command(QString::fromStdString(nlohmann::json{
                    {"command","sketch.entities"},{"arguments",{{"sketch",target.id}}}}.dump()));
                check(untouched.ok&&untouched.data.at("total")==0,"Face choice committed all branches without selection");
                const auto branch=find_hit(true);std::cerr<<"Face-plane middle begin"<<std::endl;click(branch,Qt::MiddleButton);std::cerr<<"Face-plane middle end"<<std::endl;
                check(action->isChecked(),"Short middle click ended command instead of returning to faces");
                choose_face();
                for(unsigned i=0;i<2;++i){std::cerr<<"Face-plane branch begin"<<std::endl;click(find_hit(true));std::cerr<<"Face-plane branch end"<<std::endl;}
            }
            const auto stored=snapshot();const auto& result=read(stored);
            check(spline_case?result.bsplines.size()==1&&result.bsplines.front().degree==3&&result.bsplines.front().control_point_ids.size()==4:result.bsplines.empty(),"Face-plane contour lost its exact source type/basis");
            const auto expected=multiple?(contour?6u:2u):1u;check(result.external_references.size()==expected,"Wrong number of selected branches/endpoints");
            for(const auto& r:result.external_references)check(r.intersection.has_value()&&!r.broken,"Saved branch binding missing or broken");
            check(contour?(multiple?result.segments.size()==2:trimmed_case?result.arcs.size()==1:spline_case?result.bsplines.size()==1:result.circles.size()==1):result.segments.empty()&&result.circles.empty(),"Reference and contour commands have the same output");
            if(contour&&!multiple&&!spline_case&&!trimmed_case)check(std::abs(result.circles.front().radius-5)<1e-7&&result.points.size()==1,"Certified circle lost its radius or gained spline poles");
            if(trimmed_case)check(result.circles.empty()&&std::abs(result.arcs.front().radius-5)<1e-7&&result.points.size()==3,"Trimmed source face became an untrimmed circle or extra control points");
            const auto saved_source=assembly_mode?document::PartDocument::load(source_file).sketches.front():stored.front();
            check(saved_source.serialized()==part.sketches.front().serialized(),"Intersection changed read-only source Sketch");
            const auto stable=result.serialized();
            for(unsigned i=0;i<(multiple?2u:1u);++i){window.findChild<QAction*>("undoAction")->trigger();flush();}
            const auto undone=snapshot();check(read(undone).external_references.empty()&&read(undone).segments.empty()&&read(undone).circles.empty()&&read(undone).bsplines.empty()&&read(undone).arcs.empty(),"Intersection Undo did not restore input");
            for(unsigned i=0;i<(multiple?2u:1u);++i){window.findChild<QAction*>("redoAction")->trigger();flush();}
            const auto restored=snapshot();check(read(restored).serialized()==stable,"Intersection Redo/save/reopen changed selected branch");
            window.grab().save(QString::fromStdString(document::path_to_utf8(directory/(stem+".png"))));
        }
        std::cout<<"Face-plane reference/contour, individual branch mouse selection, MMB, circle type, Undo/Redo and persistence passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
