int verify_sketch_external_dimension_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        const auto source=qEnvironmentVariable("ZIMA_VERIFY_EXTERNAL_DIMENSION_FILE");
        for(const bool line:{false,true})for(const bool reverse:{false,true}) {
            std::cout<<"Face dimension case line="<<line<<" reverse="<<reverse<<std::endl;
            std::vector<kernel::BodyResult> cached;
            auto part=document::PartDocument::load(std::filesystem::path(source.toStdWString()),&cached);
            auto& target=part.sketches.back();
            check(target.external_references.size()==2,"Supplied dimension Sketch references changed");
            const auto ref=target.external_references.front();
            check(sketcher::external_reference_line(ref).has_value(),"Supplied face reference is not straight");
            const double rx=ref.cached_points.front()[0];
            const auto segment=std::ranges::find_if(target.segments,[&](const auto& s){const auto* a=target.find_point(s.first_point_id);const auto* b=target.find_point(s.second_point_id);return a&&b&&std::abs(a->x-b->x)<1e-8&&a->x<0;});
            check(segment!=target.segments.end(),"Supplied vertical segment missing");
            const auto segment_id=segment->id,point_id=line?segment->first_point_id:target.add_point(rx+30,15);
            const auto sketch=target;
            const auto* initial=sketch.find_point(point_id);check(initial,"Test point missing");
            const auto initial_x=initial->x;
            const auto path=directory/("external-dimension-"+std::to_string(line)+std::to_string(reverse)+".prtz");part.save(path,cached);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
            check(window.open_document_path(QString::fromStdString(path.string())),"External dimension fixture failed to open");flush();
            const auto* owner=part.body_owner_for_object(sketch.owner_container_id);check(owner,"External dimension Body missing");
            check(activate_test_body(application,window,owner->scope.id),"External dimension Body unavailable");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"External dimension Sketch row missing");window.show_tree_item_properties(row);flush();
            auto* finish=window.findChild<QAction*>("finishSketchAction");
            if(!finish->isEnabled())for(const auto* name:{"sketchOpenButton","featureSketchButton","primitiveOwnSketchButton"})if(auto* open=window.findChild<QPushButton*>(name)){open->click();flush();break;}
            check(finish->isEnabled(),"External dimension Sketch editor missing");
            QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"External dimension View missing");
            view->fit_points({sketch.world_point(rx-20,-25),sketch.world_point(rx+100,55)});flush();
            const auto screen=[&](double x,double y){
                const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Camera ray missing");const auto q=sketch.intersect_ray(ray->first,ray->second);check(q.has_value(),"Camera plane missing");return *q;};
                const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
                const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
                return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);
            };
            const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button){QMouseEvent event(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();};
            const auto click=[&](QPointF p){mouse(p,QEvent::MouseMove,Qt::NoButton);mouse(p,QEvent::MouseButtonPress,Qt::LeftButton);mouse(p,QEvent::MouseButtonRelease,Qt::LeftButton);};
            const auto pick=[&](double x,double y,const std::string& key){const auto p=screen(x,y);mouse(p,QEvent::MouseMove,Qt::NoButton);const auto offers=view->selection_candidates_at(p);check(std::ranges::any_of(offers,[&](const auto& c){return c.semantic_key==key;}),"External dimension support not offered by common picker");for(int cycle=0;cycle<16&&(!view->hovered_candidate()||view->hovered_candidate()->semantic_key!=key);++cycle){mouse(p,QEvent::MouseButtonPress,Qt::RightButton);mouse(p,QEvent::MouseButtonRelease,Qt::RightButton);}check(view->hovered_candidate()&&view->hovered_candidate()->semantic_key==key,"External dimension candidate cycle failed");mouse(p,QEvent::MouseButtonPress,Qt::LeftButton);mouse(p,QEvent::MouseButtonRelease,Qt::LeftButton);};
            auto* command=window.findChild<QAction*>("sketchUniversalDimensionAction");check(command&&command->isEnabled(),"Universal dimension unavailable");command->trigger();flush();
            const auto native=[&]{pick(initial_x,line?15:initial->y,(line?"segment:":"point:")+(line?segment_id:point_id));};
            const auto external=[&]{pick(rx,16,"external_face:"+ref.id);};
            if(reverse){external();native();}else{native();external();}
            std::cout<<"After supports: "<<window.findChild<QLabel*>("workspaceState")->text().toStdString()<<std::endl;
            click(screen(rx+10,-15));
            std::cout<<"After placement: "<<window.findChild<QLabel*>("workspaceState")->text().toStdString()<<std::endl;
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);flush();
            std::string dim;
            for(const auto& d:view->mesh().dimensions)if(d.reference.owner_id==sketch.id&&d.reference.semantic_key.starts_with("dimension:")) {
                const auto id=d.reference.semantic_key.substr(10);
                if(std::ranges::none_of(sketch.dimensions,[&](const auto& old){return old.id==id;})) {check(dim.empty(),"Multiple unexpected dimensions created");dim=id;}
            }
            check(!dim.empty(),"Point/segment to Face dimension was not created");
            const auto current_point=[&] {
                const auto p=std::ranges::find_if(view->mesh().points,[&](const auto& p){return p.reference.owner_id==sketch.id&&p.reference.semantic_key=="point:"+point_id;});
                check(p!=view->mesh().points.end(),"Live native point missing");return sketch.local_point(p->position);
            };
            const auto verify=[&](double value,double tolerance=1e-6){const auto p=current_point();std::cout<<"Verify distance requested="<<value<<" actual="<<std::abs(p[0]-rx)<<" point="<<p[0]<<","<<p[1]<<std::endl;check(std::abs(std::abs(p[0]-rx)-value)<tolerance,"External distance equation failed");};
            verify(std::abs(initial_x-rx));
            for(const double value:line?std::vector<double>{18,16,20}:std::vector<double>{32,28,35}) {
                viewer::ViewerCandidate candidate;candidate.owner_id=sketch.id;candidate.kind=viewer::CandidateKind::Dimension;candidate.semantic_key="dimension:"+dim;
                bool found=false;for(std::size_t i=0;i<view->mesh().dimensions.size();++i)if(view->mesh().dimensions[i].reference.semantic_key==candidate.semantic_key){candidate.geometry_index=i;found=true;break;}
                check(found,"External dimension display missing");window.edit_dimension_inline(candidate);flush();auto* field=view->findChild<QLineEdit*>("inlineDimensionValueEdit");check(field,"External dimension value editor missing");field->setText(QString::number(value));QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(field,&enter);flush();verify(value);
            }
            window.findChild<QAction*>("undoAction")->trigger();flush();verify(line?16:28);
            window.findChild<QAction*>("redoAction")->trigger();flush();verify(line?20:35);
            if(!line) {
                for(int drag=0;drag<3;++drag) {
                    const auto before=current_point();const auto begin=screen(before[0],before[1]),end=screen(before[0],before[1]+3);
                    mouse(begin,QEvent::MouseMove,Qt::NoButton);mouse(begin,QEvent::MouseButtonPress,Qt::LeftButton);
                    QMouseEvent event(QEvent::MouseMove,end,QPointF(view->mapToGlobal(end.toPoint())),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();mouse(end,QEvent::MouseButtonRelease,Qt::LeftButton);
                    check(std::abs(current_point()[1]-before[1])>1,"GUI external-dimension point did not move along permitted degree of freedom");verify(35,1e-3);
                }
            }
            const auto expected=current_point();finish->trigger();flush();
            for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible())if(auto* buttons=dialog->findChild<QDialogButtonBox*>())if(auto* ok=buttons->button(QDialogButtonBox::Ok)){ok->click();flush();break;}
            check(window.execute_console_command("save").ok,"External dimension Save failed");
            const auto reopened=document::PartDocument::load(path).sketches.back();check(reopened.dimensions.size()==sketch.dimensions.size()+1&&reopened.external_references==sketch.external_references,"External dimension reopen changed definition or support");const auto* saved_point=reopened.find_point(point_id);check(saved_point&&std::hypot(saved_point->x-expected[0],saved_point->y-expected[1])<1e-6,"External dimension reopen changed native geometry");
            std::cout<<"Supplied Face dimension line="<<line<<" reverse="<<reverse<<" creation, repeated value edits, equations, Undo/Redo and reopen passed"<<std::endl;
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
