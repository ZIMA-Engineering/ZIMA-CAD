Q_NEVER_INLINE static int verify_partial_generator_ui(QApplication& application,
        const std::filesystem::path& directory) {
    try {
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        const auto source=std::filesystem::u8path(qEnvironmentVariable("ZIMA_VERIFY_PARTIAL_GENERATOR_SOURCE").toStdString());
        for(bool profile:{false,true}) {
            std::vector<kernel::BodyResult> geometry;const auto model=document::PartDocument::load(source,&geometry);
            const auto owner=std::ranges::find_if(model.history,[](const auto& feature){return feature.name=="Vytažení 005";});
            check(owner!=model.history.end(),"Actual target Feature 005 missing");
            const auto original=std::ranges::find(model.sketches,owner->feature.sketch_id,&sketcher::Sketch::id);
            check(original!=model.sketches.end(),"Actual offset Sketch missing");
            const auto file=directory/(profile?"partial-generator-profile.prtz":"partial-generator-reference.prtz");model.save(file,geometry);
            AssemblyWorkspaceWindow window(QString::fromStdString(document::path_to_utf8(directory)));window.showMaximized();flush();
            check(window.open_document_path(QString::fromStdString(document::path_to_utf8(file))),"Actual generator copy did not open");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");
            QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==original->id&&
                    (*it)->data(0,Qt::UserRole+3).toString()=="part-sketch"){row=*it;break;}
            check(row,"Actual generator Sketch Tree item missing");window.show_tree_item_properties(row);flush();
            auto* open=window.findChild<QPushButton*>("sketchOpenButton");
            if(!open)open=window.findChild<QPushButton*>("featureSketchButton");
            if(!open)open=window.findChild<QPushButton*>("primitiveOwnSketchButton");
            // Opening the child Sketch of a Feature already enters its
            // embedded editor through the shared Tree Properties path.
            if(open){open->click();flush();}
            check(window.findChild<QAction*>("finishSketchAction")->isEnabled(),"Actual generator Sketch editor missing");
            QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();
            auto* action=window.findChild<QAction*>(profile?"sketchExternalProfileAction":"sketchExternalReferenceAction");
            check(action&&action->isEnabled(),"Actual generator reference action unavailable");action->trigger();flush();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));check(view,"Actual generator View missing");
            view->set_view_direction({1,-2,3},0);QEventLoop camera;QTimer::singleShot(950,&camera,&QEventLoop::quit);camera.exec();view->fit_all();flush();
            const kernel::ViewerEdge* left{};double leftmost=std::numeric_limits<double>::infinity();
            const auto first=std::ranges::find_if(model.history,[](const auto& feature){return feature.name=="Vytažení 001";});
            check(first!=model.history.end(),"Actual rounded source Feature missing");
            for(const auto& edge:view->mesh().original_references.edges)
                if(edge.reference.owner_id==first->id&&sketcher::is_face_curve_key(edge.reference.semantic_key)&&edge.points.size()==2) {
                    const auto x=(edge.points.front().x+edge.points.back().x)*.5;
                    if(x<leftmost){leftmost=x;left=&edge;}
                }
            check(left,"Actual partial-cylinder source has no individual generator");
            const auto expected=*left;std::optional<QPointF> pixel;std::size_t selected_index{};
            for(int y=10;y<view->height()-10&&!pixel;y+=3)for(int x=10;x<view->width()-10&&!pixel;x+=3) {
                const auto hits=view->selection_candidates_at({double(x),double(y)});
                for(std::size_t i=0;i<hits.size();++i)if(hits[i].owner_id==expected.reference.owner_id&&hits[i].semantic_key==expected.reference.semantic_key) {
                    pixel=QPointF(x,y);selected_index=i;break;
                }
            }
            check(pixel.has_value(),"Actual left generator cannot be offered by the real View picker");
            const auto mouse=[&](Qt::MouseButton button) {
                for(auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                    QMouseEvent event(type,*pixel,view->mapToGlobal(pixel->toPoint()),type==QEvent::MouseMove?Qt::NoButton:button,
                        type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);
                    QApplication::sendEvent(view,&event);flush();
                }
            };
            for(std::size_t i=0;i<selected_index;++i)mouse(Qt::RightButton);mouse(Qt::LeftButton);
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            bool accepted=false;
            for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible())
                if(auto* buttons=dialog->findChild<QDialogButtonBox*>())if(auto* ok=buttons->button(QDialogButtonBox::Ok);ok&&ok->isEnabled()) {
                    ok->click();flush();accepted=true;
                }
            check(accepted,"Actual generator owner could not accept the pending Sketch");
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            const auto saved=document::PartDocument::load(file);const auto stored=std::ranges::find(saved.sketches,original->id,&sketcher::Sketch::id);
            check(stored!=saved.sketches.end(),"Saved generator Sketch missing");
            const auto reference=std::ranges::find_if(stored->external_references,[&](const auto& r){
                return r.source_owner_id==expected.reference.owner_id&&r.source_semantic_key==expected.reference.semantic_key;});
            check(reference!=stored->external_references.end()&&!reference->broken&&!reference->infinite,
                "Actual generator click did not persist one finite analytic reference");
            check(reference->cached_paths.size()==1&&reference->cached_paths.front().size()==2,"Actual generator reference contains a whole face contour");
            for(unsigned i=0;i<2;++i) {
                const auto point=original->local_point(expected.points[i]);const auto actual=reference->cached_paths.front()[i];
                check(std::hypot(actual[0]-point[0],actual[1]-point[1])<1e-7,"Actual offset generator cache changed its exact source projection");
            }
            check(stored->bsplines==original->bsplines,"A straight generator created a spline");
            check(stored->segments.size()==original->segments.size()+(profile?1:0),"Actual generator contour did not create exactly one native Segment");
            window.grab().save(QString::fromStdString(document::path_to_utf8(directory/(profile?"partial-generator-profile.png":"partial-generator-reference.png"))));
            std::cout<<"Actual left partial-cylinder generator "<<(profile?"profile":"reference")<<" passed\n";
        }
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
