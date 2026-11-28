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
    auto arc=circle;arc.reference.semantic_key="arc";arc.points.clear();
    for(int i=0;i<=32;++i){const double a=std::numbers::pi*i/32;arc.points.push_back({100+8*std::cos(a),10+8*std::sin(a),0});}
    mesh.edges.push_back(arc);
    mesh.edges.push_back({{{-25,0,0},{-25,20,0}},{"repeat","drawing-contour:4:wall:side:1",{}}});
    mesh.edges.push_back({{{-15,0,0},{-15,20,0}},{"repeat","drawing-contour:4:wall:side:-1",{}}});
    auto document=DrawingDocument::create_default();
    for(int i=0;i<2;++i) {
        auto view=DrawingDocument::create_view("repeat-source",{},{});view.camera={{1,0,0},{0,1,0},{0,0,-1}};
        view.x=220;view.y=i?80:160;view.scale=2;
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
    const auto cycle_preview=[&](app::DrawingDimensionDialog* pending,int view,int cycles) {
        const auto pointer=screen(view,{86,30});
        mouse(canvas,QEvent::MouseMove,pointer,Qt::NoButton,Qt::NoButton);
        const auto initial=pending->value();
        const auto revision=live.open_drawing(document_id)->revision();
        const auto before=evaluate_drawing_dimension(*window.document_for_test().find_view(initial.view_id),initial);
        for(int step=1;step<=cycles;++step) {
            mouse(canvas,QEvent::MouseButtonPress,pointer,Qt::RightButton,Qt::RightButton);
            mouse(canvas,QEvent::MouseButtonRelease,pointer,Qt::RightButton,Qt::NoButton);
            QContextMenuEvent context(QContextMenuEvent::Mouse,pointer.toPoint(),canvas->mapToGlobal(pointer.toPoint()));
            QApplication::sendEvent(canvas,&context);flush();
            const auto& value=pending->value();const auto& layout=value.segments.front().layout;
            const bool radius=initial.kind==DrawingDimensionKind::Radius;
            require(layout.arrows_reversed==(radius?step%3!=0:step%2!=0)&&
                    layout.radius_center_line_hidden==(radius&&step%3==2),
                "Placement right-click did not cycle the shared arrow/leader presentation");
            require(value.attachments==initial.attachments&&value.kind==initial.kind&&pending->placing()&&
                    live.open_drawing(document_id)->revision()==revision,
                "Placement right-click changed references, kind, transaction or command stage");
            const auto measured=evaluate_drawing_dimension(*window.document_for_test().find_view(value.view_id),value);
            require(measured.state==MeasurementState::Resolved&&before.presentations.front().value==measured.presentations.front().value,
                "Presentation cycling changed the dimension's measurement");
        }
        const auto layout=pending->value().segments.front().layout;
        mouse(canvas,QEvent::MouseMove,pointer+QPointF(12,6),Qt::NoButton,Qt::NoButton);
        require(pending->value().segments.front().layout.arrows_reversed==layout.arrows_reversed&&
                pending->value().segments.front().layout.radius_center_line_hidden==layout.radius_center_line_hidden,
            "Moving the pending dimension discarded its arrow/leader layout");
        mouse(canvas,QEvent::MouseButtonPress,pointer,Qt::MiddleButton,Qt::MiddleButton);
        mouse(canvas,QEvent::MouseButtonPress,pointer,Qt::RightButton,Qt::MiddleButton|Qt::RightButton);
        mouse(canvas,QEvent::MouseMove,pointer+QPointF(10,5),Qt::NoButton,Qt::MiddleButton|Qt::RightButton);
        mouse(canvas,QEvent::MouseButtonRelease,pointer+QPointF(10,5),Qt::RightButton,Qt::MiddleButton);
        QContextMenuEvent navigation_context(QContextMenuEvent::Mouse,pointer.toPoint(),canvas->mapToGlobal(pointer.toPoint()));
        QApplication::sendEvent(canvas,&navigation_context);
        mouse(canvas,QEvent::MouseButtonRelease,pointer+QPointF(10,5),Qt::MiddleButton,Qt::NoButton);
        require(pending->placing()&&pending->value().segments.front().layout.arrows_reversed==layout.arrows_reversed&&
                pending->value().segments.front().layout.radius_center_line_hidden==layout.radius_center_line_hidden&&
                live.open_drawing(document_id)->revision()==revision,
            "View navigation cycled or committed the pending dimension");
        return layout;
    };
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
            const auto cycled=cycle_preview(pending,view,iteration+1);
            if(kind==DrawingDimensionKind::Radius)
                window.grab().save(QString("build/drawing-radius-placement-%1.png").arg(iteration+1));
            const auto revision=live.open_drawing(document_id)->revision();
            auto* ok=pending->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);require(ok->isEnabled(),"Repeated dimension cannot commit");
            pick(canvas,screen(view,{86,30}));
            if(kind!=DrawingDimensionKind::Linear){ok->click();flush();}
            else require(dialog()==pending&&pending->entering(),"Linear placement did not reset the same dialog");
            ++committed;
            require(window.document_for_test().sheets.front().dimensions.size()==committed&&live.open_drawing(document_id)->revision()==revision+1,"Dimension placement/confirmation did not create exactly one transaction");
            const auto& accepted=window.document_for_test().sheets.front().dimensions.back();
            require(accepted.segments.front().layout.arrows_reversed==cycled.arrows_reversed&&
                    accepted.segments.front().layout.radius_center_line_hidden==cycled.radius_center_line_hidden,
                "Confirmation lost the pending arrow/leader layout");
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
        if(kind==DrawingDimensionKind::Linear)dialog()->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        else mouse(canvas,QEvent::MouseButtonDblClick,{5,5},Qt::MiddleButton,Qt::MiddleButton);
        flush();
        require(!dialog()&&live.open_drawing(document_id)->revision()==revision,"Empty MMB double-click failed to end the command without a transaction");
        const auto before_undo=window.document_for_test().sheets.front().dimensions;
        auto* state=live.open_drawing(document_id);require(state->undo(),"Repeated dimension has no Undo");window.edit_workspace_document(document_id);flush();
        require(window.document_for_test().sheets.front().dimensions.size()==committed-1,"Repeated Undo removed more than one dimension");
        require(state->redo(),"Repeated dimension has no Redo");window.edit_workspace_document(document_id);flush();
        require(window.document_for_test().sheets.front().dimensions==before_undo,
            "Redo changed the placed arrow/leader layouts or references");
    }
    struct AutoCase {Point2 first;std::optional<Point2> second;DrawingDimensionKind kind;double expected;};
    const std::vector<AutoCase> auto_cases={{{75.65685424949238,15.65685424949238},{},DrawingDimensionKind::Diameter,16},
        {{100,18},{},DrawingDimensionKind::Radius,8},{{12.3,0},Point2{40,6.3},DrawingDimensionKind::Angular,90},
        {{12.3,0},Point2{12.3,20},DrawingDimensionKind::Linear,20},{{-25,7},Point2{-15,13},DrawingDimensionKind::Linear,10}};
    window.findChild<QAction*>("drawingDimensionAction")->trigger();flush();
    require(dialog()->findChild<QComboBox*>("drawingDimensionType")->currentIndex()==5,
        "Ordinary dimension command does not start in automatic mode");
    for(const auto& item:auto_cases) {
        auto* pending=dialog();const auto revision=live.open_drawing(document_id)->revision();
        pick(canvas,screen(0,item.first));if(item.second)pick(canvas,screen(0,*item.second));
        if(pending->value().kind!=item.kind||!pending->placing())throw std::runtime_error("Automatic mouse kind="+std::to_string(int(pending->value().kind))+" expected="+std::to_string(int(item.kind))+" placing="+std::to_string(pending->placing())+" active="+std::to_string(pending->active_reference()));
        require(pending->findChild<QLabel*>("drawingDimensionStatus")->text().contains(QString::fromUtf8("→")),"Automatic inferred type is not visible");
        const auto cycled=cycle_preview(pending,0,item.kind==DrawingDimensionKind::Radius?2:1);
        pick(canvas,screen(0,{85,35}));++committed;
        require(dialog()==pending&&pending->entering()&&pending->findChild<QComboBox*>("drawingDimensionType")->currentIndex()==5,"Automatic placement lost the active mode");
        const auto& accepted=window.document_for_test().sheets.front().dimensions.back();
        require(accepted.segments.front().layout.arrows_reversed==cycled.arrows_reversed&&
                accepted.segments.front().layout.radius_center_line_hidden==cycled.radius_center_line_hidden,
            "Automatic placement lost the pending arrow/leader layout");
        const auto result=evaluate_drawing_dimension(*window.document_for_test().find_view(accepted.view_id),accepted);
        require(result.state==MeasurementState::Resolved&&std::abs(result.presentations.front().value-item.expected)<1e-7,"Automatic committed measurement is incorrect");
        require(live.open_drawing(document_id)->revision()==revision+1,"Automatic placement created multiple undo transactions");
    }
    // Manual override of the inferred diameter retains the picked curve.
    pick(canvas,screen(0,auto_cases.front().first));auto* override_dialog=dialog();
    const auto before_cancel=window.document_for_test().sheets.front().dimensions;
    const auto cancel_revision=live.open_drawing(document_id)->revision();
    static_cast<void>(cycle_preview(override_dialog,0,1));
    const auto binding=override_dialog->value().attachments.front();
    override_dialog->findChild<QComboBox*>("drawingDimensionType")->setCurrentIndex(int(DrawingDimensionKind::Radius));flush();
    require(override_dialog->value().kind==DrawingDimensionKind::Radius&&override_dialog->value().attachments.front()==binding,"Manual override lost the automatic circle binding");
    override_dialog->reject();flush();
    require(window.document_for_test().sheets.front().dimensions.size()==committed,"Cancel removed already placed automatic dimensions");
    require(window.document_for_test().sheets.front().dimensions==before_cancel&&
            live.open_drawing(document_id)->revision()==cancel_revision,
        "Cancel saved the draft arrow/leader layout");
    // Both real silhouette references must also work with explicit angular entry.
    window.findChild<QAction*>("drawingDimensionAction")->trigger();flush();
    dialog()->findChild<QComboBox*>("drawingDimensionType")->setCurrentIndex(int(DrawingDimensionKind::Angular));flush();
    pick(canvas,screen(0,{-25,7}));pick(canvas,screen(0,{-15,13}));
    require(dialog()->placing(),"Angular command cannot pick cylinder silhouettes");
    pick(canvas,screen(0,{-30,25}));
    const auto angle=evaluate_drawing_dimension(*window.document_for_test().find_view(dialog()->value().view_id),dialog()->value());
    require(angle.state==MeasurementState::Resolved&&std::abs(std::remainder(angle.presentations.front().value,180))<1e-7,"Angular silhouette pick does not resolve");
    dialog()->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();++committed;
    dialog()->reject();flush();
    QTemporaryDir temporary;require(temporary.isValid(),"Repeated dimension native directory missing");
    const auto file=std::filesystem::path(temporary.path().toStdString())/"repeated.drwz";
    window.document_for_test().save(file);
    require(DrawingDocument::load(file).sheets.front().dimensions==window.document_for_test().sheets.front().dimensions,"Repeated dimensions lost their references or layouts on native reopen");
    std::cout<<"Repeated drawing dimensions: 15 actual mouse creations across two views and five kinds; linear placement continues and OK ends, other kinds retain OK continuation, short MMB stays, double MMB ends, one Undo per dimension and native reopen passed\n";
    return 0;
}
