int verify_drawing_clearance_ui() {
    using namespace zima;using namespace drawing;
    QTemporaryDir temporary;require(temporary.isValid(),"Clearance directory missing");
    const auto directory=std::filesystem::path(temporary.path().toStdString());
    kernel::OcctKernel kernel;auto part=document::PartDocument::create_default();
    part.history={test::rectangular_feature(part,{20,10,6})};
    auto calculated=kernel.evaluate_history(part.kernel_operations());
    const auto source=directory/"source.prtz";part.save(source,calculated);
    workspace::Workspace live;live.add_part(part,calculated,source);
    auto doc=DrawingDocument::create_default();
    auto view=DrawingDocument::create_view(part.document_id,source,calculated.back().mesh);
    view.camera={{1,0,0},{0,1,0},{0,0,1}};view.scale=2;view.x=105;view.y=148;
    prepare_interactive_view(view,std::make_shared<const kernel::ViewerMesh>(calculated.back().mesh));
    auto first=view.measurement_geometry->points.front(),last=first;
    for(const auto& p:view.measurement_geometry->points) {
        if(p.position.x<first.position.x|| (p.position.x==first.position.x&&p.position.y<first.position.y))first=p;
        if(p.position.x>last.position.x|| (p.position.x==last.position.x&&p.position.y<last.position.y))last=p;
    }
    auto dimension=make_drawing_dimension(view.id);dimension.direction=DimensionDirection::Horizontal;
    dimension.attachments={{DimensionAttachmentKind::Point,first.source},{DimensionAttachmentKind::Point,last.source}};
    place_drawing_dimension(view,dimension,0,{0,7.5});
    doc.sheets.front().views={view};doc.sheets.front().dimensions={dimension};
    const auto path=directory/"clearance.drwz";live.add_drawing(doc,path);
    app::DrawingWindow window(&live,false);window.resize(1300,900);window.edit_workspace_document(doc.document_id);window.show();flush();
    auto* canvas=window.findChild<QWidget*>("drawingCanvas");require(canvas,"Clearance canvas missing");
    double gap=5;
    for(const double size:{30.,14.,40.}) {
        auto changed=part;test::resize_rectangular_feature(changed,changed.history.front(),{size,size/2,6});
        auto* source_state=live.open_part(part.document_id);
        source_state->session.commit(changed,kernel.evaluate_history(changed.kernel_operations()));
        const auto source_revision=source_state->session.revision();auto* state=live.open_drawing(doc.document_id);
        const auto before=state->document().sheets.front().dimensions;
        window.findChild<QAction*>("regenerateDrawingViewAction")->trigger();flush();
        const auto& current=window.document_for_test();const auto& value=current.sheets.front().dimensions.front();
        const auto result=evaluate_drawing_dimension(*current.find_view(view.id),value);
        require(result.state==MeasurementState::Resolved&&std::abs(result.presentations.front().value-size)<1e-8,"GUI regeneration lost resized dimension");
        require(std::abs((result.presentations.front().line_first.y-size/4)*view.scale-gap)<1e-8,"GUI regeneration lost paper clearance");
        require(source_state->session.revision()==source_revision,"Drawing regeneration changed its Part");
        const auto after=state->document().sheets.front().dimensions;
        require(state->undo()&&state->document().sheets.front().dimensions==before,"Clearance regeneration Undo lost layout");
        require(state->redo()&&state->document().sheets.front().dimensions==after,"Clearance regeneration Redo lost layout");
        window.edit_workspace_document(doc.document_id);flush();
        const auto grip=window.annotation_handle_for_test(dimension.id,0,true);require(grip.has_value(),"Resized dimension grip missing");
        pick(canvas,*grip);const QPointF delta(10,-8);
        mouse(canvas,QEvent::MouseButtonPress,*grip,Qt::LeftButton,Qt::LeftButton);
        mouse(canvas,QEvent::MouseMove,*grip+delta,Qt::NoButton,Qt::LeftButton);
        mouse(canvas,QEvent::MouseButtonRelease,*grip+delta,Qt::LeftButton,Qt::NoButton);
        const auto moved=window.annotation_handle_for_test(dimension.id,0,true);
        require(moved&&QLineF(*moved,*grip).length()>3,"Resized dimension cannot be dragged");
        const auto& updated=window.document_for_test();const auto shown=evaluate_drawing_dimension(*updated.find_view(view.id),updated.sheets.front().dimensions.front());
        gap=(shown.presentations.front().line_first.y-size/4)*view.scale;
        updated.save(path);const auto reopened=DrawingDocument::load(path);
        require(reopened.sheets.front().dimensions==updated.sheets.front().dimensions,"Clearance layout did not survive reopen");
        const auto loaded=evaluate_drawing_dimension(*reopened.find_view(view.id),reopened.sheets.front().dimensions.front());
        require(loaded.state==MeasurementState::Resolved&&std::abs(loaded.presentations.front().value-size)<1e-8&&
            std::abs((loaded.presentations.front().line_first.y-size/4)*view.scale-gap)<1e-8,"Reopened geometry lost resized dimension or paper clearance");
    }
    std::cout<<"Drawing clearance GUI: three source resizes, regenerate, exact paper offsets, drags, Undo/Redo and reopen passed\n";
    return 0;
}
