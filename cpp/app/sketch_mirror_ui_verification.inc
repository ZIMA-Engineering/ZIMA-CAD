int verify_sketch_mirror_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        for(int axis_kind=0;axis_kind<4;++axis_kind) {
            auto part=document::PartDocument::create_default();auto feature=document::PartDocument::create_sketch_container();
            auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;
            const auto first=sketch.add_segment(12,10,28,10),second=sketch.add_segment(28,10,28,24);
            const auto source_point=sketch.segments.front().first_point_id;
            auto dimension=sketch.create_segment_dimension(first);dimension.locked=false;sketch.apply_dimension(dimension);
            const auto axis=axis_kind<2?std::string(axis_kind==0?"sketch_axis:x":"sketch_axis:y"):
                sketch.add_segment(axis_kind==2?3:-20,-30,axis_kind==2?3:20,30,1e-6,true);
            part.history={feature};part.sketches={sketch};document::BodyHistoryGraph graph;
            const auto body=graph.create_body("Mirror interaction");graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
            const auto path=directory/("sketch-mirror-"+std::to_string(axis_kind)+".prtz");part.save(path);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
            check(window.open_document_path(QString::fromStdString(path.string())),"Mirror fixture cannot open");flush();
            check(activate_test_body(application,window,body),"Mirror fixture Body cannot activate");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"Mirror Sketch row missing");window.show_tree_item_properties(row);flush();
            window.findChild<QPushButton*>("sketchOpenButton")->click();flush();
            QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Mirror View missing");
            const auto local=[&](QPointF pixel){const auto ray=view->ray_at(pixel);check(ray.has_value(),"Mirror View has no ray");const auto q=sketch.intersect_ray(ray->first,ray->second);check(q.has_value(),"Mirror View is not facing Sketch");return *q;};
            const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
            const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
            const auto screen=[&](double x,double y){return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);};
            const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons=Qt::NoButton){QMouseEvent e(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&e);flush();};
            const auto click=[&](QPointF p){mouse(p,QEvent::MouseMove,Qt::NoButton);mouse(p,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(p,QEvent::MouseButtonRelease,Qt::LeftButton);};
            auto* mirror=window.findChild<QAction*>("sketchMirrorAction");check(mirror&&mirror->isEnabled(),"Mirror command unavailable");
            // No preselection is an explicit no-op. Cancel after preselection
            // also leaves the Sketch unchanged and restores ordinary picking.
            mirror->trigger();flush();check(mirror->isEnabled(),"Mirror armed without source selection");
            click(screen(16,10));check(view->confirmed_candidate()&&view->confirmed_candidate()->semantic_key=="segment:"+first,"Mirror source click did not select its exact segment");
            mirror->trigger();flush();QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);flush();
            check(mirror->isEnabled(),"Cancelled Mirror did not restore command availability");
            // Use the ordinary selection rectangle for a connected pair.
            const auto p=screen(9,27),q=screen(31,7);
            mouse(p,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(q,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(q,QEvent::MouseButtonRelease,Qt::LeftButton);
            check(view->sketch_selection().size()>=2,"Mirror box selection did not collect a connected pair");
            mirror->trigger();flush();check(!mirror->isEnabled(),"Mirror did not enter axis selection");
            const std::string axis_key=axis_kind<2?axis:"segment:"+axis;
            const auto axis_pixel=axis_kind==0?screen(-12,0):axis_kind==1?screen(0,-20):axis_kind==2?screen(3,-20):screen(-12,-18);
            const auto candidates=view->selection_candidates_at(axis_pixel);
            check(!candidates.empty()&&candidates.front().semantic_key==axis_key,"Mirror axis was not offered by the common picker");
            click(axis_pixel);
            check(mirror->isEnabled(),"Mirror axis click was ignored; command remained active");
            const auto geometry_keys=[&]{std::set<std::string> keys;for(const auto& edge:view->mesh().edges)if(edge.reference.owner_id==sketch.id&&edge.reference.semantic_key.starts_with("segment:"))keys.insert(edge.reference.semantic_key);return keys;};
            check(geometry_keys().size()==(axis_kind<2?4u:5u),"Mirror axis confirmation did not create both copied segments");
            window.findChild<QAction*>("undoAction")->trigger();flush();check(geometry_keys().size()==(axis_kind<2?2u:3u),"Mirror Undo did not remove exactly the copied pair");
            window.findChild<QAction*>("redoAction")->trigger();flush();check(geometry_keys().size()==(axis_kind<2?4u:5u),"Mirror Redo did not restore the copied pair");
            // Real point drag after leaving the command; verify its reflection
            // independently from persisted Symmetric equations after saving.
            click(screen(-18,35));const auto from=screen(12,10),to=screen(15,13);
            mouse(from,QEvent::MouseMove,Qt::NoButton);mouse(from,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(to,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(to,QEvent::MouseButtonRelease,Qt::LeftButton);
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* dialog:window.findChildren<QDialog*>())if(auto* properties=dynamic_cast<app::SketchPropertiesDialog*>(dialog);properties&&properties->isVisible())properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            const auto reopened=document::PartDocument::load(path);const auto& result=reopened.sketches.front();
            check(result.segments.size()==(axis_kind<2?4u:5u),"Mirror native save/reopen lost geometry");
            check(std::hypot(result.find_point(source_point)->x-12,result.find_point(source_point)->y-10)>1,"Mirror source could not be dragged after completion");
            const auto origin=axis_kind==0?std::array{0.,0.}:axis_kind==1?std::array{0.,0.}:axis_kind==2?std::array{3.,-30.}:std::array{-20.,-30.};
            const auto direction=axis_kind==0?std::array{1.,0.}:axis_kind==1?std::array{0.,1.}:axis_kind==2?std::array{0.,60.}:std::array{40.,60.};
            int relations=0;
            for(const auto& relation:result.constraints)if(relation.kind==sketcher::ConstraintKind::Symmetric&&relation.geometry_id==axis) {
                const auto* source=result.find_point(relation.first_point_id);const auto* copy=result.find_point(relation.second_point_id);
                const auto dot=((source->x-origin[0])*direction[0]+(source->y-origin[1])*direction[1])/(direction[0]*direction[0]+direction[1]*direction[1]);
                check(std::hypot(copy->x-(2*(origin[0]+dot*direction[0])-source->x),copy->y-(2*(origin[1]+dot*direction[1])-source->y))<1e-6,"Saved Mirror point does not satisfy independent reflection equation");++relations;
            }
            check(relations==3&&result.dimensions.size()==1&&result.dimensions.front().id==dimension.id,"Mirror lost shared corner topology or duplicated source dimensions");
            if(axis_kind==3)view->grabFramebuffer().save(QString::fromStdString((directory/"sketch-mirror.png").string()));
            std::cout<<"Sketch Mirror GUI axis "<<axis_kind<<": source/axis picking, Cancel, Undo/Redo, point drag and native reopen passed\n";
        }
        return 0;
    }catch(const std::exception& error){std::cerr<<"Sketch Mirror GUI: "<<error.what()<<'\n';return 1;}
}

