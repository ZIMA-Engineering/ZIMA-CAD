static int verify_sketch_curve_fillet_ui(QApplication& application,const std::filesystem::path& parent) {
    using namespace zima;using namespace zima::test::mechanism;
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto settle=[&]{QEventLoop loop;QTimer::singleShot(950,&loop,&QEventLoop::quit);loop.exec();flush();};
    try {
        QTemporaryDir isolated(QString::fromStdString((parent/"curve-fillet-ui-XXXXXX").string()));require(isolated.isValid(),"Cannot isolate curve fillet GUI test");
        const auto directory=std::filesystem::u8path(isolated.path().toStdString());unsigned cases{};
        for(bool assembly_mode:{false,true})for(bool real:{false,true})for(unsigned type=0;type<7;++type)for(bool reverse:{false,true}) {
            if(assembly_mode&&!real)continue;
            if(type==6&&real)continue;
            std::cout<<"Curve fillet GUI "<<real<<' '<<type<<' '<<reverse<<" assembly="<<assembly_mode<<std::endl;
            auto s=Sketch::create_default();std::array<std::string,2> curves{s.add_segment(0,0,25,0),{}};
            if(type==0)curves[1]=s.add_segment(0,0,0,20);
            if(type==1)curves[1]=s.add_arc(-10,0,0,0,-10,10);
            if(type==2)curves[1]=s.add_elliptical_arc(-15,0,0,0,-15,10,0,0,-15,10);
            if(type>=3&&type<5)curves[1]=s.add_bspline({{0,0},{0,7},{5,14},{12,20}},3,false,false,1e-6,type==4);
            if(type>=5) {
                s=Sketch::create_default();
                curves[1]=s.add_arc(-10,0,0,0,-10,10);
                if(type==5)curves[0]=s.add_arc(0,-10,10,-10,0,0);
                else {
                    const auto source=s.add_arc(0,-10,11,-10,-11,-10,true);
                    curves[0]=s.add_offset(source,1,false);
                    static_cast<void>(s.retain_curve_intervals(curves[0],{{0,.5}}));
                }
            }
            s.constraints.clear();if(reverse)std::swap(curves[0],curves[1]);
            auto authored=part(s);s=authored.sketches[0];const auto file=directory/("fillet-"+std::to_string(cases)+(assembly_mode?".asmz":".prtz"));
            authored.document_units["Length"]=reverse?"in":"mm";
            if(assembly_mode) {
                auto context_sketch=Sketch::create_default();static_cast<void>(context_sketch.add_rectangle(-30,-20,65,55));
                auto context=part(context_sketch);auto feature=document::PartDocument::create_feature_container(context.sketches.front().id);
                feature.placement.z=-8;feature.feature.sides[0].length=2;
                context.sketches.front().owner_container_id=feature.id;context.history={feature};
                document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Fillet context"));
                graph.insert({document::PartHistoryKind::Feature,feature.id});context.set_body_history(graph);context.resolve_constructions();
                kernel::OcctKernel kernel;const auto bodies=kernel.evaluate_history(context.kernel_operations());
                require(!bodies.empty()&&bodies.back().calculation_errors.empty(),"Assembly Fillet context calculation failed");
                const auto context_file=directory/("fillet-context-"+std::to_string(cases)+".prtz");context.save(context_file,bodies);
                auto doc=assembly::AssemblyDocument::create_default();
                doc.document_units["Length"]=reverse?"in":"mm";
                auto component=assembly::AssemblyDocument::create_part_occurrence("Context",context.document_id,context_file,bodies.back());
                doc.components.push_back(component);doc.insert_history_entry(assembly::AssemblyDocument::HistoryKind::Component,component.occurrence_id);
                doc.insert_sketch(s,authored.history.front());doc.save(file);
            } else authored.save(file);
            const std::array<const char*,5> languages{"cs","en","de","fr","ru"};
            QSettings config(isolated.filePath("config.ini"),QSettings::IniFormat);
            config.setValue("Application/Language",languages[type%languages.size()]);
            config.setValue("Paths/Localization",QString::fromStdString(std::filesystem::absolute("config/localization").string()));config.sync();
            const auto settings=app::ApplicationSettings::load(isolated.path());
            app::AssemblyWorkspaceWindow window(isolated.path());window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1200,900);window.show();flush();
            require(window.open_document_path(QString::fromStdString(file.string())),"Fillet fixture cannot open");flush();
            const auto* body=authored.body_owner_for_object(s.owner_container_id);
            if(!assembly_mode)require(body&&activate_test_body(application,window,body->scope.id),"Fillet Body cannot activate");
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==s.id&&(*i)->data(0,Qt::UserRole+3)==(assembly_mode?"assembly-sketch":"part-sketch")){row=*i;break;}
            require(row,"Fillet Sketch row missing");window.show_tree_item_properties(row);flush();window.findChild<QPushButton*>("sketchOpenButton")->click();settle();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());require(view,"Fillet View missing");view->fit_points({s.world_point(-25,-15),s.world_point(35,35)});settle();
            const auto screen=[&](XY p) {
                const auto local=[&](QPointF pixel){const auto ray=view->ray_at(pixel);require(ray.has_value(),"Fillet ray missing");const auto hit=s.intersect_ray(ray->first,ray->second);require(hit.has_value(),"Fillet plane missing");return *hit;};
                const auto a=local({0,0}),b=local({100,0}),c=local({0,100});const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
                return QPointF(((p[0]-a[0])*vy-(p[1]-a[1])*vx)/det,(ux*(p[1]-a[1])-uy*(p[0]-a[0]))/det);
            };
            const auto mouse=[&](QPointF at,QEvent::Type type,Qt::MouseButton button=Qt::NoButton,Qt::MouseButtons held=Qt::NoButton) {
                QMouseEvent event(type,at,QPointF(view->mapToGlobal(at.toPoint())),button,held,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();
            };
            const auto click=[&](QPointF p){mouse(p,QEvent::MouseMove);mouse(p,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(p,QEvent::MouseButtonRelease,Qt::LeftButton);};
            const auto point_on=[&](const std::string& id){const auto p=kernel::bspline_value(sketcher::sketch_curve_geometry(*window.active_sketch_for_test(),id),.5);return screen({p.x,p.y});};
            auto* action=window.findChild<QAction*>(real?"sketchCurveFilletAction":"sketchCornerFilletAction");require(action&&(!real||action->isEnabled()),"Fillet command missing");
            const auto original=window.active_sketch_for_test()->serialized();
            for(bool cancel:{true,false}) {
                if(real&&!cancel) {
                    const auto p=screen({-22,30}),q=screen({30,-13});
                    mouse(p,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);
                    mouse(q,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);
                    mouse(q,QEvent::MouseButtonRelease,Qt::LeftButton);
                    require(view->sketch_selection().size()>=2,"Fillet preselection rectangle missed curves");
                }
                action->trigger();flush();
                if(!real||cancel){click(point_on(curves[0]));click(point_on(curves[1]));}
                QDialogButtonBox* buttons{};
                for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible())if(auto* box=dialog->findChild<QDialogButtonBox*>();box&&box->button(QDialogButtonBox::Ok)){buttons=box;break;}
                require(buttons,"Two picked curves did not open the radius dialog");
                if(real&&cancel&&!assembly_mode&&type==0&&!reverse) {
                    const auto revision=window.execute_console_command("documents").data;
                    for(const bool dark:{false,true,false}) {
                        auto themed=settings;themed.theme=dark?"dark":"light";
                        app::apply_application_appearance(application,themed);flush();
                        const auto icon=action->icon().pixmap(72,72).toImage();
                        bool purple{},white{};
                        for(int y=0;y<icon.height();++y)for(int x=0;x<icon.width();++x) {
                            const auto color=icon.pixelColor(x,y);if(color.alpha()<200)continue;
                            purple|=color.blue()>170&&color.red()>110&&color.green()+25<color.blue();
                            white|=color.red()>230&&color.green()>230&&color.blue()>230;
                        }
                        require(purple&&white,"Fillet icon lost its purple arc or white endpoint dots after theme change");
                        require(window.active_sketch_for_test()->serialized()==original&&
                            window.execute_console_command("documents").data==revision,
                            "Fillet theme change edited the Sketch or document");
                        window.grab().save(QString::fromStdString((parent/(dark?"fillet-dark.png":"fillet-light.png")).string()));
                    }
                    app::apply_application_appearance(application,settings);flush();
                }
                if(real) {
                    auto* radius=window.findChild<QDoubleSpinBox*>("sketchFilletRadius");
                    auto* fields=window.findChild<QTableWidget*>("sketchFilletCurves");
                    require(radius&&fields&&fields->rowCount()==2,"Fillet reference form is missing");
                    require(std::abs(radius->value()-(reverse?1.016:1.))<1e-12,
                        "Sketch Fillet did not choose the document-unit default");
                    auto* dialog=window.findChild<QDialog*>("sketchCurveFilletDialog");
                    require(dialog&&dialog->windowType()==Qt::SubWindow&&dialog->parentWidget()==&window,"Fillet is not an internal properties window");
                    require(dialog->windowTitle()==settings.qt_translations.value("Zaoblení")&&
                        fields->horizontalHeaderItem(1)->text()==settings.qt_translations.value("Křivky"),"Fillet labels ignored the configured language");
                    require(buttons->button(QDialogButtonBox::Ok)->isEnabled(),"Fillet preselection did not prepare a valid preview");
                    const auto radius_top=radius->mapTo(dialog,QPoint{}).y(),table_top=fields->mapTo(dialog,QPoint{}).y();
                    const auto row_height=fields->rowHeight(0);
                    dialog->resize(dialog->width()+160,dialog->height()+120);flush();
                    require(radius->mapTo(dialog,QPoint{}).y()==radius_top&&fields->mapTo(dialog,QPoint{}).y()==table_top&&
                        fields->rowHeight(0)==row_height&&fields->columnWidth(0)==32&&fields->columnWidth(2)==32,"Fillet resizing moved or stretched its reference rows");
                    auto* eye=fields->cellWidget(0,2)->findChild<QToolButton*>();
                    auto* item=dynamic_cast<ui::ReferenceCellItem*>(fields->item(0,1));
                    require(eye&&item,"Fillet shared inspection control missing");eye->click();flush();
                    require(item->is_inspected()&&!item->is_active_input(),"Fillet inspection armed reference entry");
                    require(window.active_sketch_for_test()->serialized()==original,"Fillet inspection mutated the Sketch");
                    const auto cleared_curve=item->reference().toStdString();
                    auto* clear=fields->cellWidget(0,0)->findChild<QPushButton*>();
                    require(clear,"Fillet clear control missing");clear->click();flush();
                    require(item->is_active_input()&&!item->is_inspected()&&!eye->isEnabled()&&
                        !buttons->button(QDialogButtonBox::Ok)->isEnabled(),"Fillet clear did not reset inspection and reactivate entry");
                    require(item->text()==settings.qt_translations.value("Vyberte křivku ve View."),"Fillet entry prompt is untranslated");
                    click(point_on(cleared_curve));
                    require(buttons->button(QDialogButtonBox::Ok)->isEnabled(),"Fillet curve replacement failed");
                    for(double value:{1.2,.8,1.}) {
                        radius->setValue(value);flush();
                        require(window.active_sketch_for_test()->serialized()==original,"Fillet preview mutated the live Sketch");
                        require(buttons->button(QDialogButtonBox::Ok)->isEnabled(),"Interactive fillet radius failed");
                        if(assembly_mode)require(!view->mesh().triangles.empty(),"Assembly Fillet preview hid component context");
                    }
                }
                buttons->button(cancel?QDialogButtonBox::Cancel:QDialogButtonBox::Ok)->click();flush();
                if(cancel)require(window.active_sketch_for_test()->serialized()==original,"Fillet Cancel changed the Sketch");
            }
            auto draft=*window.active_sketch_for_test();require(draft.dimensions.empty(),"Fillet automatically added a radius dimension");
            require(real?draft.corner_radii.empty():draft.corner_radii.size()==1,"Wrong fillet representation");
            const auto expected=draft.serialized();window.findChild<QAction*>("undoAction")->trigger();flush();require(window.active_sketch_for_test()->serialized()==original,"Fillet is not one Undo transaction");
            window.findChild<QAction*>("redoAction")->trigger();flush();require(window.active_sketch_for_test()->serialized()==expected,"Fillet Redo changed identity");
            // Exercise actual Delete, including a curved support with virtual
            // rounding and selection of the complete chain. Dismiss failures
            // automatically so a regression produces an error, not a modal hang.
            struct DeleteMessageGuard final:QObject {
                QString error;
                bool eventFilter(QObject* object,QEvent* event)override {
                    if(event->type()==QEvent::Show)if(auto* box=qobject_cast<QMessageBox*>(object)) {
                        error=box->text();box->setAttribute(Qt::WA_DontShowOnScreen);
                        QTimer::singleShot(0,box,[box]{box->accept();});
                    }return false;
                }
            } delete_messages;
            application.installEventFilter(&delete_messages);
            for(const auto& id:curves) {
                click(point_on(id));QKeyEvent remove(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);
                QApplication::sendEvent(&window,&remove);flush();
                require(delete_messages.error.isEmpty(),"Rounded curve deletion displayed an error");
                require(!sketcher::fillet_curve_endpoints(*window.active_sketch_for_test(),id),"Delete retained selected rounded support");
                window.findChild<QAction*>("undoAction")->trigger();flush();
                require(window.active_sketch_for_test()->serialized()==expected,"Rounded support deletion Undo changed the Sketch");
            }
            {
                const auto p=screen({-22,30}),q=screen({30,-13});
                mouse(p,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);
                mouse(q,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);
                mouse(q,QEvent::MouseButtonRelease,Qt::LeftButton);
                require(view->sketch_selection().size()>=2,"Rounded chain selection missed curves");
                QKeyEvent remove(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);QApplication::sendEvent(&window,&remove);flush();
                require(delete_messages.error.isEmpty(),"Rounded chain deletion displayed an error");
                const auto empty=window.active_sketch_for_test()->serialized();
                for(const auto& id:curves)require(!sketcher::fillet_curve_endpoints(*window.active_sketch_for_test(),id),"Chain Delete retained selected geometry");
                window.findChild<QAction*>("undoAction")->trigger();flush();require(window.active_sketch_for_test()->serialized()==expected,"Chain deletion Undo failed");
                window.findChild<QAction*>("redoAction")->trigger();flush();require(window.active_sketch_for_test()->serialized()==empty,"Chain deletion Redo failed");
                window.findChild<QAction*>("undoAction")->trigger();flush();
            }
            application.removeEventFilter(&delete_messages);
            if(real) {
                const auto arc_id=draft.arcs.back().id;
                // Resolve a one-millimetre arc clearly, independently of the
                // endpoint picker tolerance used for the full input curves.
                view->fit_points({s.world_point(-1,-1),s.world_point(3,3)});settle();
                auto* dimension=window.findChild<QAction*>("sketchUniversalDimensionAction");require(dimension&&dimension->isEnabled(),"Dimension command missing");dimension->trigger();flush();
                mouse(point_on(arc_id),QEvent::MouseMove);
                if(!view->hovered_candidate()||view->hovered_candidate()->semantic_key!="arc:"+arc_id)
                    throw std::runtime_error("Fillet size picker offered "+(view->hovered_candidate()?view->hovered_candidate()->semantic_key:std::string("nothing"))+" instead of arc:"+arc_id);
                click(point_on(arc_id));
                if(reverse)click(point_on(arc_id));click(screen({2.5,2.5}));
                QEventLoop delay;QTimer::singleShot(30,&delay,&QEventLoop::quit);delay.exec();flush();
                if(auto* editor=view->findChild<QLineEdit*>("inlineDimensionValueEdit");editor&&editor->isVisible()){QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);flush();}
                draft=*window.active_sketch_for_test();require(draft.dimensions.size()==1,"Manual fillet size was not created");const auto size=draft.dimensions.front();
                window.findChild<QAction*>("viewSelectionAction")->trigger();flush();
                for(double radius:{1.2,.8,1.1}) {
                    const auto shown=std::ranges::find(view->mesh().dimensions,"dimension:"+size.id,[](const auto& d){return d.reference.semantic_key;});require(shown!=view->mesh().dimensions.end(),"Fillet size label missing");
                    viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;candidate.owner_id=s.id;candidate.semantic_key="dimension:"+size.id;candidate.geometry_index=std::size_t(shown-view->mesh().dimensions.begin());
                    const auto label=view->candidate_dimension_label_position(candidate);require(label.has_value(),"Fillet label position missing");click(*label);mouse(*label,QEvent::MouseButtonDblClick,Qt::LeftButton,Qt::LeftButton);mouse(*label,QEvent::MouseButtonRelease,Qt::LeftButton);
                    auto* editor=view->findChild<QLineEdit*>("inlineDimensionValueEdit");require(editor&&editor->isVisible(),"Fillet dimension editor missing");editor->setText(QString::number(radius*(reverse?2:1))+" mm");
                    QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);flush();draft=*window.active_sketch_for_test();
                    if(std::abs(*draft.circular_radius(arc_id)-radius)>=1e-7) {
                        std::ofstream(parent/"curve-fillet-dimension-failure.json")<<draft.serialized();
                        throw std::runtime_error("GUI fillet radius edit failed: expected="+std::to_string(radius)+
                            " actual="+std::to_string(*draft.circular_radius(arc_id))+" diameter="+std::to_string(reverse));
                    }
                    const auto arc=*std::ranges::find(draft.arcs,arc_id,&sketcher::SketchArc::id);const auto p=position(draft,arc.center_point_id);
                    mouse(screen(p),QEvent::MouseMove);mouse(screen(p),QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(screen({p[0]+1,p[1]+.5}),QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(screen({p[0]+1,p[1]+.5}),QEvent::MouseButtonRelease,Qt::LeftButton);
                    draft=*window.active_sketch_for_test();const auto after=position(draft,arc.center_point_id);require(std::hypot(after[0]-p[0],after[1]-p[1])>.1,"GUI fillet center did not move");
                    for(const auto& c:draft.constraints)if(c.kind==sketcher::ConstraintKind::Tangent&&c.second_geometry_id==arc_id) {
                        const auto curve=sketcher::sketch_curve_geometry(draft,c.geometry_id);const auto contact=position(draft,c.first_point_id);const auto a=kernel::bspline_value(curve,0),b=kernel::bspline_value(curve,1);const bool first=std::hypot(a.x-contact[0],a.y-contact[1])<1e-6;
                        require(first||std::hypot(b.x-contact[0],b.y-contact[1])<1e-6,"GUI fillet lost shared endpoint");
                        // Difference two points on the exact support. Subtracting
                        // a persisted endpoint's solver residual from a tiny
                        // finite-difference step magnifies that residual.
                        const auto end=first?a:b;
                        require(std::hypot(end.x-contact[0],end.y-contact[1])<1e-8,"GUI support endpoint left its native point");
                        const auto q=kernel::bspline_value(curve,first?1e-7:1-1e-7);const double dx=q.x-end.x,dy=q.y-end.y;
                        const double residual=std::abs((contact[0]-after[0])*dx+(contact[1]-after[1])*dy)/std::hypot(dx,dy);
                        if(residual>=1e-5) {
                            std::ofstream(parent/"curve-fillet-gui-failure.json")<<draft.serialized();
                            throw std::runtime_error("GUI fillet lost tangency: radius="+std::to_string(radius)+" residual="+std::to_string(residual)+" support="+c.geometry_id+" arc="+arc_id);
                        }
                    }
                }
            }
            draft=*window.active_sketch_for_test();window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible())if(auto* buttons=dialog->findChild<QDialogButtonBox*>();buttons&&buttons->button(QDialogButtonBox::Ok)){buttons->button(QDialogButtonBox::Ok)->click();flush();break;}
            require(window.execute_console_command("save").ok,"Fillet save failed");
            const auto reopened=assembly_mode?assembly::AssemblyDocument::load(file).sketches:document::PartDocument::load(file).sketches;
            require(reopened.front().serialized()==draft.serialized(),"Fillet native reopen changed authored data");++cases;
        }
        std::cout<<cases<<" curve fillet GUI cases passed: Part/Assembly, five languages, shared reference replacement/inspection, resizing, both selection orders, Cancel, native/virtual creation, manual R/diameter, repeated edits/drags, Undo/Redo and reopen\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Curve fillet GUI: "<<e.what()<<'\n';return 1;}
}
