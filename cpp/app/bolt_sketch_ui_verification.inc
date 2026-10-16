int verify_bolt_sketch_ui(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        const auto source=std::filesystem::path(qEnvironmentVariable("ZIMA_BOLT_SOURCE").toStdString());
        const auto path=directory/"bolt-sketch-regression.prtz";std::filesystem::copy_file(source,path,std::filesystem::copy_options::overwrite_existing);
        const auto initial=document::PartDocument::load(path);
        const auto sketch_it=std::ranges::find_if(initial.sketches,[](const auto& s){return std::ranges::any_of(s.dimensions,[](const auto& d){return d.kind==sketcher::DimensionKind::AngleBetween;});});
        check(sketch_it!=initial.sketches.end(),"Bolt angular Sketch missing");const auto sketch_id=sketch_it->id,dimension_id=sketch_it->dimensions.front().id;
        check(window.open_document_path(QString::fromStdString(path.string())),"Bolt did not open");window.resize(1200,900);window.show();flush();
        check(!initial.body_history.active_body_id().empty(),"Bolt editable Body context missing");
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Bolt View missing");
        const auto native=[&] {window.findChild<QAction*>("saveDocumentAction")->trigger();flush();return document::PartDocument::load(path);};
        const auto authored=[&](const document::PartDocument& part) {
            const auto sketch=std::ranges::find(part.sketches,sketch_id,&sketcher::Sketch::id);
            check(sketch!=part.sketches.end(),"Bolt Sketch lost during history verification");
            return sketch->serialized();
        };
        const auto open=[&] {
            QTreeWidgetItem* row{};for(QTreeWidgetItemIterator i(window.findChild<QTreeWidget*>("documentTree"));*i;++i)
                if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch_id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"Bolt Sketch Tree row missing");window.show_tree_item_properties(row);flush();
            QPushButton* button{};for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible()) {
                button=dialog->findChild<QPushButton*>("featureSketchButton");if(!button)button=dialog->findChild<QPushButton*>("sketchOpenButton");if(!button)button=dialog->findChild<QPushButton*>("primitiveOwnSketchButton");if(button)break;
            }
            if(button){button->click();flush();}
            check(window.findChild<QAction*>("finishSketchAction")->isEnabled(),"Bolt owned Sketch did not enter editing");
            QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
            std::vector<kernel::Vec3> fit;for(const auto& point:view->mesh().points)if(point.reference.owner_id==sketch_id)fit.push_back(point.position);
            view->fit_points(fit);flush();
        };
        const auto finish=[&] {
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible())if(auto* buttons=dialog->findChild<QDialogButtonBox*>())if(auto* ok=buttons->button(QDialogButtonBox::Ok);ok&&ok->isEnabled())ok->click();
            flush();return native();
        };
        for(double target:{-35.,-45.,-30.}) {
            const auto before=native();open();
            if(target==-35.) {
                const auto& source_dimension=sketch_it->dimensions.front();
                const auto segment=std::ranges::find(sketch_it->segments,source_dimension.geometry_id,&sketcher::SketchSegment::id);
                check(segment!=sketch_it->segments.end(),"Bolt angular arm missing");
                const auto key="point:"+segment->second_point_id;
                const auto position=[&] {
                    const auto point=std::ranges::find_if(view->mesh().points,[&](const auto& p){return p.reference.owner_id==sketch_id&&p.reference.semantic_key==key;});
                    check(point!=view->mesh().points.end(),"Bolt drag point identity lost");
                    return sketch_it->local_point(point->position);
                };
                for(unsigned drag=0;drag<3;++drag) {
                    std::optional<QPointF> hit;
                    for(int y=8;y<view->height()-8&&!hit;y+=3)for(int x=8;x<view->width()-8;x+=3) {
                        const auto candidates=view->selection_candidates_at({double(x),double(y)});
                        if(!candidates.empty()&&candidates.front().owner_id==sketch_id&&candidates.front().semantic_key==key){hit=QPointF(x,y);break;}
                    }
                    check(hit.has_value(),"Bolt endpoint cannot be picked for dragging");const auto point_before=position();
                    const auto mouse=[&](QEvent::Type type,QPointF point,Qt::MouseButton button,Qt::MouseButtons buttons) {
                        QMouseEvent event(type,point,QPointF(view->mapToGlobal(point.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();
                    };
                    mouse(QEvent::MouseMove,*hit,Qt::NoButton,Qt::NoButton);mouse(QEvent::MouseButtonPress,*hit,Qt::LeftButton,Qt::LeftButton);
                    const auto to=*hit+QPointF(-24,0);mouse(QEvent::MouseMove,to,Qt::NoButton,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,to,Qt::LeftButton,Qt::NoButton);
                    const auto moved=position();check(std::hypot(moved[0]-point_before[0],moved[1]-point_before[1])>1e-5,"Bolt unlocked angular endpoint did not move");
                    check(std::abs(moved[1]-point_before[1])<1e-6,"Bolt endpoint escaped its external face support");
                    window.findChild<QAction*>("undoAction")->trigger();flush();const auto restored=position();
                    check(std::hypot(restored[0]-point_before[0],restored[1]-point_before[1])<1e-6,"Bolt point drag Undo failed");
                    window.findChild<QAction*>("redoAction")->trigger();flush();const auto redone=position();
                    check(std::hypot(redone[0]-moved[0],redone[1]-moved[1])<1e-6,"Bolt point drag Redo failed");
                    window.findChild<QAction*>("undoAction")->trigger();flush();
                }
            }
            if(target==-35.)check(view->grabFramebuffer().save(QString::fromStdString((directory/"bolt-sketch-reference-priority.png").string())),"Bolt Sketch capture failed");
            viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;candidate.owner_id=sketch_id;candidate.semantic_key="dimension:"+dimension_id;
            const auto shown=std::ranges::find_if(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==sketch_id&&d.reference.semantic_key==candidate.semantic_key;});
            check(shown!=view->mesh().dimensions.end(),"Bolt angle is not displayed");candidate.geometry_index=std::size_t(shown-view->mesh().dimensions.begin());
            view->confirm_reference(sketch_id,candidate.semantic_key,{},viewer::CandidateKind::Dimension);flush();
            const auto position=view->candidate_dimension_label_position(candidate);check(position.has_value(),"Bolt angle label missing");
            QMouseEvent press(QEvent::MouseButtonDblClick,QPointF(*position),QPointF(view->mapToGlobal(*position)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&press);
            QMouseEvent release(QEvent::MouseButtonRelease,QPointF(*position),QPointF(view->mapToGlobal(*position)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&release);flush();
            auto* editor=window.findChild<QLineEdit*>("inlineDimensionValueEdit");check(editor&&editor->isVisible(),"Bolt angle inline editor missing");editor->setText(QString::number(target));
            QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);flush();
            const auto changed=finish();const auto s=std::ranges::find(changed.sketches,sketch_id,&sketcher::Sketch::id);check(s!=changed.sketches.end(),"Bolt Sketch identity lost");
            check(std::abs(s->dimensions.front().value-target)<1e-6,"Bolt GUI angle did not change");
            if(changed.serialized()!=before.serialized()) {
                window.findChild<QAction*>("undoAction")->trigger();flush();const auto undone=native();
                check(authored(undone)==authored(before),"Bolt GUI angle Undo failed");
                window.findChild<QAction*>("redoAction")->trigger();flush();check(authored(native())==authored(changed),"Bolt GUI angle Redo failed");
            }
        }
        std::cout<<"Bolt actual angle double-click, successive values, Undo/Redo, native persistence and reference stroke capture passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Bolt Sketch GUI: "<<error.what()<<'\n';return 1;}
}
