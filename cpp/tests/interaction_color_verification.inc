void verify_interaction_colors(QApplication& application,QWidget& parent) {
    using namespace zima;
    for(const bool toolbar:{false,true}) {
        std::unique_ptr<QAbstractButton> check(toolbar
            ? static_cast<QAbstractButton*>(new QToolButton(&parent))
            : static_cast<QAbstractButton*>(new QPushButton(&parent)));
        check->setGeometry(20,20,90,40);
        check->setIcon(app::resource_icon("active-check"));check->setIconSize({24,24});
        check->setStyleSheet("QPushButton,QToolButton{background:white;border:none} QPushButton:hover,QToolButton:hover{background:#4DD811}");
        check->show();check->raise();
        const auto cursor=QCursor::pos();
        const auto dark_pixels=[&] {
            const auto frame=check->grab().toImage();int count=0;
            for(int y=0;y<frame.height();++y)for(int x=0;x<frame.width();++x) {
                const auto c=frame.pixelColor(x,y);if(c.red()<10&&c.green()<10&&c.blue()<10)++count;
            }return count;
        };
        QCursor::setPos(parent.mapToGlobal(QPoint(300,300)));application.processEvents();
        check->setAttribute(Qt::WA_UnderMouse,false);
        QEvent leave(QEvent::Leave);QApplication::sendEvent(check.get(),&leave);
        require(dark_pixels()==0,"Idle confirmation checkmark is not green");
        QCursor::setPos(check->mapToGlobal(check->rect().center()));application.processEvents();
        check->setAttribute(Qt::WA_UnderMouse,true);
        QEnterEvent enter(check->rect().center(),check->rect().center(),check->mapToGlobal(check->rect().center()));
        QApplication::sendEvent(check.get(),&enter);application.processEvents();
        check->grab().save(toolbar?"checkmark-toolbar-hover.png":"checkmark-ok-hover.png");
        require(dark_pixels()>20,"Hovered confirmation checkmark is not black");
        QCursor::setPos(parent.mapToGlobal(QPoint(300,300)));application.processEvents();
        check->setAttribute(Qt::WA_UnderMouse,false);
        QApplication::sendEvent(check.get(),&leave);
        require(dark_pixels()==0,"Confirmation checkmark did not restore after hover");
        QCursor::setPos(cursor);
    }
    const auto origin=app::resource_icon("origin");
    for(const auto mode:{QIcon::Normal,QIcon::Active,QIcon::Selected}) {
        const auto icon=origin.pixmap(QSize(32,32),mode).toImage();
        int red=0;for(int y=0;y<icon.height();++y)for(int x=0;x<icon.width();++x) {
            const auto c=icon.pixelColor(x,y);if(c.alpha()>200&&c.red()>220&&c.green()<30&&c.blue()<30)++red;
        }
        require(red>35,"Origin icon lost its red identity in a GUI state");
    }
    viewer::MeshView view(&parent);view.setGeometry(0,0,500,360);
    kernel::ViewerMesh mesh;
    mesh.vertices={{-1,-1,0},{1,-1,0},{0,1,0}};mesh.triangles={0,1,2};
    mesh.triangle_references={{"feature","profile",{}}};
    mesh.edges.push_back({{{-1,0,0},{1,0,0}},{"feature","rim",{}}});
    mesh.edges.push_back({{{-1,.6,0},{1,.6,0}},{"reference-a","edge",{}}});
    mesh.edges.push_back({{{-1,-.6,0},{1,-.6,0}},{"reference-b","edge",{}}});
    mesh.original_references.edges=mesh.edges;
    mesh.original_references.vertices=mesh.vertices;
    mesh.original_references.triangles=mesh.triangles;
    mesh.original_references.triangle_references=mesh.triangle_references;
    view.set_mesh(mesh);view.set_display_mode(viewer::DisplayMode::Wire);
    auto camera=view.camera_state();camera[0]=1;camera[1]=camera[2]=camera[3]=0;view.set_camera_state(camera);
    view.set_selection_contract({viewer::CandidateKind::Container});view.show();view.raise();
    const QPointF center(250,180);
    QCursor::setPos(view.mapToGlobal(center.toPoint()));
    application.processEvents(); // Drain native pointer/resize events before the synthetic hover.
    {
        const QColor color("#00D1FF");
        view.clear_selection();
        QMouseEvent move(QEvent::MouseMove,center,center,view.mapToGlobal(center.toPoint()),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(&view,&move);application.processEvents();
        require(view.hovered_candidate().has_value(),"Selection colour fixture has no hover candidate");
        require(framebuffer_contains_color_near(view.grabFramebuffer(),view.size(),center,QColor("#4DD811"),4),
            "Feature hover does not use green");
        view.confirm_container("feature");application.processEvents();
        require(view.confirmed_candidate().has_value(),"Selection colour fixture cannot confirm feature");
        auto frame=view.grabFramebuffer();
        require(framebuffer_contains_color_near(frame,view.size(),center,color,4),"Confirmed feature has the wrong selection colour");
        QApplication::sendEvent(&view,&move);application.processEvents();
        require(framebuffer_contains_color_near(view.grabFramebuffer(),view.size(),center,color,4),
            "Hover replaced a confirmed feature colour");
        frame.save(QString("interaction-selected-%1.png").arg(color.name().mid(1)));
        view.set_feature_selected_edges({{"reference-a","edge",{}}});
        view.set_constraint_reference_highlights({},{{"reference-b","edge",{}}});
        application.processEvents();
        frame=view.grabFramebuffer();
        const double offset=.6/view.world_tolerance_for_pixels(1);
        for(const double sign:{-1.,1.})
            require(framebuffer_contains_color_near(frame,view.size(),center+QPointF(0,sign*offset),QColor("#00D1FF"),5),
                "Confirmed operation leaked its colour into a separate reference");
        require(framebuffer_contains_color_near(frame,view.size(),center,color,4),
            "Reference inspection replaced the selected feature selection colour");
        frame.save(QString("interaction-references-%1.png").arg(color.name().mid(1)));
        view.set_feature_selected_edges({});view.set_constraint_reference_highlights({},{});
    }
    view.clear_selection();mesh.triangles.clear();mesh.edges.clear();mesh.original_references={};view.set_mesh(mesh);
    kernel::ViewerEdge edge{{{-1,0,0},{1,0,0}},{"feature","preview:end",{}}};
    view.set_transient_edges({edge});application.processEvents();
    const auto solid=view.grabFramebuffer();
    require(framebuffer_contains_color_near(solid,view.size(),center,QColor("#00D1FF"),4),"Preview is not azure");
    const auto count_azure=[](const QImage& frame) {
        int count=0;for(int y=0;y<frame.height();++y)for(int x=0;x<frame.width();++x) {
            const auto c=frame.pixelColor(x,y);if(c.red()<35&&c.green()>180&&c.blue()>220)++count;
        }return count;
    };
    edge.preview_terminal_dashed=true;
    view.set_transient_edges({edge});application.processEvents();const auto dashed=view.grabFramebuffer();
    require(count_azure(solid)>100 && count_azure(dashed)<count_azure(solid)*.85 && count_azure(dashed)>count_azure(solid)*.3,
        "Through-all terminal preview is not dashed or changed its selection colour");
    dashed.save("interaction-through-all.png");

    auto document=document::PartDocument::create_default();
    auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_circle(0,0,10));
    document.sketches.push_back(sketch);
    auto extrusion=document::PartDocument::create_extrusion_container(sketch.id);
    extrusion.extrusion.extent_mode=document::ProfileExtentMode::TwoSides;
    extrusion.extrusion.end_condition_forward=document::EndCondition::ThroughAll;
    extrusion.extrusion.end_condition_reverse=document::EndCondition::ThroughAll;
    const auto wire=document.extrusion_preview_edges(extrusion);
    int ends=0,sides=0;
    for(const auto& item:wire) {
        if(item.reference.semantic_key.starts_with("preview:side")) {
            ++sides;require(!item.preview_terminal_dashed,"Through-all dashed a longitudinal connector");
        } else {++ends;require(item.preview_terminal_dashed,"Through-all terminal lacks its display role");}
    }
    require(ends==2&&sides>0,"Through-all preview lost an end or its longitudinal connector");
    std::cout<<"Interaction selection colours, green hover, confirmation and symbolic through-all passed\n";
}
