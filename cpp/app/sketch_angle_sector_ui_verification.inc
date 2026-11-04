static int verify_sketch_angle_sector_ui(QApplication& application,const std::filesystem::path& parent) {
    using namespace zima;using namespace test::mechanism;
    try {
        QTemporaryDir isolated(QString::fromStdString((parent/"angle-sector-ui-XXXXXX").string()));require(isolated.isValid(),"Cannot isolate angle-sector GUI");
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        const auto settle=[&]{QEventLoop loop;QTimer::singleShot(950,&loop,&QEventLoop::quit);loop.exec();flush();};
        unsigned cases{};
        for(double rotation:{0.,90.})for(bool order:{false,true})for(bool reversed:{false,true}) {
            auto s=Sketch::create_default();
            const auto xy=[&](double r,double a){a=(a+rotation)*radians;return XY{r*std::cos(a),r*std::sin(a)};};
            const auto base=xy(30,0),tip=xy(25,40);
            const auto fixed=reversed?s.add_segment(base[0],base[1],0,0):s.add_segment(0,0,base[0],base[1]);
            for(const auto& p:{s.segments.front().first_point_id,s.segments.front().second_point_id})s.find_point(p)->fixed=true;
            const auto arm=s.add_segment(0,0,tip[0],tip[1]);
            auto length=s.create_segment_dimension(arm);length.locked=true;s.apply_dimension(length);
            auto authored=part(s);s=authored.sketches.front();
            const auto file=std::filesystem::path(isolated.path().toStdWString())/"angle.prtz";authored.save(file);
            app::AssemblyWorkspaceWindow window(isolated.path());window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1200,900);window.show();flush();
            require(window.open_document_path(QString::fromStdWString(file.wstring())),"Angle native fixture cannot open");
            const auto* body=authored.body_owner_for_object(s.owner_container_id);require(body&&activate_test_body(application,window,body->scope.id),"Angle Body cannot activate");
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==s.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            require(row,"Angle Sketch row missing");window.show_tree_item_properties(row);flush();window.findChild<QPushButton*>("sketchOpenButton")->click();settle();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());require(view,"Angle View missing");
            view->fit_points({s.world_point(-50,-50),s.world_point(50,50)});settle();
            const auto screen=[&](XY p) {
                const auto local=[&](QPointF pixel){const auto ray=view->ray_at(pixel);require(ray.has_value(),"Angle cursor ray missing");const auto hit=s.intersect_ray(ray->first,ray->second);require(hit.has_value(),"Angle cursor plane missing");return *hit;};
                const auto a=local({0,0}),b=local({100,0}),c=local({0,100});const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
                return QPointF(((p[0]-a[0])*vy-(p[1]-a[1])*vx)/det,(ux*(p[1]-a[1])-uy*(p[0]-a[0]))/det);
            };
            const auto mouse=[&](XY p,QEvent::Type type,Qt::MouseButton button=Qt::NoButton){const auto at=screen(p);QMouseEvent event(type,at,QPointF(view->mapToGlobal(at.toPoint())),button,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();};
            const auto click=[&](XY p){mouse(p,QEvent::MouseMove);mouse(p,QEvent::MouseButtonPress,Qt::LeftButton);mouse(p,QEvent::MouseButtonRelease,Qt::LeftButton);};
            const auto pick_line=[&](XY p,const std::string& id) {
                mouse(p,QEvent::MouseMove);
                for(unsigned i=0;i<16;++i) {
                    const auto offered=view->hovered_candidate();
                    if(offered&&offered->kind==viewer::CandidateKind::SketchSegment&&offered->semantic_key=="segment:"+id) {
                        mouse(p,QEvent::MouseButtonPress,Qt::LeftButton);mouse(p,QEvent::MouseButtonRelease,Qt::LeftButton);return;
                    }
                    mouse(p,QEvent::MouseButtonPress,Qt::RightButton);mouse(p,QEvent::MouseButtonRelease,Qt::RightButton);
                }
                throw std::runtime_error("Angular support cannot be selected through the common mouse candidate list");
            };
            const auto original=window.active_sketch_for_test()->serialized();
            for(double sector:{20.,110.,200.,290.}) {
                window.findChild<QAction*>("sketchUniversalDimensionAction")->trigger();flush();
                if(order){pick_line(xy(12,40),arm);pick_line(xy(12,0),fixed);}
                else{pick_line(xy(12,0),fixed);pick_line(xy(12,40),arm);}
                mouse(xy(19,sector),QEvent::MouseMove);
                require(window.active_sketch_for_test()->serialized()==original,"Angle preview changed the document");
                const auto offered=std::ranges::find_if(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==s.id&&d.kind==kernel::ViewerDimensionKind::Angular;});
                if(offered==view->mesh().dimensions.end()||offered->value<=0) {
                    std::cerr<<"Angular GUI rotation="<<rotation<<" order="<<order<<" reversed="<<reversed<<" sector="<<sector<<'\n';
                    for(const auto& d:view->mesh().dimensions)std::cerr<<"Dimension "<<d.reference.owner_id<<" "<<d.reference.semantic_key<<" kind="<<int(d.kind)<<" value="<<d.value<<'\n';
                    throw std::runtime_error("GUI automatically offered a negative angle or no angular sector");
                }
                if(sector!=290){QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);flush();require(window.active_sketch_for_test()->serialized()==original,"Angle Cancel modified the Sketch");}
                else {
                    click(xy(19,sector));flush();
                    if(auto* editor=view->findChild<QLineEdit*>("inlineDimensionValueEdit");editor&&editor->isVisible()){QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);flush();}
                    const auto changed=*window.active_sketch_for_test();
                    const auto dimension=std::ranges::find_if(changed.dimensions,[](const auto& d){return d.kind==sketcher::DimensionKind::AngleBetween;});
                    require(dimension!=changed.dimensions.end()&&dimension->value>0,"Placed automatic angle became negative");
                    const auto state=changed.serialized();window.findChild<QAction*>("undoAction")->trigger();flush();window.findChild<QAction*>("redoAction")->trigger();flush();
                    require(window.active_sketch_for_test()->serialized()==state,"GUI angular-sector Undo/Redo changed the geometry");
                }
                ++cases;
            }
        }
        std::cout<<"Automatic angle GUI: "<<cases<<" sector/orientation/selection-order/endpoint-direction previews, positive values, Cancel, placement and Undo/Redo passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Angle-sector GUI: "<<e.what()<<'\n';return 1;}
}
