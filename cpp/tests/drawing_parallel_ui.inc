#include <zima/kernel/calculation_diagnostics.hpp>
int verify_drawing_parallel_ui() {
    using namespace zima;using namespace drawing;
    const auto path=std::filesystem::path(qEnvironmentVariable("ZIMA_VERIFY_DRAWING_PARALLEL_FILE").toStdString());
    auto document=DrawingDocument::create_default();
    if(!path.empty())document=DrawingDocument::load(path);
    else {
        kernel::OcctKernel kernel;auto part=document::PartDocument::create_default();part.history={test::rectangular_feature(part,{80,20,20})};
        const auto mesh=kernel.evaluate_history(part.kernel_operations()).back().mesh;
        auto view=DrawingDocument::create_view(part.document_id,{},mesh);view.x=105;view.y=100;view.scale=2;
        document.sheets.front().views={view};
        auto section=document::create_section();static_cast<void>(section.sketch.add_segment(-60,0,60,0));
        view.id=kernel::make_stable_id();view.section_id=section.id;view.section_snapshot=section;
        view.camera={{1,0,0},{0,0,1},{0,-1,0}};view.y=200;refresh_view_geometry(view,mesh);
        document.sheets.front().views.push_back(view);
    }
    require(document.sheets.front().views.size()>1,"Parallel fixture needs a side view");
    if(qEnvironmentVariableIsSet("ZIMA_VERIFY_DRAWING_SECTION_REFRESH")) {
        const auto source=document.sheets.front().views.front().output_source;require(bool(source),"Section source packet missing");
        refresh_view_geometry(document.sheets.front().views[1],*source);
    }
    const auto view_id=document.sheets.front().views[1].id;
    const auto original_count=document.sheets.front().dimensions.size();
    workspace::Workspace live;live.add_drawing(document);
    app::DrawingWindow window(&live,false);window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1500,1100);
    window.edit_workspace_document(document.document_id);window.show();flush();
    auto* canvas=window.findChild<QWidget*>("drawingCanvas");require(canvas,"Parallel canvas missing");
    const auto dialog=[&]()->app::DrawingDimensionDialog* {
        for(auto* candidate:window.findChildren<QDialog*>())if(auto* d=dynamic_cast<app::DrawingDimensionDialog*>(candidate);d&&d->isVisible())return d;
        return nullptr;
    };
    const auto screen=[&](Point2 p) {
        const auto& sheet=window.document_for_test().sheets.front();const auto& view=*window.document_for_test().find_view(view_id);
        const auto box=window.sheet_rectangle_for_test();const double ratio=box.height()/sheet.height_mm();
        return QPointF(box.right()-view.x*ratio+p.x*view.scale*ratio,box.bottom()-view.y*ratio-p.y*view.scale*ratio);
    };
    const bool web=path.empty()||qEnvironmentVariableIsSet("ZIMA_VERIFY_DRAWING_SECTION_LINE");
    const double x=path.empty()?13:web?55:10,y=web?10:20;
    const auto calculations=kernel::body_calculation_count.load();
    const auto geometry=window.document_for_test().find_view(view_id)->measurement_geometry;
    auto* state=live.open_drawing(document.document_id);
    for(bool reverse:{false,true}) {
        const auto revision=state->revision();
        window.findChild<QAction*>("drawingDimensionAction")->trigger();flush();
        auto* pending=dialog();require(pending,"Parallel dimension command missing");
        pick(canvas,screen({x,reverse?-y:y}));pick(canvas,screen({x,reverse?y:-y}));
        require(pending->placing(),"Parallel mouse picks did not reach placement");
        require(std::ranges::all_of(pending->value().attachments,[](const auto& a){return a.kind==DimensionAttachmentKind::Line;}),"Parallel supports were not selected as lines");
        require(state->revision()==revision,"Picking changed the Drawing before OK");
        const auto measured=evaluate_drawing_dimension(*window.document_for_test().find_view(view_id),pending->value());
        require(measured.state==MeasurementState::Resolved&&std::abs(measured.presentations.front().value-2*y)<1e-7,"Parallel supports measured the wrong thickness");
        // Cancel must discard the first draft and its exact same mouse choices.
        if(!reverse) {
            pending->reject();flush();require(state->revision()==revision,"Cancel changed the Drawing");
            window.findChild<QAction*>("drawingDimensionAction")->trigger();flush();pending=dialog();
            pick(canvas,screen({x,y}));pick(canvas,screen({x,-y}));require(pending->placing(),"Repeated parallel entry failed after Cancel");
        }
        pick(canvas,screen({path.empty()?45.:web?65.:-45.,0}));
        auto* ok=pending->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);require(ok->isEnabled(),"Parallel dimension cannot commit");ok->click();flush();
        mouse(canvas,QEvent::MouseButtonDblClick,{5,5},Qt::MiddleButton,Qt::MiddleButton);
        require(window.document_for_test().sheets.front().dimensions.size()==original_count+1&&state->revision()!=revision,"Parallel OK did not commit the dimension");
        const auto accepted=window.document_for_test().sheets.front().dimensions;
        require(state->undo(),"Parallel dimension has no Undo");window.edit_workspace_document(document.document_id);flush();
        require(window.document_for_test().sheets.front().dimensions.size()==original_count,"Parallel Undo lost existing dimensions");
        require(state->redo(),"Parallel dimension has no Redo");window.edit_workspace_document(document.document_id);flush();
        require(window.document_for_test().sheets.front().dimensions==accepted,"Parallel Redo changed references");
        QTemporaryDir temporary;const auto saved=std::filesystem::path(temporary.path().toStdString())/"parallel.drwz";
        window.document_for_test().save(saved);const auto reopened=DrawingDocument::load(saved);
        require(reopened.sheets.front().dimensions==accepted,"Parallel native reopen lost references");
        const auto persisted=evaluate_drawing_dimension(*reopened.find_view(view_id),accepted.back());
        require(persisted.state==MeasurementState::Resolved&&std::abs(persisted.presentations.front().value-2*y)<1e-7,"Parallel native reopen lost the measured thickness");
        if(!path.empty())require(canvas->grab().save("build/drawing-parallel-after.png"),"Parallel result capture failed");
        require(state->undo(),"Parallel fixture cleanup has no Undo");window.edit_workspace_document(document.document_id);flush();
    }
    require(kernel::body_calculation_count.load()==calculations,"Dimension picking or editing calculated a Body");
    require(window.document_for_test().find_view(view_id)->measurement_geometry==geometry,"Dimension editing replaced source geometry");
    std::cout<<"Parallel GUI: "<<2*y<<" mm, both mouse orders, Cancel, one OK transaction, Undo/Redo, native reopen and no Body calculation passed\n";
    return 0;
}
