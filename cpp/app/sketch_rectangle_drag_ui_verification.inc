int verify_sketch_rectangle_drag_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool value,const char* text){if(!value)throw std::runtime_error(text);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        for(const QString theme:{"light","dark"}) {
            QSettings preferences(QString::fromStdString((directory/"config.ini").string()),QSettings::IniFormat);
            preferences.setValue("Application/Theme",theme);preferences.sync();
            std::vector<kernel::BodyResult> bodies;document::PartDocument part;std::string sketch_id,body;
            const auto source=qEnvironmentVariable("ZIMA_VERIFY_SYMMETRIC_RECTANGLE_FILE");
            if(!source.isEmpty()) {
                part=document::PartDocument::load(std::filesystem::u8path(source.toStdString()),&bodies);
                sketch_id="01a16725ed247f3080fa42688a17c36f";
                const auto sketch=std::ranges::find(part.sketches,sketch_id,&sketcher::Sketch::id);
                check(sketch!=part.sketches.end(),"Actual Feature 005 Sketch is missing");
                const auto* owner=part.body_owner_for_object(sketch->owner_container_id);check(owner,"Actual rectangle has no owning Body");body=owner->scope.id;
            } else {
                part=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
                static_cast<void>(sketch.add_rectangle(-12,7,12,-7));
                const auto a=sketch.segments[0].first_point_id,b=sketch.segments[0].second_point_id,c=sketch.segments[1].second_point_id;
                static_cast<void>(sketch.add_symmetric_constraint(a,b,"sketch_axis:y"));static_cast<void>(sketch.add_symmetric_constraint(b,c,"sketch_axis:x"));
                auto feature=document::PartDocument::create_sketch_container();feature.feature.sketch_id=sketch.id;sketch.owner_container_id=feature.id;sketch_id=sketch.id;
                part.sketches={sketch};part.history={feature};document::BodyHistoryGraph graph;body=graph.create_body("Centered rectangle");
                graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
            }
            const auto original=*std::ranges::find(part.sketches,sketch_id,&sketcher::Sketch::id);
            check(original.points.size()==4,"Rectangle GUI fixture does not have four native vertices");
            const auto file=directory/("rectangle-drag-"+theme.toStdString()+".prtz");part.save(file,bodies);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(document::path_to_utf8(directory)));window.resize(1200,900);window.show();
            check(window.open_document_path(QString::fromStdString(document::path_to_utf8(file))),"Cannot open rectangle GUI fixture");flush();
            check(activate_test_body(application,window,body),"Cannot activate rectangle Body");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch_id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"Rectangle Sketch is missing in Tree");window.show_tree_item_properties(row);flush();
            auto* finish=window.findChild<QAction*>("finishSketchAction");
            check(finish,"Rectangle Finish Sketch action is missing");
            if(!finish->isEnabled()) {
                auto* open=window.findChild<QPushButton*>("sketchOpenButton");
                if(!open)open=window.findChild<QPushButton*>("featureSketchButton");
                if(!open)open=window.findChild<QPushButton*>("primitiveOwnSketchButton");
                check(open,"Rectangle Sketch entry control is missing");open->click();flush();
            }
            check(finish->isEnabled(),"Rectangle Sketch editor did not open");
            std::cout<<"Symmetric rectangle GUI "<<theme.toStdString()<<": Sketch editor opened"<<std::endl;
            QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Rectangle View is missing");
            window.findChild<QAction*>("fitViewAction")->trigger();flush();
            const auto local=[&](QPointF pixel){const auto ray=view->ray_at(pixel);check(ray.has_value(),"Rectangle ray is missing");const auto point=original.intersect_ray(ray->first,ray->second);check(point.has_value(),"Rectangle Sketch plane is missing");return *point;};
            const auto o=local({0,0}),u=local({100,0}),v=local({0,100});const double ux=(u[0]-o[0])/100,uy=(u[1]-o[1])/100,vx=(v[0]-o[0])/100,vy=(v[1]-o[1])/100,det=ux*vy-uy*vx;
            const auto screen=[&](double x,double y){return QPointF(((x-o[0])*vy-(y-o[1])*vx)/det,(ux*(y-o[1])-uy*(x-o[0]))/det);};
            const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons=Qt::NoButton){QMouseEvent event(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();};
            // Embedded Feature sketches remain private drafts until the
            // owning dialog's OK. Read exact native viewer points during the
            // gesture, then verify persistence after committing that dialog.
            const auto snapshot=[&]{
                auto result=part;auto& sketch=*std::ranges::find(result.sketches,sketch_id,&sketcher::Sketch::id);
                for(auto& point:sketch.points) {
                    const auto found=std::ranges::find_if(view->mesh().points,[&](const auto& value){
                        return value.reference.owner_id==sketch_id&&value.reference.semantic_key=="point:"+point.id;});
                    check(found!=view->mesh().points.end(),"Rectangle native viewer point is missing");
                    const auto position=original.local_point(found->position);point.x=position[0];point.y=position[1];
                }
                return result;
            };
            const auto verify=[&](const auto& doc) {
                const auto& sketch=*std::ranges::find(doc.sketches,sketch_id,&sketcher::Sketch::id);
                check(sketch.points.size()==4&&sketch.external_references==original.external_references,"GUI rectangle drag changed native identities or read-only sources");
                double sx{},sy{};const auto first=sketch.points.front();
                for(const auto& p:sketch.points){sx+=p.x;sy+=p.y;check(std::abs(std::abs(p.x)-std::abs(first.x))<1e-6&&std::abs(std::abs(p.y)-std::abs(first.y))<1e-6,"GUI drag violated centered rectangle equations");}
                check(std::abs(sx)<1e-6&&std::abs(sy)<1e-6,"GUI drag shifted the rectangle center");
            };
            for(const auto& vertex:original.points)for(int repeat=0;repeat<3;++repeat) {
                const auto before=snapshot();const auto& before_sketch=*std::ranges::find(before.sketches,sketch_id,&sketcher::Sketch::id);const auto point=*before_sketch.find_point(vertex.id);
                const auto from=screen(point.x,point.y);
                auto delta=screen(point.x+std::copysign(1.2,point.x),point.y+std::copysign(.8,point.y))-from;
                if(delta.manhattanLength()<24)delta*=24/std::max(.001,delta.manhattanLength());
                const auto to=from+delta;
                const auto candidates=view->selection_candidates_at(from);check(!candidates.empty()&&candidates.front().owner_id==sketch_id&&candidates.front().semantic_key=="point:"+vertex.id,"Common picker did not offer the exact rectangle point");
                mouse(from,QEvent::MouseMove,Qt::NoButton);mouse(from,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);
                mouse(to,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(to,QEvent::MouseButtonRelease,Qt::LeftButton);
                const auto after=snapshot();verify(after);const auto& after_sketch=*std::ranges::find(after.sketches,sketch_id,&sketcher::Sketch::id);const auto* moved=after_sketch.find_point(vertex.id);
                if(std::hypot(moved->x-point.x,moved->y-point.y)<=.1) {
                    std::cerr<<"Unmoved rectangle vertex "<<vertex.id<<" from "<<point.x<<','<<point.y
                        <<" pixel "<<from.x()<<','<<from.y()<<" delta "<<delta.x()<<','<<delta.y()<<std::endl;
                    window.grab().save(QString::fromStdString(document::path_to_utf8(directory/"rectangle-drag-failure.png")));
                }
                check(std::hypot(moved->x-point.x,moved->y-point.y)>.1,"Actual rectangle point did not move after mouse drag");
                window.findChild<QAction*>("undoAction")->trigger();flush();const auto undone=snapshot();check(std::ranges::find(undone.sketches,sketch_id,&sketcher::Sketch::id)->serialized()==before_sketch.serialized(),"GUI rectangle drag Undo lost the original Sketch");
                window.findChild<QAction*>("redoAction")->trigger();flush();const auto redone=snapshot();verify(redone);
            }
            const auto expected=snapshot();
            view->grabFramebuffer().save(QString::fromStdString(document::path_to_utf8(directory/("rectangle-drag-"+theme.toStdString()+".png"))));
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible())
                if(auto* buttons=dialog->findChild<QDialogButtonBox*>())if(auto* ok=buttons->button(QDialogButtonBox::Ok)){ok->click();flush();break;}
            check(window.execute_console_command("save").ok,"Rectangle native Save failed");flush();
            const auto reopened=document::PartDocument::load(file);verify(reopened);
            const auto& saved=*std::ranges::find(reopened.sketches,sketch_id,&sketcher::Sketch::id);
            const auto& wanted=*std::ranges::find(expected.sketches,sketch_id,&sketcher::Sketch::id);
            for(const auto& point:wanted.points) {
                const auto* found=saved.find_point(point.id);check(found&&std::hypot(found->x-point.x,found->y-point.y)<1e-6,"Rectangle native reopen lost a dragged point");
            }
            std::cout<<"Symmetric rectangle GUI "<<theme.toStdString()<<": all four exact point picks, 12 real drags, native readback, immutable references and Undo/Redo passed"<<std::endl;
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<"Symmetric rectangle GUI: "<<e.what()<<'\n';return 1;}
}
