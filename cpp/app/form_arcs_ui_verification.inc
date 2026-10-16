int verify_form_source_motion_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        const auto source=std::filesystem::u8path(qEnvironmentVariable("ZIMA_VERIFY_FORM_SOURCE_MOTION_DOCUMENT").toStdString());
        const auto path=directory/"form-source-motion.prtz";document::PartDocument::load(source).save(path);
        app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        check(window.open_document_path(QString::fromStdString(path.string())),"FORM source-motion fixture open failed");flush();
        const auto verify=[&] {
            std::vector<kernel::BodyResult> bodies;const auto part=document::PartDocument::load(path,&bodies);
            const auto& s=*std::ranges::find_if(part.sketches,[](const auto& value){return !value.external_references.empty();});const auto& ref=s.external_references.front();
            const auto& source=*std::ranges::find(part.sketches,ref.source_owner_id,&sketcher::Sketch::id);const auto* p=source.find_point(ref.source_semantic_key.substr(6));const auto expected=s.local_point(source.world_point(p->x,p->y));
            const auto k=std::ranges::find_if(s.constraints,[&](const auto& c){return c.kind==sketcher::ConstraintKind::PointReference&&c.second_point_id==ref.id;});const auto* contact=s.find_point(k->first_point_id);
            check(!ref.broken&&std::hypot(contact->x-expected[0],contact->y-expected[1])<1e-6&&std::hypot(ref.cached_points.front()[0]-expected[0],ref.cached_points.front()[1]-expected[1])<1e-6,"FORM GUI external/native point did not follow source");
            for(const auto& body:bodies)check(body.calculation_errors.empty(),"FORM GUI source edit left a failed feature");
            check(std::abs(s.dimensions.front().value-10.5)<1e-6&&std::abs(s.dimensions.back().value-8)<1e-6,"FORM GUI source edit changed authored dimensions");
        };
        const auto regenerate=[&]{window.findChild<QAction*>("regenerateDocumentAction")->trigger();flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();verify();};
        regenerate();
        for(double diameter:{32.,36.,35.}) {
            const auto part=document::PartDocument::load(path);const auto sketch=part.sketches.front();
            check(activate_test_body(application,window,part.body_history.owner(sketch.owner_container_id)->scope.id),"FORM source Sketch Body unavailable");
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"FORM source Sketch row missing");window.show_tree_item_properties(row);flush();if(auto* open=window.findChild<QPushButton*>("sketchOpenButton")){open->click();flush();}
            QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"FORM source View missing");
            std::optional<QPointF> hit;
            for(int y=10;y<view->height()-10&&!hit;y+=2)for(int x=10;x<view->width()-10;x+=2){const auto c=view->selection_candidates_at({double(x),double(y)});if(!c.empty()&&c.front().owner_id==sketch.id&&c.front().semantic_key=="dimension:"+sketch.dimensions.front().id){hit=QPointF(x,y);break;}}
            check(hit.has_value(),"FORM source diameter has no mouse candidate");QMouseEvent click(QEvent::MouseButtonDblClick,*hit,QPointF(view->mapToGlobal(hit->toPoint())),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&click);flush();
            auto* edit=view->findChild<QLineEdit*>("inlineDimensionValueEdit");check(edit,"FORM source diameter editor missing");edit->setText(QString::number(diameter));QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(edit,&enter);flush();
            window.findChild<QAction*>("undoAction")->trigger();flush();window.findChild<QAction*>("redoAction")->trigger();flush();window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* d:window.findChildren<QDialog*>())if(d->isVisible()&&(dynamic_cast<app::SketchPropertiesDialog*>(d)||dynamic_cast<app::PrimitivePropertiesDialog*>(d)))d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            regenerate();check(std::abs(document::PartDocument::load(path).sketches.front().dimensions.front().value-diameter)<1e-6,"FORM GUI source diameter did not persist");
            std::cout<<"FORM GUI source diameter="<<diameter<<" external contact, dimensions and regeneration passed"<<std::endl;
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

