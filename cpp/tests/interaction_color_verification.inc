void benchmark_selection_wire(QApplication& application,QWidget& parent) {
    using namespace zima;
    viewer::MeshView view(&parent);view.resize(800,600);
    kernel::ViewerMesh mesh;
    for(int i=0;i<400;++i) {
        const double y=-100.+i*.5;
        mesh.edges.push_back({{{-100.,y,0},{100.,y,0}}, {"fixture","edge:"+std::to_string(i),{}}});
    }
    view.set_mesh(mesh);view.set_display_mode(viewer::DisplayMode::Wire);
    auto camera=view.camera_state();camera[0]=1;camera[1]=camera[2]=camera[3]=0;view.set_camera_state(camera);
    view.confirm_container("fixture");view.show();view.raise();application.processEvents();
    QCursor::setPos(parent.mapToGlobal(QPoint(850,630)));application.processEvents();
    for(int i=0;i<10;++i)static_cast<void>(view.grabFramebuffer());
    std::vector<double> samples;
    for(int i=0;i<80;++i) {
        const auto begin=std::chrono::steady_clock::now();
        const auto frame=view.grabFramebuffer();require(!frame.isNull(),"Wire benchmark has no framebuffer");
        samples.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());
    }
    std::sort(samples.begin(),samples.end());
    std::cout<<"selection-wire 400 edges, 800x600, framebuffer median_ms="<<samples[samples.size()/2]<<" p90_ms="<<samples[72]<<'\n';
}

