int verify_repeated_drawing_dimensions() {
    using namespace zima;using namespace drawing;
    workspace::Workspace live;
    kernel::ViewerMesh mesh;
    mesh.edges={{{{0,0,0},{40,0,0}},{"repeat","bottom",{}}},
                {{{0,20,0},{40,20,0}},{"repeat","top",{}}},
                {{{40,0,0},{40,20,0}},{"repeat","right",{}}}};
    kernel::ViewerEdge circle;circle.reference={"repeat","circle",{}};
    for(int i=0;i<=64;++i){const double a=2*std::numbers::pi*i/64;circle.points.push_back({70+8*std::cos(a),10+8*std::sin(a),0});}
    mesh.edges.push_back(circle);
    auto document=DrawingDocument::create_default();
    for(int i=0;i<2;++i) {
        auto view=DrawingDocument::create_view("repeat-source",{},{});view.camera={{1,0,0},{0,1,0},{0,0,-1}};
        view.x=140;view.y=i?80:160;view.scale=1;
        prepare_interactive_view(view,std::make_shared<const kernel::ViewerMesh>(mesh));document.sheets.front().views.push_back(std::move(view));
    }
    const auto document_id=document.document_id;
    live.add_drawing(document);live.activate(document_id);live.display_top_level(document_id);
    app::DrawingWindow window(&live,false);window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1400,1000);
    window.edit_workspace_document(document_id);window.show();flush();
    auto* canvas=window.findChild<QWidget*>("drawingCanvas");require(canvas,"Repeated dimension canvas missing");
    const auto dialog=[&]()->app::DrawingDimensionDialog* {
        for(auto* candidate:window.findChildren<QDialog*>())if(auto* d=dynamic_cast<app::DrawingDimensionDialog*>(candidate);d&&d->isVisible())return d;
        return nullptr;
    };
    const auto screen=[&](int index,Point2 p) {
        const auto& sheet=window.document_for_test().sheets.front();const auto& view=sheet.views[index];
        const auto box=window.sheet_rectangle_for_test();const double ratio=box.height()/sheet.height_mm();
        return QPointF(box.right()-view.x*ratio+p.x*view.scale*ratio,box.bottom()-view.y*ratio-p.y*view.scale*ratio);
    };
    unsigned committed{};
    for(auto kind:{DrawingDimensionKind::Linear,DrawingDimensionKind::Radius,DrawingDimensionKind::Diameter,DrawingDimensionKind::Angular,DrawingDimensionKind::Chain}) {
        const auto initial_revision=live.open_drawing(document_id)->revision();
        window.findChild<QAction*>("drawingDimensionAction")->trigger();flush();
        require(dialog(),"Dimension command did not start");
        dialog()->findChild<QComboBox*>("drawingDimensionType")->setCurrentIndex(int(kind));flush();
        require(live.open_drawing(document_id)->revision()==initial_revision,"Opening repeated dimension changed the document");
        for(int iteration=0;iteration<3;++iteration) {
            auto* pending=dialog();require(pending&&pending->value().kind==kind,"OK did not retain the dimension kind");
            require(std::ranges::none_of(pending->value().attachments,[](const auto& a){return a.reference.valid();}),"Next dimension retained a previous reference");
            const int view=iteration%2;
            if(kind==DrawingDimensionKind::Radius||kind==DrawingDimensionKind::Diameter)pick(canvas,screen(view,{78,10}));
            else {
                pick(canvas,screen(view,{12.3,0}));
                pick(canvas,screen(view,kind==DrawingDimensionKind::Angular?Point2{40,6.3}:Point2{12.3,20}));
            }
            require(pending->placing(),"Repeated dimension did not accept its actual mouse references");
            pick(canvas,screen(view,{86,30}));
            const auto revision=live.open_drawing(document_id)->revision();
            auto* ok=pending->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);require(ok->isEnabled(),"Repeated dimension cannot commit");
            ok->click();flush();++committed;
            require(window.document_for_test().sheets.front().dimensions.size()==committed&&live.open_drawing(document_id)->revision()==revision+1,"Repeated OK did not create exactly one dimension transaction");
            const auto& accepted=window.document_for_test().sheets.front().dimensions.back();
            const auto& accepted_view=*window.document_for_test().find_view(accepted.view_id);
            const auto measured=evaluate_drawing_dimension(accepted_view,accepted);
            const double expected=kind==DrawingDimensionKind::Radius?8:kind==DrawingDimensionKind::Diameter?16:kind==DrawingDimensionKind::Angular?90:20;
            require(measured.state==MeasurementState::Resolved&&std::abs(measured.presentations.front().value-expected)<1e-7,"Repeated dimension has the wrong measured value");
            require(dialog()&&dialog()->value().kind==kind,"OK did not open the next dimension");
        }
        const auto revision=live.open_drawing(document_id)->revision();
        mouse(canvas,QEvent::MouseButtonPress,{5,5},Qt::MiddleButton,Qt::MiddleButton);
        mouse(canvas,QEvent::MouseButtonRelease,{5,5},Qt::MiddleButton,Qt::NoButton);
        require(dialog()&&live.open_drawing(document_id)->revision()==revision,"Short MMB ended the repeating command or created a transaction");
        mouse(canvas,QEvent::MouseButtonDblClick,{5,5},Qt::MiddleButton,Qt::MiddleButton);
        require(!dialog()&&live.open_drawing(document_id)->revision()==revision,"Empty MMB double-click failed to end the command without a transaction");
        auto* state=live.open_drawing(document_id);require(state->undo(),"Repeated dimension has no Undo");window.edit_workspace_document(document_id);flush();
        require(window.document_for_test().sheets.front().dimensions.size()==committed-1,"Repeated Undo removed more than one dimension");
        require(state->redo(),"Repeated dimension has no Redo");window.edit_workspace_document(document_id);flush();
    }
    QTemporaryDir temporary;require(temporary.isValid(),"Repeated dimension native directory missing");
    const auto file=std::filesystem::path(temporary.path().toStdString())/"repeated.drwz";
    window.document_for_test().save(file);
    require(DrawingDocument::load(file).sheets.front().dimensions==window.document_for_test().sheets.front().dimensions,"Repeated dimensions lost their references or layouts on native reopen");
    std::cout<<"Repeated drawing dimensions: 15 actual mouse creations across two views and five kinds; OK continues, short MMB stays, double MMB ends, one Undo per dimension and native reopen passed\n";
    return 0;
}
