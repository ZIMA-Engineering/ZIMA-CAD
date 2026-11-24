// Exercise the production Drawing command against a disposable in-memory copy.
int verify_cylinder_diameter_ui(const std::filesystem::path& path) {
    using namespace zima;using namespace drawing;
    auto input=DrawingDocument::load(path);
    if(qEnvironmentVariableIsSet("ZIMA_VERIFY_CYLINDER_REPROJECT")) {
        workspace::DrawingProjection projection(nullptr,path);
        for(auto& sheet:input.sheets)for(auto& view:sheet.views)projection.project(view,{.interactive=true});
    }
    unsigned cases{};
    for(const auto& original_sheet:input.sheets)for(const auto& source_view:original_sheet.views) {
        const auto curves=projected_measurement_curves(source_view);
        std::cout<<"View "<<source_view.name<<": cylinder sides="<<std::ranges::count_if(curves,[](const auto& c){return c.diameter_offset.has_value();})<<'\n';
        if(qEnvironmentVariableIsSet("ZIMA_VERIFY_CYLINDER_DIAGNOSTICS")) {
            std::cout<<"Section="<<source_view.section_id<<" source="<<bool(source_view.output_source)<<" camera depth="<<source_view.camera.depth.x<<','<<source_view.camera.depth.y<<','<<source_view.camera.depth.z<<'\n';
            for(const auto& c:curves)std::cout<<"Curve line="<<c.line<<" circle="<<bool(c.center)<<" points="<<c.points.size()<<" key="<<c.source.semantic_key<<'\n';
            for(const auto& e:source_view.projected_edges)if(!e.hatch)std::cout<<"Edge silhouette="<<e.silhouette<<" points="<<e.points.size()<<" key="<<e.source.semantic_key<<'\n';
        }
        unsigned sides{};
        for(const auto& target:curves) {
            if(!target.diameter_offset||sides>=2)continue;
            const auto fragments=measurement_reference_geometry(source_view,target.source);
            if(fragments.empty())continue;
            const auto& fragment=*std::ranges::max_element(fragments,{},[](const auto& points){return std::hypot(points.back().x-points.front().x,points.back().y-points.front().y);});
            const Point2 middle{(fragment.front().x+fragment.back().x)/2,(fragment.front().y+fragment.back().y)/2};
            MeasurementPickRequest request;request.circles_only=true;request.cylinder_diameters=true;
            const auto candidates=measurement_candidates(source_view,middle,.01,request);
            if(candidates.empty()||candidates.front().attachment.reference!=target.source)continue;
            auto document=input;document.sheets={original_sheet};auto& sheet=document.sheets.front();
            sheet.views={source_view};sheet.dimensions.clear();auto& view=sheet.views.front();
            view.parent_view_id.clear();
            view.projection_direction=ProjectionDirection::None;
            view.inherit_parent_breaks=false;
            view.scale=2;view.show_caption=false;
            view.x=sheet.width_mm()/2+middle.x*view.scale;view.y=sheet.height_mm()/2-middle.y*view.scale;
            const auto geometry=view.measurement_geometry;
            workspace::Workspace live;live.add_drawing(document);
            app::DrawingWindow window(&live,false);window.setAttribute(Qt::WA_DontShowOnScreen);
            window.resize(1500,1100);window.edit_workspace_document(document.document_id);window.show();flush();
            auto* canvas=window.findChild<QWidget*>("drawingCanvas");require(canvas,"Diameter canvas missing");
            auto* state=live.open_drawing(document.document_id);
            const auto calculations=kernel::body_calculation_count.load();
            const auto point=[&](Point2 p){const auto box=window.sheet_rectangle_for_test();const auto ratio=box.height()/sheet.height_mm();return QPointF(box.right()-view.x*ratio+p.x*view.scale*ratio,box.bottom()-view.y*ratio-p.y*view.scale*ratio);};
            const auto dialog=[&]()->app::DrawingDimensionDialog* {
                for(auto* candidate:window.findChildren<QDialog*>())if(auto* found=dynamic_cast<app::DrawingDimensionDialog*>(candidate);found&&found->isVisible())return found;
                return nullptr;
            };
            const auto revision=state->revision();
            for(bool cancel:{true,false}) {
                window.findChild<QAction*>("drawingDimensionAction")->trigger();flush();auto* pending=dialog();require(pending,"Diameter command missing");
                pending->findChild<QComboBox*>("drawingDimensionType")->setCurrentIndex(int(DrawingDimensionKind::Diameter));flush();
                require(pending->pick_request().cylinder_diameters,"Diameter UI did not enable cylinder supports");
                pick(canvas,point(middle));
                require(pending->placing()&&pending->value().attachments.front().reference==target.source,"Actual mouse click did not bind the cylinder silhouette");
                require(state->revision()==revision,"Diameter picking changed the document before OK");
                if(cancel){pending->reject();flush();require(state->revision()==revision,"Diameter Cancel created an undo entry");continue;}
                pick(canvas,point({middle.x+12,middle.y+12}));
                auto* ok=pending->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);require(ok->isEnabled(),"Diameter OK unavailable");ok->click();flush();
            }
            mouse(canvas,QEvent::MouseButtonDblClick,{5,5},Qt::MiddleButton,Qt::MiddleButton);flush();
            const auto created=state->document().sheets.front().dimensions;require(created.size()==1,"Diameter not committed");
            const auto id=created.front().id;
            require(state->undo()&&state->document().sheets.front().dimensions.empty(),"Diameter Undo failed");
            require(state->redo()&&state->document().sheets.front().dimensions==created,"Diameter Redo lost identity");window.edit_workspace_document(document.document_id);flush();
            const double expected=std::hypot(target.diameter_offset->x,target.diameter_offset->y);
            require(std::abs(expected-60)<1e-7,"50.drwz fixture no longer has the expected 60 mm boss");
            for(const auto delta:std::array{QPointF(20,-15),QPointF(-35,25),QPointF(15,10)}) {
                const auto before=state->document().sheets.front().dimensions;
                const auto grip=window.annotation_handle_for_test(id,0,true);require(grip.has_value(),"Diameter label grip missing");
                pick(canvas,*grip);mouse(canvas,QEvent::MouseButtonPress,*grip,Qt::LeftButton,Qt::LeftButton);
                mouse(canvas,QEvent::MouseMove,*grip+delta,Qt::NoButton,Qt::LeftButton);
                mouse(canvas,QEvent::MouseButtonRelease,*grip+delta,Qt::LeftButton,Qt::NoButton);
                const auto after=state->document().sheets.front().dimensions;
                const auto measured=evaluate_drawing_dimension(*state->document().find_view(view.id),after.front());
                require(after!=before&&measured.state==MeasurementState::Resolved&&measured.presentations.front().kind==kernel::ViewerDimensionKind::Linear&&
                    measured.presentations.front().label_prefix=="⌀"&&std::abs(measured.presentations.front().value-expected)<1e-7,"Diameter drag lost its value or linear presentation");
                require(drawing_dimension_text(after.front(),measured.presentations.front())=="⌀60","Actual diameter text lost its symbol or value");
                require(state->undo()&&state->document().sheets.front().dimensions==before&&state->redo()&&state->document().sheets.front().dimensions==after,"Diameter drag Undo/Redo failed");
                window.edit_workspace_document(document.document_id);flush();
            }
            QTemporaryDir temporary;const auto saved=std::filesystem::path(temporary.path().toStdString())/"diameter.drwz";
            state->document().save(saved);const auto reopened=DrawingDocument::load(saved);
            const auto dimensions=state->document().sheets.front().dimensions;
            require(reopened.sheets.front().dimensions==dimensions&&evaluate_drawing_dimension(*reopened.find_view(view.id),dimensions.front()).state==MeasurementState::Resolved,"Native diameter reopen failed");
            auto precise=state->document();prepare_output_view(precise.sheets.front().views.front());
            require(!precise.sheets.front().views.front().output_source,"Precise view retained its interactive source");
            precise.save(saved);const auto detached=DrawingDocument::load(saved);
            const auto detached_value=evaluate_drawing_dimension(*detached.find_view(view.id),dimensions.front());
            require(detached_value.state==MeasurementState::Resolved&&std::abs(detached_value.presentations.front().value-60)<1e-7,"Precise native view needs external cylinder metadata");
            require(state->document().find_view(view.id)->measurement_geometry==geometry&&kernel::body_calculation_count.load()==calculations,"Diameter interaction changed/calculated the model");
            require(canvas->grab().save(QString("build/drawing-diameter-20261124/diameter-%1.png").arg(cases)),"Diameter screenshot failed");
            std::cout<<"Mouse diameter "<<expected<<" mm: Cancel, OK, repeated drags, Undo/Redo and reopen passed\n";
            ++cases;++sides;
        }
    }
    require(cases>=2,"Real Drawing did not provide two selectable cylinder sides");
    return 0;
}
