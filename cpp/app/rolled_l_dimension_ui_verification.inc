int verify_rolled_l_dimension_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        const auto source=qEnvironmentVariable("ZIMA_VERIFY_ROLLED_L_FILE");
        check(!source.isEmpty(),"Rolled L verification needs its private source path");
        for(bool universal:{false,true})for(bool reverse:{false,true}) {
            if(qEnvironmentVariableIsSet("ZIMA_VERIFY_ROLLED_L_UNIVERSAL_ONLY")&&!universal)continue;
            std::cout<<"Rolled L GUI universal="<<universal<<" reverse="<<reverse<<std::endl;
            auto part=document::PartDocument::load(std::filesystem::path(source.toStdWString()));
            const auto sketch=part.sketches.front();check(sketch.points.size()==6&&sketch.corner_radii.size()==3,"Unexpected rolled L source");
            const auto path=directory/("rolled-l-copy-"+std::to_string(universal)+"-"+std::to_string(reverse)+".prtz");part.save(path);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
            check(window.open_document_path(QString::fromStdString(path.string())),"Rolled L copy failed to open");flush();
            check(activate_test_body(application,window,part.body_history.bodies().front().scope.id),"Rolled L Body missing");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"Rolled L Sketch row missing");window.show_tree_item_properties(row);flush();
            auto* open=window.findChild<QPushButton*>("sketchOpenButton");if(!open)open=window.findChild<QPushButton*>("featureSketchButton");
            if(!open)open=window.findChild<QPushButton*>("primitiveOwnSketchButton");
            // An owned Feature Sketch enters its editor directly through
            // the ordinary Tree path; a standalone Sketch exposes Open.
            if(open){open->click();flush();}
            check(window.findChild<QAction*>("finishSketchAction")->isEnabled(),"Rolled L Sketch editor did not open");
            QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Rolled L View missing");
            const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Camera ray missing");const auto q=sketch.intersect_ray(ray->first,ray->second);check(q.has_value(),"Camera not facing Sketch");return *q;};
            const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
            const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
            const auto screen=[&](double x,double y){return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);};
            const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button){QMouseEvent event(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();};
            const auto click=[&](QPointF p){mouse(p,QEvent::MouseMove,Qt::NoButton);mouse(p,QEvent::MouseButtonPress,Qt::LeftButton);mouse(p,QEvent::MouseButtonRelease,Qt::LeftButton);};
            const auto pick=[&](QPointF p,const std::string& key) {
                const auto candidates=view->selection_candidates_at(p);const auto found=std::ranges::find(candidates,key,&viewer::ViewerCandidate::semantic_key);
                check(found!=candidates.end(),"Requested rolled L native geometry not offered");mouse(p,QEvent::MouseMove,Qt::NoButton);
                for(auto i=candidates.begin();i!=found;++i){mouse(p,QEvent::MouseButtonPress,Qt::RightButton);mouse(p,QEvent::MouseButtonRelease,Qt::RightButton);}
                mouse(p,QEvent::MouseButtonPress,Qt::LeftButton);mouse(p,QEvent::MouseButtonRelease,Qt::LeftButton);
            };
            const auto action=universal?"sketchUniversalDimensionAction":"sketchDimensionAction";
            auto* command=window.findChild<QAction*>(action);check(command,"Rolled L dimension action missing");command->trigger();flush();
            std::cout<<"Select left long segment"<<std::endl;
            pick(screen(0,sketch.points[2].y*.5),"segment:"+sketch.segments[1].id);
            click(screen(-10,sketch.points[2].y*.5));
            std::cout<<"Left long dimension placed"<<std::endl;
            check(!view->mesh().dimensions.empty(),"Left long segment dimension was not published");
            // End the repeat command before testing a point and a displayed
            // tangent handle. Keep the exact authored file and its IDs.
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);flush();
            command->trigger();flush();
            const auto corner=sketch.corner_radii.front().id;
            std::vector<kernel::ViewerPoint> handles;
            for(const auto& p:view->mesh().points)if(p.reference.semantic_key.starts_with("corner_radius_handle:"+corner+":"))handles.push_back(p);
            check(handles.size()==2,"Toe tangent handles missing");
            const auto handle=*std::ranges::max_element(handles,{},[](const auto& p){return p.position.y;});
            const auto point=screen(sketch.points[1].x,sketch.points[1].y),end=screen(handle.position.x,handle.position.y);
            const auto log_candidates=[&](QPointF p){for(const auto& candidate:view->selection_candidates_at(p))std::cout<<"Offered "<<candidate.semantic_key<<std::endl;};
            if(!reverse){std::cout<<"Select right native endpoint"<<std::endl;pick(point,"point:"+sketch.points[1].id);}
            std::cout<<"Select lower toe tangent endpoint "<<handle.reference.semantic_key<<std::endl;log_candidates(end);click(end);
            if(reverse){std::cout<<"Select right native endpoint second"<<std::endl;log_candidates(point);click(point);}
            std::cout<<"Move dimension cursor"<<std::endl;mouse(screen(sketch.points[1].x+12,5),QEvent::MouseMove,Qt::NoButton);
            view->grabFramebuffer().save(QString::fromStdString((directory/("rolled-l-dimension-preview-"+std::to_string(universal)+"-"+std::to_string(reverse)+".png")).string()));
            std::cout<<"Confirm dimension placement"<<std::endl;click(screen(sketch.points[1].x+12,5));
            QApplication::sendEvent(view,&escape);flush();
            const auto curve_point=[&](const std::string& id) {
                const auto found=std::ranges::find_if(view->mesh().edges,[&](const auto& edge){return edge.reference.semantic_key=="corner_radius:"+id;});
                check(found!=view->mesh().edges.end()&&!found->points.empty(),"Rolled L corner curve missing");
                const auto p=found->points[found->points.size()/2];return screen(p.x,p.y);
            };
            std::cout<<"Equalize the two toe radii"<<std::endl;window.findChild<QAction*>("sketchEqualAction")->trigger();flush();
            pick(curve_point(sketch.corner_radii[0].id),"corner_radius:"+sketch.corner_radii[0].id);
            pick(curve_point(sketch.corner_radii[1].id),"corner_radius:"+sketch.corner_radii[1].id);
            QApplication::sendEvent(view,&escape);flush();
            window.findChild<QAction*>("sketchDimensionAction")->trigger();flush();
            pick(curve_point(corner),"corner_radius:"+corner);click(screen(sketch.points[1].x+12,-12));
            QApplication::sendEvent(view,&escape);flush();
            viewer::ViewerCandidate radius;radius.kind=viewer::CandidateKind::Dimension;radius.owner_id=sketch.id;radius.semantic_key="corner_dimension:"+corner;
            bool radius_found=false;
            for(std::size_t i=0;i<view->mesh().dimensions.size();++i)if(view->mesh().dimensions[i].reference.semantic_key==radius.semantic_key){radius.geometry_index=i;radius_found=true;break;}
            check(radius_found,"Toe radius dimension was not published");
            for(double value:{4.1,3.8,4.2}) {
                std::cout<<"Edit equal toe radius "<<value<<std::endl;window.edit_dimension_inline(radius);flush();
                auto* field=view->findChild<QLineEdit*>("inlineDimensionValueEdit");check(field,"Toe radius inline editor missing");
                field->setText(QString::number(value));QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(field,&enter);flush();
                check(view->candidate_dimension_value(radius).has_value()&&std::abs(*view->candidate_dimension_value(radius)-value)<1e-6,"Toe radius inline edit failed");
            }
            const auto native_point=[&](const std::string& id) {
                const auto found=std::ranges::find_if(view->mesh().points,[&](const auto& p){return p.reference.semantic_key=="point:"+id;});
                check(found!=view->mesh().points.end(),"Authored rolled L point disappeared");return found->position;
            };
            for(double length:{47.,49.,46.}) {
                const auto old=native_point(sketch.points[1].id);const auto begin=screen(old.x,old.y),end=screen(length,0);
                std::cout<<"Drag outer rolled L leg to "<<length<<std::endl;mouse(begin,QEvent::MouseMove,Qt::NoButton);mouse(begin,QEvent::MouseButtonPress,Qt::LeftButton);
                QMouseEvent move(QEvent::MouseMove,end,QPointF(view->mapToGlobal(end.toPoint())),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&move);flush();mouse(end,QEvent::MouseButtonRelease,Qt::LeftButton);
                const auto actual=native_point(sketch.points[1].id);check(std::hypot(actual.x-old.x,actual.y-old.y)>.1&&std::abs(actual.x-length)<1e-5,"Permitted rolled L GUI drag did not reach target");
            }
            std::cout<<"Finish rolled L Sketch"<<std::endl;window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible())if(auto* buttons=dialog->findChild<QDialogButtonBox*>())if(auto* ok=buttons->button(QDialogButtonBox::Ok)){ok->click();flush();break;}
            check(window.execute_console_command("save").ok,"Rolled L copy Save failed");
            const auto after=document::PartDocument::load(path).sketches.front();
            check(after.points.size()==sketch.points.size()&&after.segments==sketch.segments&&after.corner_radii.size()==3,"Rolled L edit changed source topology");
            for(std::size_t i=0;i<sketch.points.size();++i)check(after.points[i].id==sketch.points[i].id,"Rolled L design-point identity changed");
            for(std::size_t i=0;i<3;++i)check(after.corner_radii[i].id==sketch.corner_radii[i].id&&after.corner_radii[i].vertex_id==sketch.corner_radii[i].vertex_id,"Rolled L corner identity changed");
            check(std::abs(after.corner_radii[0].radius-4.2)<1e-6&&std::abs(after.corner_radii[1].radius-4.2)<1e-6,"Equal toe radii differ after edit/reopen");
            check(std::abs(after.points[1].x-46)<1e-5&&std::abs(after.points[2].y-46)<1e-5,"Equal long legs differ after dragging/reopen");
            check(after.dimensions.size()>=1,"Left long dimension lost on reopen");
            for(const auto& dimension:after.dimensions)for(const auto& id:{dimension.first_point_id,dimension.second_point_id})
                check(id.empty()||after.find_point(id)||id=="sketch_origin","Dimension accepted a derived tangent handle as a native point");
            std::cout<<"Rolled L placed dimensions: "<<after.dimensions.size()<<std::endl;
            check(window.execute_console_command("undo").ok&&window.execute_console_command("redo").ok&&window.execute_console_command("save").ok,"Rolled L GUI Undo/Redo failed");
            check(document::PartDocument::load(path).sketches.front().serialized()==after.serialized(),"Rolled L Undo/Redo lost Sketch geometry or identities");
        }
        std::cout<<"Rolled L left-leg then toe-endpoint dimension gestures completed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
