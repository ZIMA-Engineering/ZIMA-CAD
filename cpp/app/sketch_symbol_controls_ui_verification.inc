static int verify_sketch_symbol_controls(QApplication& application,
    zima::app::AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto run=[&](const char* name,commands::Json arguments=commands::Json::object()) {
        const auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",name},{"arguments",arguments}}.dump()));
        if(!result.ok)throw std::runtime_error(result.message);flush();return result;
    };
    try {
        window.resize(1200,900);window.show();flush();
        auto* tree=window.findChild<QTreeWidget*>("documentTree");
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));
        auto* text_action=window.findChild<QAction*>("sketchTextAction");
        auto* finish=window.findChild<QAction*>("finishSketchAction");
        check(tree&&view&&text_action&&finish,"Sketch symbol controls are missing");
        const auto definition=symbols::projection_method();
        for(bool assembly:{false,true}) {
            auto sketch=sketcher::Sketch::create_default();
            sketcher::SymbolInstance instance;instance.id="movable-symbol";instance.definition=definition.serialized();
            instance.variant=definition.default_variant;instance.x=25;instance.y=15;sketch.add_symbol(instance);
            const auto grip=sketch.symbols.front().anchor_point_id;
            auto owner=document::PartDocument::create_sketch_container();sketch.owner_container_id=owner.id;owner.feature.sketch_id=sketch.id;
            const auto path=directory/(assembly?"assembly-symbol-controls.asmz":"part-symbol-controls.prtz");
            std::string body;
            if(assembly) {auto doc=assembly::AssemblyDocument::create_default();doc.sketches={sketch};doc.sketch_containers={owner};doc.save(path);}
            else {auto doc=document::PartDocument::create_default();doc.sketches={sketch};doc.history={owner};
                document::BodyHistoryGraph graph;body=graph.create_body("Symbol controls");graph.insert({document::PartHistoryKind::Feature,owner.id});doc.set_body_history(graph);doc.resolve_constructions();doc.save(path);}
            check(window.open_document_path(QString::fromStdString(path.string())),"Cannot open Sketch symbol fixture");flush();
            if(!assembly)run("body.activate",{{"body",body}});
            QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)==(assembly?"assembly-sketch":"part-sketch")){row=*i;break;}
            check(row,"Sketch child row is missing");window.show_tree_item_properties(row);flush();
            if(auto* open=window.findChild<QPushButton*>("sketchOpenButton"))open->click();flush();
            if(!finish->isEnabled()||!text_action->isEnabled()) {
                window.grab().save(QString::fromStdString((directory/"symbol-sketch-entry-failure.png").string()));
                std::cerr<<"Entry assembly="<<assembly<<" header="<<tree->headerItem()->text(0).toStdString()<<" finish="<<finish->isEnabled()<<" text="<<text_action->isEnabled()<<'\n';
                for(auto* dialog:window.findChildren<QDialog*>())std::cerr<<dialog->objectName().toStdString()<<" visible="<<dialog->isVisible()<<'\n';
                if(auto* state=window.findChild<QLabel*>("workspaceState"))std::cerr<<state->text().toStdString()<<'\n';
                throw std::runtime_error("Sketcher did not become editable");
            }
            const auto stored=[&] {window.findChild<QAction*>("saveDocumentAction")->trigger();flush();return assembly?assembly::AssemblyDocument::load(path).sketches.front():document::PartDocument::load(path).sketches.front();};
            for(bool accept:{false,true}) {
                text_action->trigger();flush();
                auto* dialog=dynamic_cast<app::SketchTextPropertiesDialog*>(window.findChild<QDialog*>("sketchTextProperties"));
                if(!dialog||!dialog->isVisible()) {
                    std::cerr<<"Text entry assembly="<<assembly<<" accept="<<accept<<" finish="<<finish->isEnabled()<<" text="<<text_action->isEnabled()<<'\n';
                    for(auto* child:window.findChildren<QDialog*>())std::cerr<<"Dialog "<<child->objectName().toStdString()<<" visible="<<child->isVisible()<<'\n';
                    if(auto* state=window.findChild<QLabel*>("workspaceState"))std::cerr<<state->text().toStdString()<<'\n';
                    window.grab().save(QString::fromStdString((directory/"symbol-text-entry-failure.png").string()));
                    throw std::runtime_error("Text command did not open its dialog in an editable Sketch");
                }
                dialog->set_anchor(10,10);dialog->findChild<QPlainTextEdit*>("sketchTextValue")->setPlainText("ZIMA");
                auto* mode=dialog->findChild<QComboBox*>("sketchTextMode");mode->setCurrentIndex(mode->findData(false));
                if(accept)dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();else dialog->reject();flush();
                check(stored().texts.size()==(accept?1:0),"Text OK/Cancel changed the wrong Sketch state");
            }
            auto* undo=window.findChild<QAction*>("undoAction");auto* redo=window.findChild<QAction*>("redoAction");
            check(undo&&redo,"History actions are missing");undo->trigger();flush();check(stored().texts.empty(),"Text Undo failed");
            redo->trigger();flush();check(stored().texts.size()==1,"Text Redo failed");
            // Invert the actual camera rays to locate the native anchor. No
            // guessed screen coordinates or alternate test-only drag path.
            sketch=stored();
            QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();flush();
            const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Sketch camera ray missing");const auto q=sketch.intersect_ray(ray->first,ray->second);check(q.has_value(),"Camera is not facing the Sketch");return *q;};
            const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
            const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
            const auto screen=[&](double x,double y){return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);};
            const auto mouse=[&](QEvent::Type type,QPointF point,Qt::MouseButton button,Qt::MouseButtons buttons){
                QMouseEvent event(type,point,QPointF(view->mapToGlobal(point.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();};
            for(int step=0;step<3;++step) {
                const auto previous=stored();const auto& symbol=previous.symbols.front();const auto from=screen(symbol.x,symbol.y),to=from+QPointF(35,20);
                const auto candidates=view->selection_candidates_at(from);
                check(std::ranges::any_of(candidates,[&](const auto& candidate){return candidate.kind==viewer::CandidateKind::SketchPoint&&candidate.semantic_key=="point:"+grip;}),"The ordinary picker does not offer the symbol anchor");
                mouse(QEvent::MouseMove,from,Qt::NoButton,Qt::NoButton);
                mouse(QEvent::MouseButtonPress,from,Qt::LeftButton,Qt::LeftButton);
                mouse(QEvent::MouseMove,to,Qt::NoButton,Qt::LeftButton);
                mouse(QEvent::MouseButtonRelease,to,Qt::LeftButton,Qt::NoButton);
                const auto moved=stored();const auto& value=moved.symbols.front();const auto* point=moved.find_point(grip);
                check(point&&value.anchor_point_id==grip&&std::hypot(value.x-symbol.x,value.y-symbol.y)>1e-3,"Mouse drag did not move the symbol");
                check(std::abs(value.x-point->x)<1e-8&&std::abs(value.y-point->y)<1e-8,"Mouse drag detached symbol geometry from its point");
            }
            window.grab().save(QString::fromStdString((directory/(assembly?"assembly-symbol-anchor.png":"part-symbol-anchor.png")).string()));
            const auto symbol_state=stored();const auto symbol_mesh=symbols::instance_mesh(symbol_state.symbols.front());
            const auto& stroke=symbol_mesh.edges.front().points;const auto pick=screen((stroke.front().x+stroke.back().x)/2,(stroke.front().y+stroke.back().y)/2);
            view->clear_selection();mouse(QEvent::MouseMove,pick,Qt::NoButton,Qt::NoButton);mouse(QEvent::MouseButtonPress,pick,Qt::LeftButton,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,pick,Qt::LeftButton,Qt::NoButton);
            check(view->confirmed_candidate()&&view->confirmed_candidate()->kind==viewer::CandidateKind::Symbol,"A normal View click did not select the symbol");
            bool view_offered=false;
            QTimer::singleShot(0,&window,[&] {
                auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());auto* action=window.findChild<QAction*>("explodeSketchSymbolAction");
                view_offered=menu&&action&&menu->actions().contains(action);if(menu)menu->close();
            });
            mouse(QEvent::MouseButtonPress,pick,Qt::RightButton,Qt::RightButton);mouse(QEvent::MouseButtonRelease,pick,Qt::RightButton,Qt::NoButton);
            check(view_offered,"View context menu does not offer symbol decomposition");
            QTreeWidgetItem* symbol_row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole+3)=="sketch-symbol"){symbol_row=*i;break;}
            check(symbol_row,"Moved symbol Tree row is missing");
            for(auto* parent=symbol_row->parent();parent;parent=parent->parent())parent->setExpanded(true);
            tree->scrollToItem(symbol_row);flush();bool offered=false;
            QTimer::singleShot(0,&window,[&] {
                auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());
                auto* action=window.findChild<QAction*>("explodeSketchSymbolAction");
                if(menu&&action&&menu->actions().contains(action)){offered=true;action->trigger();menu->close();}
                else if(menu)menu->close();
            });
            tree->customContextMenuRequested(tree->visualItemRect(symbol_row).center());flush();
            const auto exploded=stored();
            if(!offered||!exploded.symbols.empty()||exploded.segments.empty()||exploded.circles.empty())throw std::runtime_error("Context-menu Explode: assembly="+std::to_string(assembly)+" offered="+std::to_string(offered)+" symbols="+std::to_string(exploded.symbols.size())+" segments="+std::to_string(exploded.segments.size()));
            undo->trigger();flush();check(stored().symbols.size()==1,"Explode was not one reversible transaction");
            redo->trigger();flush();check(stored().symbols.empty(),"Explode Redo failed");
            finish->trigger();flush();
            for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible())dialog->reject();flush();
        }
        for(const auto kind:{sketcher::DimensionKind::DistanceX,sketcher::DimensionKind::DistanceY,sketcher::DimensionKind::Distance})for(int state:{0,1,2}) {
            std::cout<<"Symbol GUI dimension kind="<<static_cast<int>(kind)<<" state="<<state<<std::endl;
            auto sketch=sketcher::Sketch::create_default();sketcher::SymbolInstance symbol;
            symbol.id="dimensioned-symbol";symbol.definition=definition.serialized();symbol.variant=definition.default_variant;symbol.x=30;symbol.y=40;sketch.add_symbol(symbol);
            const auto grip=sketch.symbols.front().anchor_point_id,fixed=sketch.add_point(0,0);sketch.set_point_fixed(fixed,true);
            auto dimension=sketch.create_point_dimension(fixed,grip,kind);dimension.driving=state!=0;dimension.locked=state==2;sketch.apply_dimension(dimension);
            auto doc=document::PartDocument::create_default();auto owner=document::PartDocument::create_sketch_container();owner.feature.sketch_id=sketch.id;sketch.owner_container_id=owner.id;
            doc.sketches={sketch};doc.history={owner};document::BodyHistoryGraph graph;const auto body=graph.create_body("Dimensioned symbol");graph.insert({document::PartHistoryKind::Feature,owner.id});doc.set_body_history(graph);doc.resolve_constructions();
            const auto path=directory/("dimensioned-symbol-"+std::to_string(static_cast<int>(kind))+"-"+std::to_string(state)+".prtz");doc.save(path);
            check(window.open_document_path(QString::fromStdString(path.string())),"Cannot open dimensioned symbol fixture");flush();run("body.activate",{{"body",body}});
            QTreeWidgetItem* row{};for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"Dimensioned Sketch row missing");window.show_tree_item_properties(row);flush();window.findChild<QPushButton*>("sketchOpenButton")->click();flush();
            QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();flush();
            const auto stored=[&]{window.findChild<QAction*>("saveDocumentAction")->trigger();flush();return document::PartDocument::load(path).sketches.front();};
            sketch=stored();view->fit_points({sketch.world_point(-20,-20),sketch.world_point(110,110)});flush();
            const auto candidate=[&] {
                for(std::size_t i=0;i<view->mesh().dimensions.size();++i)if(view->mesh().dimensions[i].reference.semantic_key=="dimension:"+dimension.id) {
                    viewer::ViewerCandidate result;result.kind=viewer::CandidateKind::Dimension;result.owner_id=sketch.id;result.semantic_key="dimension:"+dimension.id;result.geometry_index=i;return result;
                }
                throw std::runtime_error("Native symbol dimension missing from View");
            };
            const auto screen=[&](double x,double y) {
                const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Dimensioned symbol ray missing");return *sketch.intersect_ray(ray->first,ray->second);};
                const auto a=local({0,0}),b=local({100,0}),c=local({0,100});const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
                return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);
            };
            for(double value:{55.,65.,45.}) {
                if(state) {
                    window.edit_dimension_inline(candidate());flush();auto* editor=window.findChild<QLineEdit*>("inlineDimensionValueEdit");check(editor&&editor->isVisible(),"Symbol dimension value editor missing");
                    editor->setText(QString::number(value)+" mm");QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);flush();
                    check(std::abs(stored().dimensions.front().value-value)<1e-5,"GUI value edit did not drive symbol point");
                }
                const auto previous=stored();const auto* p=previous.find_point(grip);const auto from=screen(p->x,p->y),to=from+QPointF(30,15);
                const auto mouse=[&](QEvent::Type type,QPointF position,Qt::MouseButton button,Qt::MouseButtons buttons){QMouseEvent event(type,position,QPointF(view->mapToGlobal(position.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();};
                mouse(QEvent::MouseMove,from,Qt::NoButton,Qt::NoButton);mouse(QEvent::MouseButtonPress,from,Qt::LeftButton,Qt::LeftButton);mouse(QEvent::MouseMove,to,Qt::NoButton,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,to,Qt::LeftButton,Qt::NoButton);
                const auto moved=stored();const auto* q=moved.find_point(grip);check(q&&std::hypot(q->x-p->x,q->y-p->y)>1e-5,"GUI dimensioned symbol did not move along a free degree of freedom");
                const double measured=kind==sketcher::DimensionKind::DistanceX?std::abs(q->x):kind==sketcher::DimensionKind::DistanceY?std::abs(q->y):std::hypot(q->x,q->y);
                check(std::abs(measured-std::abs(moved.dimensions.front().value))<1e-5,"GUI symbol drag violated its dimension equation");
                check(state!=2||std::abs(moved.dimensions.front().value-value)<1e-5,"GUI drag changed locked symbol dimension");
                check(moved.symbols.front().anchor_point_id==grip&&std::abs(moved.symbols.front().x-q->x)<1e-8&&std::abs(moved.symbols.front().y-q->y)<1e-8,"GUI dimension edit lost symbol incidence");
                window.findChild<QAction*>("undoAction")->trigger();flush();check(stored().find_point(grip)->x==p->x&&stored().find_point(grip)->y==p->y,"Dimensioned drag Undo failed");
                window.findChild<QAction*>("redoAction")->trigger();flush();
            }
            finish->trigger();flush();for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible())dialog->reject();flush();
        }
        // A newly owned profile has no persisted Sketch yet. Text must use
        // the same active draft accessor as the other Sketcher commands.
        auto part=document::PartDocument::create_default();document::BodyHistoryGraph graph;const auto body=graph.create_body("Draft text");part.set_body_history(graph);
        const auto path=directory/"draft-text.prtz";part.save(path);check(window.open_document_path(QString::fromStdString(path.string())),"Cannot open draft-text fixture");flush();run("body.activate",{{"body",body}});
        window.findChild<QAction*>("extrusionAction")->trigger();flush();
        app::PrimitivePropertiesDialog* parent{};
        for(auto* candidate:window.findChildren<QDialog*>())if(auto* primitive=dynamic_cast<app::PrimitivePropertiesDialog*>(candidate);primitive&&primitive->isVisible())parent=primitive;
        auto* open=parent?parent->findChild<QPushButton*>("primitiveOwnSketchButton"):nullptr;
        check(open,"New Extrusion profile control missing");open->click();flush();text_action->trigger();flush();
        auto* text=dynamic_cast<app::SketchTextPropertiesDialog*>(window.findChild<QDialog*>("sketchTextProperties"));
        check(text&&text->isVisible(),"Text does not open in a new owned profile");text->set_anchor(2,3);
        text->findChild<QPlainTextEdit*>("sketchTextValue")->setPlainText("Draft");
        text->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();finish->trigger();flush();
        parent=nullptr;
        for(auto* candidate:window.findChildren<QDialog*>())if(auto* primitive=dynamic_cast<app::PrimitivePropertiesDialog*>(candidate);primitive&&primitive->isVisible())parent=primitive;
        check(parent,"Owned profile did not return to its properties");
        parent->reject();flush();run("save");check(document::PartDocument::load(path).sketches.empty(),"Parent Cancel committed draft text");
        std::cout<<"Part/Assembly text dialogs, draft text, repeated real mouse symbol drags, persistence and Undo/Redo passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
