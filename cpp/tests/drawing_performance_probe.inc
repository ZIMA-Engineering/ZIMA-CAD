// Optional, read-only profiling of a real Drawing through the native GUI.
// ZIMA_DRAWING_PROFILE_INPUT supplies the document; no user files are saved.
int profile_drawing_ui(const QString& input) {
    using Clock=std::chrono::steady_clock;
    auto tick=Clock::now();
    const auto mark=[&](const char* label){const auto now=Clock::now();std::cout<<label<<" "<<std::chrono::duration<double>(now-tick).count()<<" s"<<std::endl;tick=now;};
    const auto path=std::filesystem::path(input.toStdWString());
    auto doc=zima::drawing::DrawingDocument::load(path);mark("load drawing");
    require(!doc.sheets.front().views.empty(),"Profiling needs an existing view");
    auto base=doc.sheets.front().views.front();
    zima::workspace::Workspace workspace;workspace.add_drawing(doc,path);workspace.activate(doc.document_id);workspace.display_top_level(doc.document_id);
    zima::workspace::DrawingProjection projection(&workspace,path);
    const auto& source=projection.source(base);mark("load source bundle");
    std::cout<<"source edges "<<source.mesh.edges.size()<<" triangles "<<source.mesh.triangles.size()/3<<std::endl;
    const auto equal_edges=[](const auto& a,const auto& b) {
        return std::ranges::equal(a,b,[](const auto& x,const auto& y){return x.points==y.points&&x.source==y.source&&x.hidden==y.hidden&&x.silhouette==y.silhouette&&x.tangent==y.tangent&&x.hatch==y.hatch&&x.hatch_pattern==y.hatch_pattern&&x.thread==y.thread&&x.thread_leadin==y.thread_leadin;});
    };
    for(const auto& v:doc.sheets.front().views) {
        const auto edges=zima::drawing::project_edges(source.mesh,v.camera);mark(("project edges "+v.name).c_str());
        require(equal_edges(edges,v.projected_edges),"Projection changed the persisted reference result");
    }
    auto gpu_preview=base;gpu_preview.camera=zima::drawing::standard_camera(zima::drawing::ViewOrientation::Isometric);
    zima::drawing::prepare_interactive_view(gpu_preview,std::make_shared<const zima::kernel::ViewerMesh>(source.mesh));mark("prepare isometric GPU geometry");
    if(qEnvironmentVariableIsSet("ZIMA_DRAWING_PROFILE_GPU_VIEW")) {
        auto displayed=doc;gpu_preview.id="profile-gpu-view";gpu_preview.x=105;gpu_preview.y=148;
        displayed.sheets.front().views.push_back(gpu_preview);workspace.open_drawing(doc.document_id)->commit(std::move(displayed));
    }
    zima::app::DrawingWindow window(&workspace,false);window.edit_workspace_document(doc.document_id);window.resize(1600,1000);window.show();flush();window.grab();mark("open drawing UI");
    if(QGuiApplication::platformName()=="windows") {
        const auto* gpu=window.findChild<QOpenGLWidget*>("drawingGpuSurface");
        require(gpu&&gpu->isValid(),"Native drawing canvas did not initialize its GPU surface");
    }
    if(qEnvironmentVariableIsSet("ZIMA_DRAWING_PROFILE_INTERACTION")) {
        for(int i=0;i<20;++i)static_cast<void>(zima::drawing::title_block_layout(doc.sheets.front(),{}));
        mark("20 title block layouts");
        auto* canvas=window.findChild<QWidget*>("drawingCanvas");
        auto* surface=window.findChild<QOpenGLWidget*>("drawingGpuSurface");
        for(int i=0;i<20;++i){if(surface)surface->repaint();else canvas->repaint();flush();}
        mark("20 native repaints without screenshots");
        for(int i=0;i<20;++i) {
            const auto point=window.sheet_rectangle_for_test().center();
            QWheelEvent wheel(point,QPointF(canvas->mapToGlobal(point.toPoint())),{},QPoint(0,i%2? -120:120),Qt::NoButton,Qt::NoModifier,Qt::ScrollUpdate,false);
            QApplication::sendEvent(canvas,&wheel);flush();
        }
        mark("20 zoom events without screenshots");
        window.select_view(base.id);window.findChild<QAction*>("editDrawingViewAction")->trigger();flush();mark("open properties for interaction profile");
        auto* properties=window.findChild<QDialog*>("drawingViewProperties");require(properties,"Interaction profile properties missing");
        auto* name=properties->findChild<QLineEdit*>("drawingViewName");const auto initial_name=name->text();
        for(int i=0;i<10;++i){name->setText(initial_name+QString::number(i));flush();}
        mark("10 name edits with repaint");name->setText(initial_name);flush();mark("restore name");
        auto* x=properties->findChild<QDoubleSpinBox*>("drawingViewX");const auto initial_x=x->value();
        for(int i=0;i<10;++i){x->setValue(initial_x+i+1);flush();}
        mark("10 position edits with repaint");
        properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();mark("cancel interaction profile");
        return 0;
    }
    const auto revision=workspace.open_drawing(doc.document_id)->revision();
    window.select_view(base.id);window.findChild<QAction*>("editDrawingViewAction")->trigger();flush();mark("open existing properties");
    QPointer<QDialog> dialog=window.findChild<QDialog*>("drawingViewProperties");require(dialog,"Profile properties missing");
    dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();mark("unchanged properties OK");
    require(!dialog||!dialog->isVisible(),"Profile OK failed");
    require(workspace.open_drawing(doc.document_id)->revision()==revision,"Unchanged OK created history");
    require(equal_edges(window.document_for_test().find_view(base.id)->projected_edges,base.projected_edges),"Unchanged OK altered geometry");
    window.findChild<QAction*>("insertDrawingViewAction")->trigger();flush();mark("insert action");
    auto* canvas=window.findChild<QWidget*>("drawingCanvas");
    mouse(canvas,QEvent::MouseMove,window.sheet_rectangle_for_test().center(),Qt::NoButton,Qt::NoButton);flush();
    window.grab().save("build/dgst-placement-frame.png");
    click(canvas,window.sheet_rectangle_for_test().center());flush();mark("place new view and properties");
    dialog=nullptr;for(auto* d:window.findChildren<QDialog*>("drawingViewProperties"))if(d->isVisible())dialog=d;
    window.grab().save("build/dgst-properties-frame.png");mark("properties frame screenshot");
    require(dialog,"Profile insertion properties missing");dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();mark("insert view OK");
    require(!dialog||!dialog->isVisible(),"Profile insertion failed");
    require(window.document_for_test().sheets.front().views.size()==doc.sheets.front().views.size()+1,"Insertion did not create a view");
    require(bool(window.document_for_test().sheets.front().views.back().output_source),"Insertion did not defer vector visibility calculation");
    for(int delta:{120,-120,120,-120}) {
        const auto point=window.sheet_rectangle_for_test().center();
        QWheelEvent wheel(point,QPointF(canvas->mapToGlobal(point.toPoint())),{},QPoint(0,delta),Qt::NoButton,Qt::NoModifier,Qt::ScrollUpdate,false);
        QApplication::sendEvent(canvas,&wheel);flush();window.grab();
    }
    mark("four zoom and repaint steps");
    window.grab().save("build/dgst-interactive-preview.png");
    QTemporaryDir saved;require(saved.isValid(),"Cannot create native drawing test directory");
    const auto snapshot=std::filesystem::path(saved.path().toStdWString())/"interactive.drwz";
    window.document_for_test().save(snapshot);
    const auto reopened=zima::drawing::DrawingDocument::load(snapshot);
    auto output=reopened.sheets.front().views.back();require(bool(output.output_source),"Reopened interactive view lost output snapshot");
    const auto& live_view=window.document_for_test().sheets.front().views.back();
    const auto exact=zima::drawing::project_edges(*live_view.output_source,output.camera);zima::drawing::prepare_output_view(output);
    if(!equal_edges(exact,output.projected_edges)) {
        std::cerr<<"Deferred edge counts "<<exact.size()<<" / "<<output.projected_edges.size()<<std::endl;
        for(std::size_t i=0;i<std::min(exact.size(),output.projected_edges.size());++i) {
            const auto& a=exact[i];const auto& b=output.projected_edges[i];
            if(a.points!=b.points||a.source!=b.source||a.hidden!=b.hidden){std::cerr<<"First mismatch "<<i<<" "<<a.source.semantic_key<<" / "<<b.source.semantic_key<<" points "<<a.points.size()<<" / "<<b.points.size()<<" hidden "<<a.hidden<<" / "<<b.hidden<<std::endl;break;}
        }
    }
    require(equal_edges(exact,output.projected_edges),"Deferred output differs from original exact projection");
    mark("save reopen and exact output verification");
    window.export_pdf("build/dgst-interactive-profile.pdf");mark("PDF export");
    window.select_view(base.id);flush();mark("select projection parent");
    window.findChild<QAction*>("projectDrawingViewAction")->trigger();flush();mark("projected action");
    click(canvas,window.sheet_rectangle_for_test().center()+QPointF(180,0));flush();mark("place and commit projected view");
    dialog=nullptr;for(auto* d:window.findChildren<QDialog*>("drawingViewProperties"))if(d->isVisible())dialog=d;
    require(!dialog,"Projected insertion opened properties");
    require(window.document_for_test().sheets.front().views.size()==doc.sheets.front().views.size()+2,"Projected insertion failed");
    return 0;
}
