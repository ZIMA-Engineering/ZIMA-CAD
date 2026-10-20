namespace {
void verify_balloon_continuous_curve_ui() {
    using namespace zima;using namespace drawing;
    workspace::Workspace live;auto doc=DrawingDocument::create_default();kernel::ViewerMesh mesh;
    mesh.edges={{{{0,0,0},{100,0,0}},{"pin","long-edge","1:a"}}};
    kernel::ViewerEdge arc;arc.reference={"leaf","arc","1:s4:leaf"};
    for(int i=0;i<=96;++i){const double t=std::numbers::pi*i/96;arc.points.push_back({50+20*std::cos(t),40+20*std::sin(t),0});}
    mesh.edges.push_back(arc);
    mesh.edges.push_back({{{0,90,0},{40,110,0},{100,90,0}},{"pin","curved-edge","1:a"}});
    auto view=DrawingDocument::create_view("assembly",{},mesh,ViewOrientation::Top);
    view.camera={{1,0,0},{0,1,0},{0,0,1}};view.x=145;view.y=180;view.scale=1;
    refresh_view_geometry(view,mesh);auto& sheet=doc.sheets.front();sheet.views={view};sheet.bom_source_document_id="assembly";
    BomRow pin;pin.item_number=1;pin.occurrence_paths={"1:a"};BomRow sub;sub.item_number=2;sub.occurrence_paths={"1:s"};sheet.bom_rows={pin,sub};
    live.add_drawing(doc);auto* state=live.open_drawing(doc.document_id);
    app::DrawingWindow window(&live,false);window.resize(1300,900);window.edit_workspace_document(doc.document_id);window.show();flush();
    auto* canvas=window.findChild<QWidget*>("drawingCanvas");auto* action=window.findChild<QAction*>("drawingBalloonAction");
    check(canvas&&action,"Continuous balloon command missing");
    const auto properties=[&]()->app::DrawingBalloonDialog*{for(auto* child:window.findChildren<QDialog*>())
        if(auto* p=dynamic_cast<app::DrawingBalloonDialog*>(child);p&&p->isVisible())return p;return nullptr;};
    const auto point=[&](Point2 p){const auto box=window.sheet_rectangle_for_test();const auto scale=box.height()/sheet.height_mm();
        return QPointF(box.right()-view.x*scale+p.x*view.scale*scale,box.bottom()-view.y*scale-p.y*view.scale*scale);};
    const auto begin=[&](){action->trigger();flush();auto* p=properties();check(p,"Continuous balloon properties missing");
        if(p->choosing_view())pick(canvas,point({50,40}));check(p->view_id()==view.id,"Continuous balloon chose another view");
        p->findChild<QPushButton*>("drawingBalloonAdd")->click();return p;};
    const auto verify_anchor=[](const DrawingSheet& sheet,const DrawingBalloon& balloon,Point2 at,int number){
        const auto value=evaluate_balloon(sheet,balloon);check(!value.unresolved&&value.anchor&&value.item_number==number,
            "Continuous balloon lost its exact BOM occurrence");
        if(std::hypot(value.anchor->x-at.x,value.anchor->y-at.y)>=1e-6)
            throw std::runtime_error("Balloon contact mismatch on "+balloon.attachment.reference.semantic_key+": "+
                std::to_string(value.anchor->x)+","+std::to_string(value.anchor->y)+" expected "+std::to_string(at.x)+","+std::to_string(at.y)+
                " parameter "+std::to_string(balloon.attachment.parameter));
    };
    const double t=.31*std::numbers::pi;
    const std::array<Point2,3> targets{{{23,0},{50+20*std::cos(t),40+20*std::sin(t)},{17.3,98.65}}};
    for(std::size_t i=0;i<targets.size();++i) {
        const auto previous=state->document().sheets.front().balloons;const auto revision=state->revision();
        auto* p=begin();pick(canvas,point(targets[i]));check(p->placing(),"General curve contact did not create a balloon preview");
        verify_anchor(p->staged_sheet(),*p->selected_balloon(),targets[i],i==1?2:1);
        check(state->revision()==revision,"Balloon curve preview committed early");p->reject();flush();
        check(state->document().sheets.front().balloons==previous&&state->revision()==revision,"Balloon curve Cancel modified the document");
        p=begin();pick(canvas,point(targets[i]));pick(canvas,point({targets[i].x+10,targets[i].y+12}));
        p->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        const auto committed=state->document().sheets.front().balloons;
        check(committed.size()==previous.size()+1,"Continuous balloon did not commit exactly one item");
        verify_anchor(state->document().sheets.front(),committed.back(),targets[i],i==1?2:1);
        check(state->undo()&&state->document().sheets.front().balloons==previous,"Continuous balloon Undo lost the preceding state");
        check(state->redo()&&state->document().sheets.front().balloons==committed,"Continuous balloon Redo lost its curve parameter");
        window.edit_workspace_document(doc.document_id);flush();
    }
    const auto original=state->document().sheets.front().balloons.front();
    auto handle=window.balloon_handle_for_test(original.id),endpoint=window.balloon_handle_for_test(original.id,1);
    check(handle&&endpoint,"Continuous balloon drag handles missing");pick(canvas,*handle);
    mouse(canvas,QEvent::MouseButtonPress,*endpoint,Qt::LeftButton,Qt::LeftButton);
    mouse(canvas,QEvent::MouseMove,point({32.7,0}),Qt::NoButton,Qt::LeftButton);
    mouse(canvas,QEvent::MouseButtonRelease,point({32.7,0}),Qt::LeftButton,Qt::NoButton);
    verify_anchor(state->document().sheets.front(),state->document().sheets.front().balloons.front(),{32.7,0},1);
    check(state->undo()&&state->document().sheets.front().balloons.front()==original,"Continuous endpoint drag did not undo");
    check(state->redo(),"Continuous endpoint drag did not redo");
    auto regenerated=state->document();auto moved=mesh;for(auto& edge:moved.edges)for(auto& p:edge.points)p.y+=5;
    refresh_view_geometry(regenerated.sheets.front().views.front(),moved);refresh_balloons(regenerated.sheets.front());
    verify_anchor(regenerated.sheets.front(),regenerated.sheets.front().balloons.front(),{32.7,5},1);
    const auto output=std::filesystem::path("build/balloon-continuous-curve");std::filesystem::create_directories(output);
    regenerated.save(output/"curve-attachments.drwz");const auto reopened=DrawingDocument::load(output/"curve-attachments.drwz");
    check(reopened.sheets.front().balloons==regenerated.sheets.front().balloons,"Native balloon save/reopen lost arbitrary curve locations");
    for(std::size_t i=1;i<targets.size();++i)verify_anchor(reopened.sheets.front(),reopened.sheets.front().balloons[i],{targets[i].x,targets[i].y+5},i==1?2:1);
    window.edit_workspace_document(doc.document_id);flush();canvas->grab().save(QString::fromStdString((output/"mouse-proof.png").string()));
    std::cout<<"Balloon arbitrary line/arc/curved-edge contacts: mouse creation, Cancel, exact BOM ownership, endpoint drag, Undo/Redo, regeneration and native save/reopen passed\n";
}
}
