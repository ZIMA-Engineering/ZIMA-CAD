int verify_spring_washer_ui(QApplication& application,const std::filesystem::path& directory,const std::filesystem::path& source) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        std::vector<kernel::BodyResult> cache;const auto original=document::PartDocument::load(source,&cache);
        const auto slot=std::ranges::find_if(original.sketches,[](const auto& sketch){return sketch.name=="Crown slot profile 0";});
        const bool castle=slot!=original.sketches.end();
        const auto meridian=std::ranges::find_if(original.sketches,[](const auto& sketch){return sketch.name=="Knurled nut meridian";});
        const bool knurl=meridian!=original.sketches.end();
        const auto cap_meridian=std::ranges::find_if(original.sketches,[](const auto& sketch){return sketch.name=="Cap dome meridian"||sketch.name=="DIN cap meridian";});
        const bool cap_nut=cap_meridian!=original.sketches.end();
        const auto weld_profile=std::ranges::find_if(original.sketches,[](const auto& sketch){return sketch.name=="Weld projection profile"||sketch.name=="Flange weld projection profile";});
        const bool weld=weld_profile!=original.sketches.end();
        const auto profile=castle?*slot:knurl?*meridian:cap_nut?*cap_meridian:weld?*weld_profile:original.sketches.front();
        const bool tab=profile.arcs.size()==3&&profile.circles.size()==1;
        check(castle||knurl||cap_nut||weld||tab||profile.circles.size()==2,"Library profile fixture missing");
        const auto path=directory/"spring-washer-gui.prtz";original.save(path,cache);
        app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
        check(window.open_document_path(QString::fromStdString(path.string())),"Washer GUI source did not open");flush();
        const auto supports=cap_nut||weld?profile.external_references:original.sketches.back().external_references;
        std::vector<std::pair<std::size_t,double>> edits;
        if(knurl||cap_nut) {
            for(std::size_t i=0;i<profile.dimensions.size();++i) {
                const auto& d=profile.dimensions[i];if(!d.driving)continue;
                const auto* a=profile.find_point(d.first_point_id);const auto* b=profile.find_point(d.second_point_id);
                const bool height=d.kind==sketcher::DimensionKind::DistanceY&&a&&b&&std::abs(a->x)<1e-8&&std::abs(b->x)<1e-8;
                const bool outside=d.kind==sketcher::DimensionKind::DistanceX&&a&&b&&std::abs(a->x)<1e-8&&std::abs(a->y)<1e-8&&std::abs(b->y)>1e-8;
                if((knurl&&(height||outside))||d.kind==sketcher::DimensionKind::Radius)
                    for(double factor:{1.02,1.04,1.})edits.emplace_back(i,d.value*factor);
            }
            check(edits.size()==(cap_nut?3:9),"Nut GUI must exercise its selected meridian drivers");
        }
        else if(tab||castle||weld) {
            std::string floor_point;
            for(const auto& constraint:profile.constraints)
                if(constraint.kind==sketcher::ConstraintKind::PointOnCircle)floor_point=constraint.first_point_id;
            for(std::size_t i=0;i<profile.dimensions.size();++i) {
                const auto& dimension=profile.dimensions[i];
                if(castle&&dimension.kind!=sketcher::DimensionKind::Diameter&&dimension.first_point_id!=floor_point)continue;
                // Projected distance labels edit a positive magnitude; the
                // persisted signed value separately retains the contact side.
                for(double factor:{1.02,1.04,1.})edits.emplace_back(i,(weld?std::abs(dimension.value):dimension.value)*factor);
            }
            check(!edits.empty(),"Library profile has no editable drivers");
        }
        else edits={{1,3.3},{1,2.9},{1,3.1},{0,5.3},{0,4.9},{0,5.1}};
        for(const auto [index,value]:edits) {
            check(activate_test_body(application,window,original.body_history.active_body_id()),"Washer Body activation failed");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==profile.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"Washer annulus Sketch Tree row missing");window.show_tree_item_properties(row);flush();
            if(auto* open=window.findChild<QPushButton*>("sketchOpenButton")){open->click();flush();}
            QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Washer GUI View missing");
            viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;candidate.owner_id=profile.id;
            candidate.semantic_key="dimension:"+profile.dimensions[index].id;
            for(std::size_t i=0;i<view->mesh().dimensions.size();++i)if(view->mesh().dimensions[i].reference.owner_id==candidate.owner_id&&view->mesh().dimensions[i].reference.semantic_key==candidate.semantic_key)candidate.geometry_index=i;
            auto label=view->candidate_dimension_label_position(candidate);check(label.has_value(),"Washer diameter label missing");
            if(cap_nut||weld) {
                // The crown's H glyph overlaps the lower end of the rotated
                // R label. Exercise an exposed part of the same visible text,
                // without bypassing the ordinary shared picking order.
                const auto center=*label;bool exposed=false;
                for(int dy:{0,-3,-6,-9,-12}) {
                    const auto point=center+QPoint(0,dy);
                    const auto offered=view->selection_candidates_at(point);
                    if(!offered.empty()&&offered.front().kind==candidate.kind&&
                        offered.front().owner_id==candidate.owner_id&&offered.front().semantic_key==candidate.semantic_key) {
                        label=point;exposed=true;break;
                    }
                }
                check(exposed,"Cap radius text has no directly selectable exposed position");
            }
            // A real double click includes its first press/release. That
            // press selects this exact label before the double-click handler
            // deliberately retains the confirmed identity.
            QMouseEvent press(QEvent::MouseButtonPress,QPointF(*label),QPointF(view->mapToGlobal(*label)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(view,&press);flush();
            QMouseEvent release(QEvent::MouseButtonRelease,QPointF(*label),QPointF(view->mapToGlobal(*label)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view,&release);flush();
            QMouseEvent twice(QEvent::MouseButtonDblClick,QPointF(*label),QPointF(view->mapToGlobal(*label)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(view,&twice);flush();
            auto* edit=view->findChild<QLineEdit*>("inlineDimensionValueEdit");
            if(!edit) {
                view->grabFramebuffer().save(QString::fromStdString((directory/"dimension-editor-failure-view.png").string()));
                window.grab().save(QString::fromStdString((directory/"dimension-editor-failure-window.png").string()));
                std::cerr<<"Dimension label "<<index<<" at "<<label->x()<<','<<label->y()<<", View "<<view->width()<<'x'<<view->height()<<'\n';
            }
            check(edit,"Washer diameter mouse editor missing");
            edit->setText(QString::number(value));QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(edit,&enter);flush();
            const auto measured=view->candidate_dimension_value(candidate);
            if(!measured||std::abs(*measured-value)>=1e-8) {
                window.grab().save(QString::fromStdString((directory/"dimension-value-failure-window.png").string()));
                std::cerr<<"Library GUI dimension "<<index<<" expected="<<value<<" actual="<<(measured?*measured:-1)<<'\n';
            }
            check(measured&&std::abs(*measured-value)<1e-8,"Washer GUI diameter edit rejected");
            window.findChild<QAction*>("undoAction")->trigger();flush();window.findChild<QAction*>("redoAction")->trigger();flush();
            check(view->candidate_dimension_value(candidate)&&std::abs(*view->candidate_dimension_value(candidate)-value)<1e-8,"Washer diameter Undo/Redo failed");
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* d:window.findChildren<QDialog*>())if(d->isVisible()&&(dynamic_cast<app::SketchPropertiesDialog*>(d)||dynamic_cast<app::PrimitivePropertiesDialog*>(d)))
                d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
            flush();window.findChild<QAction*>("regenerateDocumentAction")->trigger();flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            std::vector<kernel::BodyResult> bodies;const auto saved=document::PartDocument::load(path,&bodies);
            const auto saved_profile=std::ranges::find(saved.sketches,profile.id,&sketcher::Sketch::id);
            check(saved_profile!=saved.sketches.end()&&std::abs((weld?std::abs(saved_profile->dimensions[index].value):saved_profile->dimensions[index].value)-value)<1e-8,"Library profile GUI dimension did not persist");
            if(weld)check(std::signbit(saved_profile->dimensions[index].value)==std::signbit(profile.dimensions[index].value)&&
                saved_profile->dimensions[index].solution_side==profile.dimensions[index].solution_side,"Weld projection GUI edit reversed its persisted side");
            auto cutter=castle||knurl||cap_nut||weld?*saved_profile:saved.sketches.back();
            if(!tab&&!castle&&!knurl&&!cap_nut&&!weld)check(cutter.external_references.size()==2,"Washer GUI lost an external end-plane support");
            for(const auto& before:supports)check(std::ranges::any_of(cutter.external_references,[&](const auto& after){return after.id==before.id&&after.source_semantic_key==before.source_semantic_key&&!after.broken;}),"Washer GUI changed external edge identity");
            const auto solved=cutter.solve();check(solved.remaining_degrees_of_freedom==0&&solved.maximum_residual<1e-8,"Washer GUI cutter failed after diameter change");
            for(const auto& body:bodies)check(body.calculation_errors.empty(),"Washer GUI regeneration left a failed feature");
            for(const auto& c:cutter.constraints)if(c.kind==sketcher::ConstraintKind::PointOnLine) {
                const auto r=std::ranges::find(cutter.external_references,c.geometry_id,&sketcher::SketchExternalReference::id);
                if(r==cutter.external_references.end())continue;
                const auto line=sketcher::external_reference_line(*r);const auto* point=cutter.find_point(c.first_point_id);
                check(line&&std::abs((point->x-line->first[0])*line->second[1]-(point->y-line->first[1])*line->second[0])/std::hypot(line->second[0],line->second[1])<1e-8,
                    "Washer GUI cutter corner did not follow its end plane");
            }
        }
        if(cap_nut&&cap_meridian->name=="DIN cap meridian") {
            const auto hole=std::ranges::find(original.history,document::FeatureKind::Thread,&document::HistoryContainer::feature_kind);
            check(hole!=original.history.end(),"DIN cap opening missing");
            const auto saved_opening=[&] {
                window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
                return *document::PartDocument::load(path).find_container(hole->id);
            };
            for(const double factor:{.9,.96,1.}) {
                const auto before=saved_opening();
                auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
                for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==hole->id&&
                    (*i)->data(0,Qt::UserRole+3)=="part-container"){row=*i;break;}
                check(row,"DIN cap opening Tree row missing");window.show_tree_item_properties(row);flush();
                QDialog* dialog{};
                for(auto* d:window.findChildren<QDialog*>())if(d->isVisible()&&d->findChild<QDoubleSpinBox*>("threadLengthForward")){dialog=d;break;}
                check(dialog,"DIN cap Opening Properties missing");
                const double length=hole->thread.length_forward*factor;
                dialog->findChild<QDoubleSpinBox*>("threadLengthForward")->setValue(length);flush();
                dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
                const auto after=saved_opening();
                check(std::abs(after.thread.length_forward-length)<1e-8&&
                    std::abs(after.thread.bore_length-std::min(hole->thread.bore_length,length+hole->thread.pitch))<1e-8,
                    "DIN cap GUI did not update its derived bore before OK");
                check(after.hole==hole->hole&&after.placement==hole->placement,"DIN cap GUI changed profile/placement identities");
                window.findChild<QAction*>("undoAction")->trigger();flush();check(saved_opening()==before,"DIN cap GUI derived bore Undo failed");
                window.findChild<QAction*>("redoAction")->trigger();flush();check(saved_opening()==after,"DIN cap GUI derived bore Redo failed");
            }
        }
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());
        view->grabFramebuffer().save(QString::fromStdString((directory/"spring-washer-gui.png").string()));
        std::cout<<(cap_nut?"Cap nut GUI: three radius mouse edits, independent Origin axis, retained supports, Undo/Redo, regeneration and native save/reopen passed\n":knurl?"Knurled nut GUI: nine height/diameter/radius mouse edits, Undo/Redo, regeneration and native save/reopen passed\n":castle?"Castle nut GUI: repeated slot-width and floor-height mouse edits, Undo/Redo, regeneration and native save/reopen passed\n":tab?"Tab washer GUI: fifteen mouse dimension edits, Undo/Redo, regeneration and native save/reopen passed\n":
            "Spring washer GUI: six mouse diameter edits, Undo/Redo, regeneration, native save/reopen and both external end-plane contacts passed\n");return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