int verify_form_dimensions_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        const auto source=std::filesystem::u8path(qEnvironmentVariable("ZIMA_VERIFY_FORM_DIMENSIONS_DOCUMENT").toStdString());
        for(int support=0;support<2;++support) {
            std::cout<<"FORM dimension GUI opening support="<<support<<std::endl;
            const auto original=document::PartDocument::load(source);const auto sketch=original.sketches.back();
            const auto path=directory/("form-dimensions-"+std::to_string(support)+".prtz");original.save(path);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
            const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
            check(window.open_document_path(QString::fromStdString(path.string())),"FORM dimension fixture open failed");flush();
            std::cout<<"FORM dimension GUI regenerating"<<std::endl;
            window.findChild<QAction*>("regenerateDocumentAction")->trigger();flush();
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            const auto regenerated=document::PartDocument::load(path);
            for(const auto& ref:regenerated.sketches.back().external_references)check(!ref.broken,"GUI Regenerate retained broken FORM point");
            check(activate_test_body(application,window,original.body_history.owner(sketch.owner_container_id)->scope.id),"FORM dimension Body unavailable");
            std::cout<<"FORM dimension GUI editing"<<std::endl;
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"FORM dimension Sketch row missing");window.show_tree_item_properties(row);flush();
            QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"FORM dimension View missing");
            const auto mouse=[&](QPointF p,QEvent::Type type){QMouseEvent e(type,p,QPointF(view->mapToGlobal(p.toPoint())),Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&e);flush();};
            const auto hit=[&](const std::string& key) {
                for(int y=10;y<view->height()-10;y+=2)for(int x=10;x<view->width()-10;x+=2){const auto c=view->selection_candidates_at(QPointF(x,y));if(!c.empty()&&c.front().owner_id==sketch.id&&c.front().semantic_key==key)return QPointF(x,y);}
                throw std::runtime_error("FORM dimension GUI candidate missing: "+key);
            };
            auto* perpendicular=window.findChild<QAction*>("sketchPerpendicularAction");check(perpendicular&&perpendicular->isEnabled(),"FORM perpendicular action unavailable");perpendicular->trigger();flush();
            for(const auto& key:{"segment:"+sketch.segments.front().id,support?"segment:"+sketch.segments.back().id:std::string("sketch_axis:y")}) {
                const auto p=hit(key);mouse(p,QEvent::MouseButtonPress);mouse(p,QEvent::MouseButtonRelease);
            }
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&window,&escape);flush();
            std::cout<<"FORM dimension GUI changing values"<<std::endl;
            for(int step=0;step<3;++step)for(const auto& d:sketch.dimensions) {
                const auto p=hit("dimension:"+d.id);mouse(p,QEvent::MouseButtonDblClick);
                auto* edit=view->findChild<QLineEdit*>("inlineDimensionValueEdit");check(edit,"FORM dimension double-click did not open editor");
                const double delta=step==1?-.2:.3;
                edit->setText(QString::number(sketcher::dimension_display_value(d)+(d.kind==sketcher::DimensionKind::DistanceSymmetric?delta:-delta),'f',9));
                QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(edit,&enter);flush();
                auto* undo=window.findChild<QAction*>("undoAction");auto* redo=window.findChild<QAction*>("redoAction");check(undo->isEnabled(),"FORM dimension edit has no Undo");undo->trigger();flush();redo->trigger();flush();
            }
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* d:window.findChildren<QDialog*>())if(d->isVisible()&&(dynamic_cast<app::SketchPropertiesDialog*>(d)||dynamic_cast<app::PrimitivePropertiesDialog*>(d)))d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
            flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            auto saved=document::PartDocument::load(path);auto result=saved.sketches.back();
            check(std::ranges::any_of(result.constraints,[](const auto& c){return c.kind==sketcher::ConstraintKind::Perpendicular;}),"FORM GUI perpendicular did not persist");
            const auto* a=result.find_point(result.segments.front().first_point_id);const auto* b=result.find_point(result.segments.front().second_point_id);check(std::abs(a->y-b->y)<1e-6,"FORM GUI perpendicular equation failed");
            for(const auto& d:sketch.dimensions){const auto found=std::ranges::find(result.dimensions,d.id,&sketcher::SketchDimension::id);check(found!=result.dimensions.end()&&std::abs(found->value-d.value-.3)<1e-6,"FORM GUI repeated dimension edit did not persist");}
            const auto solved=result.solve();check(solved.maximum_residual<1e-6&&solved.status!=sketcher::SolveStatus::Conflicting,"FORM saved GUI dimensions violate equations");
            std::cout<<"FORM dimension GUI support="<<support<<" repeated value edits, Undo/Redo and save/reopen passed\n";
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

