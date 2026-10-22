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
    std::cout<<"Feature reversal GUI: stable panels/locks/references, repeated swaps, second-panel edit, draft scene count, Cancel, OK, Undo/Redo and native reopen passed\n";
}
