#include "feature_rotation_drag_ui_verification.inc"
static void verify_feature_side_reversal(QApplication& application,AssemblyWorkspaceWindow& window,
                                         const std::filesystem::path& directory) {
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    auto part=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
    static_cast<void>(sketch.add_rectangle(0,0,20,10));
    auto feature=document::PartDocument::create_feature_container(sketch.id);
    feature.feature.sides[0].length=12;feature.feature.sides[1].length=5;
    feature.feature.sides[1].operation=document::FeatureSideOperation::Extrusion;
    feature.value_locks={"side0_length"};sketch.owner_container_id=feature.id;
    document::ExtrusionParameters::EndTarget target;target.kind=document::EndTargetKind::Plane;
    target.reference={"retained-source","retained-plane","source-occurrence"};
    target.fallback_origin={0,0,30};target.fallback_normal={0,0,1};
    feature.feature.sides[0].targets={target}; // Inactive Length target must remain in its panel.
    part.history={feature};part.sketches={sketch};document::BodyHistoryGraph graph;
    const auto body=graph.create_body("Reversal GUI");graph.insert({document::PartHistoryKind::Feature,feature.id});
    part.set_body_history(graph);part.resolve_constructions();
    std::filesystem::create_directories(directory);const auto path=directory/"feature-reversal.prtz";
    part.save(path,kernel::OcctKernel{}.evaluate_history(part.kernel_operations()));
    window.resize(1366,900);window.show();
    check(window.open_document_path(QString::fromStdWString(path.wstring())),"Reversal GUI cannot open fixture");flush();
    check(window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",body}}}}.dump())).ok,"Reversal GUI cannot activate Body");flush();
    auto* tree=window.findChild<QTreeWidget*>("documentTree");
    auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));
    check(tree&&view,"Reversal GUI workspace missing");
    const auto open=[&] {
        QTreeWidgetItem* item{};
        for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole+3)=="part-container"&&
            (*it)->data(0,Qt::UserRole).toString().toStdString()==feature.id){item=*it;break;}
        check(item,"Reversal GUI Feature row missing");window.show_tree_item_properties(item);flush();
        auto* dialog=dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("featurePropertiesDialog"));
        check(dialog,"Reversal GUI Properties missing");return dialog;
    };
    const auto extent=[&](double low,double high) {
        double minimum=1e100,maximum=-1e100;
        for(const auto& edge:view->transient_edges())for(const auto point:edge.points){minimum=std::min(minimum,point.z);maximum=std::max(maximum,point.z);}
        check(std::abs(minimum-low)<1e-7&&std::abs(maximum-high)<1e-7,"Reversal preview differs from physical side extents");
    };
    const auto initial=window.execute_console_command("documents").data;
    auto* dialog=open();const auto before=dialog->pending_value();
    auto* swap=dialog->findChild<QPushButton*>("featureSwapSides");
    auto* first=dialog->findChild<QDoubleSpinBox*>("featureSideValue0");
    auto* second=dialog->findChild<QDoubleSpinBox*>("featureSideValue1");
    check(swap&&first&&second,"Reversal GUI side controls missing");
    const auto revision=view->base_mesh_revision();extent(-5,12);
    auto* direction=dialog->findChild<QDoubleSpinBox*>("featureDirection");check(direction,"Feature scalar direction is missing");
    const auto check_live_direction=[&](double sign) {
        const auto base=view->mesh().dimensions.size();bool found=false;
        for(std::size_t i=base;i<base+64;++i) {
            viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;candidate.geometry_index=i;
            const auto value=view->dimension_source(candidate);if(!value)break;
            if(value->reference.owner_id!=feature.id||value->reference.semantic_key!="parameter:direction")continue;
            candidate.owner_id=value->reference.owner_id;candidate.semantic_key=value->reference.semantic_key;
            check(value->label_only&&value->unit_suffix.empty()&&value->value==sign,"Live direction is not the canonical scalar parameter");
            const auto expected=QObject::tr("Směr tažení")+QString(sign>0?": +1":": -1");
            check(view->dimension_label_text(*value)==expected,"Live Properties direction lacks its localized numeric label");
            check(view->candidate_dimension_label_position(candidate).has_value(),"Live direction label is not renderable in View");
            found=true;break;
        }
        check(found,"Live Properties direction annotation is missing");
    };
    check_live_direction(1.);
    for(double sign:{-1.,1.,-1.,1.}) {
        direction->setValue(sign);flush();check_live_direction(sign);
        check(dialog->pending_value().feature.sides==before.feature.sides,"Scalar direction exchanged panels");
        if(sign<0)extent(-12,5);else extent(-5,12);
    }
    direction->setValue(0);flush();check(direction->value()==1,"Invalid scalar direction was retained");
    second->findChild<QLineEdit*>()->setText("70-100");
    {QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(second,&enter);}flush();extent(-12,30);
    check(second->value()==30&&dialog->pending_value().feature.reversed_sides,"Negative length arithmetic failed to reverse");
    second->setValue(-5);flush();extent(-5,12);
    second->setValue(-7);flush();extent(-12,7);
    check(second->value()==7&&dialog->pending_value().feature.sides[0]==before.feature.sides[0],"Negative extent changed its opposite panel");
    second->setValue(-5);flush();extent(-5,12);
    for(double coordinate:{-6.,-8.,-7.,5.}) {
        check(dialog->set_feature_length_from_drag(1,coordinate,false),"Signed Feature drag was rejected");flush();
        if(coordinate<0)extent(-12,-coordinate);else extent(-coordinate,12);
    }
    const auto camera=view->camera_state();const auto projection=view->projection_mode();
    view->set_projection_mode(viewer::ProjectionMode::Orthographic);view->set_standard_view(viewer::StandardView::Front);
    {QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();}view->fit_all();flush();
    const auto handle=std::ranges::find_if(view->extent_manipulators(),[](const auto& h){return h.end_key=="side1_length";});
    check(handle!=view->extent_manipulators().end(),"Second-side mouse handle is missing");const auto baseline_handle=*handle;
    const auto plane=[&](QPointF pixel) {
        const auto ray=view->ray_at(pixel);check(ray&&std::abs(ray->second.y)>1e-9,"Front drag ray is unavailable");
        const double t=(baseline_handle.origin.y-ray->first.y)/ray->second.y;
        return kernel::Vec3{ray->first.x+t*ray->second.x,baseline_handle.origin.y,ray->first.z+t*ray->second.z};
    };
    const auto o=plane({0,0}),u=plane({100,0}),v=plane({0,100});
    const double ux=(u.x-o.x)/100,uz=(u.z-o.z)/100,vx=(v.x-o.x)/100,vz=(v.z-o.z)/100,det=ux*vz-uz*vx;
    check(std::abs(det)>1e-12,"Front drag projection is singular");
    const auto screen=[&](double coordinate) {
        const auto a=baseline_handle.origin,d=baseline_handle.direction;
        const double x=a.x+d.x*coordinate-o.x,z=a.z+d.z*coordinate-o.z;
        return QPointF((x*vz-z*vx)/det,(ux*z-uz*x)/det);
    };
    const auto mouse=[&](double coordinate,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons) {
        const auto at=screen(coordinate);QMouseEvent event(type,at,QPointF(view->mapToGlobal(at.toPoint())),button,buttons,Qt::NoModifier);
        QApplication::sendEvent(view,&event);flush();
    };
    mouse(baseline_handle.length,QEvent::MouseMove,Qt::NoButton,Qt::NoButton);
    mouse(baseline_handle.length,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);
    for(double coordinate:{-6.,-8.,-7.,5.}) {
        mouse(coordinate,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);
        const auto pending=dialog->pending_value();const double magnitude=pending.feature.sides[1].length;
        check(std::abs(magnitude-std::abs(coordinate))<1e-3&&pending.feature.reversed_sides==(coordinate<0),"Mouse crossing zero lost its baseline direction or extent");
        if(coordinate<0)extent(-12,magnitude);else extent(-magnitude,12);
    }
    mouse(5,QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
    second->setValue(5);flush();view->set_projection_mode(projection);view->set_camera_state(camera);flush();
    auto* direction_lock=direction->findChild<QAction*>("valueLock:direction");check(direction_lock,"Direction lock is missing");
    direction_lock->setChecked(true);flush();check(direction->isReadOnly()&&!swap->isEnabled(),"Direction lock did not protect controls");
    check(!dialog->set_feature_length_from_drag(1,-8,false),"Crossing zero ignored a direction lock");
    second->setValue(-9);flush();check(second->value()==5&&!dialog->pending_value().feature.reversed_sides,"Negative entry ignored direction lock");
    direction_lock->setChecked(false);flush();extent(-5,12);
    check(dialog->pending_value().value_locks==before.value_locks,"Direction lock test changed the existing side lock");
    for(unsigned pass=0;pass<4;++pass) {
        swap->click();flush();const auto pending=dialog->pending_value();
        check(pending.feature.sides==before.feature.sides&&pending.value_locks==before.value_locks,
            "Reversal GUI exchanged values, references or locks");
        check(first->isReadOnly()&&!second->isReadOnly()&&first->value()==12&&second->value()==5,
            "Reversal GUI changed the owning side editor");
        check(view->base_mesh_revision()==revision&&window.execute_console_command("documents").data==initial,
            "Reversal draft republished base geometry or committed a document change");
        if(pass%2==0)extent(-12,5);else extent(-5,12);
    }
    swap->click();flush();second->setValue(7);flush();extent(-12,7);
    check(dialog->pending_value().feature.sides[0]==before.feature.sides[0],"Editing original second panel changed first panel");
    dialog->reject();flush();check(window.execute_console_command("documents").data==initial,"Reversal Cancel changed document");
    dialog=open();dialog->findChild<QPushButton*>("featureSwapSides")->click();flush();extent(-12,5);
    dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
    check(window.execute_console_command("save").ok,"Reversal GUI Save failed");
    auto saved=document::PartDocument::load(path);
    check(saved.find_container(feature.id)->feature.reversed_sides&&saved.find_container(feature.id)->feature.sides==before.feature.sides,
        "Reversal GUI committed the wrong side definitions");
    check(window.execute_console_command("undo").ok,"Reversal GUI Undo failed");flush();
    dialog=open();check(!dialog->pending_value().feature.reversed_sides,"Reversal Undo lost original direction");extent(-5,12);dialog->reject();flush();
    check(window.execute_console_command("redo").ok,"Reversal GUI Redo failed");flush();
    dialog=open();check(dialog->pending_value().feature.reversed_sides,"Reversal Redo lost direction");extent(-12,5);dialog->reject();flush();
    check(window.open_document_path(QString::fromStdWString(path.wstring())),"Reversal GUI reopen failed");flush();
    const auto reopened_state=window.execute_console_command("documents").data;
    dialog=open();check(dialog->pending_value().feature.reversed_sides,"Reversal GUI reopen lost direction");extent(-12,5);
    dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
    check(window.execute_console_command("documents").data==reopened_state,"Unchanged reversed Properties OK created a document transaction");
    const auto inline_value=[&](const char* key,const char* text) {
        window.show_parameter_dimensions(feature.id);flush();
        const auto value=std::ranges::find_if(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==feature.id&&d.reference.semantic_key==std::string("parameter:")+key;});
        check(value!=view->mesh().dimensions.end(),"Feature inline parameter annotation missing");
        if(std::string_view(key)=="direction")check(value->label_only&&value->unit_suffix.empty(),"Direction annotation is not scalar");
        viewer::ViewerCandidate candidate{viewer::CandidateKind::Dimension,0,static_cast<std::size_t>(value-view->mesh().dimensions.begin()),feature.id,value->reference.semantic_key,{}};
        const auto pixel=view->candidate_dimension_label_position(candidate);check(pixel.has_value(),"Feature inline label has no screen position");
        QMouseEvent move(QEvent::MouseMove,QPointF(*pixel),QPointF(view->mapToGlobal(*pixel)),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&move);flush();
        QMouseEvent open_value(QEvent::MouseButtonDblClick,QPointF(*pixel),QPointF(view->mapToGlobal(*pixel)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&open_value);flush();
        auto* editor=view->findChild<QLineEdit*>("inlineDimensionValueEdit");check(editor&&editor->isVisible(),"Feature inline value editor did not open");
        editor->setText(QString::fromLatin1(text));QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);flush();
        check(!view->findChild<QLineEdit*>("inlineDimensionValueEdit"),"Feature inline value was not accepted");
        check(window.execute_console_command("save").ok,"Feature inline edit Save failed");return document::PartDocument::load(path);
    };
    auto edited=inline_value("side1_length","-7");const auto* definition=edited.find_container(feature.id);
    check(!definition->feature.reversed_sides&&definition->feature.sides[1].length==7&&definition->feature.sides[0]==before.feature.sides[0],"Negative View dimension exchanged panels or failed to reverse");
    const auto direction_id=edited.dimension_identifiers.identifier(feature.id,"parameter:direction");check(!direction_id.empty(),"Direction annotation has no Relation identifier");
    edited=inline_value("direction","-1");definition=edited.find_container(feature.id);
    check(definition->feature.reversed_sides&&definition->feature.sides[1].length==7&&edited.dimension_identifiers.identifier(feature.id,"parameter:direction")==direction_id,"Scalar View edit lost its direction identity or panels");
    const auto unchanged=window.execute_console_command("documents").data;static_cast<void>(inline_value("direction","-1"));
    check(window.execute_console_command("documents").data==unchanged,"Unchanged scalar View edit created a transaction");
    check(window.execute_console_command("undo").ok,"Scalar View Undo failed");flush();
    check(window.execute_console_command("undo").ok,"Negative View Undo failed");flush();
    check(window.execute_console_command("save").ok,"View Undo Save failed");edited=document::PartDocument::load(path);
    check(edited.find_container(feature.id)->feature.reversed_sides&&edited.find_container(feature.id)->feature.sides==before.feature.sides,"View Undo did not restore original direction and panels");
    check(window.execute_console_command("redo").ok&&window.execute_console_command("redo").ok,"Feature View Redo failed");flush();
    std::cout<<"Feature reversal GUI: scalar/negative View edits, actual zero-crossing drag, locks, stable panels/references, repeated swaps, no-op inspection, Cancel, OK, Undo/Redo and native reopen passed\n";
    verify_feature_rotation_drag(application,window,directory);
}
