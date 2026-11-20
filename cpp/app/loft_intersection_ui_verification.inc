int verify_loft_intersection_ui(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        const auto click=[&](QWidget* widget,QPointF at,Qt::MouseButton button=Qt::LeftButton){
            QMouseEvent move(QEvent::MouseMove,at,QPointF(widget->mapToGlobal(at.toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(widget,&move);flush();
            for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){QMouseEvent e(type,at,QPointF(widget->mapToGlobal(at.toPoint())),button,type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(widget,&e);flush();}
        };
        const bool outline=qEnvironmentVariableIsSet("ZIMA_VERIFY_REFERENCE_OUTLINE_ONLY");
        std::vector<kernel::BodyResult> cache;
        auto part=document::PartDocument::load(std::filesystem::path(qEnvironmentVariable("ZIMA_INTERSECTION_FIXTURE").toStdWString()),&cache);
        auto found=std::ranges::find_if(part.history,[&](const auto& f){return outline?
            std::ranges::any_of(part.sketches,[&](const auto& sketch){return sketch.owner_container_id==f.id&&sketch.bsplines.size()==4;}):
            f.feature_kind==document::FeatureKind::Loft;});check(found!=part.history.end(),"No profile fixture");
        const auto feature_id=found->id;const auto body=part.body_history.owner(feature_id)->scope.id;
        const auto path=directory/"intersection-input.prtz";part.save(path,cache);
        window.resize(1450,1050);window.show();flush();check(window.open_document_path(QString::fromStdString(path.string())),"Cannot open intersection copy");flush();
        check(window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",body}}}}.dump())).ok,"Cannot activate intersection Body");flush();
        auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* item{};
        if(outline) {
            window.findChild<QAction*>("regenerateDocumentAction")->trigger();flush();
            bool checked=false;
            for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==part.history.back().id&&
                (*it)->data(0,Qt::UserRole+3)=="part-container") {
                check(!(*it)->data(0,missing_reference_role).toBool(),"Independent Sketch remains red after reference recovery");checked=true;
            }
            check(checked,"Independent Sketch Tree row missing");
            window.grab().save(QString::fromStdString((directory/"recovered-sketch-tree.png").string()));
        }
        for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==feature_id&&(*it)->data(0,Qt::UserRole+3).toString()=="part-container")item=*it;
        check(item,"Intersection Loft row missing");window.show_tree_item_properties(item);flush();
        if(outline) {
            const auto sketch=std::ranges::find(part.sketches,feature_id,&sketcher::Sketch::owner_container_id);
            check(sketch!=part.sketches.end(),"Converted outline Sketch missing");
            const auto hidden=sketcher::hidden_derived_control_points(*sketch);
            check(hidden.size()>20&&sketch->bsplines.size()==4,"User outline fixture is not the converted capsule");
            auto* editor=dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("primitiveProperties"));
            if(!editor)for(auto* widget:window.findChildren<QDialog*>())if(auto* value=dynamic_cast<PrimitivePropertiesDialog*>(widget);value&&value->isVisible())editor=value;
            check(editor,"Converted outline Properties missing");
            editor->findChild<QPushButton*>("featureSketchButton")->click();flush();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));check(view,"Outline View missing");
            for(const auto& point:view->mesh().points)if(point.reference.owner_id==sketch->id&&point.reference.semantic_key.starts_with("point:"))
                check(!hidden.contains(point.reference.semantic_key.substr(6)),"Converted curve exposes interior poles as contour points");
            for(QTreeWidgetItemIterator it(tree);*it;++it)
                check(!hidden.contains((*it)->data(0,Qt::UserRole).toString().toStdString()),"Converted curve exposes interior poles in the Tree");
            check(std::ranges::count_if(view->mesh().edges,[&](const auto& edge){return edge.reference.owner_id==sketch->id&&edge.reference.semantic_key.starts_with("bspline:");})==4,
                "Hiding converted poles removed an exact contour curve");
            {QEventLoop loop;QTimer::singleShot(950,&loop,&QEventLoop::quit);loop.exec();}flush();
            window.grab().save(QString::fromStdString((directory/"converted-outline.png").string()));
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            editor=nullptr;
            for(auto* widget:window.findChildren<QDialog*>())if(auto* value=dynamic_cast<PrimitivePropertiesDialog*>(widget);value&&value->isVisible())editor=value;
            check(editor,"Converted outline parent did not reopen");
            editor->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
            std::cout<<"Converted capsule: "<<hidden.size()<<" interior poles hidden, all four exact curves retained\n";return 0;
        }
        auto* parent=dynamic_cast<LoftDialog*>(window.findChild<QWidget*>("loftDialog"));check(parent,"Intersection Loft dialog missing");
        if(qEnvironmentVariableIsSet("ZIMA_VERIFY_DRAFT_CIRCLE")) {
            auto changed=sketcher::Sketch::from_serialized(parent->pending.loft.sketches[1].sketch_serialized);
            changed.circles.front().radius*=1.13;
            parent->pending.loft.sketches[1].sketch_serialized=changed.serialized();
        }
        auto owned=document::create_loft_sketch(parent->pending);auto guide=sketcher::Sketch::from_serialized(owned.sketch_serialized);
        guide.plane=qEnvironmentVariableIsSet("ZIMA_INTERSECTION_YZ")?sketcher::SketchPlane::YZ:sketcher::SketchPlane::XZ;guide.plane_auto=false;owned.sketch_serialized=guide.serialized();
        parent->pending.loft.guides.push_back(owned.sketch_feature->id);parent->pending.loft.sketches.push_back(owned);document::resolve_loft_sketches(parent->pending,{});parent->refresh();parent->changed();flush();
        parent->open_sketch(true,0);flush();auto* child=dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("generalSurfaceSketchProperties"));check(child,"Intersection child missing");
        child->findChild<QPushButton*>("featureSketchButton")->click();flush();
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));check(view,"Intersection View missing");
        const auto sketch=document::general_surface_display_sketch(document::loft_sketch_editor_carrier(parent->pending),parent->pending.loft.sketches.back());
        const auto verify_context=[&]{
            for(const auto& edge:view->mesh().edges)if(edge.reference.owner_id!=sketch.id)
                check(!edge.reference.semantic_key.starts_with("external_edge:"),"Other owned Sketch references leaked into the guide editor");
            for(const auto& point:view->mesh().points)if(point.reference.owner_id!=sketch.id)
                check(!point.reference.semantic_key.starts_with("external_point:"),"Other owned Sketch point references leaked into the guide editor");
        };
        verify_context();
        view->set_view_direction(sketch.resolved_normal);{QEventLoop loop;QTimer::singleShot(950,&loop,&QEventLoop::quit);loop.exec();}flush();
        view->fit_points({sketch.world_point(-110,-180),sketch.world_point(110,20)});flush();
        const auto screen=[&](kernel::Vec3 world){
            const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"No guide ray");return *sketch.intersect_ray(ray->first,ray->second);};
            const auto a=local({0,0}),b=local({100,0}),c=local({0,100}),q=sketch.local_point(world);
            const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
            return QPointF(((q[0]-a[0])*vy-(q[1]-a[1])*vx)/det,(ux*(q[1]-a[1])-uy*(q[0]-a[0]))/det);
        };
        window.findChild<QAction*>("sketchIntersectionReferenceAction")->trigger();flush();
        const bool multiple=qEnvironmentVariableIsSet("ZIMA_VERIFY_MULTIPLE_INTERSECTIONS");
        const auto choices=[&]{std::vector<kernel::ViewerPoint> result;for(const auto& p:view->mesh().points)if(p.reference.owner_id==sketch.id&&p.reference.semantic_key.starts_with("external_point:")&&p.reference.semantic_key!="external_point:sketch_origin")result.push_back(p);return result;};
        const auto pick_profile_line=[&](std::size_t profile_index) {
            const auto source=document::general_surface_display_sketch(document::loft_sketch_editor_carrier(parent->pending),parent->pending.loft.sketches[profile_index]);
            for(const auto& segment:source.segments) {
                if(segment.construction)continue;
                const auto* first=source.find_point(segment.first_point_id);const auto* last=source.find_point(segment.second_point_id);
                const auto a=source.world_point(first->x,first->y),b=source.world_point(last->x,last->y);
                const auto distance=[&](auto p){return (p.x-sketch.resolved_origin.x)*sketch.resolved_normal.x+(p.y-sketch.resolved_origin.y)*sketch.resolved_normal.y+(p.z-sketch.resolved_origin.z)*sketch.resolved_normal.z;};
                if(distance(a)*distance(b)>=0)continue;
                const auto at=screen({(a.x+b.x)/2,(a.y+b.y)/2,(a.z+b.z)/2});
                const auto candidates=view->selection_candidates_at(at);
                for(std::size_t i=0;i<candidates.size();++i)if(candidates[i].owner_id==source.id&&candidates[i].semantic_key=="segment:"+segment.id) {
                    for(std::size_t j=0;j<i;++j)click(view,at,Qt::RightButton);click(view,at);flush();return;
                }
            }
            throw std::runtime_error("No crossing profile line offered");
        };
        if(multiple){pick_profile_line(0);check(choices().size()==1,"First profile intersection was not retained");}
        const auto retained_before=choices();
        const auto profile=document::general_surface_display_sketch(document::loft_sketch_editor_carrier(parent->pending),parent->pending.loft.sketches[1]);
        const auto circle=profile.circles.front();const auto* center=profile.find_point(circle.center_point_id);
        const auto at=screen(profile.world_point(center->x+(guide.plane==sketcher::SketchPlane::XZ?circle.radius*.8:0),center->y+(guide.plane==sketcher::SketchPlane::YZ?circle.radius*.8:0)));
        auto candidates=view->selection_candidates_at(at);check(!candidates.empty(),"No circle source candidates");
        std::size_t index{};for(;index<candidates.size();++index){std::cout<<"Source "<<candidates[index].owner_id<<":"<<candidates[index].semantic_key<<std::endl;if(candidates[index].owner_id==profile.id&&candidates[index].semantic_key=="circle:"+circle.id)break;}
        check(index<candidates.size(),"Circle source not offered");for(std::size_t i=0;i<index;++i)click(view,at,Qt::RightButton);click(view,at);
        auto before=choices();std::cout<<"Offered crosses "<<before.size()<<std::endl;check(before.size()==retained_before.size()+2,"Circle did not offer two intersections");
        const auto selected=std::ranges::find_if(before,[&](const auto& p){return std::ranges::none_of(retained_before,[&](const auto& old){return old.reference==p.reference;});});
        window.grab().save(QString::fromStdString((directory/"intersection-before.png").string()));
        click(view,screen(selected->position));auto after=choices();std::cout<<"After click crosses "<<after.size()<<std::endl;
        for(const auto& p:after)std::cout<<p.reference.semantic_key<<" at "<<p.position.x<<","<<p.position.y<<","<<p.position.z<<std::endl;
        window.grab().save(QString::fromStdString((directory/"intersection-after.png").string()));
        check(after.size()==before.size(),"Selecting one intersection removed the chosen or remaining cross");
        verify_context();
        for(const auto& p:before)check(std::ranges::any_of(after,[&](const auto& q){return q.reference==p.reference&&std::hypot(q.position.x-p.position.x,q.position.y-p.position.y,q.position.z-p.position.z)<1e-8;}),"Selected intersection changed position or identity");
        const bool both=qEnvironmentVariableIsSet("ZIMA_VERIFY_BOTH_INTERSECTIONS");
        if(both) {
            const auto other=std::ranges::find_if(before,[&](const auto& p){return p.reference!=selected->reference&&std::ranges::none_of(retained_before,[&](const auto& old){return old.reference==p.reference;});});
            click(view,screen(other->position));flush();
            for(const auto& p:{*selected,*other}) {
                const auto local=profile.local_point(p.position);
                check(std::abs(std::hypot(local[0]-center->x,local[1]-center->y)-circle.radius)<1e-6,"Circle intersection does not use the current draft radius");
            }
            const auto snapshot=window.active_sketch_for_test()->serialized();
            window.findChild<QAction*>("undoAction")->trigger();flush();
            window.findChild<QAction*>("redoAction")->trigger();flush();
            check(window.active_sketch_for_test()->serialized()==snapshot,"Circle branch Undo/Redo changed identity");
        }
        click(view,{50,50},Qt::MiddleButton);flush();
        after=choices();check(after.size()==retained_before.size()+(both?2:1)&&std::ranges::any_of(after,[&](const auto& p){return p.reference==selected->reference&&p.always_visible;}),"Confirmed intersection vanished after leaving branch selection");
        if(multiple){pick_profile_line(2);check(choices().size()==(both?4:3),"Third profile intersection replaced an earlier source");}
        verify_context();
        window.findChild<QAction*>("finishSketchAction")->trigger();flush();
        child->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        const auto stored=sketcher::Sketch::from_serialized(parent->pending.loft.sketches.back().sketch_serialized);
        check(stored.external_references.size()==(multiple?3u:1u)+(both?1u:0u)&&std::ranges::all_of(stored.external_references,[](const auto& r){return r.curve_intersection.has_value()&&!r.broken;}),"Intersection did not persist as curve intersection");
        parent->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        std::cout<<"Owned Loft intersection selection passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
