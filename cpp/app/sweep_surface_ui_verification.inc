int verify_sweep_surface_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        auto part=document::PartDocument::create_default();
        auto sweep=document::PartDocument::create_sweep2d_container();
        auto guide=sketcher::Sketch::from_serialized(sweep.sweep2d.path_sketch);
        static_cast<void>(guide.add_segment(0,0,0,20));
        static_cast<void>(guide.add_arc(10,20,0,20,10,30,false,1e-6,true));
        sweep.sweep2d.path_sketch=guide.serialized();document::PartDocument::reframe_sweep2d_sketches(sweep);
        const auto station=document::PartDocument::sweep2d_route(sweep).stations.front();
        const auto index=document::PartDocument::ensure_sweep2d_profile(sweep,station.point_id,station.incoming);
        auto profile=sketcher::Sketch::from_serialized(sweep.sweep2d.profiles[index].sketch_serialized);
        static_cast<void>(profile.add_circle(0,0,2));sweep.sweep2d.profiles[index].sketch_serialized=profile.serialized();
        part.history={sweep};document::BodyHistoryGraph graph;auto body=graph.create_body("Surface Sweep");
        graph.insert({document::PartHistoryKind::Feature,sweep.id});part.set_body_history(graph);part.resolve_constructions();
        if(qEnvironmentVariableIsSet("ZIMA_VERIFY_SWEEP_SURFACE_FILE")) {
            part=document::PartDocument::load(std::filesystem::path(qEnvironmentVariable("ZIMA_VERIFY_SWEEP_SURFACE_FILE").toStdWString()));
            sweep=part.history.back();check(sweep.feature_kind==document::FeatureKind::Sweep2D,"Actual fixture must end with 2D Sweep");
            body=part.body_owner_for_object(sweep.id)->scope.id;
        }
        kernel::OcctKernel engine;const auto calculated=workspace::calculate_part_with_resolved_references(engine,part);
        const auto path=directory/"surface-sweep.prtz";part.save(path,calculated);
        app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1400,950);window.show();
        QTimer modal_watch;bool unexpected=false;
        QObject::connect(&modal_watch,&QTimer::timeout,&window,[&]{for(auto* widget:application.topLevelWidgets())
            if(auto* message=qobject_cast<QMessageBox*>(widget);message&&message->isVisible()){
                std::cerr<<message->text().toStdString()<<std::endl;unexpected=true;message->reject();}});modal_watch.start(100);
        check(window.open_document_path(QString::fromStdString(path.string())),"Surface Sweep fixture cannot open");flush();
        const auto run=[&](const char* command,commands::Json args=commands::Json::object()) {
            const auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",args}}.dump()));
            check(result.ok,result.message.c_str());flush();return result.data;
        };
        run("body.activate",{{"body",body}});
        auto* tree=window.findChild<QTreeWidget*>("documentTree");
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));check(tree&&view,"Surface Sweep View/Tree missing");
        const auto edit=[&] {
            QTreeWidgetItem* row{};for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==sweep.id){row=*it;break;}
            check(row,"Surface Sweep Tree row missing");window.show_tree_item_properties(row);flush();
            auto* dialog=dynamic_cast<app::Sweep2DDialog*>(window.findChild<QDialog*>("sweep2dDialog"));
            check(dialog&&dialog->isVisible(),"Surface Sweep Properties missing");return dialog;
        };
        const auto surface_count=[&] {
            return std::ranges::count_if(view->mesh().triangle_references,[&](const auto& face){return face.surface_result;});
        };
        for(int mode:{2,0,2}) {
            auto* dialog=edit();auto* result=dialog->findChild<QComboBox*>("sweep2dResultType");check(result,"Surface Sweep result selector missing");
            result->setCurrentIndex(mode);flush();dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            check(!unexpected,"Surface Sweep Properties failed");
            check((surface_count()>0)==(mode==2),"Surface Sweep GUI retained the wrong result color");
        }
        auto* toggle=window.findChild<QAction*>("showSurfacesAction");check(toggle,"Surface visibility action missing");
        toggle->setChecked(false);flush();check(surface_count()==0,"Hidden Sweep Surface is still visible");
        toggle->setChecked(true);flush();check(surface_count()>0,"Sweep Surface did not return after showing surfaces");
        run("save");view->fit_all();flush();check(window.grab().save(QString::fromStdString((directory/"sweep-surface-yellow.png").string())),"Surface screenshot failed");
        run("undo");check(surface_count()==0,"Surface conversion Undo retained yellow faces");
        run("redo");check(surface_count()>0,"Surface conversion Redo lost yellow faces");
        run("close");check(window.open_document_path(QString::fromStdString(path.string())),"Surface Sweep reopen failed");flush();
        check(surface_count()>0,"Surface Sweep reopen lost yellow faces");
        std::cout<<"Surface Sweep GUI: line/arc route, Solid/Surface conversions, visibility, Undo/Redo and native reopen passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