void verify_interaction_colors(QApplication& application,QWidget& parent) {
    using namespace zima;
    {
        const auto saved=application.palette();
        // Keep the same QIcon objects while switching themes, as existing
        // QAction and Tree items do during a Windows appearance change.
        const std::array icons{app::resource_icon("origin-document"),
            app::resource_icon("sketch"),app::resource_icon("insert-symbol"),
            app::resource_icon("sketch",true)};
        for (const bool dark : {false,true,false}) {
            auto palette=saved;
            const QColor text=dark?QColor("#EEEEEE"):QColor("#202020");
            palette.setColor(QPalette::WindowText,text);
            palette.setColor(QPalette::Text,text);
            palette.setColor(QPalette::Window,dark?QColor("#202020"):QColor("#FFFFFF"));
            palette.setColor(QPalette::Base,dark?QColor("#202020"):QColor("#FFFFFF"));
            palette.setColor(QPalette::HighlightedText,Qt::white);
            application.setPalette(palette);
            QToolButton toggle;
            toggle.setStyleSheet(app::command_button_style());toggle.setText("Orto");
            toggle.setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            toggle.setIcon(app::resource_icon("view-orthographic"));toggle.setIconSize({24,24});
            toggle.setCheckable(true);toggle.setChecked(true);toggle.resize(140,38);
            const auto checked=toggle.grab().toImage();
            require(checked.pixelColor(120,19)==QColor("#00D1FF"),"Native checked toolbar button is not azure");
            int contrast=0;for(int y=0;y<checked.height();++y)for(int x=0;x<checked.width();++x) {
                const auto c=checked.pixelColor(x,y);if(c.red()<50&&c.green()<60&&c.blue()<70)++contrast;
            }
            require(contrast>75,"Checked toolbar text/icon lacks dark contrast");
            checked.save(dark?"azure-button-dark.png":"azure-button-light.png");
            for (const auto& icon:icons) {
                const auto frame=icon.pixmap(QSize(32,32)).toImage();
                int pixels=0;
                for(int y=0;y<frame.height();++y)for(int x=0;x<frame.width();++x) {
                    const auto c=frame.pixelColor(x,y);
                    if(c.alpha()>200&&std::abs(c.red()-text.red())<8&&
                        std::abs(c.green()-text.green())<8&&std::abs(c.blue()-text.blue())<8)++pixels;
                }
                require(pixels>10,"Existing neutral icon did not follow the current theme");
            }
            QTableWidget table(1,1);table.setPalette(palette);
            auto* reference=new ui::ReferenceCellItem("Stored reference");
            reference->set_reference("reference-id");table.setItem(0,0,reference);
            ui::install_reference_cell_delegate(&table);
            QImage cell(240,36,QImage::Format_ARGB32_Premultiplied);cell.fill(palette.color(QPalette::Window));
            QPainter painter(&cell);QStyleOptionViewItem option;
            option.rect=cell.rect();option.palette=palette;option.font=table.font();
            option.state=QStyle::State_Enabled;option.widget=&table;
            table.itemDelegate()->paint(&painter,option,table.model()->index(0,0));painter.end();
            require(framebuffer_contains_color_near(cell,cell.size(),{70,18},text,65),
                "Stored reference text did not follow the light/dark text palette");
            const auto selected=icons[0].pixmap(QSize(32,32),QIcon::Selected).toImage();
            int light=0;for(int y=0;y<selected.height();++y)for(int x=0;x<selected.width();++x) {
                const auto c=selected.pixelColor(x,y);if(c.alpha()>200&&c.red()>240&&c.green()>240&&c.blue()>240)++light;
            }
            require(light>20,"Selected neutral Origin does not use highlighted text contrast");
        }
        application.setPalette(saved);
        require(app::resource_icon("missing-test-icon").isNull(),"Missing resource icon no longer reports null");
    }
    for(const bool toolbar:{false,true}) {
        std::unique_ptr<QAbstractButton> check(toolbar
            ? static_cast<QAbstractButton*>(new QToolButton(&parent))
            : static_cast<QAbstractButton*>(new QPushButton(&parent)));
        check->setGeometry(20,20,90,40);
        check->setIcon(app::resource_icon("active-check"));check->setIconSize({24,24});
        check->show();check->raise();
        const auto cursor=QCursor::pos();
        const auto green_pixels=[&] {
            const auto frame=check->icon().pixmap(QSize(24,24),check->underMouse()?QIcon::Active:QIcon::Normal).toImage();int count=0;
            for(int y=0;y<frame.height();++y)for(int x=0;x<frame.width();++x) {
                const auto c=frame.pixelColor(x,y);if(c.alpha()>200&&c.red()<110&&c.green()>180&&c.blue()<60)++count;
            }return count;
        };
        QCursor::setPos(parent.mapToGlobal(QPoint(300,300)));application.processEvents();
        check->setAttribute(Qt::WA_UnderMouse,false);
        QEvent leave(QEvent::Leave);QApplication::sendEvent(check.get(),&leave);
        require(green_pixels()>20,"Idle confirmation checkmark is not green");
        QCursor::setPos(check->mapToGlobal(check->rect().center()));application.processEvents();
        check->setAttribute(Qt::WA_UnderMouse,true);
        QEnterEvent enter(check->rect().center(),check->rect().center(),check->mapToGlobal(check->rect().center()));
        QApplication::sendEvent(check.get(),&enter);application.processEvents();
        check->grab().save(toolbar?"checkmark-toolbar-hover.png":"checkmark-ok-hover.png");
        require(green_pixels()>20,"Hovered confirmation checkmark lost its green identity");
        QCursor::setPos(parent.mapToGlobal(QPoint(300,300)));application.processEvents();
        check->setAttribute(Qt::WA_UnderMouse,false);
        QApplication::sendEvent(check.get(),&leave);
        require(green_pixels()>20,"Confirmation checkmark did not restore after hover");
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
    for(const auto mode:{QIcon::Normal,QIcon::Active,QIcon::Selected}) {
        const auto icon=app::resource_icon("origin-feature").pixmap(QSize(32,32),mode).toImage();
        int green=0;for(int y=0;y<icon.height();++y)for(int x=0;x<icon.width();++x) {
            const auto c=icon.pixelColor(x,y);if(c.alpha()>200&&c.red()<110&&c.green()>180&&c.blue()<60)++green;
        }
        require(green>35,"Container Origin icon is not green in every enabled state");
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
        require(framebuffer_contains_color_near(view.grabFramebuffer(),view.size(),center,QColor("#00D1FF"),4),
            "Feature hover does not use azure");
        const auto hover_frame=view.grabFramebuffer();
        const auto wire_pixels=[&](const QImage& image) {
            int count=0;const double ratio=image.width()/double(view.width());
            for(int y=int((center.y()-5)*ratio);y<int((center.y()+5)*ratio);++y)
                for(int x=int((center.x()-60)*ratio);x<int((center.x()+60)*ratio);++x) {
                    const auto c=image.pixelColor(x,y);if(c.red()<50&&c.green()>170&&c.blue()>230)++count;
                }
            return count;
        };
        view.confirm_container("feature");application.processEvents();
        require(view.confirmed_candidate().has_value(),"Selection colour fixture cannot confirm feature");
        auto frame=view.grabFramebuffer();
        require(wire_pixels(frame)>wire_pixels(hover_frame)*1.2,"Confirmed wire is not visibly thicker than hover");
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
            const auto c=frame.pixelColor(x,y);if(c.red()<50&&c.green()>170&&c.blue()>230)++count;
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
    std::cout<<"Interaction selection colours, azure hover, confirmation and symbolic through-all passed\n";
}
