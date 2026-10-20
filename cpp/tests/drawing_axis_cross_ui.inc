namespace {
void verify_drawing_axis_cross_source_regeneration(const std::filesystem::path& output) {
    using namespace zima;using namespace drawing;
    kernel::OcctKernel kernel;auto part=document::PartDocument::create_default();
    auto block=test::rectangular_feature(part,{100,100,40});
    auto first=document::PartDocument::create_thread_container();first.placement={};first.placement.x=-20;first.placement.z=-20;
    first.thread.enabled=false;first.thread.nominal_diameter=10;first.thread.bore_length=40;
    first.thread.end_condition_forward=document::EndCondition::ThroughAll;first.thread.chamfer_enabled=false;first.hole.drill_point_enabled=false;
    // Each opening owns its native profile and axis identities.
    auto second=document::PartDocument::create_thread_container();second.placement=first.placement;second.placement.x=20;
    second.thread=first.thread;second.hole.drill_point_enabled=false;
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Axis-cross test plate"));
    for(const auto& feature:{block,first,second})graph.insert({document::PartHistoryKind::Feature,feature.id});
    part.history={block,first,second};part.set_body_history(graph);
    const auto source=std::filesystem::absolute(output/"source-plate.prtz"),drawing_path=std::filesystem::absolute(output/"source-regeneration.drwz");
    part.save(source,kernel.evaluate_history(part.kernel_operations()));
    DrawingView view;view.id=kernel::make_stable_id();view.source_document_id=part.document_id;view.source_path=source;
    view.camera={{1,0,0},{0,1,0},{0,0,1}};view.scale=1;
    workspace::DrawingProjection initial(nullptr,drawing_path);initial.project(view,{.interactive=true});
    for(auto& axis:view.model_annotations)if(axis.kind==ModelAnnotationKind::Axis)axis.visible=true;
    const auto pick=[&](const std::string& owner,AxisCrossStroke stroke){
        const auto axis=std::ranges::find_if(view.model_annotations,[&](const auto& a){return a.source.owner_id==owner&&a.kind==ModelAnnotationKind::Axis;});
        require(axis!=view.model_annotations.end(),"Actual native opening axis missing after projection");
        const auto g=axis_annotation_geometry(view,*axis);require(g.curves.size()==4,"Actual opening is not end-on");
        const auto end=g.curves[stroke==AxisCrossStroke::Horizontal?1:3].back();
        Point2 point{g.center.x+.8*(end.x-g.center.x),g.center.y+.8*(end.y-g.center.y)};
        MeasurementPickRequest request;request.lines_only=true;const auto candidates=measurement_candidates(view,point,.02,request);
        const auto found=std::ranges::find_if(candidates,[&](const auto& c){return c.attachment.reference.owner_id==owner&&measurement_axis_cross_stroke(c.attachment.reference)==stroke;});
        require(found!=candidates.end(),"Actual native opening stroke cannot be selected");return found->attachment;
    };
    auto linear=make_drawing_dimension(view.id);linear.attachments={pick(first.id,AxisCrossStroke::Vertical),pick(second.id,AxisCrossStroke::Vertical)};
    auto angular=make_drawing_dimension(view.id,DrawingDimensionKind::Angular);
    angular.attachments={pick(first.id,AxisCrossStroke::Horizontal),pick(second.id,AxisCrossStroke::Vertical)};
    for(const double offset:{2.,-3.,4.}) {
        part.history.back().placement.x=20+offset;part.save(source,kernel.evaluate_history(part.kernel_operations()));
        workspace::DrawingProjection regeneration(nullptr,drawing_path);regeneration.project(view,{.interactive=true});
        const auto distance=evaluate_drawing_dimension(view,linear),angle=evaluate_drawing_dimension(view,angular);
        require(distance.state==MeasurementState::Resolved&&angle.state==MeasurementState::Resolved,"Native source regeneration lost an axis-cross binding");
        require(std::abs(distance.presentations.front().value-(40+offset))<1e-6&&std::abs(angle.presentations.front().value-90)<1e-6,
            "Drawing axis-cross dimensions did not follow actual regenerated source geometry");
        require(regeneration.calculated_camera_count()<=1,"Axis-cross projection recalculated the camera repeatedly");
    }
    refresh_drawing_dimension(view,linear);refresh_drawing_dimension(view,angular);
    auto drawing=DrawingDocument::create_default();drawing.sheets.front().views={view};drawing.sheets.front().dimensions={linear,angular};drawing.save(drawing_path);
    const auto reopened=DrawingDocument::load(drawing_path);
    for(const auto& dimension:reopened.sheets.front().dimensions)
        require(evaluate_drawing_dimension(reopened.sheets.front().views.front(),dimension).state==MeasurementState::Resolved,"Reopened native source Drawing lost axis-cross bindings");
}
int verify_drawing_axis_cross_ui() {
    using namespace zima;using namespace drawing;
    const auto output=std::filesystem::path("build/drawing-axis-cross-gui");std::filesystem::create_directories(output);
    verify_drawing_axis_cross_source_regeneration(output);
    for(const bool vertical:{false,true})for(const auto kind:{DrawingDimensionKind::Angular,DrawingDimensionKind::Linear,DrawingDimensionKind::Chain}) {
        workspace::Workspace live;auto doc=DrawingDocument::create_default();kernel::ViewerMesh mesh;
        mesh.edges={{{{0,0,0},{60,0,0},{60,60,0},{0,60,0},{0,0,0}},{"plate","outline",{}}}};
        auto view=DrawingDocument::create_view("source","axis-cross-source.prtz",mesh);
        view.camera={{1,0,0},{0,1,0},{0,0,1}};view.projected_edges=project_edges(mesh,view.camera);
        view.x=115;view.y=115;view.scale=1.5;view.show_caption=false;
        for(const double coordinate:{15.,35.,50.}) {
            ModelAnnotation axis;axis.kind=ModelAnnotationKind::Axis;axis.visible=true;
            axis.source={"source","hole","axis:opening",std::to_string(coordinate)};
            axis.model_axis=std::array<kernel::Vec3,2>{{{coordinate,coordinate,-5},{coordinate,coordinate,5}}};
            axis.model_envelope.include({coordinate-5,coordinate-5,-5});axis.model_envelope.include({coordinate+5,coordinate+5,5});
            view.model_annotations.push_back(axis);
        }
        doc.sheets.front().views={view};live.add_drawing(doc);auto* state=live.open_drawing(doc.document_id);
        app::DrawingWindow window(&live,false);window.resize(1300,900);window.edit_workspace_document(doc.document_id);window.show();flush();
        auto* canvas=window.findChild<QWidget*>("drawingCanvas");require(canvas,"Axis-cross canvas missing");
        const auto point=[&](double x,double y){const auto box=window.sheet_rectangle_for_test();const double scale=box.height()/doc.sheets.front().height_mm();
            return QPointF(box.right()-view.x*scale+x*view.scale*scale,box.bottom()-view.y*scale-y*view.scale*scale);};
        const auto arm=[&](double coordinate,bool direction){return direction?point(coordinate,coordinate+5):point(coordinate+5,coordinate);};
        const auto properties=[&]()->app::DrawingDimensionDialog*{for(auto* dialog:window.findChildren<QDialog*>())
            if(auto* p=dynamic_cast<app::DrawingDimensionDialog*>(dialog);p&&p->isVisible())return p;return nullptr;};
        auto* command=window.findChild<QAction*>(kind==DrawingDimensionKind::Chain?"drawingChainDimensionAction":"drawingDimensionAction");
        require(command,"Axis-cross dimension action missing");
        const auto begin=[&](){command->trigger();flush();auto* p=properties();require(p,"Axis-cross properties missing");
            if(kind!=DrawingDimensionKind::Chain){p->findChild<QComboBox*>("drawingDimensionType")->setCurrentIndex(int(kind));flush();}return p;};
        const auto revision=state->revision();auto* pending=begin();
        mouse(canvas,QEvent::MouseMove,arm(15,vertical),Qt::NoButton,Qt::NoButton);
        require(state->revision()==revision,"Axis-cross hover modified the document");
        pick(canvas,arm(15,vertical));
        const auto first=pending->value().attachments.front();
        require(first.kind==DimensionAttachmentKind::Line&&measurement_axis_cross_stroke(first.reference)==
            (vertical?AxisCrossStroke::Vertical:AxisCrossStroke::Horizontal),"Mouse selected the wrong axis-cross stroke");
        auto* table=pending->findChild<QTableWidget*>("drawingDimensionReferences");
        require(table->item(0,2)->text()==QString::fromUtf8(vertical?"Osový kříž (svislá osa)":"Osový kříž (vodorovná osa)"),"Axis-cross reference label exposes its internal key");
        auto* eye=table->cellWidget(0,3)->findChild<QToolButton*>();require(eye,"Axis-cross inspection eye missing");
        eye->click();flush();require(pending->inspected_references()==std::vector{first.reference},"Independent eye inspects the wrong axis-cross reference");
        require(state->revision()==revision,"Axis-cross inspection modified the document");
        pending->reject();flush();require(state->revision()==revision&&state->document().sheets.front().dimensions.empty(),"Axis-cross Cancel committed data");
        pending=begin();pick(canvas,arm(15,vertical));
        pick(canvas,kind==DrawingDimensionKind::Angular?arm(15,!vertical):arm(35,vertical));
        require(pending->placing(),"Axis-cross references did not enter dimension placement");
        pick(canvas,vertical?point(25,58):point(58,25));
        if(kind==DrawingDimensionKind::Chain)pick(canvas,arm(50,vertical));
        require(state->revision()==revision,"Axis-cross preview committed before OK");
        pending->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        require(!properties(),"Axis-cross OK did not close properties");
        const auto committed=state->document().sheets.front().dimensions;
        require(committed.size()==(kind==DrawingDimensionKind::Chain?2:1),"Axis-cross command committed the wrong number of dimensions");
        for(std::size_t index=0;index<committed.size();++index) {
            const auto value=evaluate_drawing_dimension(*state->document().find_view(view.id),committed[index]);
            require(value.state==MeasurementState::Resolved,"GUI axis-cross dimension is unresolved");
            require(std::abs(value.presentations.front().value-(kind==DrawingDimensionKind::Angular?90:index?35:20))<1e-6,"GUI axis-cross dimension measured a wrong value");
        }
        require(state->undo()&&state->document().sheets.front().dimensions.empty(),"Axis-cross commit did not undo atomically");
        require(state->redo()&&state->document().sheets.front().dimensions==committed,"Axis-cross Redo lost references or presentation");
        window.edit_workspace_document(doc.document_id);flush();
        const auto stem=std::string(vertical?"vertical-":"horizontal-")+std::to_string(int(kind));
        const auto path=output/(stem+".drwz");state->document().save(path);
        const auto reopened=DrawingDocument::load(path);require(reopened.sheets.front().dimensions==committed,"GUI native save/reopen lost axis-cross references");
        require(canvas->grab().save(QString::fromStdString((output/(stem+".png")).string())),"Cannot save axis-cross GUI proof");
    }
    std::cout<<"Axis-cross GUI: six angular/linear/chain mouse scenarios, exact stroke inspection, Cancel, Undo/Redo and native save/reopen passed\n";
    return 0;
}
} // namespace
