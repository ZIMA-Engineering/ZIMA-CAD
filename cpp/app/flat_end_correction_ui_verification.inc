static int verify_flat_end_correction_ui(QApplication& application,const std::filesystem::path& parent) {
    using namespace zima;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        QTemporaryDir isolated(QString::fromStdString((parent/"flat-end-ui-XXXXXX").string()));
        check(isolated.isValid(),"Cannot isolate Flat end GUI test");
        const auto directory=std::filesystem::path(isolated.path().toStdWString());
        for(std::size_t stage:{0u,2u})for(bool last:{false,true}) {
            auto part=document::PartDocument::create_default();document::BodyHistoryGraph graph;
            const auto body=graph.create_body("Flat end correction");part.set_body_history(graph);
            auto driver=sketcher::Sketch::create_default();
            static_cast<void>(driver.add_segment(0,0,20,0));static_cast<void>(driver.add_arc(20,5,20,0,25,5));
            static_cast<void>(driver.add_segment(25,5,25,25));static_cast<void>(driver.add_arc(20,25,25,25,20,30));
            static_cast<void>(driver.add_segment(20,30,0,30));
            auto profile=document::PartDocument::create_sheet_profile_container(driver.id);driver.owner_container_id=profile.id;
            profile.sheet_profile.width.symmetric=true;profile.sheet_profile.width.sides[0].length=10;
            profile.sheet_profile.thickness_override=true;profile.sheet_profile.thickness=1;
            auto prepared=workspace::prepare_sheet_profile(part,profile,driver);
            const auto flat_id=prepared.logical_children[stage];
            const auto sketch_id=prepared.document.find_container(flat_id)->flat.sketch_id;
            const auto dimension_id=sketch_id+":difference:end"+(last?":last":":first");
            kernel::OcctKernel kernel;auto cache=workspace::calculate_part_with_resolved_references(kernel,prepared.document);
            check(!cache.empty()&&cache.back().calculation_errors.empty(),"Flat end GUI fixture failed");
            const auto path=directory/("flat-"+std::to_string(stage)+(last?"-last.prtz":"-first.prtz"));
            prepared.document.save(path,cache);
            app::AssemblyWorkspaceWindow window(isolated.path());window.setAttribute(Qt::WA_DontShowOnScreen);
            window.resize(1400,1000);window.show();check(window.open_document_path(QString::fromStdWString(path.wstring())),"Cannot open Flat end fixture");flush();
            check(activate_test_body(application,window,body),"Cannot activate Flat end Body");
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Flat end View missing");
            double committed_value=0.;
            for(double value:{.75,-.25,1.}) {
                const bool cancel=value==1.;
                auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
                for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==flat_id&&(*it)->data(0,Qt::UserRole+3)=="part-container"){row=*it;break;}
                check(row,"Generated Flat Tree row missing");window.show_tree_item_properties(row);flush();
                auto* dialog=window.findChild<QDialog*>("flatPropertiesDialog");check(dialog&&dialog->isVisible(),"Generated Flat did not open its own Properties");
                auto* open=dialog->findChild<QPushButton*>("sketchOpenButton");check(open&&open->isEnabled(),"Generated Flat correction Sketch is disabled");open->click();flush();
                QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();flush();
                const auto* active=window.active_sketch_for_test();check(active&&active->id==sketch_id,"Wrong generated Flat Sketch opened");
                const auto initial=*std::ranges::find(active->dimensions,dimension_id,&sketcher::SketchDimension::id);
                const double expected_native=sketcher::dimension_value_from_input(initial,value,active->plane);
                const auto shown=std::ranges::find(view->mesh().dimensions,"dimension:"+dimension_id,[](const auto& d){return d.reference.semantic_key;});
                check(shown!=view->mesh().dimensions.end(),"Flat end correction is missing from View");
                viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;candidate.owner_id=sketch_id;
                candidate.semantic_key="dimension:"+dimension_id;candidate.geometry_index=std::size_t(shown-view->mesh().dimensions.begin());
                const auto pixel=view->candidate_dimension_label_position(candidate);check(pixel.has_value(),"Flat end dimension label missing");
                for(auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease,QEvent::MouseButtonDblClick,QEvent::MouseButtonRelease}) {
                    QMouseEvent mouse(type,*pixel,QPointF(view->mapToGlobal(*pixel)),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                        type==QEvent::MouseButtonPress||type==QEvent::MouseButtonDblClick?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
                    QApplication::sendEvent(view,&mouse);flush();
                }
                auto* editor=view->findChild<QLineEdit*>("inlineDimensionValueEdit");check(editor&&editor->isVisible(),"Flat end dimension editor missing");
                editor->setText(QString::number(value));QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);flush();
                active=window.active_sketch_for_test();const auto d=std::ranges::find(active->dimensions,dimension_id,&sketcher::SketchDimension::id);
                if(d==active->dimensions.end()||std::abs(d->value-expected_native)>1e-8) {
                    std::cerr<<"Flat GUI stage "<<stage<<" end "<<last<<" wanted "<<value<<" actual "<<(d==active->dimensions.end()?999:d->value)<<'\n';
                    for(auto* label:window.findChildren<QLabel*>())if(label->isVisible()&&!label->text().isEmpty())std::cerr<<label->text().toStdString()<<'\n';
                    throw std::runtime_error("Flat end numeric edit failed");
                }
                check(std::abs(active->find_point(d->second_point_id)->x-active->find_point(d->first_point_id)->x-expected_native)<1e-7,"Flat end coordinate equation failed");
                window.findChild<QAction*>("finishSketchAction")->trigger();flush();
                check(dialog->isVisible()&&!window.active_sketch_for_test(),"Flat correction cannot leave Sketcher");
                dialog->findChild<QDialogButtonBox*>()->button(cancel?QDialogButtonBox::Cancel:QDialogButtonBox::Ok)->click();flush();
                check(!window.findChild<QDialog*>("flatPropertiesDialog"),"Flat correction did not close Properties");
                check(window.execute_console_command("save").ok,"Flat correction save failed");
                auto saved=document::PartDocument::load(path,&cache);const auto& s=*std::ranges::find(saved.sketches,sketch_id,&sketcher::Sketch::id);
                const auto stored=*std::ranges::find(s.dimensions,dimension_id,&sketcher::SketchDimension::id);
                if(!cancel)committed_value=expected_native;
                check(std::abs(stored.value-committed_value)<1e-8&&!cache.empty()&&cache.back().calculation_errors.empty(),"Flat correction persistence/Cancel failed");
                if(!cancel) {
                    const auto expected=s.serialized();window.findChild<QAction*>("undoAction")->trigger();flush();window.findChild<QAction*>("redoAction")->trigger();flush();
                    check(window.execute_console_command("save").ok,"Flat correction Redo save failed");saved=document::PartDocument::load(path);
                    check(std::ranges::find(saved.sketches,sketch_id,&sketcher::Sketch::id)->serialized()==expected,"Flat correction Undo/Redo changed identity");
                }
            }
        }
        std::cout<<"Flat end GUI: master/intermediate, both ends, View dimensions, repeated edits, embedded exit, Cancel, native reopening and Undo/Redo passed\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<"Flat end GUI: "<<e.what()<<'\n';return 1;}
}