int verify_axis_circle_trim_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        auto part=document::PartDocument::create_default();auto feature=document::PartDocument::create_sketch_container();
        auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;
        static_cast<void>(sketch.add_circle(0,0,15));part.history={feature};part.sketches={sketch};
        document::BodyHistoryGraph graph;const auto body=graph.create_body("Axis trim fixture");graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
        const auto path=directory/"axis-circle-trim.prtz";part.save(path);
        app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        check(window.open_document_path(QString::fromStdString(path.string())),"Circle trim fixture open failed");flush();
        check(activate_test_body(application,window,body),"Circle trim fixture Body unavailable");
        auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
        for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
        check(row,"Circle trim Sketch row missing");window.show_tree_item_properties(row);flush();
        auto* open=window.findChild<QPushButton*>("sketchOpenButton");check(open,"Circle trim editor unavailable");open->click();flush();
        QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());
        auto* action=window.findChild<QAction*>("sketchTrimAction");check(view&&action&&action->isEnabled(),"Circle axis trim command unavailable");action->trigger();flush();
        std::optional<QPointF> hit;
        for(int y=20;y<view->height()-20&&!hit;y+=3)for(int x=20;x<view->width()-20;x+=3) {
            const auto c=view->selection_candidates_at(QPointF(x,y));
            if(!c.empty()&&c.front().kind==viewer::CandidateKind::SketchTrimPiece&&c.front().owner_id==sketch.id){hit=QPointF(x,y);break;}
        }
        check(hit.has_value(),"Circle has no selectable axis-bounded trim piece");
        for(const auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
            QMouseEvent event(type,*hit,QPointF(view->mapToGlobal(hit->toPoint())),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();
        }
        window.findChild<QAction*>("finishSketchAction")->trigger();flush();
        app::SketchPropertiesDialog* properties{};for(auto* d:window.findChildren<QDialog*>())if(auto* p=dynamic_cast<app::SketchPropertiesDialog*>(d);p&&p->isVisible())properties=p;
        check(properties,"Circle trim did not return to Sketch Properties");const auto result=properties->pending_value().first;
        check(result.circles.empty()&&result.arcs.size()==1,"Circle mouse trim did not retain one arc");
        for(const auto& id:{result.arcs.front().start_point_id,result.arcs.front().end_point_id}) {
            const auto* p=result.find_point(id);
            check(std::ranges::any_of(result.constraints,[&](const auto& c){return c.kind==sketcher::ConstraintKind::PointOnLine&&c.first_point_id==id&&((c.geometry_id=="sketch_axis:x"&&std::abs(p->y)<1e-7)||(c.geometry_id=="sketch_axis:y"&&std::abs(p->x)<1e-7));}),"Mouse-trimmed endpoint lacks axis C");
        }
        properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        check(document::PartDocument::load(path).sketches.front().serialized()==result.serialized(),"Mouse trim lost native endpoint C on save/reopen");
        std::cout<<"Circle axis mouse trim: both endpoint C equations and native save/reopen passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

int verify_form_arcs_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        const auto source=std::filesystem::u8path(qEnvironmentVariable("ZIMA_VERIFY_FORM_ARCS_DOCUMENT").toStdString());
        for(int kind=0;kind<3;++kind)for(bool reverse:{false,true}) {
            auto part=document::PartDocument::load(source);const auto sketch=part.sketches.back();
            const auto body=part.body_history.owner(sketch.owner_container_id)->scope.id;
            const auto path=directory/("form-arcs-"+std::to_string(kind)+"-"+std::to_string(reverse)+".prtz");part.save(path);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
            const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
            check(window.open_document_path(QString::fromStdString(path.string())),"FORM arc fixture open failed");flush();
            check(activate_test_body(application,window,body),"FORM arc Body activation failed");
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"FORM arc Sketch row missing");window.show_tree_item_properties(row);flush();
            QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"FORM arc View missing");
            const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button){QMouseEvent e(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&e);flush();};
            const auto pick=[&](const std::string& key) {
                std::optional<QPointF> hit;
                for(int y=15;y<view->height()-15&&!hit;y+=3)for(int x=15;x<view->width()-15;x+=3) {
                    const auto c=view->selection_candidates_at(QPointF(x,y));
                    if(!c.empty()&&c.front().owner_id==sketch.id&&c.front().semantic_key==key){hit=QPointF(x,y);break;}
                }
                if(!hit)throw std::runtime_error("FORM GUI candidate missing: "+key+" kind="+std::to_string(kind));
                mouse(*hit,QEvent::MouseMove,Qt::NoButton);
                check(view->hovered_candidate()&&view->hovered_candidate()->semantic_key==key,"FORM hover differs from candidate");
                mouse(*hit,QEvent::MouseButtonPress,Qt::LeftButton);mouse(*hit,QEvent::MouseButtonRelease,Qt::LeftButton);
            };
            const std::string first=kind==0?"point:"+sketch.segments.front().second_point_id:"arc:"+sketch.arcs.front().id;
            const std::string second=kind==0?"point:"+sketch.arcs.back().start_point_id:kind==1?"sketch_axis:x":"arc:"+sketch.arcs.back().id;
            auto* action=window.findChild<QAction*>(kind==0?"sketchCoincidentAction":kind==1?"sketchTangentAction":"sketchEqualAction");
            check(action&&action->isEnabled(),"FORM arc constraint action unavailable");action->trigger();flush();
            pick(reverse?second:first);pick(reverse?first:second);
            const auto marker_count=view->mesh().constraint_markers.size();
            auto* undo=window.findChild<QAction*>("undoAction");auto* redo=window.findChild<QAction*>("redoAction");
            check(undo&&undo->isEnabled(),"FORM relation has no Undo");undo->trigger();flush();
            check(view->mesh().constraint_markers.size()<marker_count||kind==0,"FORM Undo did not remove relation");
            redo->trigger();flush();check(view->mesh().constraint_markers.size()==marker_count,"FORM Redo did not restore relation");
            // Relation tools stay armed for repeated entry. End the tool before
            // exercising the ordinary point-drag interaction.
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&window,&escape);flush();
            const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"FORM drag camera ray missing");const auto q=sketch.intersect_ray(ray->first,ray->second);check(q.has_value(),"FORM drag plane missing");return *q;};
            const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
            const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
            const auto screen=[&](double x,double y){return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);};
            const auto center_position=[&] {
                const auto p=std::ranges::find_if(view->mesh().points,[&](const auto& p){return p.reference.owner_id==sketch.id&&p.reference.semantic_key=="point:"+sketch.arcs.front().center_point_id;});
                check(p!=view->mesh().points.end(),"FORM drag lost arc center identity");return sketch.local_point(p->position);
            };
            for(int i=0;i<3;++i) {
                const auto before=center_position();const auto at=screen(before[0],before[1]),to=screen(before[0],before[1]-.4);
                mouse(at,QEvent::MouseMove,Qt::NoButton);
                check(view->hovered_candidate()&&view->hovered_candidate()->semantic_key=="point:"+sketch.arcs.front().center_point_id,"FORM center is not available for ordinary dragging");
                mouse(at,QEvent::MouseButtonPress,Qt::LeftButton);
                QMouseEvent drag(QEvent::MouseMove,to,QPointF(view->mapToGlobal(to.toPoint())),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&drag);flush();mouse(to,QEvent::MouseButtonRelease,Qt::LeftButton);
                const auto after=center_position();check(std::hypot(after[0]-before[0],after[1]-before[1])>1e-4,"FORM permitted center drag did not move");
                undo->trigger();flush();const auto undone=center_position();check(std::hypot(undone[0]-before[0],undone[1]-before[1])<1e-7,"FORM center drag Undo failed");
                redo->trigger();flush();const auto redone=center_position();check(std::hypot(redone[0]-after[0],redone[1]-after[1])<1e-7,"FORM center drag Redo failed");
            }
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* d:window.findChildren<QDialog*>())if(d->isVisible()&&(dynamic_cast<app::SketchPropertiesDialog*>(d)||dynamic_cast<app::PrimitivePropertiesDialog*>(d)))
                d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
            flush();QEventLoop commit;QTimer::singleShot(150,&commit,&QEventLoop::quit);commit.exec();flush();
            if(auto* error=window.findChild<QLabel*>("featureValidationError");error&&error->isVisible()&&!error->text().isEmpty())
                throw std::runtime_error("FORM feature commit rejected: "+error->text().toStdString());
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            const auto saved=document::PartDocument::load(path);const auto& result=saved.sketches.back();
            if(kind==0)check(result.segments.front().second_point_id==result.arcs.back().start_point_id,"FORM GUI C did not persist shared endpoint");
            if(kind==1)check(std::ranges::any_of(result.constraints,[&](const auto& c){return c.kind==sketcher::ConstraintKind::Tangent&&(c.geometry_id=="sketch_axis:x"||c.second_geometry_id=="sketch_axis:x");}),"FORM GUI T did not persist");
            if(kind==1){const auto* p=result.find_point(result.arcs.front().start_point_id);const auto* c=result.find_point(result.arcs.front().center_point_id);check(std::abs(p->y)<1e-6&&std::abs(p->x-c->x)<1e-6,"FORM GUI drag lost horizontal tangency");}
            if(kind==2)check(std::abs(result.arcs.front().radius-result.arcs.back().radius)<1e-6&&std::ranges::any_of(result.constraints,[](const auto& c){return c.kind==sketcher::ConstraintKind::EqualRadius;}),"FORM GUI equal radii did not persist");
            for(const auto& arc:result.arcs)for(const auto& id:{arc.start_point_id,arc.end_point_id}) {
                const auto* p=result.find_point(id);const auto* c=result.find_point(arc.center_point_id);
                check(std::abs(std::hypot(p->x-c->x,p->y-c->y)-arc.radius)<1e-6,"FORM saved GUI endpoint escaped its arc");
            }
            std::cout<<"FORM arc GUI kind="<<kind<<" reverse="<<reverse<<" passed\n";
        }
        return verify_axis_circle_trim_ui(application,directory);
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
