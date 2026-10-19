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
        const auto cap_meridian=std::ranges::find_if(original.sketches,[](const auto& sketch){return sketch.name=="Cap dome meridian"||sketch.name=="DIN cap meridian"||sketch.name=="Closed end meridian";});
        const bool cap_nut=cap_meridian!=original.sketches.end();
        const bool csn_closed=cap_nut&&cap_meridian->name=="Closed end meridian";
        const auto weld_profile=std::ranges::find_if(original.sketches,[](const auto& sketch){return sketch.name=="Weld projection profile"||sketch.name=="Flange weld projection profile";});
        const bool weld=weld_profile!=original.sketches.end();
        const auto shaped_meridian=std::ranges::find_if(original.sketches,[](const auto& sketch){return sketch.name=="Flange meridian"||sketch.name=="Spherical collar meridian"||sketch.name=="Conical wheel seat meridian"||sketch.name=="Spherical washer meridian";});
        const bool shaped=shaped_meridian!=original.sketches.end();
        const bool hex_nut=!castle&&!knurl&&!cap_nut&&!weld&&original.sketches.front().name=="Hexagon profile";
        const auto profile=castle?*slot:knurl?*meridian:cap_nut?*cap_meridian:weld?*weld_profile:shaped?*shaped_meridian:original.sketches.front();
        const bool tab=profile.arcs.size()==3&&profile.circles.size()==1;
        check(castle||knurl||cap_nut||weld||hex_nut||shaped||tab||profile.circles.size()==2,"Library profile fixture missing");
        const auto path=directory/"spring-washer-gui.prtz";original.save(path,cache);
        app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
        check(window.open_document_path(QString::fromStdString(path.string())),"Washer GUI source did not open");flush();
        const auto supports=cap_nut||weld||hex_nut||shaped?profile.external_references:original.sketches.back().external_references;
        std::vector<std::pair<std::size_t,double>> edits;
        if(shaped) {
            const bool conical=profile.name=="Conical wheel seat meridian";
            for(std::size_t i=0;i<profile.dimensions.size();++i)if(profile.dimensions[i].driving&&
                (profile.dimensions[i].kind==sketcher::DimensionKind::Radius||(conical&&(profile.dimensions[i].kind==sketcher::DimensionKind::AngleBetween||profile.dimensions[i].kind==sketcher::DimensionKind::DistanceX))))
                for(double factor:{1.01,1.02,1.})edits.emplace_back(i,profile.dimensions[i].value*factor);
            check(!edits.empty(),"Shaped fastener GUI has no selected meridian driver");
        }
        else if(hex_nut) {
            for(std::size_t i=0;i<profile.dimensions.size();++i)if(profile.dimensions[i].driving&&profile.dimensions[i].kind==sketcher::DimensionKind::Diameter)
                for(double factor:{1.02,1.04,1.})edits.emplace_back(i,profile.dimensions[i].value*factor);
            check(edits.size()==3,"Hexagon nut GUI must exercise its width driver");
        }
        else if(knurl||cap_nut) {
            for(std::size_t i=0;i<profile.dimensions.size();++i) {
                const auto& d=profile.dimensions[i];if(!d.driving)continue;
                const auto* a=profile.find_point(d.first_point_id);const auto* b=profile.find_point(d.second_point_id);
                const bool height=d.kind==sketcher::DimensionKind::DistanceY&&a&&b&&std::abs(a->x)<1e-8&&std::abs(b->x)<1e-8;
                const bool outside=d.kind==sketcher::DimensionKind::DistanceX&&a&&b&&std::abs(a->x)<1e-8&&std::abs(a->y)<1e-8&&std::abs(b->y)>1e-8;
                if((knurl&&(height||outside))||d.kind==sketcher::DimensionKind::Radius||
                    (csn_closed&&(d.kind==sketcher::DimensionKind::DistanceX||d.kind==sketcher::DimensionKind::AngleBetween)))
                    for(double factor:{1.02,1.04,1.})edits.emplace_back(i,d.value*factor);
            }
            check(edits.size()==(csn_closed?6:cap_nut?3:9),"Nut GUI must exercise its selected meridian drivers");
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
            if(cap_nut||weld||shaped) {
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
                check(exposed,"Cap dimension text has no directly selectable exposed position");
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
            edit->setText(QString::number(value,'g',15));QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(edit,&enter);flush();
            const auto measured=view->candidate_dimension_value(candidate);
            if(!measured||std::abs(*measured-value)>=1e-8) {
                window.grab().save(QString::fromStdString((directory/"dimension-value-failure-window.png").string()));
                std::cerr<<"Library GUI dimension "<<index<<" expected="<<value<<" actual="<<(measured?*measured:-1)<<'\n';
            }
            check(measured&&std::abs(*measured-value)<1e-8,"Washer GUI diameter edit rejected");
            window.findChild<QAction*>("undoAction")->trigger();flush();window.findChild<QAction*>("redoAction")->trigger();flush();
            check(view->candidate_dimension_value(candidate)&&std::abs(*view->candidate_dimension_value(candidate)-value)<1e-8,"Washer diameter Undo/Redo failed");
            if(csn_closed&&index==edits.back().first&&std::abs(value-profile.dimensions[index].value)<1e-8) {
                const auto live_sketch=[&] {
                    // An owned profile is a transient Properties draft until
                    // Finish/OK. Inspect its actual viewer packet; Save must
                    // not publish that pending draft to the native document.
                    auto current=profile;
                    for(auto& point:current.points) {
                        const auto found=std::ranges::find_if(view->mesh().points,[&](const auto& p){return p.reference.owner_id==profile.id&&p.reference.semantic_key=="point:"+point.id;});
                        check(found!=view->mesh().points.end(),"Closed nut draft point missing");
                        const auto q=profile.local_point(found->position);point.x=q[0];point.y=q[1];
                    }
                    for(auto& d:current.dimensions)for(std::size_t i=0;i<view->mesh().dimensions.size();++i)
                        if(view->mesh().dimensions[i].reference.owner_id==profile.id&&view->mesh().dimensions[i].reference.semantic_key=="dimension:"+d.id) {
                            viewer::ViewerCandidate c;c.kind=viewer::CandidateKind::Dimension;c.owner_id=profile.id;c.semantic_key="dimension:"+d.id;c.geometry_index=i;
                            const auto value=view->candidate_dimension_value(c);check(value.has_value(),"Closed nut draft dimension missing");d.value=*value;
                        }
                    const auto& first=current.segments.front();const auto* A=current.find_point(first.first_point_id);const auto* C=current.find_point(first.second_point_id);const auto* B=current.find_point(current.segments[1].second_point_id);
                    check(std::abs(A->y-B->y)<1e-8&&std::abs(B->x-C->x)<1e-8,"Closed nut draft H/V equation");
                    const auto& angle=*std::ranges::find(current.dimensions,sketcher::DimensionKind::AngleBetween,&sketcher::SketchDimension::kind);
                    check(std::abs(std::atan2(C->y-A->y,C->x-A->x)*180/std::acos(-1.)-angle.value)<1e-7,"Closed nut draft angle equation");
                    return current;
                };
                const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Closed nut GUI ray missing");const auto q=profile.intersect_ray(ray->first,ray->second);check(q.has_value(),"Closed nut GUI camera misses Sketch");return *q;};
                const auto a=local({0,0}),b=local({100,0}),c=local({0,100});const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
                const auto screen=[&](double x,double y){return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);};
                const auto mouse=[&](QEvent::Type type,QPointF p,Qt::MouseButton button,Qt::MouseButtons buttons){QMouseEvent e(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&e);flush();};
                for(double pixels:{30.,-30.,40.}) {
                    const auto previous=live_sketch();const auto point=previous.segments.front().second_point_id;const auto p=*previous.find_point(point);
                    // Use real screen travel beyond Qt's drag threshold at
                    // every model scale, rather than a sub-threshold mm step.
                    const double delta=pixels*std::abs(det)/std::hypot(ux,vx);
                    const auto from=screen(p.x,p.y),to=screen(p.x,p.y+delta);view->clear_selection();const auto offered=view->selection_candidates_at(from);
                    check(!offered.empty()&&offered.front().semantic_key=="point:"+point,"Closed nut handle is not first in ordinary picker");
                    mouse(QEvent::MouseMove,from,Qt::NoButton,Qt::NoButton);mouse(QEvent::MouseButtonPress,from,Qt::LeftButton,Qt::LeftButton);
                    mouse(QEvent::MouseMove,to,Qt::NoButton,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,to,Qt::LeftButton,Qt::NoButton);
                    auto moved=live_sketch();if(std::hypot(moved.find_point(point)->x-p.x,moved.find_point(point)->y-p.y)<=1e-3)
                        window.grab().save(QString::fromStdString((directory/"closed-nut-drag-failure.png").string()));
                    check(std::hypot(moved.find_point(point)->x-p.x,moved.find_point(point)->y-p.y)>1e-3,"Free closed nut handle did not move");
                    const auto status=moved.solve();check(status.maximum_residual<1e-8&&status.remaining_degrees_of_freedom==0,"Dragged closed nut violated equations");
                    check(moved.external_references==previous.external_references,"Closed nut GUI drag changed external support");
                    window.findChild<QAction*>("undoAction")->trigger();flush();check(live_sketch().points==previous.points,"Closed nut drag Undo");
                    window.findChild<QAction*>("redoAction")->trigger();flush();check(live_sketch().points==moved.points,"Closed nut drag Redo");
                    window.findChild<QAction*>("undoAction")->trigger();flush();
                }
            }
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* d:window.findChildren<QDialog*>())if(d->isVisible()&&(dynamic_cast<app::SketchPropertiesDialog*>(d)||dynamic_cast<app::PrimitivePropertiesDialog*>(d)))
                d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
            flush();window.findChild<QAction*>("regenerateDocumentAction")->trigger();flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            std::vector<kernel::BodyResult> bodies;const auto saved=document::PartDocument::load(path,&bodies);
            const auto saved_profile=std::ranges::find(saved.sketches,profile.id,&sketcher::Sketch::id);
            check(saved_profile!=saved.sketches.end()&&std::abs((weld?std::abs(saved_profile->dimensions[index].value):saved_profile->dimensions[index].value)-value)<1e-8,"Library profile GUI dimension did not persist");
            if(weld)check(std::signbit(saved_profile->dimensions[index].value)==std::signbit(profile.dimensions[index].value)&&
                saved_profile->dimensions[index].solution_side==profile.dimensions[index].solution_side,"Weld projection GUI edit reversed its persisted side");
            auto cutter=castle||knurl||cap_nut||weld||hex_nut||shaped?*saved_profile:saved.sketches.back();
            if(!tab&&!castle&&!knurl&&!cap_nut&&!weld&&!hex_nut&&!shaped)check(cutter.external_references.size()==2,"Washer GUI lost an external end-plane support");
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
        std::cout<<(hex_nut?"Hexagon nut GUI: three width mouse edits, Undo/Redo, regeneration, native save/reopen and retained profile identities passed\n":csn_closed?"CSN closed nut GUI: six coordinate/angle mouse edits, independent Origin axis, retained supports, Undo/Redo, regeneration and native save/reopen passed\n":cap_nut?"Cap nut GUI: three radius mouse edits, independent Origin axis, retained supports, Undo/Redo, regeneration and native save/reopen passed\n":knurl?"Knurled nut GUI: nine height/diameter/radius mouse edits, Undo/Redo, regeneration and native save/reopen passed\n":castle?"Castle nut GUI: repeated slot-width and floor-height mouse edits, Undo/Redo, regeneration and native save/reopen passed\n":tab?"Tab washer GUI: fifteen mouse dimension edits, Undo/Redo, regeneration and native save/reopen passed\n":
            "Spring washer GUI: six mouse diameter edits, Undo/Redo, regeneration, native save/reopen and both external end-plane contacts passed\n");return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
