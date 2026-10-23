// Full-window regression: the real View picker owns each target click.
Q_NEVER_INLINE static int verify_ordered_up_to_ui(QApplication& application,
        AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    try {
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        auto part=document::PartDocument::create_default();document::BodyHistoryGraph graph;
        static_cast<void>(graph.create_body("Ordered GUI"));std::vector<std::string> sources;
        const std::array<double,3> slopes{{1,0,-.5}},heights{{10,12,15}},first{{-2,2.3,6.3}},last{{1.7,5.7,10}};
        for(unsigned row=0;row<3;++row) {
            auto target=document::create_general_surface();target.name="Ordered target "+std::to_string(row+1);
            const auto p=[&](double x,double y){return kernel::Vec3{x,y,slopes[row]*x+heights[row]};};
            const std::array<kernel::Vec3,4> corners{{p(first[row],-2),p(last[row],-2),p(last[row],5),p(first[row],5)}};
            for(unsigned edge=0;edge<4;++edge) {
                target.general_surface.boundaries[edge]=document::create_general_surface_curve(target);
                auto& curve=*target.general_surface.boundaries[edge].curve;curve.curve_type=document::Curve3DType::Polyline;curve.curve_points.clear();
                for(const auto point:{corners[edge],corners[(edge+1)%4]}) {
                    auto owned=document::create_owned_point(curve.id);owned.origin=point;curve.curve_points.push_back(owned);
                }
            }
            sources.push_back(target.id);graph.insert({document::PartHistoryKind::Feature,target.id});part.history.push_back(std::move(target));
        }
        auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(-2,0,10,3));
        auto feature=document::PartDocument::create_feature_container(sketch.id);feature.name="Ordered GUI feature";
        sketch.owner_container_id=feature.id;feature.feature.sides[0].length=8;
        part.sketches={sketch};part.history.push_back(feature);graph.insert({document::PartHistoryKind::Feature,feature.id});
        part.set_body_history(graph);part.resolve_constructions();
        kernel::OcctKernel engine;auto geometry=workspace::calculate_part_with_resolved_references(engine,part);
        const auto file=directory/("ordered-feature-ui-"+QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()+".prtz");
        part.save(file,geometry);window.showMaximized();flush();
        check(window.open_document_path(QString::fromStdString(file.string())),"Ordered GUI fixture did not open");flush();
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));
        auto* tree=window.findChild<QTreeWidget*>("documentTree");check(view&&tree,"Ordered GUI View or Tree is missing");
        const auto run=[&](const char* command,commands::Json arguments=commands::Json::object()) {
            auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",std::move(arguments)}}.dump()));
            if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.message);flush();return result.data;
        };
        const auto get=[&]{return run("feature.get",{{"container",feature.id}});};
        const auto original=get();
        const auto edit=[&]() {
            QTreeWidgetItem* item=nullptr;
            for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==feature.id){item=*it;break;}
            check(item,"Ordered Feature Tree row is missing");window.show_tree_item_properties(item);flush();
            auto* dialog=dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("featurePropertiesDialog"));
            check(dialog&&dialog->isVisible(),"Ordered Feature Properties did not open");return dialog;
        };
        const auto mouse=[&](QWidget* widget,QPointF point,Qt::MouseButton button) {
            const auto global=widget->mapToGlobal(point.toPoint());
            QMouseEvent move(QEvent::MouseMove,point,global,Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(widget,&move);flush();
            QMouseEvent press(QEvent::MouseButtonPress,point,global,button,button,Qt::NoModifier);QApplication::sendEvent(widget,&press);
            QMouseEvent release(QEvent::MouseButtonRelease,point,global,button,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(widget,&release);flush();
        };
        const auto select=[&](PrimitivePropertiesDialog* dialog) {
            dialog->findChild<QComboBox*>("featureSideEnd0")->setCurrentIndex(1);flush();view->fit_all();flush();
            auto* limits=static_cast<FeatureLimitTable*>(dialog->findChild<QWidget*>("featureSideLimits0"));auto* table=limits->table();
            for(unsigned row=0;row<3;++row) {
                table->scrollToItem(table->item(row,3));
                for(auto* ancestor=table->parentWidget();ancestor;ancestor=ancestor->parentWidget())
                    if(auto* scroll=qobject_cast<QScrollArea*>(ancestor))scroll->ensureWidgetVisible(table);
                flush();mouse(table->viewport(),table->visualItemRect(table->item(row,3)).center(),Qt::LeftButton);
                std::optional<QPointF> pixel;
                for(int y=12;y<view->height()-12&&!pixel;y+=7)for(int x=12;x<view->width()-12&&!pixel;x+=7) {
                    const auto hits=view->selection_candidates_at({double(x),double(y)});
                    if(!hits.empty()&&hits.front().owner_id==sources[row]&&hits.front().kind==viewer::CandidateKind::Face)pixel=QPointF(x,y);
                }
                check(pixel.has_value(),"Ordered source face was not offered by the real View picker");
                mouse(view,*pixel,Qt::LeftButton);
                const auto pending=dialog->pending_value();
                check(pending.feature.sides[0].targets.size()==row+1&&pending.feature.sides[0].targets[row].reference.owner_id==sources[row],
                    "View target click replaced another row or reached placement input");
            }
            check(dialog->findChild<QLabel*>("featureUpToDraftNotice")->isVisible()&&!view->transient_edges().empty(),
                "Ordered draft did not expose its labelled profile preview");
            const auto draft=dialog->pending_value();
            auto* eye=table->cellWidget(0,6)->findChild<QToolButton*>();mouse(eye,eye->rect().center(),Qt::LeftButton);
            mouse(table->viewport(),table->visualItemRect(table->item(1,3)).center(),Qt::LeftButton);
            check(dialog->highlighted_reference_entries().size()==1,"Arming another target cleared its independent eye");
            mouse(view,{view->width()*.5,view->height()*.5},Qt::MiddleButton);
            check(!limits->active_row()&&dialog->highlighted_reference_entries().empty()&&dialog->pending_value().feature==draft.feature,
                "Short middle click deleted data, retained entry or committed the Feature");
        };
        auto* dialog=edit();select(dialog);check(get().at("revision")==original.at("revision"),"Draft picking committed the native document");
        dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(get().at("parameters")==original.at("parameters")&&get().at("revision")==original.at("revision"),"Ordered target Cancel changed the native Feature");
        dialog=edit();select(dialog);dialog->findChild<QDoubleSpinBox*>("featureSideCorrection0")->setValue(1.5);
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(get().at("parameters").at("sides")[0].at("targets").size()==3,"Ordered GUI OK lost its target chain");run("save");
        std::vector<kernel::BodyResult> saved_geometry;const auto saved=document::PartDocument::load(file,&saved_geometry);
        auto measured=saved_geometry.back();engine.ensure_body_properties(measured);
        check(std::abs(measured.volume-450.)<1e-5,"Ordered GUI result disagrees with the independent volume");
        document::DocumentSession session(saved,saved_geometry);const auto wire=authored_feature_wire(session,feature.id);
        check(!wire.empty(),"Accepted ordered Feature has no original operand wire");
        for(const auto& edge:wire)for(const auto p:edge.points)
            check(edge.reference.owner_id==feature.id&&p.x>=-2-1e-6&&p.x<=10+1e-6&&p.y>=-1e-6&&p.y<=3+1e-6,
                "Accepted original wire contains a target's or another body's edges");
        const auto accepted=get();dialog=edit();dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(get().at("revision")==accepted.at("revision"),"Unchanged ordered GUI OK created an Undo transaction");
        window.show_parameter_dimensions(feature.id);flush();check(get().at("revision")==accepted.at("revision"),"Correction inspection changed the document");
        bool correction=false;
        for(std::size_t index=0;index<10000;++index) {
            viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;candidate.geometry_index=index;
            const auto source=view->dimension_source(candidate);if(!source)break;
            if(source->reference.owner_id==feature.id&&source->reference.semantic_key=="parameter:side0_up_to_length_correction") {
                check(source->label_only&&std::abs(source->value-1.5)<1e-9,"Correction annotation has the wrong value or geometry");correction=true;
            }
        }
        check(correction,"Ordered correction is absent from ordinary parameter inspection");
        run("undo");check(get().at("parameters")==original.at("parameters"),"Ordered GUI Undo lost its original definition");
        run("redo");check(get().at("parameters")==accepted.at("parameters"),"Ordered GUI Redo lost retained sides or correction");
        view->grabFramebuffer().save(QString::fromStdString((directory/"ordered-feature-ui-result.png").string()));
        std::cout<<"Ordered Feature GUI: View row picking, independent eyes, short MMB, Cancel, exact OK, original operand wire, unchanged OK, correction inspection and Undo/Redo passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
