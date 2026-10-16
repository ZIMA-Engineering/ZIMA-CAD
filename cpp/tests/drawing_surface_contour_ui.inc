int verify_surface_contour_ui(const std::filesystem::path& path) {
    using namespace zima;using namespace drawing;
    QTemporaryDir temporary;require(temporary.isValid(),"Contour test directory unavailable");
    const auto output_directory=std::filesystem::path(temporary.path().toStdString());
    auto original=DrawingDocument::load(path);
    workspace::DrawingProjection projection(nullptr,path);
    for(auto& sheet:original.sheets)for(auto& view:sheet.views)projection.project(view,{});
    auto source_view=original.sheets.front().views.front();
    for(auto& annotation:source_view.model_annotations)
        if(annotation.kind==ModelAnnotationKind::Axis&&!annotation.unresolved)annotation.visible=true;
    std::vector<ProjectedMeasurementCurve> targets;
    for(const auto& curve:projected_measurement_curves(source_view))
        if(curve.source.semantic_key.starts_with("drawing-contour:"))targets.push_back(curve);
    require(std::ranges::count_if(targets,[](const auto& curve){return curve.circular;})==2,"Actual bolt has no pair of circular contour references");
    std::vector<std::pair<ProjectedMeasurementCurve,DrawingDimensionKind>> cases;
    for(const auto& target:targets) {
        cases.emplace_back(target,target.circular?DrawingDimensionKind::Radius:DrawingDimensionKind::Linear);
        if(target.line)cases.emplace_back(target,DrawingDimensionKind::Angular);
    }
    unsigned radii=0,lines=0,angles=0;
    for(const auto& [target,kind]:cases) {
        auto document=original;auto& sheet=document.sheets.front();sheet.views={source_view};sheet.dimensions.clear();
        auto& view=sheet.views.front();view.scale=40;view.show_caption=false;
        const auto midpoint=target.circular?target.points[target.points.size()/2]:Point2{(target.points.front().x+target.points.back().x)/2,(target.points.front().y+target.points.back().y)/2};
        view.x=sheet.width_mm()/2+midpoint.x*view.scale;view.y=sheet.height_mm()/2-midpoint.y*view.scale;
        workspace::Workspace live;live.add_drawing(document);
        app::DrawingWindow window(&live,false);window.resize(1300,900);window.edit_workspace_document(document.document_id);window.show();flush();
        auto* canvas=window.findChild<QWidget*>("drawingCanvas");require(canvas,"Contour canvas missing");
        const auto point=[&](Point2 p){const auto box=window.sheet_rectangle_for_test();const auto ratio=box.height()/sheet.height_mm();return QPointF(box.right()-view.x*ratio+p.x*view.scale*ratio,box.bottom()-view.y*ratio-p.y*view.scale*ratio);};
        window.findChild<QAction*>("drawingDimensionAction")->trigger();flush();
        app::DrawingDimensionDialog* dialog=nullptr;
        for(auto* candidate:window.findChildren<QDialog*>())if(auto* found=dynamic_cast<app::DrawingDimensionDialog*>(candidate);found&&found->isVisible())dialog=found;
        require(dialog,"Contour dimension dialog missing");
        if(kind!=DrawingDimensionKind::Linear)dialog->findChild<QComboBox*>("drawingDimensionType")->setCurrentIndex(int(kind));
        else {
            auto* mode=dialog->findChild<QComboBox*>("dimensionAttachmentMode0");
            mode->setCurrentIndex(mode->findData(int(DimensionAttachmentKind::CurvePoint)));
        }
        flush();pick(canvas,point(midpoint));
        require(dialog->value().attachments[0].reference==target.source,"Mouse did not bind the offered contour identity");
        if(target.circular) {
            require(dialog->placing(),"Radius contour click did not arm placement");
            pick(canvas,point({midpoint.x+1,midpoint.y+1}));
        }else if(kind==DrawingDimensionKind::Angular) {
            pick(canvas,point({midpoint.x,0}));
            require(dialog->placing(),"Chamfer/axis lines did not arm angular dimension placement");
            pick(canvas,point({midpoint.x,midpoint.y*.4}));
        }else {
            pick(canvas,point(target.points.back()));
            require(dialog->placing(),"Chamfer contour endpoint did not arm dimension placement");
            pick(canvas,point({midpoint.x+1,midpoint.y+1}));
        }
        auto* ok=dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);require(ok&&ok->isEnabled(),"Contour dimension cannot commit");ok->click();flush();
        const auto dimensions=window.document_for_test().sheets.front().dimensions;require(dimensions.size()==1,"Contour dimension not committed");
        const auto id=dimensions.back().id;
        const auto created=dimensions;auto* state=live.open_drawing(document.document_id);
        require(state->undo()&&state->document().sheets.front().dimensions.empty(),"GUI contour creation has no atomic Undo");
        window.edit_workspace_document(document.document_id);flush();
        require(state->redo()&&state->document().sheets.front().dimensions==created,"GUI contour creation Redo lost its reference/layout");
        window.edit_workspace_document(document.document_id);flush();
        if(target.circular) {
            ++radii;
            for(const auto delta:std::array{QPointF(25,-20),QPointF(-50,35),QPointF(20,15)}) {
                const auto before=window.document_for_test().sheets.front().dimensions;
                const auto grip=window.annotation_handle_for_test(id,0,true);require(grip.has_value(),"Radius text grip missing");
                pick(canvas,*grip);
                mouse(canvas,QEvent::MouseButtonPress,*grip,Qt::LeftButton,Qt::LeftButton);
                mouse(canvas,QEvent::MouseMove,*grip+delta,Qt::NoButton,Qt::LeftButton);
                mouse(canvas,QEvent::MouseButtonRelease,*grip+delta,Qt::LeftButton,Qt::NoButton);
                const auto& changed=window.document_for_test().sheets.front().dimensions.front();
                const auto evaluated=evaluate_drawing_dimension(*window.document_for_test().find_view(view.id),changed);
                const double radius=std::hypot(target.cosine_axis.x,target.cosine_axis.y);
                require(evaluated.state==MeasurementState::Resolved&&std::abs(evaluated.presentations.front().value-radius)<1e-8,"Actual radius changed during GUI drag");
                const auto rim=evaluated.presentations.front().witness_second;
                require(std::abs(std::hypot(rim.x-target.center->x,rim.y-target.center->y)-radius)<1e-8,"GUI arrow escaped radius");
                const auto after=window.document_for_test().sheets.front().dimensions;
                require(after!=before&&state->undo()&&state->document().sheets.front().dimensions==before,"Radius drag did not move or undo atomically");
                window.edit_workspace_document(document.document_id);flush();
                require(state->redo()&&state->document().sheets.front().dimensions==after,"Radius drag Redo lost its reference/layout");
                window.edit_workspace_document(document.document_id);flush();
            }
        }else {
            const auto evaluated=evaluate_drawing_dimension(*window.document_for_test().find_view(view.id),dimensions.front());
            require(evaluated.state==MeasurementState::Resolved,"Chamfer dimension is unresolved");
            if(kind==DrawingDimensionKind::Angular) {
                const auto delta=Point2{target.points.back().x-target.points.front().x,target.points.back().y-target.points.front().y};
                const double expected=std::atan2(std::abs(delta.y),std::abs(delta.x))*180/std::numbers::pi;
                ++angles;require(std::abs(evaluated.presentations.front().value-expected)<1e-7,"Chamfer/axis angle is incorrect");
            }else {
            const auto direction=window.document_for_test().sheets.front().dimensions.front().direction;
                const double dx=target.points.back().x-midpoint.x,dy=target.points.back().y-midpoint.y;
                const double expected=direction==DimensionDirection::Horizontal?std::abs(dx):direction==DimensionDirection::Vertical?std::abs(dy):std::hypot(dx,dy);
                ++lines;require(std::abs(evaluated.presentations.front().value-expected)<1e-7,"Chamfer half-span dimension is incorrect");
            }
        }
        const auto output=output_directory/(target.circular?"contour-radius-gui.drwz":"contour-chamfer-gui.drwz");
        window.document_for_test().save(output);const auto reopened=DrawingDocument::load(output);
        require(reopened.sheets.front().dimensions==window.document_for_test().sheets.front().dimensions,"GUI contour annotation did not survive reopen");
        window.grab().save(QString::fromStdString((std::filesystem::path("build")/("contour-ui-"+std::to_string(radii)+"-"+std::to_string(lines)+"-"+std::to_string(angles)+".png")).string()));
    }
    require(radii==2&&lines>=2&&angles>=2,"Both fillet and chamfer sides were not exercised");
    std::cout<<"Actual bolt contour GUI: "<<radii<<" radii, "<<lines<<" chamfer spans, "<<angles<<" chamfer angles passed\n";return 0;
}
