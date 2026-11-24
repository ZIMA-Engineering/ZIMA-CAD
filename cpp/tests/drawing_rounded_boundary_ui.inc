void verify_rounded_boundary_ui() {
    using namespace zima;using namespace drawing;
    unsigned cases{};
    for(bool horizontal:{false,true})for(bool reverse:{false,true})for(int pair=0;pair<3;++pair) {
        auto document=DrawingDocument::create_default();auto& sheet=document.sheets.front();
        auto view=test::rounded_boundary_view(3,5,horizontal);view.x=sheet.width_mm()/2;view.y=sheet.height_mm()/2;view.show_caption=false;
        sheet.views={view};const auto geometry=view.measurement_geometry;
        workspace::Workspace live;live.add_drawing(document);app::DrawingWindow window(&live,false);
        window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1500,1100);
        window.edit_workspace_document(document.document_id);window.show();flush();
        auto* canvas=window.findChild<QWidget*>("drawingCanvas");require(canvas,"Boundary canvas missing");
        auto* state=live.open_drawing(document.document_id);const auto revision=state->revision();
        const auto calculations=kernel::body_calculation_count.load();
        const auto point=[&](double x,double y) {
            if(horizontal)std::swap(x,y);
            const auto box=window.sheet_rectangle_for_test();const auto ratio=box.height()/sheet.height_mm();
            return QPointF(box.right()-view.x*ratio+x*view.scale*ratio,box.bottom()-view.y*ratio-y*view.scale*ratio);
        };
        auto first=point(pair==2?0:-23,4),second=point(pair==1?0:35,4);if(reverse)std::swap(first,second);
        for(bool cancel:{true,false}) {
            window.findChild<QAction*>("drawingDimensionAction")->trigger();flush();
            app::DrawingDimensionDialog* pending{};
            for(auto* dialog:window.findChildren<QDialog*>())if(auto* d=dynamic_cast<app::DrawingDimensionDialog*>(dialog);d&&d->isVisible())pending=d;
            require(pending,"Boundary dimension dialog missing");
            pending->findChild<QComboBox*>("drawingDimensionType")->setCurrentIndex(int(DrawingDimensionKind::Linear));flush();
            pick(canvas,first);pick(canvas,second);
            require(pending->placing(),"Two independent boundary/axis mouse clicks did not arm linear placement");
            require(std::ranges::all_of(pending->value().attachments,[](const auto& a){return a.kind==DimensionAttachmentKind::Line;}),
                "Automatic boundary/axis entry chose endpoints instead of lines");
            require(state->revision()==revision,"Boundary selection changed the document before OK");
            if(cancel){pending->reject();flush();require(state->revision()==revision,"Boundary Cancel created an undo entry");continue;}
            pick(canvas,point(5,18));auto* ok=pending->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
            require(ok->isEnabled(),"Boundary dimension OK disabled");ok->click();flush();
        }
        mouse(canvas,QEvent::MouseButtonDblClick,{5,5},Qt::MiddleButton,Qt::MiddleButton);flush();
        const auto created=state->document().sheets.front().dimensions;require(created.size()==1,"Boundary dimension not committed");
        const double expected=pair==0?58:pair==1?23:35;const auto id=created.front().id;
        require(state->undo()&&state->document().sheets.front().dimensions.empty()&&state->redo()&&state->document().sheets.front().dimensions==created,
            "Boundary creation Undo/Redo lost reference identity");window.edit_workspace_document(document.document_id);flush();
        for(auto delta:std::array{QPointF(15,-20),QPointF(-25,30),QPointF(10,-10)}) {
            const auto before=state->document().sheets.front().dimensions;
            const auto grip=window.annotation_handle_for_test(id,0,true);require(grip.has_value(),"Boundary dimension label grip missing");
            pick(canvas,*grip);mouse(canvas,QEvent::MouseButtonPress,*grip,Qt::LeftButton,Qt::LeftButton);
            mouse(canvas,QEvent::MouseMove,*grip+delta,Qt::NoButton,Qt::LeftButton);mouse(canvas,QEvent::MouseButtonRelease,*grip+delta,Qt::LeftButton,Qt::NoButton);
            const auto after=state->document().sheets.front().dimensions;
            const auto value=evaluate_drawing_dimension(*state->document().find_view(view.id),after.front());
            require(after!=before&&value.state==MeasurementState::Resolved&&std::abs(value.presentations.front().value-expected)<1e-8,
                "Boundary dimension drag lost its exact value");
            require(state->undo()&&state->document().sheets.front().dimensions==before&&state->redo()&&state->document().sheets.front().dimensions==after,
                "Boundary dimension drag Undo/Redo failed");window.edit_workspace_document(document.document_id);flush();
        }
        QTemporaryDir temporary;const auto file=std::filesystem::path(temporary.path().toStdString())/"boundaries.drwz";
        state->document().save(file);const auto reopened=DrawingDocument::load(file);
        const auto value=evaluate_drawing_dimension(reopened.sheets.front().views.front(),reopened.sheets.front().dimensions.front());
        require(value.state==MeasurementState::Resolved&&std::abs(value.presentations.front().value-expected)<1e-8,"Boundary native reopen changed measurement");
        require(kernel::body_calculation_count.load()==calculations&&state->document().find_view(view.id)->measurement_geometry==geometry,
            "Boundary interaction recalculated or replaced model geometry");
        require(canvas->grab().save(QString("build/drawing-diameter-20261124/linear-boundaries-%1.png").arg(cases)),"Boundary screenshot failed");++cases;
    }
    std::cout<<"Independent boundary GUI: "<<cases<<" line/axis/order/orientation cases, Cancel, OK, label drags, Undo/Redo and native reopen passed\n";
}
