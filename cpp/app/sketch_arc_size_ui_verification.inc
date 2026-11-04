static int verify_sketch_arc_size_ui(QApplication& application,const std::filesystem::path& parent) {
    using namespace zima;using namespace zima::test::mechanism;
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto settle=[&]{QEventLoop loop;QTimer::singleShot(950,&loop,&QEventLoop::quit);loop.exec();flush();};
    try {
        QTemporaryDir isolated(QString::fromStdString((parent/"arc-size-ui-XXXXXX").string()));require(isolated.isValid(),"Cannot isolate arc-size GUI test");
        const auto directory=std::filesystem::u8path(isolated.path().toStdString());
        unsigned cases{},edits{};
        for(const QString language:{"cs","en","de","fr","ru"})for(unsigned toggles=0;toggles<4;++toggles)for(bool major:{false,true}) {
            QSettings config(isolated.filePath("config.ini"),QSettings::IniFormat);config.setValue("Application/Language",language);
            config.setValue("Paths/Localization",QDir::current().absoluteFilePath("config/localization"));config.sync();
            const auto settings=app::ApplicationSettings::load(isolated.path());app::apply_application_translations(application,settings);
            auto s=Sketch::create_default();const auto arc=s.add_arc(20,20,40,20,20,major?0:40);
            const auto center=s.arcs[0].center_point_id;s.find_point(center)->fixed=true;
            for(const auto& endpoint:{s.arcs[0].start_point_id,s.arcs[0].end_point_id}) {
                const auto p=position(s,endpoint);const auto ray=s.add_segment(20,20,p[0],p[1],1e-7,true);
                auto angle=s.create_segment_dimension(ray,sketcher::DimensionKind::Angle);angle.locked=true;s.apply_dimension(angle);
            }
            auto authored=part(s);s=authored.sketches[0];const auto file=directory/("arc-size-"+std::to_string(cases)+".prtz");authored.save(file);
            app::AssemblyWorkspaceWindow window(isolated.path());window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1200,900);window.show();flush();
            require(window.open_document_path(QString::fromStdString(file.string())),"Arc-size file cannot open");flush();
            const auto* body=authored.body_owner_for_object(s.owner_container_id);require(body&&activate_test_body(application,window,body->scope.id),"Arc-size Body cannot activate");
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==s.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            require(row,"Arc-size Tree row missing");window.show_tree_item_properties(row);flush();window.findChild<QPushButton*>("sketchOpenButton")->click();settle();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());require(view,"Arc-size View missing");
            view->fit_points({s.world_point(-15,-15),s.world_point(85,85)});settle();
            const auto screen=[&](XY p) {
                const auto local=[&](QPointF pixel){const auto ray=view->ray_at(pixel);require(ray.has_value(),"Arc-size ray missing");const auto hit=s.intersect_ray(ray->first,ray->second);require(hit.has_value(),"Arc-size plane missing");return *hit;};
                const auto a=local({0,0}),b=local({100,0}),c=local({0,100});const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
                return QPointF(((p[0]-a[0])*vy-(p[1]-a[1])*vx)/det,(ux*(p[1]-a[1])-uy*(p[0]-a[0]))/det);
            };
            const auto mouse=[&](QPointF at,QEvent::Type type,Qt::MouseButton button=Qt::NoButton) {
                QMouseEvent event(type,at,QPointF(view->mapToGlobal(at.toPoint())),button,
                    type==QEvent::MouseButtonPress||type==QEvent::MouseButtonDblClick?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();
            };
            const auto click=[&](QPointF at){mouse(at,QEvent::MouseMove);mouse(at,QEvent::MouseButtonPress,Qt::LeftButton);mouse(at,QEvent::MouseButtonRelease,Qt::LeftButton);};
            auto* action=window.findChild<QAction*>("sketchUniversalDimensionAction");require(action&&action->isEnabled(),"Universal dimension command missing");
            const auto original=window.active_sketch_for_test()->serialized();const auto on_arc=screen({20+std::sqrt(200.),20+std::sqrt(200.)});
            for(bool cancel:{true,false}) {
                action->trigger();flush();mouse(on_arc,QEvent::MouseMove);
                require(view->hovered_candidate()&&view->hovered_candidate()->semantic_key=="arc:"+arc,"Actual arc cannot be picked for a size dimension");click(on_arc);
                for(unsigned i=0;i<toggles;++i) {
                    // Exercise both ordinary re-clicks and Qt's fast second
                    // click, whose press is replaced by MouseButtonDblClick.
                    if(i==0&&major){mouse(on_arc,QEvent::MouseButtonDblClick,Qt::LeftButton);mouse(on_arc,QEvent::MouseButtonRelease,Qt::LeftButton);}
                    else click(on_arc);
                    require(window.active_sketch_for_test()->serialized()==original,"R/diameter preview changed the document");
                }
                mouse(screen({65,55}),QEvent::MouseMove);
                const auto expected=toggles%2?kernel::ViewerDimensionKind::Diameter:kernel::ViewerDimensionKind::Radius;
                require(std::ranges::count_if(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==s.id&&d.kind==expected&&std::abs(d.value-(toggles%2?40:20))<1e-7;})==1,"R/diameter preview has the wrong kind or measured value");
                const auto hint=QString::fromUtf8("Univerzální kóta: dalším kliknutím na oblouk přepnete R/⌀; kliknutím do prostoru určete polohu.");
                require(!settings.qt_translations.value(hint).isEmpty()&&QObject::tr(hint.toUtf8().constData())==settings.qt_translations.value(hint),"Arc-size interaction hint is untranslated");
                if(cancel){QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);flush();require(window.active_sketch_for_test()->serialized()==original,"Cancelling R/diameter preview changed geometry");continue;}
                click(screen({65,55}));QEventLoop delay;QTimer::singleShot(30,&delay,&QEventLoop::quit);delay.exec();flush();
                if(auto* editor=view->findChild<QLineEdit*>("inlineDimensionValueEdit");editor&&editor->isVisible()){QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);flush();}
            }
            auto draft=*window.active_sketch_for_test();require(draft.dimensions.size()==3,"Arc-size placement did not create exactly one dimension");
            const auto size=*std::ranges::find_if(draft.dimensions,[](const auto& d){return d.kind==sketcher::DimensionKind::Radius||d.kind==sketcher::DimensionKind::Diameter;});
            require(size.kind==(toggles%2?sketcher::DimensionKind::Diameter:sketcher::DimensionKind::Radius),"Placed arc-size kind changed");
            window.findChild<QAction*>("viewSelectionAction")->trigger();flush();
            for(double radius:{22.,18.,21.}) {
                const auto shown=std::ranges::find(view->mesh().dimensions,"dimension:"+size.id,[](const auto& d){return d.reference.semantic_key;});require(shown!=view->mesh().dimensions.end(),"Placed arc size is missing from View");
                viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;candidate.owner_id=s.id;candidate.semantic_key="dimension:"+size.id;candidate.geometry_index=std::size_t(shown-view->mesh().dimensions.begin());
                const auto label=view->candidate_dimension_label_position(candidate);require(label.has_value(),"Arc-size label has no screen position");click(*label);mouse(*label,QEvent::MouseButtonDblClick,Qt::LeftButton);mouse(*label,QEvent::MouseButtonRelease,Qt::LeftButton);
                auto* editor=view->findChild<QLineEdit*>("inlineDimensionValueEdit");require(editor&&editor->isVisible(),"Arc-size inline value editor missing");editor->setText(QString::number(radius*(toggles%2?2:1)));
                QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);flush();
                draft=*window.active_sketch_for_test();require(std::abs(draft.arcs[0].radius-radius)<1e-7,"Arc-size numeric edit changed the wrong radius");
                for(const auto& id:{draft.arcs[0].start_point_id,draft.arcs[0].end_point_id}){const auto p=position(draft,id);require(std::abs(std::hypot(p[0]-20,p[1]-20)-radius)<1e-7,"Arc endpoint left its edited circle");}
                const auto edited=draft.serialized();window.findChild<QAction*>("undoAction")->trigger();flush();window.findChild<QAction*>("redoAction")->trigger();flush();require(window.active_sketch_for_test()->serialized()==edited,"Arc-size GUI Undo/Redo failed");++edits;
            }
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible())if(auto* buttons=dialog->findChild<QDialogButtonBox*>();buttons&&buttons->button(QDialogButtonBox::Ok)){buttons->button(QDialogButtonBox::Ok)->click();flush();break;}
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            const auto loaded=document::PartDocument::load(file);require(loaded.sketches[0].serialized()==draft.serialized(),"Arc-size native Save/reopen changed the dimension or geometry");++cases;
        }
        std::cout<<"Arc R/diameter GUI: "<<cases<<" language/arc/click variants and "<<edits<<" numeric edits, preview Cancel, Undo/Redo and native reopen passed\n";return 0;
    } catch(const std::exception& e){std::cerr<<"Arc-size GUI: "<<e.what()<<'\n';return 1;}
}