void verify_form_edge_dimension_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    std::ifstream file(qgetenv("ZIMA_FORM_EDGE_SKETCH_FIXTURE").toStdString());check(bool(file),"FORM-EDGE GUI fixture missing");std::stringstream buffer;buffer<<file.rdbuf();auto sketch=sketcher::Sketch::from_serialized(buffer.str());
    auto part=document::PartDocument::create_default();auto feature=document::PartDocument::create_sketch_container();feature.feature.sketch_id=sketch.id;sketch.owner_container_id=feature.id;part.history={feature};part.sketches={sketch};document::BodyHistoryGraph graph;auto body=graph.create_body("FORM-EDGE dimensions");graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();sketch=part.sketches.front();const auto path=directory/"form-edge-dimensions.prtz";part.save(path);
    app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();check(window.open_document_path(QString::fromStdString(path.string())),"FORM-EDGE GUI cannot open");flush();check(activate_test_body(application,window,body),"FORM-EDGE Body cannot activate");flush();auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}check(row,"FORM-EDGE Sketch missing");window.show_tree_item_properties(row);flush();window.findChild<QPushButton*>("sketchOpenButton")->click();flush();QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"FORM-EDGE View missing");
    for(const auto& d:sketch.dimensions)for(double factor:{1.04,.97,1.02}) {
        viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;candidate.owner_id=sketch.id;candidate.semantic_key="dimension:"+d.id;for(std::size_t i=0;i<view->mesh().dimensions.size();++i)if(view->mesh().dimensions[i].reference.semantic_key==candidate.semantic_key)candidate.geometry_index=i;
        window.edit_dimension_inline(candidate);flush();auto* editor=view->findChild<QLineEdit*>("inlineDimensionValueEdit");check(editor&&editor->isVisible(),"FORM-EDGE inline dimension editor missing");editor->setText(QString::number(sketcher::dimension_display_value(d)*factor,'g',15));QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);flush();window.findChild<QAction*>("undoAction")->trigger();flush();window.findChild<QAction*>("redoAction")->trigger();flush();
    }
    const auto local=[&](QPointF pixel){const auto ray=view->ray_at(pixel);check(ray.has_value(),"FORM-EDGE ray missing");const auto p=sketch.intersect_ray(ray->first,ray->second);check(p.has_value(),"FORM-EDGE sketch plane missing");return *p;};const auto o=local({0,0}),u=local({100,0}),v=local({0,100});const double ux=(u[0]-o[0])/100,uy=(u[1]-o[1])/100,vx=(v[0]-o[0])/100,vy=(v[1]-o[1])/100,det=ux*vy-uy*vx;const auto screen=[&](std::array<double,2> p){return QPointF(((p[0]-o[0])*vy-(p[1]-o[1])*vx)/det,(ux*(p[1]-o[1])-uy*(p[0]-o[0]))/det);};const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons=Qt::NoButton){QMouseEvent e(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&e);flush();};
    const auto center_id=sketch.arcs.back().center_point_id;std::optional<kernel::Vec3> center;for(const auto& p:view->mesh().original_references.points)if(p.reference.semantic_key=="point:"+center_id)center=p.position;check(center.has_value(),"FORM-EDGE center cannot be dragged");const auto c=sketch.intersect_ray(*center,sketch.normal());check(c.has_value(),"FORM-EDGE center frame missing");const auto from=screen(*c),to=screen({(*c)[0],(*c)[1]-.3});mouse(from,QEvent::MouseMove,Qt::NoButton);mouse(from,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(to,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(to,QEvent::MouseButtonRelease,Qt::LeftButton);
    window.findChild<QAction*>("finishSketchAction")->trigger();flush();for(auto* dialog:window.findChildren<QDialog*>())if(auto* properties=dynamic_cast<app::SketchPropertiesDialog*>(dialog);properties&&properties->isVisible())properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();const auto loaded=document::PartDocument::load(path);const auto& result=loaded.sketches.front();verification::tangency::arc_equations(result);for(const auto& d:sketch.dimensions)if(d.kind==sketcher::DimensionKind::Radius)check(std::abs(std::ranges::find(result.dimensions,d.id,&sketcher::SketchDimension::id)->value-d.value*1.02)<1e-6,"FORM-EDGE GUI radius edit failed");check(std::abs(result.find_point(center_id)->y-(*c)[1])>.1,"FORM-EDGE GUI center drag failed");view->grabFramebuffer().save(QString::fromStdString((directory/"form-edge-dimensions.png").string()));std::cout<<"FORM-EDGE GUI: all four dimensions edited three times, Undo/Redo, center drag, native reopen and independent equations passed\n";
}

void verify_form_edge_preview_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    auto part=document::PartDocument::load(qgetenv("ZIMA_FORM_EDGE_LIMIT_FIXTURE").toStdString());
    const auto id=part.history.front().id;const auto body=part.body_history.active_body_id();const auto path=directory/"form-edge-preview.prtz";part.save(path);
    app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();check(window.open_document_path(QString::fromStdString(path.string())),"FORM-EDGE preview cannot open");flush();check(activate_test_body(application,window,body),"FORM-EDGE preview Body cannot activate");flush();
    auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==id&&(*i)->data(0,Qt::UserRole+3)=="part-container"){row=*i;break;}check(row,"FORM-EDGE preview Feature missing");window.show_tree_item_properties(row);flush();
    app::PrimitivePropertiesDialog* dialog{};for(auto* widget:window.findChildren<QDialog*>())if(widget->objectName()=="featurePropertiesDialog"&&widget->isVisible())dialog=dynamic_cast<app::PrimitivePropertiesDialog*>(widget);check(dialog&&dialog->isVisible(),"FORM-EDGE preview Properties missing");auto* type=dialog->findChild<QComboBox*>("featureType");type->setCurrentIndex(static_cast<int>(document::FeatureType::Modeling));flush();
    auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"FORM-EDGE preview View missing");
    const auto verify=[&]{const auto& wire=view->transient_edges();check(!wire.empty(),"FORM-EDGE GUI cyan wire is empty");bool corner{};for(const auto& edge:wire)for(const auto& p:edge.points){check(p.y<=1e-6&&p.z<=1e-6,"FORM-EDGE GUI wire crossed Origin plane");corner|=std::abs(p.y)<1e-6&&std::abs(p.z)<1e-6;}check(corner,"FORM-EDGE GUI wire lost zero-length contact");};
    verify();auto* result=dialog->findChild<QComboBox*>("featureResult");result->setCurrentIndex(2);dialog->findChild<QDoubleSpinBox*>("featureThickness")->setValue(.5);flush();auto* side=dialog->findChild<QComboBox*>("featureThinSide");for(int i=0;i<3;++i){side->setCurrentIndex(i);flush();verify();}view->grabFramebuffer().save(QString::fromStdString((directory/"form-edge-preview.png").string()));
    dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();check(view->transient_edges().empty(),"FORM-EDGE Cancel retained cyan preview");window.findChild<QAction*>("saveDocumentAction")->trigger();flush();const auto loaded=document::PartDocument::load(path);check(loaded.history.front().feature.type==document::FeatureType::Sketch,"FORM-EDGE preview committed before OK");
    std::cout<<"FORM-EDGE GUI: tilted Surface and all Thin-side cyan previews, zero contact and Cancel persistence passed\n";
}

