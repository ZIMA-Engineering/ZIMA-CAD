int verify_sheet_source_edit_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        kernel::OcctKernel kernel;std::vector<kernel::BodyResult> cache;
        auto part=document::PartDocument::load(qEnvironmentVariable("ZIMA_VERIFY_SHEET_EXCHANGE").toStdString(),&cache);
        cache=workspace::calculate_part_with_resolved_references(kernel,part,&cache);
        const auto parent=std::ranges::find_if(part.history,[](const auto& f){return f.feature_kind==document::FeatureKind::SheetFromBody;});
        check(parent!=part.history.end(),"Source-edit GUI fixture has no conversion");
        const auto parent_id=parent->id,body=part.body_owner_for_object(parent_id)->scope.id;
        const auto children=parent->sheet_from_body.generated_features;
        std::string owner;
        for(auto f=part.history.begin();f!=parent;++f)
            if(f->feature_kind==document::FeatureKind::Feature&&f->feature.type!=document::FeatureType::Sketch)owner=f->id;
        check(!owner.empty(),"Source-edit GUI fixture has no source extrusion");
        const auto cursor=part.body_history.find(body)->cursor;
        const auto corrections=[&](const document::PartDocument& current) {
            nlohmann::json values=nlohmann::json::object();
            const auto record=[&](const sketcher::Sketch& sketch){for(const auto& d:sketch.dimensions)
                values[d.id]={d.value,std::signbit(d.value),d.solution_side,d.driving,d.locked};};
            for(const auto& id:children) {
                const auto* feature=current.find_container(id);
                for(const auto& sketch:current.sketches)if(sketch.owner_container_id==id)record(sketch);
                if(feature->feature_kind==document::FeatureKind::Bend)
                    record(sketcher::Sketch::from_serialized(feature->bend.auxiliary_sketches[1]));
            }
            return values;
        };
        const auto expected_corrections=corrections(part);
        const auto path=directory/"sheet-source-edit.prtz";part.save(path,cache);
        app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1600,1000);window.show();
        check(window.open_document_path(QString::fromStdString(path.string())),"Source-edit GUI fixture did not open");flush();
        auto* tree=window.findChild<QTreeWidget*>("documentTree");
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));
        check(tree&&view,"Source-edit GUI View or Tree is missing");
        const auto save=[&]{window.findChild<QAction*>("saveDocumentAction")->trigger();flush();return document::PartDocument::load(path);};
        const auto authored=[](const document::PartDocument& current){auto state=current.serialized();state.erase("dimension_identifiers");state.erase("calculation_definition_fingerprint");return state;};
        const auto editor=[&]() {
            window.finish_parameter_dimensions();flush();
            QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==owner){row=*it;break;}
            check(row,"Source-edit GUI Tree row is missing");
            tree->setCurrentItem(row);tree->itemDoubleClicked(row,0);flush();
            const auto dimension=std::ranges::find_if(view->mesh().dimensions,[&](const auto& d){
                return d.reference.owner_id==owner&&d.reference.semantic_key=="parameter:side0_length";});
            check(dimension!=view->mesh().dimensions.end(),"Source extrusion dimension is not inspectable");
            viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;candidate.owner_id=owner;
            candidate.semantic_key="parameter:side0_length";candidate.geometry_index=std::size_t(dimension-view->mesh().dimensions.begin());
            const auto pixel=view->candidate_dimension_label_position(candidate);
            check(pixel.has_value(),"Source dimension has no screen label");
            const auto identifier=save().dimension_identifiers.identifier(owner,"parameter:side0_length");
            for(auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease,QEvent::MouseButtonDblClick,QEvent::MouseButtonRelease}) {
                QMouseEvent mouse(type,*pixel,QPointF(view->mapToGlobal(*pixel)),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                    type==QEvent::MouseButtonPress||type==QEvent::MouseButtonDblClick?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(view,&mouse);flush();
            }
            auto* field=view->findChild<QLineEdit*>("inlineDimensionValueEdit");
            check(field&&field->isVisible(),"Source dimension double-click did not open the editor");
            check(field->property("dimensionIdentifier").toString().toStdString()==identifier,"Mouse selected another source dimension");
            return field;
        };
        view->set_view_direction({1,-1,1});view->fit_all();flush();
        QEventLoop camera;QTimer::singleShot(950,&camera,&QEventLoop::quit);camera.exec();
        const double original=part.find_container(owner)->feature.sides[0].length;
        for(const double requested:{original+10.,original}) {
            const auto before=authored(save());
            auto* field=editor();field->setText(QString::number(requested));
            const auto start=std::chrono::steady_clock::now();
            QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(field,&enter);flush();
            std::cout<<"source_dimension_gui="<<requested<<" seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<std::endl;
            field=view->findChild<QLineEdit*>("inlineDimensionValueEdit");
            check(!field||!field->isVisible(),"Submitted source editor remained open");
            const auto current=save();
            check(current.find_container(owner)->feature.sides[0].length==requested,"Source GUI dimension was not committed");
            check(current.body_history.find(body)->cursor==cursor,"Source edit moved the history cursor past Unbend");
            check(current.find_container(parent_id)->sheet_from_body.generated_features==children&&corrections(current)==expected_corrections,
                "Source GUI edit lost sheet identities or corrections");
            check(!view->mesh().triangles.empty(),"Source GUI edit removed displayed material");
            for(const auto& b:current.find_container(parent_id)->sheet_from_body.child_bindings)
                check(b.detached||b.references_valid,"Source GUI edit lost a source region");
            const auto after=authored(current);
            window.grab().save(QString::fromStdWString((directory/("source-length-"+std::to_string(int(requested))+".png")).wstring()));
            window.findChild<QAction*>("undoAction")->trigger();flush();check(authored(save())==before,"Source GUI Undo failed");
            window.findChild<QAction*>("redoAction")->trigger();flush();check(authored(save())==after,"Source GUI Redo failed");
        }
        const auto before=authored(save());auto* field=editor();const auto count=kernel::body_calculation_count.load();
        QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(field,&enter);flush();
        check(kernel::body_calculation_count.load()==count&&authored(save())==before,"Unchanged source GUI value recalculated or changed the model");
        field=editor();field->setText(QString::number(original+20.));
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(field,&escape);flush();
        check(kernel::body_calculation_count.load()==count&&authored(save())==before,"Cancelled source GUI value changed the model");
        std::cout<<"Source GUI actual dimension clicks, repeated values, history cursor, corrections, unchanged OK, Cancel and Undo/Redo passed"<<std::endl;
        window.close();flush();return 0;
    } catch(const std::exception& error) {std::cerr<<"Source-edit GUI verification: "<<error.what()<<std::endl;return 1;}
}
