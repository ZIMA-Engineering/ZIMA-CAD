#include <zima/kernel/calculation_diagnostics.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <QMessageBox>
#include <QTimer>
#include <QComboBox>
#include <QTableWidget>
#include <QPointer>
#include <QLabel>
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
    workspace::Workspace live;
    const bool regenerate=qEnvironmentVariableIsSet("ZIMA_VERIFY_DRAWING_REGENERATE_SOURCE");
    std::string replacement_section;
    if(regenerate) {
        require(!path.empty(),"Source regeneration needs a real Part/Drawing pair");
        auto source_path=path;source_path.replace_extension(".prtz");
        const auto input=qEnvironmentVariable("ZIMA_VERIFY_DRAWING_SOURCE_FILE");
        std::vector<kernel::BodyResult> cache;auto source=document::PartDocument::load(input.isEmpty()?source_path:std::filesystem::path(input.toStdString()),&cache);
        if(qEnvironmentVariableIsSet("ZIMA_VERIFY_DRAWING_REBIND_SECTION")) {
            require(source.sections.size()==1,"Rebinding fixture needs one replacement Section");replacement_section=source.sections.front().id;
        }
        const auto source_id=source.document_id;const auto sections=source.sections;
        live.add_part(std::move(source),std::move(cache),source_path);kernel::OcctKernel kernel;
        static_cast<void>(workspace::regenerate_part(live,kernel,source_id));
        const auto& result=live.open_part(source_id)->session.document();
        require(result.sections.size()==sections.size(),"Part regeneration replaced Sections");
        for(std::size_t i=0;i<sections.size();++i) {
            require(result.sections[i].placement.reference_valid,"Real Part regeneration left its Section invalid");
            require(result.sections[i].id==sections[i].id&&result.sections[i].sketch.id==sections[i].sketch.id&&
                result.sections[i].placement.references==sections[i].placement.references,"Real Part regeneration changed Section references");
        }
        QTemporaryDir temporary;const auto saved=std::filesystem::path(temporary.path().toStdString())/"source.prtz";
        result.save(saved,live.open_part(source_id)->session.calculated_boundaries());
        require(document::PartDocument::load(saved).sections.front().placement.reference_valid,"Reopened real Part lost its Section placement");
    }
    live.add_drawing(document,path);
    app::DrawingWindow window(&live,false);window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1500,1100);
    window.edit_workspace_document(document.document_id);window.show();flush();
    if(regenerate) {
        const auto count=kernel::body_calculation_count.load();
        if(!replacement_section.empty()) {
            // Repair the Section view before its parent refreshes dependent views.
            const auto& views=document.sheets.front().views;
            for(auto it=views.rbegin();it!=views.rend();++it) {
                const auto& before=*it;
                window.select_view_for_test(before.id);window.findChild<QAction*>("editDrawingViewAction")->trigger();flush();
                QPointer<QDialog> properties;
                for(auto* candidate:window.findChildren<QDialog*>("drawingViewProperties"))if(candidate->isVisible())properties=candidate;
                require(properties,"View Properties did not open for Section replacement");
                auto* sections=properties->findChild<QComboBox*>("drawingSection");
                const int index=sections->findData(QString::fromStdString(replacement_section));require(index>0,"Replacement Section missing from Properties");
                if(!before.section_id.empty())sections->setCurrentIndex(index);
                auto* markers=properties->findChild<QTableWidget*>("drawingSectionMarkers");
                if(!before.section_markers.empty())for(int row=0;row<markers->rowCount();++row)markers->item(row,0)->setCheckState(Qt::Checked);
                properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
                if(properties&&properties->isVisible())throw std::runtime_error("Section replacement could not commit: "+properties->findChild<QLabel*>("drawingViewError")->text().toStdString());
                const auto* after=window.document_for_test().find_view(before.id);
                // Existing View Properties numeric fields have 0.001 mm resolution.
                require(after&&std::abs(after->x-before.x)<=0.000500001&&std::abs(after->y-before.y)<=0.000500001&&after->scale==before.scale,
                    "Section replacement moved the Drawing view beyond its Properties field resolution");
            }
            require(window.document_for_test().sheets.front().dimensions==document.sheets.front().dimensions,"Section replacement changed existing dimensions");
            std::cout<<"Existing Drawing view Properties rebound the deleted Section and trace without recreating views\n";
        }
        QString error;QTimer warnings;
        QObject::connect(&warnings,&QTimer::timeout,[&] {
            for(auto* widget:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(widget);box&&box->isVisible()) {error=box->text();box->accept();}
        });
        warnings.start(20);
        auto* action=window.findChild<QAction*>("regenerateDrawingViewAction");require(action,"Drawing regeneration action missing");
        action->trigger();flush();warnings.stop();
        if(!error.isEmpty())throw std::runtime_error("Real Drawing regeneration: "+error.toStdString());
        require(kernel::body_calculation_count.load()==count,"Drawing regeneration recalculated the source Part");
        require(window.document_for_test().find_view(view_id)->section_snapshot->placement.reference_valid,"Drawing retained an invalid Section");
        std::cout<<"Real Part regeneration, native reopen and Drawing Regenerate action passed\n";
    }
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