int verify_sketch_common_tangent_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        int count{};
        for(int method=0;method<4;++method)for(bool reverse:{false,true})for(double side:{-1.,1.})for(double branch:{-1.,1.}) {

            auto part=document::PartDocument::create_default();auto feature=document::PartDocument::create_sketch_container();
            auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;
            auto first=sketch.add_circle(-12,-8,3);const auto second=sketch.add_circle(20,9,5);
            std::optional<document::HistoryContainer> source_feature;std::optional<sketcher::Sketch> source_sketch;
            auto radius=sketch.create_circle_radius_dimension(method==3?second:first);radius.locked=false;sketch.apply_dimension(radius);
            const double angle=std::atan2(17.,32.)+branch*std::acos((3-side*5)/std::hypot(32.,17.));
            std::array<double,2> a{-12+3*std::cos(angle),-8+3*std::sin(angle)},b{20+side*5*std::cos(angle),9+side*5*std::sin(angle)};
            if(method>=2)static_cast<void>(sketch.add_segment(a[0],a[1],b[0],b[1]));
            if(method==3){sketch.remove_geometry(first);source_feature=document::PartDocument::create_sketch_container();source_sketch=sketcher::Sketch::create_default();source_sketch->owner_container_id=source_feature->id;source_feature->feature.sketch_id=source_sketch->id;const auto circle=source_sketch->add_circle(-12,-8,3);auto ref=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Edge);ref.source_document_id=part.document_id;ref.source_owner_id=source_sketch->id;ref.source_semantic_key="circle:"+circle;for(int i=0;i<=64;++i){double angle=i*2*std::numbers::pi/64;ref.cached_points.push_back({-12+3*std::cos(angle),-8+3*std::sin(angle)});}sketch.add_external_reference(ref);first=ref.id;}
            part.history={feature};part.sketches={sketch};document::BodyHistoryGraph graph;const auto body=graph.create_body("Tangent interaction");if(source_feature){part.history.insert(part.history.begin(),*source_feature);part.sketches.insert(part.sketches.begin(),*source_sketch);graph.insert({document::PartHistoryKind::Feature,source_feature->id});}graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
            const auto path=directory/("sketch-common-tangent-"+std::to_string(count)+".prtz");part.save(path);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
            check(window.open_document_path(QString::fromStdString(path.string())),"Common tangent fixture cannot open");flush();check(activate_test_body(application,window,body),"Common tangent Body cannot activate");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"Common tangent Sketch missing");window.show_tree_item_properties(row);flush();window.findChild<QPushButton*>("sketchOpenButton")->click();flush();
            QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Common tangent View missing");
            const auto local=[&](QPointF pixel){const auto ray=view->ray_at(pixel);check(ray.has_value(),"Common tangent ray missing");const auto q=sketch.intersect_ray(ray->first,ray->second);check(q.has_value(),"Common tangent plane missing");return *q;};
            const auto o=local({0,0}),u=local({100,0}),v=local({0,100});const double ux=(u[0]-o[0])/100,uy=(u[1]-o[1])/100,vx=(v[0]-o[0])/100,vy=(v[1]-o[1])/100,det=ux*vy-uy*vx;
            const auto screen=[&](std::array<double,2> p){return QPointF(((p[0]-o[0])*vy-(p[1]-o[1])*vx)/det,(ux*(p[1]-o[1])-uy*(p[0]-o[0]))/det);};
            const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons=Qt::NoButton){QMouseEvent e(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&e);flush();};
            const auto click=[&](QPointF p){mouse(p,QEvent::MouseMove,Qt::NoButton);mouse(p,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(p,QEvent::MouseButtonRelease,Qt::LeftButton);};
            window.findChild<QAction*>(method>=2?"sketchTangentAction":method==1?"sketchSegmentAction":"sketchCommonTangentAction")->trigger();flush();
            if(method<2){click(screen(reverse?b:a));click(screen(reverse?a:b));}
            else for(int support=0;support<2;++support){
                std::optional<QPointF> line_pixel,circle_pixel;
                for(double fraction:{.2,.3,.4,.6,.7,.8}){const auto pixel=screen({a[0]+fraction*(b[0]-a[0]),a[1]+fraction*(b[1]-a[1])});const auto offered=view->selection_candidates_at(pixel);if(!offered.empty()&&offered.front().semantic_key=="segment:"+sketch.segments.front().id){line_pixel=pixel;break;}}
                const bool external=method==3&&support==0;const std::array center=support==0?std::array{-12.,-8.}:std::array{20.,9.};const double circle_radius=support==0?3.:5.;const std::string key=external?"external_edge:"+first:"circle:"+(support==0?first:second);
                for(int quadrant=0;quadrant<8;++quadrant){double angle=(quadrant+.3)*std::numbers::pi/4;const auto pixel=screen({center[0]+circle_radius*std::cos(angle),center[1]+circle_radius*std::sin(angle)});const auto offered=view->selection_candidates_at(pixel);if(!offered.empty()&&offered.front().semantic_key==key){circle_pixel=pixel;break;}}
                check(line_pixel&&circle_pixel,"Explicit T fixture has no unobstructed offered geometry");
                if(reverse){click(*circle_pixel);click(*line_pixel);}else{click(*line_pixel);click(*circle_pixel);}
            }
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&window,&escape);flush();
            const auto lines=[&]{std::set<std::string> ids;for(const auto& edge:view->mesh().edges)if(edge.reference.owner_id==sketch.id&&edge.reference.semantic_key.starts_with("segment:"))ids.insert(edge.reference.semantic_key);return ids.size();};
            check(lines()==1,"Common tangent or C+T Segment clicks failed to create one line");
            const auto tangents=[&]{return std::ranges::count_if(view->mesh().constraint_markers,[](const auto& marker){return marker.label=="T";});};
            check(tangents()==2,"GUI did not create both persistent T markers");
            window.findChild<QAction*>("undoAction")->trigger();flush();check(method>=2?tangents()==1:lines()==0,"Common tangent GUI Undo failed");window.findChild<QAction*>("redoAction")->trigger();flush();check(lines()==1&&tangents()==2,"Common tangent GUI Redo failed");
            viewer::ViewerCandidate dimension;dimension.kind=viewer::CandidateKind::Dimension;dimension.owner_id=sketch.id;dimension.semantic_key="dimension:"+radius.id;
            for(std::size_t i=0;i<view->mesh().dimensions.size();++i)if(view->mesh().dimensions[i].reference.semantic_key==dimension.semantic_key)dimension.geometry_index=i;
            window.edit_dimension_inline(dimension);flush();auto* editor=view->findChild<QLineEdit*>("inlineDimensionValueEdit");check(editor&&editor->isVisible(),"Common tangent radius inline editor missing");editor->setText(QString::number(radius.value+.2));QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);flush();
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();for(auto* dialog:window.findChildren<QDialog*>())if(auto* properties=dynamic_cast<app::SketchPropertiesDialog*>(dialog);properties&&properties->isVisible())properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            const auto loaded=document::PartDocument::load(path);const auto& result=*std::ranges::find(loaded.sketches,sketch.id,&sketcher::Sketch::id);check(result.segments.size()==1,"Common tangent native reopen lost line");check(std::abs(result.circles.front().radius-(radius.value+.2))<1e-6,"Common tangent GUI radius edit failed");
            const auto& line=result.segments.front();const auto* p=result.find_point(line.first_point_id);const auto* q=result.find_point(line.second_point_id);const double length=std::hypot(q->x-p->x,q->y-p->y);
            for(const auto& circle:result.circles){const auto* center=result.find_point(circle.center_point_id);check(std::abs(std::abs((center->x-p->x)*(q->y-p->y)-(center->y-p->y)*(q->x-p->x))/length-circle.radius)<1e-5,"GUI-created line is not tangent to both circles after edit");}
            if(method==3){check(result.external_references==sketch.external_references,"GUI Tangent changed its read-only source");check(std::abs(std::abs((-12-p->x)*(q->y-p->y)-(-8-p->y)*(q->x-p->x))/length-3)<1e-5,"GUI external-circle T equation failed");check(loaded.sketches.front().circles.front().radius==3,"GUI Tangent drove the source circle");}
            check(std::ranges::count_if(result.constraints,[](const auto& c){return c.kind==sketcher::ConstraintKind::Tangent;})==2&&std::ranges::count_if(result.constraints,[](const auto& c){return c.kind==sketcher::ConstraintKind::PointOnCircle;})==2,"GUI-created line did not persist both C+T pairs");
            if(count==0)view->grabFramebuffer().save(QString::fromStdString((directory/"sketch-common-tangent.png").string()));
            ++count;
        }
        std::cout<<count<<" common tangent and Segment C+T GUI branches/orders, Undo/Redo, R edit and native reopen passed\n";verify_form_edge_dimension_ui(application,directory);verify_form_edge_preview_ui(application,directory);return 0;
    }catch(const std::exception& error){std::cerr<<"Sketch common tangent GUI: "<<error.what()<<'\n';return 1;}
}
